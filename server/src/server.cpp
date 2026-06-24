// ===================================================
// server.cpp
// ===================================================

#include "server/server.hpp"
#include "generation/gpu_terrain/kernel.cuh"
#include "ecs/components.hpp"
#include "net/debug_packets.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>


// ------------------------------------------------------------------
// Start server
// ------------------------------------------------------------------
bool Server::init(uint16_t port, ServerMode mode) {
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    net_host = enet_host_create(&address, 64, CHANNEL_COUNT, 0, 0);
    if (!net_host) {
        fprintf(stderr, "[Server] Failed to create ENet host on port %u\n", port);
        return false;
    }

    // init timer
    m_tickRateTimer = std::chrono::steady_clock::now();

    serverMode = mode;
    GenBackend backend = (serverMode == ServerMode::GPU) ? GenBackend::GPU : GenBackend::CPU;
    if (serverMode == ServerMode::GPU)
        uploadTerrainParams(makeTerrainParams());
    
    workerPool = std::make_unique<ChunkWorkerPool>(backend);
    workerPool->init();

    scheduler = std::make_unique<FifoScheduler>();

    generate_spawn_chunks();

    net_running = true;
    printf("[Server] Listening on port %u\n", port);
    return true;
}

void Server::generate_spawn_chunks() {
    constexpr int SPAWN_RADIUS = 2;
    for (int dx = -SPAWN_RADIUS; dx <= SPAWN_RADIUS; ++dx)
    for (int dy = 0;             dy <= SPAWN_RADIUS; ++dy)
    for (int dz = -SPAWN_RADIUS; dz <= SPAWN_RADIUS; ++dz) {
        ChunkCoord c{ dx, dy, dz };
        int distSq = dx*dx + dy*dy + dz*dz;
        chunkRegistry.request(c, NULL_ENTITY); // no subscriber, just warm the world
        workerPool->submit(c, distSq);
    }

    // Block until spawn chunks are in WorldState
    bool allReady = false;
    while (!allReady) {
        if (serverMode == ServerMode::GPU)
            workerPool->pollGPU();

        auto completed = workerPool->drainCompleted();
        for (auto& cc : completed) {
            world.insertChunk(cc.coord) = std::move(cc.data);
            chunkRegistry.markWorldReady(cc.coord);
        }

        allReady = true;
        for (int dx = -SPAWN_RADIUS; dx <= SPAWN_RADIUS && allReady; ++dx)
        for (int dy = 0;             dy <= SPAWN_RADIUS && allReady; ++dy)
        for (int dz = -SPAWN_RADIUS; dz <= SPAWN_RADIUS && allReady; ++dz) {
            auto* chunk = world.getChunk({dx, dy, dz});
            if (!chunk) allReady = false;
        }

        if (!allReady)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}


// ------------------------------------------------------------------
// Server tick
// ------------------------------------------------------------------
void Server::tick(float dt) {
    auto tickStart = std::chrono::steady_clock::now();

    // Record debug stats
    ++m_tickCount;
    auto tickRateNow = std::chrono::steady_clock::now();
    float sinceTimer = std::chrono::duration<float>(tickRateNow - m_tickRateTimer).count();
    if (sinceTimer >= 1.0f) {
        m_measuredTickHz  = m_tickCount;
        m_tickCount       = 0;
        m_tickRateTimer   = tickRateNow;
    }

    // --- Network events (not scheduled — must always run first) ---
    ENetEvent event;
    while (enet_host_service(net_host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                onConnect(event.peer);
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                onReceive(event.peer, event.packet);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                onDisconnect(event.peer);
                break;
            default: break;
        }
    }

    // --- Submit work to scheduler ---
    scheduler->enqueue({
        TaskType::PlayerPhysics, TaskPriority::High,
        [this, dt]{ systemPlayerPhysics(dt); }
    });

    // Enqueue chunks
    scheduler->enqueue({
        TaskType::ChunkGenerate, TaskPriority::Normal,
        [this, dt]{ systemInterestUpdate(dt); }
    });

    // dispatch workers
    scheduler->enqueue({ 
        TaskType::ChunkGenerate, TaskPriority::Normal,
        [this, dt]{ systemChunkDispatch(dt); } 
    });

    // send out chunks
    scheduler->enqueue({
        TaskType::NetworkFlush, TaskPriority::High,
        [this, dt]{ systemNetworkFlush(dt); }
    });

    // Run tasks within 45ms — leaves 5ms headroom for network polling overhead
    scheduler->tick(45.f);

    // Feed metrics back & debug metrics
    auto elapsed = std::chrono::steady_clock::now() - tickStart;
    float tickMs = std::chrono::duration<float, std::milli>(elapsed).count();
    m_lastTickMs = tickMs;
    scheduler->reportMetrics(tickMs, 0, 0);
    m_bytesSentTick = 0;
    m_bytesRecvTick = 0;
}


// ------------------------------------------------------------------
// Shutdown
// ------------------------------------------------------------------
void Server::shutdown() {
    if (!net_host) return;

    workerPool->shutdown();

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

    auto& ic = ecs.addInterest(e);
    ic.renderDistance = 8;

    chunkInterest.setRenderDistance(e, ic.renderDistance);

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

    InterestDelta delta = chunkInterest.removePlayer(e);
    for (auto& coord : delta.toUnsubscribe)
        chunkRegistry.removeSubscriber(e, coord);

    chunkRegistry.removeAllSubscriptions(e);

    ecs.destroy(e);
    peerToEntity.erase(it);
}

void Server::onReceive(ENetPeer* peer, ENetPacket* packet) {
    if (packet->dataLength < 1) return;
    auto type = static_cast<PacketType>(packet->data[0]);

    switch (type) {
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
            sendLoadedChunksToPeer(peer, it->second);
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
            if (!pos) break;

            // TODO: validate before accepting (anti-cheat)
            pos->pos   = { pkt.x, pkt.y, pkt.z };
            pos->yaw   = pkt.yaw;
            pos->pitch = pkt.pitch;
            break;
        }

        case PacketType::C_DEBUG_QUERY: {
            std::lock_guard<std::mutex> lock(playersMutex);
            auto it = peerToEntity.find(peer);
            if (it == peerToEntity.end()) break;

            PKT_S_DebugSnapshot snap;

            // Registry counts
            auto& reg = snap.registry;
            reg.requested = reg.generating = reg.worldReady = reg.sent = 0;
            reg.totalPendingRecipients = 0;

            // ChunkRegistry exposes a stats method we add below
            auto stats = chunkRegistry.getStats();
            reg.requested              = stats.requested;
            reg.generating             = stats.generating;
            reg.worldReady             = stats.worldReady;
            reg.sent                   = stats.sent;
            reg.totalTracked           = stats.requested + stats.generating
                                    + stats.worldReady + stats.sent;
            reg.totalPendingRecipients = stats.totalPendingRecipients;

            // Worker pool stats
            auto poolStats             = workerPool->getStats();
            snap.workerQueueDepth      = poolStats.queueDepth;
            snap.gpuJobsInFlight       = poolStats.gpuInFlight;

            snap.tickRateHz            = m_measuredTickHz;
            snap.tickBudgetUsedPct     = (m_lastTickMs / 45.f) * 100.f;
            snap.bytesSentThisTick     = m_bytesSentTick;
            snap.bytesRecvThisTick     = m_bytesRecvTick;

            enet_peer_send(peer, CHANNEL_RELIABLE, makePacket(snap, ENET_PACKET_FLAG_RELIABLE));
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

void Server::systemInterestUpdate(float dt) {
    auto deltas = chunkInterest.computeDeltas(ecs);

    for (auto& delta : deltas) {
        for (auto& coord : delta.toSubscribe)
            chunkRegistry.request(coord, delta.playerId);

        for (auto& coord : delta.toUnsubscribe)
            chunkRegistry.removeSubscriber(delta.playerId, coord);
    }
}

void Server::systemChunkDispatch(float dt) {
    if (serverMode == ServerMode::GPU)
        workerPool->pollGPU();

    // Drain completed chunks from workers
    auto completed = workerPool->drainCompleted();
    for (auto& cc : completed) {
        world.insertChunk(cc.coord) = std::move(cc.data);
        chunkRegistry.markWorldReady(cc.coord);
    }

    // Submit all Requested chunks to the pool
    glm::vec3 centroid{ 0.f };
    int playerCount = 0;
    for (auto& [id, pos] : ecs.allPositions()) {
        centroid += pos.pos;
        ++playerCount;
    }
    if (playerCount > 0)
        centroid /= static_cast<float>(playerCount);

    ChunkCoord centerChunk{
        static_cast<int>(std::floor(centroid.x / CHUNK_SIZE)),
        static_cast<int>(std::floor(centroid.y / CHUNK_SIZE)),
        static_cast<int>(std::floor(centroid.z / CHUNK_SIZE))
    };

    auto requested = chunkRegistry.collectRequested();
    for (auto& coord : requested) {
        int dx = coord.x - centerChunk.x;
        int dz = coord.z - centerChunk.z;
        int distSq = dx*dx + dz*dz;
        workerPool->submit(coord, distSq);
        chunkRegistry.markGenerating(coord);
    }
}

void Server::systemNetworkFlush(float dt) {
    // Chunk sends — bounded per tick to avoid saturating the link
    constexpr int MAX_CHUNKS_PER_TICK = 16;
    int sent = 0;

    auto sendWork = chunkRegistry.collectSendWork();
    for (auto& work : sendWork) {
        if (sent >= MAX_CHUNKS_PER_TICK) break;

        for (EntityId id : work.recipients) {
            if (sent >= MAX_CHUNKS_PER_TICK) break;

            auto* nc = ecs.network(id);
            if (!nc) continue;

            sendChunkToPeer(nc->peer, work.coord);
            chunkRegistry.markSentTo(work.coord, id);
            ++sent;
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
    memcpy(pkt.blocks, chunk->blocks.data(), CHUNK_VOLUME * sizeof(BlockType));

    enet_peer_send(peer, CHANNEL_RELIABLE, makePacket(pkt, ENET_PACKET_FLAG_RELIABLE));
}

void Server::sendLoadedChunksToPeer(ENetPeer* peer, EntityId id) {
    for (auto& [coord, chunk] : world.chunks)
        chunkRegistry.request(coord, id);
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

    for (auto& [peer, entityId] : peerToEntity) {
        ENetPacket* ep = makePacket(pkt, 0);
        enet_peer_send(peer, CHANNEL_UNRELIABLE, ep);
    }
}
