#pragma once
// ======================================================================
// scheduler/adaptive_sched.hpp
// Reads the metrics history and does three things FIFO/priority can't:
//   (a) reallocates budget across phases each tick
//   (b) orders deferrable tasks by value/cost ratio with starvation boost
//   (c) writes the dynamic tuning knobs (maxChunksPerPeerPerTick,
//       maxChunksSubmittedPerTick, phase weights)
// ======================================================================

#include "scheduler/scheduler.hpp"
#include <array>

class AdaptiveScheduler : public IScheduler {
public:
    void beginFrame(uint64_t tickStartUs, float totalBudgetMs,
                    ServerTuning& tuning) override;

    PhaseResult runPhase(TaskPhase          phase,
                         std::vector<Task>& tasks,
                         float              subBudgetMs) override;

    SchedulerMetrics endFrame() override;

private:
    SchedulerMetrics m_metrics;
    CostTable        m_costTable;
    float            m_phaseDurations[6] = {};
    PhaseResult      m_phaseResults[6]   = {};

    // ---- Metrics history (ring buffer) ----
    static constexpr int HISTORY_SIZE = 60;
    std::array<SchedulerMetrics, HISTORY_SIZE> m_history{};
    int  m_historyIdx   = 0;
    int  m_historyCount = 0;

    // ---- Dynamic phase weights ----
    float m_dynamicWeights[6] = {1.f, 3.f, 3.f, 2.f, 15.f, 0.5f};

    // ---- Controller state ----
    uint32_t m_consecutiveDeadlineMisses = 0;

    // ---- Helpers ----
    float computeEffectivePriority(const Task& task) const;
    void  adjustController();
    void  reallocatePhaseWeights();

    static bool compareAdaptive(const Task* a, const Task* b,
                                const AdaptiveScheduler* self);
};