// server/include/scheduler/fifo_scheduler.hpp
#pragma once

#include "scheduler/scheduler.hpp"
#include <queue>
#include <chrono>

// ------------------------------------------------------------------
// FifoScheduler — baseline #1
// Runs tasks in submission order, no prioritisation.
// Used to establish a performance baseline to compare against
// StaticPriorityScheduler and AdaptiveScheduler later.
// ------------------------------------------------------------------
class FifoScheduler : public IScheduler {
public:
    void enqueue(Task t) override {
        queue.push(std::move(t));
    }

    void tick(float budgetMs) override {
        using Clock = std::chrono::steady_clock;
        auto start  = Clock::now();
        auto budget = std::chrono::duration<float, std::milli>(budgetMs);

        while (!queue.empty()) {
            // Check budget before each task, not after — avoids
            // running a long task that blows the tick deadline.
            auto elapsed = Clock::now() - start;
            if (elapsed >= budget) break;

            Task t = std::move(queue.front());
            queue.pop();
            t.work();
        }
    }

    uint32_t pendingCount() const { return (uint32_t)queue.size(); }

private:
    std::queue<Task> queue;
};