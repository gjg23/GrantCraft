// client.cpp

#include "client_core/client.hpp"
#include "game_core/remote_player.hpp"

#include <cstdio>
#include <cstring>

bool Client::connect(const char* host, uint16_t port,
                     const char* username,
                     uint32_t timeoutMs) {
    // ------------------------------------------------------------------
    // Create a client-side ENet host.
    // nullptr address = no server socket (outbound only)
    // 1 peer = we only connect to one server at a time
    // ------------------------------------------------------------------
    m_host = enet_host_create(nullptr, 1, CHANNEL_COUNT, 0, 0);
    if (!m_host) {
        fprintf(stderr, "[Client] Failed to create ENet host.\n");
        return false;
    }

    ENetAddress address;
    enet_address_set_host(&address, host);
    address.port = port;

    // Initiate connection - non-blocking, handshake happens in service()
    m_peer = enet_host_connect(m_host, &address, CHANNEL_COUNT, 0);
    if (!m_peer) {
        fprintf(stderr, "[Client] No available peers for connection.\n");
        return false;
    }

    // ------------------------------------------------------------------
    // Wait for the CONNECT event confirming the handshake succeeded
    // ------------------------------------------------------------------
    ENetEvent event;
    if (enet_host_service(m_host, &event, timeoutMs) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        printf("[Client] Connected to %s:%u\n", host, port);
    } else {
        fprintf(stderr, "[Client] Connection to %s:%u timed out.\n", host, port);
        enet_peer_reset(m_peer);
        return false;
    }

    m_connected = true;

    // Store username and immediately send the join packet
    strncpy(m_username, username, 15);
    m_username[15] = '\0';

    PKT_C_Join joinPkt;
    strncpy(joinPkt.username, m_username, 15);
    joinPkt.username[15] = '\0';
    ENetPacket* ep = makePacket(joinPkt, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_peer, CHANNEL_RELIABLE, ep);

    return true;
}

void Client::tick() {
    if (!m_host) return;

    ENetEvent event;
    // Non-blocking poll - returns immediately if no events
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {

            case ENET_EVENT_TYPE_RECEIVE:
                onReceive(event.packet);
                enet_packet_destroy(event.packet);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                printf("[Client] Disconnected from server.\n");
                m_connected = false;
                m_peer      = nullptr;
                break;

            default:
                break;
        }
    }
}

void Client::sendInput(float x, float y, float z, float yaw, float pitch) {
    if (!m_connected) return;

    PKT_C_PlayerInput pkt;
    pkt.x     = x;
    pkt.y     = y;
    pkt.z     = z;
    pkt.yaw   = yaw;
    pkt.pitch = pitch;

    ENetPacket* ep = makePacket(pkt, 0);
    enet_peer_send(m_peer, CHANNEL_UNRELIABLE, ep);
}

bool Client::copyChunkBlocks(const ChunkCoord& c, std::vector<BlockType>& out) const {
    std::lock_guard<std::mutex> lk(m_chunkMutex);
    return m_cache.copyBlocks(c, out);
}

ChunkCache::TierDelta Client::updatePlayerChunk(float x, float y, float z) {
    ChunkCoord newChunk{
        static_cast<int>(std::floor(x / CHUNK_SIZE)),
        static_cast<int>(std::floor(y / CHUNK_SIZE)),
        static_cast<int>(std::floor(z / CHUNK_SIZE))
    };

    std::lock_guard<std::mutex> lk(m_chunkMutex);
    m_playerChunk = newChunk;
    return m_cache.onPlayerMoved(newChunk);
}

void Client::disconnect() {
    if (!m_peer || !m_connected) return;

    enet_peer_disconnect(m_peer, 0);

    // Drain events briefly to let the disconnect reach the server
    ENetEvent event;
    enet_host_service(m_host, &event, 500);

    enet_host_destroy(m_host);
    m_host      = nullptr;
    m_peer      = nullptr;
    m_connected = false;
}

void Client::onReceive(ENetPacket* packet) {
    if (packet->dataLength < 1) return;
    m_totalBytesReceived += packet->dataLength; // for testing
    auto type = static_cast<PacketType>(packet->data[0]);
    switch (type) {
        case PacketType::S_WELCOME: {
            if (packet->dataLength < sizeof(PKT_S_Welcome)) break;
            PKT_S_Welcome pkt;
            memcpy(&pkt, packet->data, sizeof(pkt));
            m_myId = pkt.playerId;
            printf("[Client] Received welcome. My player ID = %u\n", m_myId);
            break;
        }

        case PacketType::S_PLAYER_STATE: {
            if (packet->dataLength < sizeof(PKT_S_PlayerState)) break;
            PKT_S_PlayerState pkt;
            memcpy(&pkt, packet->data, sizeof(pkt));

            for (uint8_t i = 0; i < pkt.playerCount; ++i) {
                const auto& ps = pkt.players[i];
                if (ps.id == m_myId) continue;  // skip ourselves

                auto& rp       = m_remotePlayers[ps.id];
                rp.state.id    = ps.id;
                rp.state.position = { ps.x, ps.y, ps.z };
                rp.state.yaw   = ps.yaw;
                rp.state.pitch = ps.pitch;
            }
            break;
        }

        case PacketType::S_CHUNK_DATA: {
            if (packet->dataLength < sizeof(PKT_S_ChunkData)) {
                printf("[Client] S_CHUNK_DATA too small: %zu / %zu bytes\n",
                    packet->dataLength, sizeof(PKT_S_ChunkData));
                break;
            }
            PKT_S_ChunkData hdr;
            memcpy(&hdr, packet->data, sizeof(hdr));

            if (packet->dataLength < sizeof(PKT_S_ChunkData) + hdr.dataSize) {
                printf("[Client] S_CHUNK_DATA truncated (%d,%d,%d)\n", hdr.cx, hdr.cy, hdr.cz);
                break;
            }

            const uint8_t* payload = packet->data + sizeof(PKT_S_ChunkData);
            std::vector<BlockType> blocks(CHUNK_VOLUME);

            if (hdr.compressed) {
                if (!rleDecodeBlocks(payload, hdr.dataSize, blocks.data(), CHUNK_VOLUME)) {
                    printf("[Client] RLE decode failed for chunk (%d,%d,%d)\n",
                        hdr.cx, hdr.cy, hdr.cz);
                    break;
                }
            } else {
                if (hdr.dataSize != CHUNK_VOLUME * sizeof(BlockType)) break;
                memcpy(blocks.data(), payload, hdr.dataSize);
            }

            ChunkCoord coord{ hdr.cx, hdr.cy, hdr.cz };
            std::lock_guard<std::mutex> lk(m_chunkMutex);
            ChunkTier tier = m_cache.receive(coord, std::move(blocks), m_playerChunk);
            m_newChunkEvents.push_back({ coord, tier });
            break;
        }

        case PacketType::S_DEBUG_SNAPSHOT: {
            if (packet->dataLength < sizeof(PKT_S_DebugSnapshot)) break;
            memcpy(&m_debugSnapshot, packet->data, sizeof(PKT_S_DebugSnapshot));
            m_hasDebugSnapshot = true;
            break;
        }

        default:
            break;
    }
}


// ---- debug helpers ----
void Client::sendDebugQuery() {
    if (!m_connected) return;
    PKT_C_DebugQuery pkt;
    enet_peer_send(m_peer, CHANNEL_RELIABLE,
                   makePacket(pkt, ENET_PACKET_FLAG_RELIABLE));
}

bool Client::hasPendingDebugSnapshot() const {
    return m_hasDebugSnapshot;
}

PKT_S_DebugSnapshot Client::popDebugSnapshot() {
    m_hasDebugSnapshot = false;
    return m_debugSnapshot;
}