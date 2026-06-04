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
#include "generation/chunk_system.hpp"
#include "scheduler/scheduler.hpp"
#include "scheduler/fifo_sched.hpp"
#include "world/world_state.hpp"
#include "generation/chunk_interest.hpp"

class Server {
public:
    bool init(uint16_t port);   // start listening on the given port
    void tick(float dt);        // Single tick for client management
    void shutdown();            // shutdown server

    // Check if server is active
    bool isRunning() const { return net_running; }

private:
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
    WorldState                   world;
    std::unique_ptr<ChunkSystem> chunkSystem;

    // =========================================================
    // Scheduler
    // =========================================================
    // Swap FifoScheduler for StaticPriorityScheduler or
    // AdaptiveScheduler here when you're ready — nothing else changes.
    std::unique_ptr<IScheduler> scheduler;

    // =========================================================
    // Systems (called by tick via scheduler)
    // =========================================================
    ChunkInterestSystem chunkInterest;
    void systemPlayerPhysics (float dt);   // integrate velocity, apply gravity
    void systemChunkInterest (float dt);   // enqueue chunks players need
    void systemNetworkFlush  (float dt);   // broadcast player states + ready chunks

    // Outbound packet helpers
    void sendChunkToPeer        (ENetPeer* peer, const ChunkCoord& coord);
    void sendLoadedChunksToPeer (ENetPeer* peer);
    void broadcastPlayerStates  ();
};