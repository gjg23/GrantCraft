#pragma once
// ======================================================================
// scheduler/priority_sched.hpp
// Baseline #2: within each phase, mandatory tasks first; then
// deferrable tasks ordered by TaskPriority → sortKey (distance).
// Fixed phase-budget split.  Rate-monotonic analogue: latency-critical
// classes always win.  Should hold tick rate better but starve the
// periphery — visible in the metrics.
// ======================================================================

#include "scheduler/scheduler.hpp"

class PriorityScheduler : public IScheduler {
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

    // Sort deferrable tasks by priority (higher first) then sortKey (lower first)
    static bool compareDeferrable(const Task* a, const Task* b);
};