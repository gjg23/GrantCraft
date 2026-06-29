#pragma once
// ======================================================================
// scheduler/scheduler.hpp
// Phase-aware IScheduler interface
// The server hands it one phase at a time
// ======================================================================

#include "scheduler/task.hpp"
#include "scheduler/metrics.hpp"
#include "scheduler/tuning.hpp"
#include "scheduler/task_pool.hpp"

#include <vector>
#include <chrono>

class IScheduler {
public:
    virtual ~IScheduler() = default;

    // ---- Lifecycle ----
    virtual void beginFrame(uint64_t tickStartUs, float totalBudgetMs, ServerTuning& tuning) = 0;

    // Execute one phase
    virtual PhaseResult runPhase(TaskPhase phase, std::vector<Task>& tasks, float subBudgetMs) = 0;

    // Called after all six phases
    // Returns full metrics snapshot
    virtual SchedulerMetrics endFrame() = 0;

    // Shared infrastructure
    void setTaskPool(TaskPool* pool) { m_pool = pool; }
    TaskPool* taskPool() const { return m_pool; }

protected:
    TaskPool*      m_pool           = nullptr;
    ServerTuning*  m_tuning         = nullptr;
    uint64_t       m_frameStartUs   = 0;
    float          m_totalBudgetMs  = 0.f;

    // ---- Shared helpers ----
    // Execute a single task, measure cost, update cost table
    void executeTask(Task& task, CostTable& costTable) {
        using Clock = std::chrono::steady_clock;
        auto t0 = Clock::now();
        if (task.work) task.work();
        auto t1 = Clock::now();
        float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
        task.measuredMs = ms;
        costTable.update(task.type, ms);
    }

    // Separate a phase's tasks into pool-eligible and main-thread-only
    struct PhaseBuckets {
        std::vector<Task*> poolTasks;
        std::vector<Task*> mainTasks;
    };
    PhaseBuckets bucketTasks(std::vector<Task>& tasks) {
        PhaseBuckets b;
        for (auto& t : tasks) {
            if (t.runLocation == RunLocation::PoolEligible)
                b.poolTasks.push_back(&t);
            else
                b.mainTasks.push_back(&t);
        }
        // Mandatory first within each bucket
        auto cmp = [](const Task* a, const Task* b) {
            return (a->mandatory && !b->mandatory);
        };
        std::stable_sort(b.poolTasks.begin(), b.poolTasks.end(), cmp);
        std::stable_sort(b.mainTasks.begin(), b.mainTasks.end(), cmp);
        return b;
    }

    // Submit pool tasks, bounded by estimated cost
    // Returns count of deferred pool tasks
    uint32_t submitPoolTasks(std::vector<Task*>& poolTasks, 
                            float& remainingBudgetMs,
                            CostTable& costTable)
    {
        uint32_t deferred = 0;
        for (auto* t : poolTasks) {
            float est = costTable.getEstimatedMs(t->type);
            if (!t->mandatory && est > remainingBudgetMs) {
                ++deferred;
                continue;
            }
            if (!t->mandatory) remainingBudgetMs -= est;

            Task* tp = t;
            CostTable* ct = &costTable;
            m_pool->submit([tp, ct]() {
                using Clock = std::chrono::steady_clock;
                auto t0 = Clock::now();
                if (tp->work) tp->work();
                auto t1 = Clock::now();
                float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
                tp->measuredMs = ms;
                ct->update(tp->type, ms);
            });
        }
        return deferred;
    }
};