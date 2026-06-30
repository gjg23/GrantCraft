#include "scheduler/fifo_sched.hpp"

#include <chrono>
#include <algorithm>

void FifoScheduler::beginFrame(uint64_t tickStartUs, float totalBudgetMs,
                               ServerTuning& tuning) {
    m_frameStartUs  = tickStartUs;
    m_totalBudgetMs = totalBudgetMs;
    m_tuning        = &tuning;
    m_metrics       = SchedulerMetrics{};
}

PhaseResult FifoScheduler::runPhase(TaskPhase          phase,
                                    std::vector<Task>& tasks,
                                    float              subBudgetMs) {
    using Clock = std::chrono::steady_clock;
    auto phaseStart = Clock::now();

    PhaseResult result;
    auto buckets = bucketTasks(tasks);

    // ---- Pool tasks (submission order, mandatory first) ----
    float poolRemaining = subBudgetMs;
    uint32_t poolDeferred = 0;
    if (m_pool && !buckets.poolTasks.empty()) {
        poolDeferred = submitPoolTasks(buckets.poolTasks, poolRemaining,
                                       m_costTable);
        m_pool->waitAll();
    }
    result.tasksRun += static_cast<uint32_t>(buckets.poolTasks.size()) - poolDeferred;
    result.tasksDeferred += poolDeferred;

    // ---- Main-thread tasks (submission order, mandatory first) ----
    float mainRemaining = subBudgetMs;
    for (auto* t : buckets.mainTasks) {
        if (!t->mandatory) {
            auto elapsed = Clock::now() - phaseStart;
            float usedMs = std::chrono::duration<float, std::milli>(elapsed).count();
            if (usedMs >= subBudgetMs) {
                ++result.tasksDeferred;
                continue;
            }
        }
        executeTask(*t, m_costTable);
        ++result.tasksRun;
    }

    auto phaseEnd = Clock::now();
    result.durationMs = std::chrono::duration<float, std::milli>(
        phaseEnd - phaseStart).count();

    int pi = static_cast<int>(phase);
    m_phaseDurations[pi] = result.durationMs;
    m_phaseResults[pi]   = result;
    return result;
}

SchedulerMetrics FifoScheduler::endFrame() {
    m_metrics.phaseDurationMs[0] = m_phaseDurations[0];
    m_metrics.phaseDurationMs[1] = m_phaseDurations[1];
    m_metrics.phaseDurationMs[2] = m_phaseDurations[2];
    m_metrics.phaseDurationMs[3] = m_phaseDurations[3];
    m_metrics.phaseDurationMs[4] = m_phaseDurations[4];
    m_metrics.phaseDurationMs[5] = m_phaseDurations[5];
    m_metrics.phaseResults[0] = m_phaseResults[0];
    m_metrics.phaseResults[1] = m_phaseResults[1];
    m_metrics.phaseResults[2] = m_phaseResults[2];
    m_metrics.phaseResults[3] = m_phaseResults[3];
    m_metrics.phaseResults[4] = m_phaseResults[4];
    m_metrics.phaseResults[5] = m_phaseResults[5];

    float total = 0.f;
    for (int i = 0; i < 6; ++i) total += m_phaseDurations[i];
    m_metrics.tickDurationMs = total;
    m_metrics.budgetUsedPct  = (m_totalBudgetMs > 0.f)
        ? (total / m_totalBudgetMs) * 100.f : 0.f;
    m_metrics.deadlineMissed = total > m_totalBudgetMs;

    uint32_t totalDeferred = 0;
    for (int i = 0; i < 6; ++i) totalDeferred += m_phaseResults[i].tasksDeferred;
    m_metrics.tasksDeferredThisTick = totalDeferred;

    return m_metrics;
}