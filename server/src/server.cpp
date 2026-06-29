#include "server/server.hpp"
#include "generation/gpu_terrain/kernel.cuh"
#include "ecs/components.hpp"
#include "net/debug_packets.hpp"
#include "settings/settings.hpp"

#include "scheduler/fifo_sched.hpp"
#include "scheduler/priority_sched.hpp"
#include "scheduler/adaptive_sched.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <lz4.h>

// ------------------------------------------------------------------
// Start server
// ------------------------------------------------------------------
bool Server::init(uint16_t port, ServerMode mode) {
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    constexpr size_t kMaxPeers = 512;
    net_host = enet_host_create(&address, kMaxPeers, CHANNEL_COUNT, 0, 0);
    if (!net_host) {
        fprintf(stderr, "[Server] Failed to create ENet host on port %u\n", port);
        return false;
    }

    m_tickRateTimer = std::chrono::steady_clock::now();

    serverMode = mode;
    GenBackend backend = (serverMode == ServerMode::GPU) ? GenBackend::GPU : GenBackend::CPU;
    if (serverMode == ServerMode::GPU)
        uploadTerrainParams(makeTerrainParams());
    workerPool = std::make_unique<ChunkWorkerPool>(backend);
    workerPool->init();

    // Initialise task pool
    int poolThreads = (m_tuning.taskPoolThreadCount > 0)
        ? m_tuning.taskPoolThreadCount
        : static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1));
    taskPool.init(poolThreads);

    // Default scheduler
    scheduler = std::make_unique<FifoScheduler>();
    scheduler->setTaskPool(&taskPool);

    generate_spawn_chunks();

    net_running = true;
    printf("[Server] Listening on port %u\n", port);
    return true;
}

void Server::generate_spawn_chunks() {
    constexpr int SPAWN_RADIUS = 2;

    uint64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    for (int dx = -SPAWN_RADIUS; dx <= SPAWN_RADIUS; ++dx)
        for (int dy = 0;             dy <= SPAWN_RADIUS; ++dy)
            for (int dz = -SPAWN_RADIUS; dz <= SPAWN_RADIUS; ++dz) {
                ChunkCoord c{ dx, dy, dz };
                int distSq = dx*dx + dy*dy + dz*dz;
                chunkRegistry.request(c, 0 /* NULL_ENTITY */, nowUs);
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
// Phase budget allocation
// ------------------------------------------------------------------
void Server::computePhaseBudgets(float budgets[6], float totalBudget) const {
    float available = totalBudget - m_tuning.reservedHeadroomMs;
    if (available < 1.f) available = 1.f;

    float totalWeight = 0.f;
    for (int i = 0; i < 6; ++i)
        totalWeight += m_tuning.phaseBudgetWeights[i];

    if (totalWeight < 0.01f) totalWeight = 1.f;

    for (int i = 0; i < 6; ++i)
        budgets[i] = available * (m_tuning.phaseBudgetWeights[i] / totalWeight);
}

// ------------------------------------------------------------------
// Server tick — redesigned with phase-based scheduling
// ------------------------------------------------------------------
void Server::tick(float dt) {
    using Clock = std::chrono::steady_clock;
    auto tickStart = Clock::now();
    uint64_t tickStartUs = std::chrono::duration_cast<std::chrono::microseconds>(
        tickStart.time_since_epoch()).count();

    // Tick rate measurement
    ++m_tickCount;
    auto tickRateNow = Clock::now();
    float sinceTimer = std::chrono::duration<float>(tickRateNow - m_tickRateTimer).count();
    if (sinceTimer >= 1.0f) {
        m_measuredTickHz = m_tickCount;
        m_tickCount      = 0;
        m_tickRateTimer  = tickRateNow;
    }

    // Clear per-tick intermediate data
    m_tickInterestOutputs.clear();

    m_bytesSentTick        = 0;
    m_bytesRecvTick        = 0;

    // Build the frame plan
    m_framePlan.clear();
    buildFramePlan(m_framePlan, dt, tickStartUs);

    // Compute per-phase budgets
    float phaseBudgets[6];
    computePhaseBudgets(phaseBudgets, m_tuning.totalTickBudgetMs);

    // Run the scheduler
    scheduler->beginFrame(tickStartUs, m_tuning.totalTickBudgetMs, m_tuning);

    for (int p = 0; p < 6; ++p) {
        auto& tasks = m_framePlan.tasksForPhase(static_cast<TaskPhase>(p));
        scheduler->runPhase(static_cast<TaskPhase>(p), tasks, phaseBudgets[p]);
    }

    m_lastMetrics = scheduler->endFrame();

    // Record debug stats
    auto elapsed = Clock::now() - tickStart;
    float tickMs = std::chrono::duration<float, std::milli>(elapsed).count();
    m_lastTickMs       = tickMs;
    m_lastBytesSentTick = m_bytesSentTick;
}

// ==================================================================
// Frame plan builder
// ==================================================================
void Server::buildFramePlan(FramePlan& plan, float dt, uint64_t tickStartUs) {
    emitIngestTasks     (plan, dt, tickStartUs);
    emitSimulationTasks (plan, dt, tickStartUs);
    emitInterestTasks   (plan, dt, tickStartUs);
    emitDispatchTasks   (plan, dt, tickStartUs);
    emitFlushTasks      (plan, dt, tickStartUs);
    emitBookkeepingTasks(plan, dt, tickStartUs);
}

// ---- Phase 0: Ingest ----
void Server::emitIngestTasks(FramePlan& plan, float dt, uint64_t tickStartUs) {
    Task t;
    t.type        = TaskType::Ingest;
    t.phase       = TaskPhase::Ingest;
    t.runLocation = RunLocation::MainThreadOnly;
    t.priority    = TaskPriority::Critical;
    t.mandatory   = true;
    t.enqueuedAtUs = tickStartUs;
    t.deadlineUs   = tickStartUs + static_cast<uint64_t>(m_tuning.totalTickBudgetMs * 1000.0);
    t.work = [this, dt]() { taskIngest(dt); };
    plan.addTask(std::move(t));
}

void Server::taskIngest(float dt) {
    ENetEvent event;
    while (enet_host_service(net_host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:    onConnect(event.peer);    break;
        case ENET_EVENT_TYPE_RECEIVE:
            onReceive(event.peer, event.packet);
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT: onDisconnect(event.peer); break;
        default: break;
        }
    }
}

// ---- Phase 1: Simulation ----
void Server::emitSimulationTasks(FramePlan& plan, float dt, uint64_t tickStartUs) {
    auto allIds = ecs.allPositionIds();
    if (allIds.empty()) return;

    // Determine batch count: at least 1, at most one per pool thread
    int batchCount = taskPool.threadCount();
    if (batchCount < 1) batchCount = 1;
    if (batchCount > static_cast<int>(allIds.size()))
        batchCount = static_cast<int>(allIds.size());

    size_t batchSize = (allIds.size() + batchCount - 1) / batchCount;

    uint64_t deadline = tickStartUs +
        static_cast<uint64_t>(m_tuning.totalTickBudgetMs * 1000.0);

    for (size_t i = 0; i < allIds.size(); i += batchSize) {
        auto begin = allIds.begin() + i;
        auto end   = allIds.begin() + std::min(i + batchSize, allIds.size());
        std::vector<EntityId> batch(begin, end);

        Task t;
        t.type         = TaskType::PhysicsBatch;
        t.phase        = TaskPhase::Simulation;
        t.runLocation  = RunLocation::PoolEligible;
        t.priority     = TaskPriority::High;
        t.mandatory    = true;
        t.deferrable   = false;
        t.enqueuedAtUs = tickStartUs;
        t.deadlineUs   = deadline;
        t.sortKey      = 0.f;
        t.work = [this, entities = std::move(batch), dt]() {
            taskPhysicsBatch(entities, dt);
        };
        plan.addTask(std::move(t));
    }
}

void Server::taskPhysicsBatch(const std::vector<EntityId>& entities, float dt) {
    for (EntityId id : entities) {
        auto* pos = ecs.position(id);
        if (!pos) continue;
        pos->vel *= 0.8f;
        pos->pos += pos->vel * dt;
    }
}

// ---- Phase 2: Interest ----
void Server::emitInterestTasks(FramePlan& plan, float dt, uint64_t tickStartUs) {
    // Detect which players moved across a chunk boundary
    auto movedPlayers = chunkInterest.detectMovedPlayers(ecs);
    if (movedPlayers.empty()) return;

    // Cap the number of interest recomputes per tick
    size_t maxComputes = m_tuning.maxInterestRecomputesPerTick;
    if (movedPlayers.size() > maxComputes)
        movedPlayers.resize(maxComputes);

    m_tickInterestOutputs.resize(movedPlayers.size());

    uint64_t deadline = tickStartUs +
        static_cast<uint64_t>(m_tuning.totalTickBudgetMs * 1000.0);

    // Parallel compute tasks: one per moved player
    for (size_t i = 0; i < movedPlayers.size(); ++i) {
        EntityId   pid = movedPlayers[i].playerId;
        glm::vec3  pos = movedPlayers[i].position;

        Task t;
        t.type         = TaskType::InterestCompute;
        t.phase        = TaskPhase::Interest;
        t.runLocation  = RunLocation::PoolEligible;
        t.priority     = TaskPriority::High;
        t.mandatory    = true;
        t.deferrable   = false;
        t.enqueuedAtUs = tickStartUs;
        t.deadlineUs   = deadline;
        t.sortKey      = 0.f;
        t.produceGroup = 0;  // decrement join-group 0 on completion
        t.work = [this, pid, pos, idx = i]() {
            m_tickInterestOutputs[idx] =
                chunkInterest.computeInterestReadonly(pid, pos);
        };
        plan.addTask(std::move(t));
    }

    // Serial commit task: applies all deltas to playerStates + ChunkRegistry
    Task commit;
    commit.type         = TaskType::InterestCommit;
    commit.phase        = TaskPhase::Interest;
    commit.runLocation  = RunLocation::MainThreadOnly;
    commit.priority     = TaskPriority::High;
    commit.mandatory    = true;
    commit.deferrable   = false;
    commit.enqueuedAtUs = tickStartUs;
    commit.deadlineUs   = deadline;
    commit.consumeGroup = 0;  // waits for join-group 0
    commit.work = [this]() { taskInterestCommit(); };
    plan.addTask(std::move(commit));
}

void Server::taskInterestCommit() {
    uint64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    size_t subscriptionsThisTick = 0;
    for (auto& output : m_tickInterestOutputs) {
        InterestDelta delta = chunkInterest.applyInterestOutput(output);
        
        // Request all subscriptions
        for (auto& coord : delta.toSubscribe)
            chunkRegistry.request(coord, delta.playerId, nowUs);

        for (auto& coord : delta.toUnsubscribe)
            chunkRegistry.removeSubscriber(delta.playerId, coord);
    }
}

// ---- Phase 3: Dispatch ----
void Server::emitDispatchTasks(FramePlan& plan, float dt, uint64_t tickStartUs) {
    uint64_t deadline = tickStartUs +
        static_cast<uint64_t>(m_tuning.totalTickBudgetMs * 1000.0);

    // DrainCompleted — mandatory
    Task drain;
    drain.type         = TaskType::DrainCompleted;
    drain.phase        = TaskPhase::Dispatch;
    drain.runLocation  = RunLocation::MainThreadOnly;
    drain.priority     = TaskPriority::High;
    drain.mandatory    = true;
    drain.deferrable   = false;
    drain.enqueuedAtUs = tickStartUs;
    drain.deadlineUs   = deadline;
    drain.work = [this]() { taskDrainCompleted(); };
    plan.addTask(std::move(drain));

    // SubmitGeneration — deferrable, bounded
    Task submit;
    submit.type         = TaskType::SubmitGeneration;
    submit.phase        = TaskPhase::Dispatch;
    submit.runLocation  = RunLocation::MainThreadOnly;
    submit.priority     = TaskPriority::Normal;
    submit.mandatory    = false;
    submit.deferrable   = true;
    submit.enqueuedAtUs = tickStartUs;
    submit.deadlineUs   = deadline;
    submit.sortKey      = 0.f;
    submit.work = [this]() { taskSubmitGeneration(); };
    plan.addTask(std::move(submit));
}

void Server::taskDrainCompleted() {
    if (serverMode == ServerMode::GPU)
        workerPool->pollGPU();

    auto completed = workerPool->drainCompleted();
    for (auto& cc : completed) {
        world.insertChunk(cc.coord) = std::move(cc.data);
        chunkRegistry.markWorldReady(cc.coord);
    }
}

void Server::taskSubmitGeneration() {
    // Compute player centroid for distance-based priority
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

    auto requested = chunkRegistry.collectRequestedByDistance(centerChunk);
    uint32_t submitted = 0;
    for (auto& rc : requested) {
        if (submitted >= m_tuning.maxChunksSubmittedPerTick) break;
        if (serverMode == ServerMode::GPU) {
            auto poolStats = workerPool->getStats();
            if (poolStats.gpuInFlight >= m_tuning.maxGpuInFlight) break;
        }
        workerPool->submit(rc.coord, rc.distSq);
        chunkRegistry.markGenerating(rc.coord);
        ++submitted;
    }
}

// ---- Phase 4: Flush ----
void Server::emitFlushTasks(FramePlan& plan, float dt, uint64_t tickStartUs) {
    uint64_t deadline = tickStartUs +
        static_cast<uint64_t>(m_tuning.totalTickBudgetMs * 1000.0);

    Task t;
    t.type         = TaskType::NetworkFlush;
    t.phase        = TaskPhase::Flush;
    t.runLocation  = RunLocation::MainThreadOnly;
    t.priority     = TaskPriority::Critical;
    t.mandatory    = true;
    t.deferrable   = false;
    t.enqueuedAtUs = tickStartUs;
    t.deadlineUs   = deadline;
    t.work = [this, tickStartUs]() { taskFlushPipeline(tickStartUs); };
    plan.addTask(std::move(t));
}

void Server::taskFlushPipeline(uint64_t tickStartUs) {
    using Clock = std::chrono::steady_clock;

    // ==================================================================
    // 1. Collect send work NOW — after Interest and Dispatch have run,
    //    so new subscriptions and newly drained chunks are visible.
    // ==================================================================
    auto sendWork = chunkRegistry.collectSendWork();

    struct FlushItem {
        ChunkCoord            coord;
        std::vector<EntityId> recipients;
        int                   minDistSq;
        uint64_t              requestedAtUs;
    };

    std::vector<FlushItem> items;
    items.reserve(sendWork.size());

    for (auto& sw : sendWork) {
        int best = INT_MAX;
        for (EntityId id : sw.recipients) {
            if (auto* pos = ecs.position(id)) {
                int dx = sw.coord.x - (int)std::floor(pos->pos.x / CHUNK_SIZE);
                int dy = sw.coord.y - (int)std::floor(pos->pos.y / CHUNK_SIZE);
                int dz = sw.coord.z - (int)std::floor(pos->pos.z / CHUNK_SIZE);
                best = std::min(best, dx*dx + dy*dy + dz*dz);
            }
        }
        items.push_back({ sw.coord, std::move(sw.recipients), best, sw.requestedAtUs });
    }

    // Nearest chunks first — this is the sortKey ordering the
    // priority and adaptive schedulers would otherwise apply.
    std::sort(items.begin(), items.end(),
        [](const FlushItem& a, const FlushItem& b) {
            return a.minDistSq < b.minDistSq;
        });

    // ==================================================================
    // 2. Build chunk packets on the task pool (parallel compression).
    //    The pool is idle at this point — all prior phases are done.
    // ==================================================================
    struct BuiltPacket {
        ChunkCoord            coord;
        ENetPacket*           packet     = nullptr;
        uint32_t              packetSize = 0;
        std::vector<EntityId> recipients;
    };

    std::vector<BuiltPacket> built(items.size());

    // Pre-fill metadata (recipients, coord) before submitting to pool
    for (size_t i = 0; i < items.size(); ++i) {
        built[i].coord       = items[i].coord;
        built[i].recipients  = std::move(items[i].recipients);
    }

    // Submit build tasks — each writes to its own BuiltPacket slot
    for (size_t i = 0; i < built.size(); ++i) {
        taskPool.submit([this, &bp = built[i]]() {
            bp.packet = buildChunkPacket(bp.coord);
            bp.packetSize = bp.packet
                ? static_cast<uint32_t>(bp.packet->dataLength) : 0;
        });
    }

    // Barrier — wait for all compression to finish
    taskPool.waitAll();

    // ==================================================================
    // 3. Send built chunk packets (serial — touches ENet + registry).
    //    Always runs; never deferred.
    // ==================================================================
    uint64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now().time_since_epoch()).count();

    std::unordered_map<ENetPeer*, uint32_t> peerCount;
    peerCount.reserve(peerToEntity.size());

    for (auto& bp : built) {
        if (!bp.packet) continue;

        bool anySent = false;
        for (EntityId id : bp.recipients) {
            auto* nc = ecs.network(id);
            if (!nc) continue;
            uint32_t& cnt = peerCount[nc->peer];
            if (cnt >= m_tuning.maxChunksPerPeerPerTick) continue;

            enet_peer_send(nc->peer, CHANNEL_RELIABLE, bp.packet);
            ++cnt;
            m_bytesSentTick += bp.packetSize;
            chunkRegistry.markSentTo(bp.coord, id, nowUs);
            anySent = true;
        }
        if (!anySent) enet_packet_destroy(bp.packet);
    }

    // ==================================================================
    // 4. Build and send player states (serial).
    // ==================================================================
    PKT_S_PlayerState pkt;
    pkt.playerCount = 0;

    for (auto& [id, pos] : ecs.allPositions()) {
        if (pkt.playerCount >= 64) break;
        auto* nc = ecs.network(id);
        if (!nc) continue;

        auto& ps = pkt.players[pkt.playerCount++];
        ps.id    = nc->playerId;
        ps.x     = pos.pos.x;
        ps.y     = pos.pos.y;
        ps.z     = pos.pos.z;
        ps.yaw   = pos.yaw;
        ps.pitch = pos.pitch;
    }

    if (pkt.playerCount > 0) {
        for (auto& [peer, entityId] : peerToEntity) {
            ENetPacket* ep = makePacket(pkt, 0);
            enet_peer_send(peer, CHANNEL_UNRELIABLE, ep);
            m_bytesSentTick += static_cast<uint32_t>(ep->dataLength);
        }
    }

    // ==================================================================
    // 5. Flush — AFTER all enet_peer_send calls, not before.
    // ==================================================================
    enet_host_flush(net_host);
}


// ---- Phase 5: Bookkeeping ----
void Server::emitBookkeepingTasks(FramePlan& plan, float dt, uint64_t tickStartUs) {
    Task t;
    t.type         = TaskType::Metrics;
    t.phase        = TaskPhase::Bookkeeping;
    t.runLocation  = RunLocation::MainThreadOnly;
    t.priority     = TaskPriority::Critical;
    t.mandatory    = true;
    t.deferrable   = false;
    t.enqueuedAtUs = tickStartUs;
    t.deadlineUs   = tickStartUs +
        static_cast<uint64_t>(m_tuning.totalTickBudgetMs * 1000.0);
    t.work = [this]() { taskMetrics(); };
    plan.addTask(std::move(t));
}

void Server::taskMetrics() {
    // Compute chunk delivery latency percentiles from registry samples
    auto& samples = chunkRegistry.getLatencySamplesMs();
    if (!samples.empty()) {
        std::vector<float> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        m_lastMetrics.chunkDeliveryLatencyP50Ms =
            sorted[sorted.size() / 2];
        m_lastMetrics.chunkDeliveryLatencyP99Ms =
            sorted[static_cast<size_t>(sorted.size() * 0.99)];
        m_lastMetrics.chunkDeliveryLatencyMaxMs =
            sorted.back();
    }

    // Populate pressure metrics
    auto regStats = chunkRegistry.getStats();
    m_lastMetrics.worldReadyUnsentBacklog = regStats.worldReady;
    m_lastMetrics.playerCount = static_cast<uint32_t>(ecs.allPositions().size());

    auto poolStats = workerPool->getStats();
    m_lastMetrics.genQueueDepth = poolStats.queueDepth;
    m_lastMetrics.gpuInFlight   = poolStats.gpuInFlight;

    m_lastMetrics.bytesSent  = m_bytesSentTick;
    m_lastMetrics.bytesRecv  = m_bytesRecvTick;
    m_lastMetrics.chunksSent = 0; // counted per-send in taskChunkSend

    // Clear latency samples after reading (rolling window)
    chunkRegistry.clearLatencySamples();
}

// ------------------------------------------------------------------
// Shutdown
// ------------------------------------------------------------------
void Server::shutdown() {
    if (!net_host) return;

    if (workerPool) workerPool->shutdown();
    taskPool.shutdown();

    // Gracefully disconnect all peers
    {
        std::lock_guard<std::mutex> lock(playersMutex);
        for (auto& [peer, entityId] : peerToEntity)
            enet_peer_disconnect(peer, 0);
    }

    ENetEvent event;
    uint32_t deadline = 500;
    while (enet_host_service(net_host, &event, deadline) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE)
            enet_packet_destroy(event.packet);
        deadline = 0;
    }

    enet_host_destroy(net_host);
    net_host    = nullptr;
    net_running = false;
    printf("[Server] Shut down.\n");
}

// ------------------------------------------------------------------
// Client lifecycle (unchanged logic, now called from Ingest task)
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
    ic.renderDistance = WorldCfg::RENDER_DISTANCE;

    chunkInterest.setRenderDistance(e, ic.renderDistance);

    {
        std::lock_guard<std::mutex> lock(playersMutex);
        peerToEntity[peer] = e;
    }

    PKT_S_Welcome welcome;
    welcome.playerId = nc.playerId;
    enet_peer_send(peer, CHANNEL_RELIABLE,
                   makePacket(welcome, ENET_PACKET_FLAG_RELIABLE));
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
    m_bytesRecvTick += static_cast<uint32_t>(packet->dataLength);
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
        printf("[Server] Player %u joined as \"%s\"\n",
               nc->playerId, nc->username);
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

        pos->pos   = { pkt.x, pkt.y, pkt.z };
        pos->yaw   = pkt.yaw;
        pos->pitch = pkt.pitch;
        break;
    }

    case PacketType::C_DEBUG_QUERY: {
        PKT_S_DebugSnapshot snap;

        auto& reg = snap.registry;
        reg.requested = reg.generating = reg.worldReady = reg.sent = 0;
        reg.totalPendingRecipients = 0;

        auto stats = chunkRegistry.getStats();
        reg.requested              = stats.requested;
        reg.generating             = stats.generating;
        reg.worldReady             = stats.worldReady;
        reg.sent                   = stats.sent;
        reg.totalTracked           = stats.requested + stats.generating
                                   + stats.worldReady + stats.sent;
        reg.totalPendingRecipients = stats.totalPendingRecipients;

        auto poolStats = workerPool->getStats();
        snap.workerQueueDepth  = poolStats.queueDepth;
        snap.gpuJobsInFlight   = poolStats.gpuInFlight;
        snap.tickRateHz        = m_measuredTickHz;
        snap.tickBudgetUsedPct = (m_lastTickMs / m_tuning.totalTickBudgetMs) * 100.f;
        snap.bytesSentThisTick = m_lastBytesSentTick;
        snap.bytesRecvThisTick = m_bytesRecvTick;

        enet_peer_send(peer, CHANNEL_RELIABLE,
                       makePacket(snap, ENET_PACKET_FLAG_RELIABLE));
        break;
    }

    default:
        printf("[Server] Unknown packet type: %u\n",
               static_cast<uint8_t>(type));
        break;
    }
}

// ------------------------------------------------------------------
// Outbound packet helpers (unchanged logic)
// ------------------------------------------------------------------
uint32_t Server::sendChunkToPeer(ENetPeer* peer, const ChunkCoord& coord) {
    auto* chunk = world.getChunk(coord);
    if (!chunk) return 0;

    const uint32_t rawSize = CHUNK_VOLUME * static_cast<uint32_t>(sizeof(BlockType));
    const uint32_t maxRle  = CHUNK_VOLUME * static_cast<uint32_t>(sizeof(uint16_t) + sizeof(BlockType));

    std::vector<uint8_t> compBuf(maxRle);
    uint32_t compSize = rleEncodeBlocks(
        chunk->blocks.data(), CHUNK_VOLUME, compBuf.data(), maxRle);

    bool useCompressed  = (compSize > 0 && compSize < rawSize);
    uint32_t payloadSz  = useCompressed ? compSize : rawSize;

    PKT_S_ChunkData hdr;
    hdr.cx       = coord.x;
    hdr.cy       = coord.y;
    hdr.cz       = coord.z;
    hdr.encoding = static_cast<uint8_t>(useCompressed ? ChunkEncoding::RLE : ChunkEncoding::Raw);
    hdr.dataSize = payloadSz;

    uint32_t totalSize = static_cast<uint32_t>(sizeof(hdr)) + payloadSz;
    ENetPacket* pkt = enet_packet_create(nullptr, totalSize, ENET_PACKET_FLAG_RELIABLE);
    memcpy(pkt->data, &hdr, sizeof(hdr));
    if (useCompressed)
        memcpy(pkt->data + sizeof(hdr), compBuf.data(), compSize);
    else
        memcpy(pkt->data + sizeof(hdr), chunk->blocks.data(), rawSize);

    enet_peer_send(peer, CHANNEL_RELIABLE, pkt);
    return totalSize;
}

void Server::sendLoadedChunksToPeer(ENetPeer* peer, EntityId id) {
    for (auto& [coord, chunk] : world.chunks)
        chunkRegistry.request(coord, id);
}

void Server::broadcastPlayerStates() {
    PKT_S_PlayerState pkt;
    pkt.playerCount = 0;

    for (auto& [id, pos] : ecs.allPositions()) {
        if (pkt.playerCount >= 64) break;
        auto* nc = ecs.network(id);
        if (!nc) continue;

        auto& ps = pkt.players[pkt.playerCount++];
        ps.id   = nc->playerId;
        ps.x    = pos.pos.x;
        ps.y    = pos.pos.y;
        ps.z    = pos.pos.z;
        ps.yaw  = pos.yaw;
        ps.pitch = pos.pitch;
    }

    if (pkt.playerCount == 0) return;

    for (auto& [peer, entityId] : peerToEntity) {
        ENetPacket* ep = makePacket(pkt, 0);
        enet_peer_send(peer, CHANNEL_UNRELIABLE, ep);
    }
}

ENetPacket* Server::buildChunkPacket(const ChunkCoord& coord) {
    Chunk* chunk = world.getChunk(coord);
    if (!chunk) return nullptr;

    const BlockType* src = chunk->blocks.data();
    const uint32_t   N   = CHUNK_VOLUME;
    const uint32_t   raw = N * static_cast<uint32_t>(sizeof(BlockType));

    PKT_S_ChunkData hdr;
    hdr.cx = coord.x; hdr.cy = coord.y; hdr.cz = coord.z;

    auto packetFrom = [&](ChunkEncoding enc, const uint8_t* payload, uint32_t size) {
        hdr.encoding = static_cast<uint8_t>(enc);
        hdr.dataSize = size;
        uint32_t total = static_cast<uint32_t>(sizeof(hdr)) + size;
        ENetPacket* pkt = enet_packet_create(nullptr, total, ENET_PACKET_FLAG_RELIABLE);
        memcpy(pkt->data, &hdr, sizeof(hdr));
        if (size) memcpy(pkt->data + sizeof(hdr), payload, size);
        return pkt;
    };

    // All same block
    bool uniform = true;
    for (uint32_t i = 1; i < N; ++i)
        if (src[i] != src[0]) { uniform = false; break; }
    if (uniform) {
        uint8_t v = static_cast<uint8_t>(src[0]);
        return packetFrom(ChunkEncoding::Uniform, &v, 1);
    }

    // LZ4
    constexpr uint32_t CHUNK_RAW_BYTES = CHUNK_VOLUME * sizeof(BlockType);
    constexpr uint32_t CHUNK_LZ4_BOUND = lz4CompressBound(CHUNK_RAW_BYTES);

    std::array<uint8_t, CHUNK_LZ4_BOUND> lz4Buf;
    int lz4Size = LZ4_compress_default(
        reinterpret_cast<const char*>(src),
        reinterpret_cast<char*>(lz4Buf.data()),
        static_cast<int>(CHUNK_RAW_BYTES),
        static_cast<int>(CHUNK_LZ4_BOUND));

    // RLE fallback
    const uint32_t maxRle = N * static_cast<uint32_t>(sizeof(uint16_t) + sizeof(BlockType));
    std::vector<uint8_t> rleBuf(maxRle);
    uint32_t rleSize = rleEncodeBlocks(src, N, rleBuf.data(), maxRle);

    // Pick smallest
    uint32_t bestSize = raw;
    ChunkEncoding bestEnc = ChunkEncoding::Raw;
    const uint8_t* bestPtr = reinterpret_cast<const uint8_t*>(src);
    if (lz4Size > 0 && static_cast<uint32_t>(lz4Size) < bestSize) {
        bestSize = lz4Size; bestEnc = ChunkEncoding::LZ4; bestPtr = lz4Buf.data();
    }
    if (rleSize > 0 && rleSize < bestSize) {
        bestSize = rleSize; bestEnc = ChunkEncoding::RLE; bestPtr = rleBuf.data();
    }
    return packetFrom(bestEnc, bestPtr, bestSize);
}