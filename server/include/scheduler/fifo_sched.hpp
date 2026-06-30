#pragma once
// ======================================================================
// scheduler/fifo_sched.hpp
// first in first out
// ======================================================================

#include "scheduler/scheduler.hpp"

class FifoScheduler : public IScheduler {
public:
    void beginFrame(uint64_t tickStartUs, float totalBudgetMs,
                    ServerTuning& tuning) override;

    PhaseResult runPhase(TaskPhase phase,
                         std::vector<Task>& tasks,
                         float subBudgetMs) override;

    SchedulerMetrics endFrame() override;

private:
    SchedulerMetrics m_metrics;
    CostTable        m_costTable;
    float            m_phaseDurations[6] = {};
    PhaseResult      m_phaseResults[6]   = {};
};