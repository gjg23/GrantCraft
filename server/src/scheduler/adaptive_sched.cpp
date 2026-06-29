#include "scheduler/adaptive_sched.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>

void AdaptiveScheduler::beginFrame(uint64_t tickStartUs, float totalBudgetMs,
                                   ServerTuning& tuning) {
    m_frameStartUs  = tickStartUs;
    m_totalBudgetMs = totalBudgetMs;
    m_tuning        = &tuning;
    m_metrics       = SchedulerMetrics{};

    // Copy current weights from tuning (adaptive may have modified last tick)
    for (int i = 0; i < 6; ++i)
        m_dynamicWeights[i] = tuning.phaseBudgetWeights[i];
}

float AdaptiveScheduler::computeEffectivePriority(const Task& task) const {
    if (!m_tuning) return 0.f;

    // Base value from priority class weight
    int pidx = static_cast<int>(task.priority);
    float base = (pidx >= 0 && pidx < 4)
        ? m_tuning->priorityClassWeights[pidx] : 1.f;

    // Starvation boost: tasks whose chunk has been waiting longer
    // get a priority increase.  ageUs is roughly how long the chunk
    // has been in the pipeline.
    uint64_t ageUs = 0;
    if (m_frameStartUs > task.enqueuedAtUs)
        ageUs = m_frameStartUs - task.enqueuedAtUs;
    float boost = static_cast<float>(ageUs) * m_tuning->starvationBoostRate;

    // Value/cost ratio: divide by estimated cost so cheap+valuable
    // tasks run before expensive+valuable ones.
    float cost = m_costTable.getEstimatedMs(task.type);
    if (cost > 0.0001f)
        return (base + boost) / cost;
    return base + boost;
}

bool AdaptiveScheduler::compareAdaptive(const Task* a, const Task* b,
                                        const AdaptiveScheduler* self) {
    if (a->mandatory != b->mandatory) return a->mandatory;
    return self->computeEffectivePriority(*a) >
           self->computeEffectivePriority(*b);
}

PhaseResult AdaptiveScheduler::runPhase(TaskPhase          phase,
                                        std::vector<Task>& tasks,
                                        float              subBudgetMs) {
    using Clock = std::chrono::steady_clock;
    auto phaseStart = Clock::now();

    PhaseResult result;
    auto buckets = bucketTasks(tasks);

    // ---- Sort pool tasks by adaptive priority ----
    std::stable_sort(buckets.poolTasks.begin(), buckets.poolTasks.end(),
        [this](const Task* a, const Task* b) {
            return compareAdaptive(a, b, this);
        });

    // ---- Pool tasks ----
    float poolRemaining = subBudgetMs;
    uint32_t poolDeferred = 0;
    if (m_pool && !buckets.poolTasks.empty()) {
        poolDeferred = submitPoolTasks(buckets.poolTasks, poolRemaining,
                                       m_costTable);
        m_pool->waitAll();
    }
    result.tasksRun += static_cast<uint32_t>(buckets.poolTasks.size()) - poolDeferred;
    result.tasksDeferred += poolDeferred;

    // ---- Sort main-thread tasks by adaptive priority ----
    std::stable_sort(buckets.mainTasks.begin(), buckets.mainTasks.end(),
        [this](const Task* a, const Task* b) {
            return compareAdaptive(a, b, this);
        });

    // ---- Main-thread tasks ----
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

void AdaptiveScheduler::reallocatePhaseWeights() {
    // Adjust weights based on actual vs. allocated time per phase.
    // If a phase consistently over-runs, give it more weight;
    // if it under-runs, shrink it.
    float totalAllocated = 0.f;
    float totalActual    = 0.f;
    for (int i = 0; i < 6; ++i) {
        totalAllocated += m_dynamicWeights[i];
        totalActual    += m_phaseDurations[i];
    }

    if (totalAllocated < 0.01f || totalActual < 0.01f) return;

    for (int i = 0; i < 6; ++i) {
        float allocFraction = m_dynamicWeights[i] / totalAllocated;
        float allocMs       = allocFraction * m_totalBudgetMs;
        float actualMs      = m_phaseDurations[i];
        if (allocMs < 0.01f) continue;

        float ratio = actualMs / allocMs;  // >1 = over-ran
        float adj   = (ratio - 1.f) * 0.1f;  // 10 % of excess
        m_dynamicWeights[i] *= (1.f + adj);
        m_dynamicWeights[i]  = std::max(0.1f, m_dynamicWeights[i]);
    }

    // Write back to tuning
    if (m_tuning) {
        for (int i = 0; i < 6; ++i)
            m_tuning->phaseBudgetWeights[i] = m_dynamicWeights[i];
    }
}

void AdaptiveScheduler::adjustController() {
    if (!m_tuning) return;

    float utilization = m_metrics.budgetUsedPct / 100.f;
    float error       = utilization - m_tuning->targetUtilization;
    float step        = m_tuning->knobAdjustmentStepSize;

    // Deadline-miss penalty: halve the aggressive knobs
    if (m_metrics.deadlineMissed) {
        ++m_consecutiveDeadlineMisses;
        m_tuning->maxChunksPerPeerPerTick = std::max(
            1u, m_tuning->maxChunksPerPeerPerTick / 2);
        m_tuning->maxChunksSubmittedPerTick = std::max(
            1u, m_tuning->maxChunksSubmittedPerTick / 2);
        return;
    }
    m_consecutiveDeadlineMisses = 0;

    // Over budget: reduce dynamic knobs
    if (error > m_tuning->hysteresisBand) {
        uint32_t reduction = static_cast<uint32_t>(error * step * 10.f + 0.5f);
        reduction = std::max(1u, reduction);
        m_tuning->maxChunksPerPeerPerTick = std::max(
            1u, m_tuning->maxChunksPerPeerPerTick - reduction);
        m_tuning->maxChunksSubmittedPerTick = std::max(
            1u, m_tuning->maxChunksSubmittedPerTick - reduction);
    }
    // Under budget: increase dynamic knobs
    else if (error < -m_tuning->hysteresisBand) {
        uint32_t increase = static_cast<uint32_t>(-error * step * 10.f + 0.5f);
        increase = std::max(1u, increase);
        m_tuning->maxChunksPerPeerPerTick = std::min(
            m_tuning->maxChunksPerPeerUpper,
            m_tuning->maxChunksPerPeerPerTick + increase);
        m_tuning->maxChunksSubmittedPerTick = std::min(
            m_tuning->maxChunksSubmittedUpper,
            m_tuning->maxChunksSubmittedPerTick + increase);
    }

    // Increase starvation boost rate if distant chunks are perpetually deferred
    uint32_t flushDeferred = m_phaseResults[static_cast<int>(TaskPhase::Flush)]
                                 .tasksDeferred;
    if (flushDeferred > 0) {
        m_tuning->starvationBoostRate = std::min(
            0.01f, m_tuning->starvationBoostRate * 1.1f);
    } else {
        m_tuning->starvationBoostRate = std::max(
            0.00001f, m_tuning->starvationBoostRate * 0.95f);
    }
}

SchedulerMetrics AdaptiveScheduler::endFrame() {
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

    // ---- Adaptive-specific: update controller ----
    reallocatePhaseWeights();
    adjustController();

    // ---- Store in history ring buffer ----
    m_history[m_historyIdx] = m_metrics;
    m_historyIdx = (m_historyIdx + 1) % HISTORY_SIZE;
    if (m_historyCount < HISTORY_SIZE) ++m_historyCount;

    m_costTable.setAlpha(m_tuning ? m_tuning->ewmaAlpha : 0.2f);

    return m_metrics;
}