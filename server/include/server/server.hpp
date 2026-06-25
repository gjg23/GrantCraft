#pragma once
// =======================================================================
// server.hpp
// The authoritative game server.
// Responsibilities:
//   - Accept / disconnect ENet peers
//   - Receive client input packets
//   - Advance ECS (position, physics, validation)
//   - Drive chunk pipeline via ChunkSystem
//   - Dispatch work through IScheduler
//   - Broadcast world state back to all clients
//
// Used by two entry points:
//   1. server/main.cpp     - standalone dedicated server binary
//   2. client/local_server - embedded server for singleplayer
// =======================================================================

#include <enet/enet.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <memory>

#include "net/net_common.hpp"
#include "ecs/registry.hpp"

#include "generation/chunk_registry.hpp"
#include "generation/chunk_worker_pool.hpp"
#include "generation/chunk_interest.hpp"

#include "scheduler/scheduler.hpp"
#include "scheduler/fifo_sched.hpp"

#include "world/world_state.hpp"

enum class ServerMode { CPU, GPU };

class Server {
public:
    bool init(uint16_t port, ServerMode mode = ServerMode::CPU);   // start listening on the given port
    void tick(float dt);        // Single tick for client management
    void shutdown();            // shutdown server
    bool isRunning() const { return net_running; }

private:
    ServerMode serverMode = ServerMode::CPU;

    // ======================================================================
    // Networking data
    // ======================================================================
    ENetHost* net_host          = nullptr;
    bool      net_running       = false;
    uint32_t  net_nextPlayerId  = 1;

    // Internal packet handlers
    void onConnect   (ENetPeer* peer);
    void onDisconnect(ENetPeer* peer);
    void onReceive   (ENetPeer* peer, ENetPacket* packet);

    // =========================================================
    // ECS
    // =========================================================
    Registry ecs;

    // map from network layer to ECS
    std::unordered_map<ENetPeer*, EntityId> peerToEntity;
    std::mutex playersMutex;

    // =========================================================
    // World + chunk pipeline
    // =========================================================
    WorldState world;

    // =========================================================
    // Chunk pipeline
    // =========================================================
    // main thread
    ChunkRegistry                    chunkRegistry;
    std::unique_ptr<ChunkWorkerPool> workerPool;
    ChunkInterestSystem              chunkInterest;
    void generate_spawn_chunks();

    // =========================================================
    // Scheduler
    // =========================================================
    std::unique_ptr<IScheduler> scheduler;

    // =========================================================
    // Systems (called by tick via scheduler)
    // =========================================================
    void systemPlayerPhysics (float dt);    // integrate velocity, apply gravity
    void systemInterestUpdate(float dt);    // enqueue chunks players need
    void systemChunkDispatch (float dt);    // registry + worker pool
    void systemNetworkFlush  (float dt);    // broadcast player states + ready chunks

    // Outbound packet helpers
    uint32_t sendChunkToPeer(ENetPeer* peer, const ChunkCoord& coord);
    void sendLoadedChunksToPeer(ENetPeer* peer, EntityId id);
    void broadcastPlayerStates();

    // Debug stats
    uint32_t m_tickCount       = 0;
    uint32_t m_measuredTickHz  = 0;
    float    m_lastTickMs      = 0.f;
    std::chrono::steady_clock::time_point m_tickRateTimer;
    uint32_t m_bytesSentTick   = 0;
    uint32_t m_bytesRecvTick   = 0;
};