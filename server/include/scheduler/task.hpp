#pragma once
// ======================================================================
// scheduler/task.hpp
// Expanded Task type that schedulers can reason about
// ======================================================================

#include <cstdint>
#include <functional>

// ---- Identity ----
enum class TaskType : uint8_t {
    Ingest            = 0,
    PhysicsBatch      = 1,
    InterestCompute   = 2,
    InterestCommit    = 3,
    DrainCompleted    = 4,
    SubmitGeneration  = 5,
    ChunkPacketBuild  = 6,
    ChunkSend         = 7,
    PlayerStateBuild  = 8,
    PlayerStateSend   = 9,
    NetworkFlush      = 10,
    Metrics           = 11,
    COUNT             = 12
};

// ---- Placement ----
enum class TaskPhase : uint8_t {
    Ingest       = 0,
    Simulation   = 1,
    Interest     = 2,
    Dispatch     = 3,
    Flush        = 4,
    Bookkeeping  = 5,
    COUNT        = 6
};

enum class RunLocation : uint8_t {
    MainThreadOnly = 0,
    PoolEligible   = 1
};

// ---- Priority ----
enum class TaskPriority : uint8_t {
    Critical = 0,   // must run this tick
    High     = 1,   // latency-sensitive
    Normal   = 2,   // routine work
    Low      = 3,   // background
    COUNT    = 4
};

// ---- Join-group handle ----
// -1 = no group affiliation
// produces decrements the group counter on completion
// consumes waits for the group counter to reach 0
using JoinGroupHandle = int;
static constexpr JoinGroupHandle NO_GROUP = -1;

struct Task {
    // Identity
    TaskType type   = TaskType::Metrics;
    uint64_t taskId = 0;

    // Placement / dependency
    TaskPhase       phase        = TaskPhase::Bookkeeping;
    RunLocation     runLocation  = RunLocation::MainThreadOnly;
    JoinGroupHandle produceGroup = NO_GROUP;   // decrement on completion
    JoinGroupHandle consumeGroup = NO_GROUP;   // wait before running

    // Priority
    TaskPriority priority = TaskPriority::Normal;
    float        sortKey  = 0.f;   // lower = higher priority;

    // Cost model
    float estimatedMs  = 0.f;   // predicted cost (looked up from CostTable)
    float measuredMs   = 0.f;   // written back after execution

    // Timing
    uint64_t enqueuedAtUs = 0;
    uint64_t deadlineUs   = 0;

    // Control flags
    bool mandatory  = true;     // must run
    bool deferrable = false;    // can be skipped

    // Payload
    // Allocation might need to be moved off the heap for 500+ players
    std::function<void()> work;
};