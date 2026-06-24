#pragma once
// ================================================================
// Debug/bench packets — only sent when server is in debug mode.
// Client requests a snapshot; server replies with current state.
// ================================================================

#include <cstdint>
#include "net/net_common.hpp"

// Client → Server: request a registry snapshot
struct PKT_C_DebugQuery {
    PacketType type = PacketType::C_DEBUG_QUERY;
};

// Counts of chunks in each lifecycle state
struct RegistrySnapshot {
    uint32_t requested;    // waiting to be submitted to worker pool
    uint32_t generating;   // in worker pool / GPU in flight
    uint32_t worldReady;   // in WorldState, pending send to subscribers
    uint32_t sent;         // fully delivered to all current subscribers
    uint32_t totalTracked; // sum of all above

    // Delivery pressure
    uint32_t totalPendingRecipients; // sum of pendingRecipients across all entries
};

// Server -> Client: current registry state + server-side metrics
struct PKT_S_DebugSnapshot {
    PacketType       type = PacketType::S_DEBUG_SNAPSHOT;
    RegistrySnapshot registry;

    // Server internals
    uint32_t workerQueueDepth;   // chunks waiting in genQueue
    uint32_t gpuJobsInFlight;    // GPU streams not yet complete
    uint32_t tickRateHz;         // measured ticks/s last second
    float    tickBudgetUsedPct;  // 0-100, how much of 45ms budget was used

    // Per-tick network
    uint32_t bytesSentThisTick;
    uint32_t bytesRecvThisTick;
};