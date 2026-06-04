// ===================================================
// server.cpp
// ===================================================

#include "server/server.hpp"
#include "scheduler/fifo_sched.hpp"
#include "ecs/components.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>


// ------------------------------------------------------------------
// Start server
// ------------------------------------------------------------------
bool Server::init(uint16_t port) {
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    net_host = enet_host_create(&address, 64, CHANNEL_COUNT, 0, 0);
    if (!net_host) {
        fprintf(stderr, "[Server] Failed to create ENet host on port %u\n", port);
        return false;
    }

    scheduler   = std::make_unique<FifoScheduler>();
    chunkSystem = std::make_unique<ChunkSystem>(world);
    chunkSystem->init();
    chunkSystem->waitForSpawnChunks(2);

    net_running = true;
    printf("[Server] Listening on port %u\n", port);
    return true;
}


// ------------------------------------------------------------------
// Server tick
// ------------------------------------------------------------------
void Server::tick(float dt) {
    auto tickStart = std::chrono::steady_clock::now();

    // --- Network events (not scheduled — must always run first) ---
    ENetEvent event;
    while (enet_host_service(net_host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:    onConnect(event.peer);                    break;
            case ENET_EVENT_TYPE_RECEIVE:    onReceive(event.peer, event.packet);
                                             enet_packet_destroy(event.packet);        break;
            case ENET_EVENT_TYPE_DISCONNECT: onDisconnect(event.peer);                 break;
            default: break;
        }
    }

    // --- Submit work to scheduler ---
    scheduler->enqueue({
        TaskType::PlayerPhysics, TaskPriority::High,
        [this, dt]{ systemPlayerPhysics(dt); }
    });

    scheduler->enqueue({
        TaskType::ChunkGenerate, TaskPriority::Normal,
        [this, dt]{ systemChunkInterest(dt); }
    });

    scheduler->enqueue({
        TaskType::NetworkFlush, TaskPriority::High,
        [this, dt]{ systemNetworkFlush(dt); }
    });

    // Run tasks within 45ms — leaves 5ms headroom for network polling overhead
    scheduler->tick(45.f);

    // Feed metrics back (useful no-op for FifoScheduler, used by AdaptiveScheduler)
    auto elapsed = std::chrono::steady_clock::now() - tickStart;
    float tickMs = std::chrono::duration<float, std::milli>(elapsed).count();
    scheduler->reportMetrics(tickMs, 0, 0); // bytesSent tracking comes later
}


// ------------------------------------------------------------------
// Shutdown
// ------------------------------------------------------------------
void Server::shutdown() {
    if (!net_host) return;

    chunkSystem->shutdown();

    // Gracefully disconnect all peers
    {
        std::lock_guard<std::mutex> lock(playersMutex);
        for (auto& [peer, entityId] : peerToEntity)
            enet_peer_disconnect(peer, 0);
    }

    ENetEvent event;
    enet_host_service(net_host, &event, 500);

    enet_host_destroy(net_host);
    net_host    = nullptr;
    net_running = false;
    printf("[Server] Shut down.\n");
}


// ------------------------------------------------------------------
// Client lifecycle
// ------------------------------------------------------------------
void Server::onConnect(ENetPeer* peer) {
    printf("[Server] Peer connected from %u:%u\n",
           peer->address.host, peer->address.port);

    EntityId e = ecs.create();
    ecs.addPosition(e);

    auto& nc    = ecs.addNetwork(e);
    nc.peer     = peer;
    nc.playerId = net_nextPlayerId++;

    ecs.addInterest(e).renderDistance = 8;

    {
        std::lock_guard<std::mutex> lock(playersMutex);
        peerToEntity[peer] = e;
    }

    // Welcome packet assigns the client its ID
    PKT_S_Welcome welcome;
    welcome.playerId = nc.playerId;
    enet_peer_send(peer, CHANNEL_RELIABLE, makePacket(welcome, ENET_PACKET_FLAG_RELIABLE));
}

void Server::onDisconnect(ENetPeer* peer) {
    std::lock_guard<std::mutex> lock(playersMutex);
    auto it = peerToEntity.find(peer);
    if (it == peerToEntity.end()) return;

    EntityId e = it->second;
    auto* nc = ecs.network(e);
    printf("[Server] Player %u disconnected.\n", nc ? nc->playerId : 0);

    chunkInterest.removePlayer(e);
    ecs.destroy(e);
    peerToEntity.erase(it);
}

void Server::onReceive(ENetPeer* peer, ENetPacket* packet) {
    if (packet->dataLength < 1) return;
    auto type = static_cast<PacketType>(packet->data[0]);

    switch (type) {
        // C_JOIN: client sends username after connect handshake.
        // onConnect already created the entity — here we just write
        // the username into the NetworkComp and send any loaded chunks.
        case PacketType::C_JOIN: {
            if (packet->dataLength < sizeof(PKT_C_Join)) break;

            PKT_C_Join pkt;
            memcpy(&pkt, packet->data, sizeof(pkt));

            std::lock_guard<std::mutex> lock(playersMutex);
            auto it = peerToEntity.find(peer);
            if (it == peerToEntity.end()) break;

            auto* nc = ecs.network(it->second);
            if (!nc) break;
            strncpy(nc->username, pkt.username, 15);
            nc->username[15] = '\0';
            printf("[Server] Player %u joined as \"%s\"\n", nc->playerId, nc->username);

            // Send all currently loaded chunks to the new player
            sendLoadedChunksToPeer(peer);
            break;
        }

        case PacketType::C_PLAYER_INPUT: {
            if (packet->dataLength < sizeof(PKT_C_PlayerInput)) break;

            PKT_C_PlayerInput pkt;
            memcpy(&pkt, packet->data, sizeof(pkt));

            std::lock_guard<std::mutex> lock(playersMutex);
            auto it = peerToEntity.find(peer);
            if (it == peerToEntity.end()) break;

            auto* pos = ecs.position(it->second);
            auto* nc  = ecs.network(it->second);
            if (!pos || !nc) break;

            // TODO: validate before accepting (anti-cheat)
            pos->pos   = { pkt.x, pkt.y, pkt.z };
            pos->yaw   = pkt.yaw;
            pos->pitch = pkt.pitch;
            break;
        }

        default:
            printf("[Server] Unknown packet type: %u\n",
                   static_cast<uint8_t>(type));
            break;
    }
}


// ------------------------------------------------------------------
// Systems (called via scheduler each tick)
// ------------------------------------------------------------------
void Server::systemPlayerPhysics(float dt) {
    // Iterate all entities with a position component and integrate
    // velocity. Gravity, collision, and validation go here later.
    for (auto& [id, pos] : ecs.allPositions()) {
        // Creative mode / no gravity for now — just damp velocity
        pos.vel *= 0.8f;
        pos.pos += pos.vel * dt;
    }
}

void Server::systemChunkInterest(float dt) {
    // Computes per-player deltas and enqueues only what's new
    chunkInterest.update(ecs, *chunkSystem);
}

void Server::systemNetworkFlush(float dt) {
    // Deliver any chunks that finished generating to players who need them
    for (auto& coord : chunkSystem->getAndClearReadyChunks()) {
        for (EntityId id : chunkInterest.getPeersNeedingChunk(coord)) {
            auto* nc = ecs.network(id);
            if (nc) sendChunkToPeer(nc->peer, coord);
            chunkInterest.markSent(id, coord);
        }
    }

    broadcastPlayerStates();
}


// ------------------------------------------------------------------
// Outbound packet helpers
// ------------------------------------------------------------------
void Server::sendChunkToPeer(ENetPeer* peer, const ChunkCoord& coord) {
    auto* chunk = world.getChunk(coord);
    if (!chunk) return;

    PKT_S_ChunkData pkt;
    pkt.cx = coord.x;
    pkt.cy = coord.y;
    pkt.cz = coord.z;
    memcpy(pkt.blocks, chunk->blocks.data(), BLOCKS_PER_CHUNK * sizeof(BlockType));

    ENetPacket* ep = makePacket(pkt, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, CHANNEL_RELIABLE, ep);
}

void Server::sendLoadedChunksToPeer(ENetPeer* peer) {
    auto it = peerToEntity.find(peer);
    if (it == peerToEntity.end()) return;

    EntityId id = it->second;
    auto* pos = ecs.position(id);
    if (!pos) return;

    // Mark all ready chunks as sent via ChunkInterestSystem
    // so it doesn't try to re-send them through systemNetworkFlush
    int sent = 0;
    for (auto& [coord, chunk] : world.chunks) {
        sendChunkToPeer(peer, coord);
        chunkInterest.markSent(id, coord);
        sent++;
    }
    printf("[Server] Sent %d existing chunks to player %u on join\n",
           sent, ecs.network(id) ? ecs.network(id)->playerId : 0);
}

void Server::broadcastPlayerStates() {
    PKT_S_PlayerState pkt;
    pkt.playerCount = 0;

    for (auto& [id, pos] : ecs.allPositions()) {
        if (pkt.playerCount >= 64) break;   // packet cap
        auto* nc = ecs.network(id);
        if (!nc) continue;

        auto& ps  = pkt.players[pkt.playerCount++];
        ps.id     = nc->playerId;
        ps.x      = pos.pos.x;
        ps.y      = pos.pos.y;
        ps.z      = pos.pos.z;
        ps.yaw    = pos.yaw;
        ps.pitch  = pos.pitch;
    }

    if (pkt.playerCount == 0) return;

    ENetPacket* ep = makePacket(pkt, 0); // unreliable — drop is fine
    std::lock_guard<std::mutex> lk(playersMutex);
    for (auto& [peer, entityId] : peerToEntity)
        enet_peer_send(peer, CHANNEL_UNRELIABLE, ep);
}