// server/include/scheduler/scheduler.hpp
#pragma once

#include <functional>
#include <cstdint>

enum class TaskType {
    ChunkGenerate,      // generate one chunk
    ChunkDispatch,      // queue chunk
    ChunkSend,          // send chunk data to a peer
    PlayerPhysics,      // update one player's position/velocity
    EntitySimulation,   // tick a batch of entities
    NetworkFlush,       // broadcast player states to all clients
};

enum class TaskPriority {
    Critical = 0,   // must run this tick (e.g. disconnect cleanup)
    High     = 1,   // player-facing latency sensitive (chunk near player)
    Normal   = 2,   // routine work
    Low      = 3,   // background work (distant chunk gen)
};

struct Task {
    TaskType     type;
    TaskPriority priority  = TaskPriority::Normal;
    std::function<void()> work;

    // Metadata — unused by FifoScheduler, read by AdaptiveScheduler
    uint64_t enqueuedAtUs = 0;   // timestamp when task was submitted
    float    estimatedMs  = 0.f; // expected wall-clock cost (learned over time)
};

// ------------------------------------------------------------------
// IScheduler
// The server calls enqueue() to submit work and tick() once per
// server tick to drain the queue within the available time budget.
// All scheduling policy lives behind this interface — swapping
// FifoScheduler for AdaptiveScheduler is a one-line change in
// Server::init().
// ------------------------------------------------------------------
class IScheduler {
public:
    virtual ~IScheduler() = default;

    virtual void enqueue(Task t)       = 0;

    // Run queued tasks until budgetMs of wall time is consumed.
    // Implementations may run fewer tasks if the queue empties first.
    virtual void tick(float budgetMs)  = 0;

    // Called by the server after each tick with observed metrics.
    // FifoScheduler ignores this; AdaptiveScheduler uses it to
    // adjust weights.
    virtual void reportMetrics(
        float    tickDurationMs,    // how long last tick actually took
        uint32_t queueDepth,        // tasks still pending
        uint32_t bytesSentThisTick  // network pressure
    ) {}
};