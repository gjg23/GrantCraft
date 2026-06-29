#pragma once
// ======================================================================
// scheduler/metrics.hpp
// SchedulerMetrics, CostTable, PhaseResult.
// ======================================================================

#include "scheduler/task.hpp"

#include <cstdint>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

// ---- Per-phase result returned by runPhase ----
struct PhaseResult {
    uint32_t tasksRun      = 0;
    uint32_t tasksDeferred = 0;
    float    durationMs    = 0.f;
};

// ---- Full metrics snapshot per tick ----
struct SchedulerMetrics {
    // Tick-level
    float    tickDurationMs   = 0.f;
    float    budgetUsedPct    = 0.f;
    bool     deadlineMissed   = false;
    uint32_t playerCount      = 0;

    // Per-phase
    float         phaseDurationMs[6]  = {};
    PhaseResult   phaseResults[6]     = {};

    // Queue / pressure
    uint32_t pendingTasksPerPhase[6]  = {};
    uint32_t tasksDeferredThisTick    = 0;
    uint32_t genQueueDepth            = 0;
    uint32_t gpuInFlight              = 0;
    uint32_t worldReadyUnsentBacklog  = 0;

    // Network
    uint32_t bytesSent   = 0;
    uint32_t bytesRecv   = 0;
    uint32_t chunksSent  = 0;

    // QoS — chunk delivery latency distribution
    float chunkDeliveryLatencyP50Ms = 0.f;
    float chunkDeliveryLatencyP99Ms = 0.f;
    float chunkDeliveryLatencyMaxMs = 0.f;
};

// ---- CostTable: EWMA of measuredMs per TaskType ----
class CostTable {
public:
    float getEstimatedMs(TaskType type) const {
        size_t idx = static_cast<size_t>(type);
        if (idx < m_hasSample.size() && m_hasSample[idx])
            return m_estimates[idx];
        return m_defaults[idx];
    }

    void update(TaskType type, float measuredMs) {
        size_t idx = static_cast<size_t>(type);
        if (idx >= m_estimates.size()) return;
        if (!m_hasSample[idx]) {
            m_estimates[idx] = measuredMs;
            m_hasSample[idx] = true;
        } else {
            m_estimates[idx] = m_alpha * measuredMs + (1.f - m_alpha) * m_estimates[idx];
        }
    }

    void setAlpha(float a) { m_alpha = a; }

private:
    static constexpr size_t N = static_cast<size_t>(TaskType::COUNT);
    float                m_alpha = 0.2f;
    std::array<float, N> m_estimates{};
    std::array<bool,  N> m_hasSample{};

    // Reasonable initial guesses (ms)
    static constexpr float m_defaults[N] = {
        0.5f,   // Ingest
        0.1f,   // PhysicsBatch
        0.05f,  // InterestCompute
        0.2f,   // InterestCommit
        0.1f,   // DrainCompleted
        0.05f,  // SubmitGeneration
        0.3f,   // ChunkPacketBuild
        0.01f,  // ChunkSend
        0.05f,  // PlayerStateBuild
        0.01f,  // PlayerStateSend
        0.01f,  // NetworkFlush
        0.05f,  // Metrics
    };
};

// Compile-time definition of the constexpr static member
constexpr float CostTable::m_defaults[CostTable::N];