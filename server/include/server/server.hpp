#pragma once
// =======================================================================
// server.hpp
// The authoritative game server using task scheduling
// =======================================================================

#include <enet/enet.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <memory>
#include <chrono>

#include "net/net_common.hpp"
#include "ecs/registry.hpp"

#include "generation/chunk_registry.hpp"
#include "generation/chunk_worker_pool.hpp"
#include "generation/chunk_interest.hpp"

#include "scheduler/scheduler.hpp"
#include "scheduler/task_pool.hpp"
#include "scheduler/frame_plan.hpp"
#include "scheduler/tuning.hpp"
#include "scheduler/metrics.hpp"

#include "world/world_state.hpp"

enum class ServerMode { CPU, GPU };

// ---- Per-tick intermediate data used by phase tasks ----
struct BuiltChunkPacket {
    ChunkCoord             coord;
    ENetPacket*            packet     = nullptr;
    uint32_t               packetSize = 0;
    std::vector<EntityId>  recipients;
    int                    minDistSq  = 0;
};

class Server {
public:
    bool init(uint16_t port, ServerMode mode = ServerMode::CPU);
    void tick(float dt);
    void shutdown();
    bool isRunning() const { return net_running; }

    // Accessors for benchmark harness
    const ServerTuning&      tuning()      const { return m_tuning; }
    const SchedulerMetrics&  lastMetrics() const { return m_lastMetrics; }

private:
    ServerMode serverMode = ServerMode::CPU;

    // ======================================================================
    // Networking data
    // ======================================================================
    ENetHost* net_host          = nullptr;
    bool      net_running       = false;
    uint32_t  net_nextPlayerId  = 1;

    void onConnect   (ENetPeer* peer);
    void onDisconnect(ENetPeer* peer);
    void onReceive   (ENetPeer* peer, ENetPacket* packet);

    // =========================================================
    // ECS
    // =========================================================
    Registry ecs;

    std::unordered_map<ENetPeer*, EntityId> peerToEntity;
    std::mutex playersMutex;

    // =========================================================
    // World + chunk pipeline
    // =========================================================
    WorldState world;

    ChunkRegistry                    chunkRegistry;
    std::unique_ptr<ChunkWorkerPool> workerPool;
    ChunkInterestSystem              chunkInterest;

    void generate_spawn_chunks();

    // =========================================================
    // Scheduler infrastructure
    // =========================================================
    std::unique_ptr<IScheduler> scheduler;
    TaskPool                    taskPool;
    ServerTuning                m_tuning;
    SchedulerMetrics            m_lastMetrics;
    FramePlan                   m_framePlan;

    // =========================================================
    // Frame plan builder: sets tasks for each phase
    // =========================================================
    void buildFramePlan(FramePlan& plan, float dt, uint64_t tickStartUs);

    // Per-phase tasks
    void emitIngestTasks      (FramePlan& plan, float dt, uint64_t tickStartUs);
    void emitSimulationTasks  (FramePlan& plan, float dt, uint64_t tickStartUs);
    void emitInterestTasks    (FramePlan& plan, float dt, uint64_t tickStartUs);
    void emitDispatchTasks    (FramePlan& plan, float dt, uint64_t tickStartUs);
    void emitFlushTasks       (FramePlan& plan, float dt, uint64_t tickStartUs);
    void emitBookkeepingTasks (FramePlan& plan, float dt, uint64_t tickStartUs);

    // Per-tick intermediate data
    std::vector<InterestComputeOutput> m_tickInterestOutputs;

    // ---- task stages ----
    // Phase 0
    void taskIngest(float dt);

    // Phase 1
    void taskPhysicsBatch(const std::vector<EntityId>& entities, float dt);

    // Phase 2
    void taskInterestCommit();

    // Phase 3
    void taskDrainCompleted();
    void taskSubmitGeneration();

    // Phase 4
    void taskFlushPipeline(uint64_t tickStartUs);

    // Phase 5
    void taskMetrics();

    // =========================================================
    // Outbound packet helpers
    // =========================================================
    uint32_t     sendChunkToPeer(ENetPeer* peer, const ChunkCoord& coord);
    void         sendLoadedChunksToPeer(ENetPeer* peer, EntityId id);
    void         broadcastPlayerStates();
    ENetPacket*  buildChunkPacket(const ChunkCoord& coord);

    // =========================================================
    // Debug stats
    // =========================================================
    uint32_t m_tickCount       = 0;
    uint32_t m_measuredTickHz  = 0;
    float    m_lastTickMs      = 0.f;
    std::chrono::steady_clock::time_point m_tickRateTimer;
    uint32_t m_bytesSentTick   = 0;
    uint32_t m_bytesRecvTick   = 0;
    uint32_t m_lastBytesSentTick = 0;

    // Phase budget computation
    void computePhaseBudgets(float budgets[6], float totalBudget) const;
};