#pragma once
// ======================================================================
// scheduler/task_pool.hpp
// general-purpose thread pool for parallel task execution
// ======================================================================

#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class TaskPool {
public:
    TaskPool() = default;
    ~TaskPool() { shutdown(); }

    // Start worker threads. Call once at server init.
    void init(int threadCount = -1);   // -1 = hw_concurrency - 1

    // Stop all workers. Call at server shutdown.
    void shutdown();

    // Submit a task for parallel execution.
    void submit(std::function<void()> task);

    // Block until every submitted task has completed.
    void waitAll();

    int threadCount() const {
        return static_cast<int>(m_workers.size());
    }

    bool isRunning() const {
        return m_running.load(std::memory_order_acquire);
    }

private:
    std::vector<std::thread>        m_workers;
    std::queue<std::function<void()>> m_queue;
    std::mutex                      m_queueMutex;
    std::condition_variable         m_queueCV;

    std::atomic<int>                m_pendingCount{0};
    std::mutex                      m_waitMutex;
    std::condition_variable         m_waitCV;

    std::atomic<bool>               m_running{false};

    void workerLoop();
};