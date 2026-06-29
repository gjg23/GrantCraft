// scheduler/task_pool.cpp
#include "scheduler/task_pool.hpp"

#include <cstdio>

void TaskPool::init(int threadCount) {
    if (m_running.load(std::memory_order_acquire)) return;

    int count = (threadCount > 0)
        ? threadCount
        : static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1));

    m_running.store(true, std::memory_order_release);
    m_workers.reserve(count);
    for (int i = 0; i < count; ++i)
        m_workers.emplace_back(&TaskPool::workerLoop, this);

    printf("[TaskPool] Started %d worker threads\n", count);
}

void TaskPool::shutdown() {
    if (!m_running.load(std::memory_order_acquire)) return;

    m_running.store(false, std::memory_order_release);
    m_queueCV.notify_all();
    for (auto& t : m_workers)
        if (t.joinable()) t.join();
    m_workers.clear();

    // Drain any remaining items (shouldn't happen in correct use)
    while (!m_queue.empty())
        m_queue.pop();

    m_pendingCount.store(0, std::memory_order_release);
}

void TaskPool::submit(std::function<void()> task) {
    m_pendingCount.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        m_queue.push(std::move(task));
    }
    m_queueCV.notify_one();
}

void TaskPool::waitAll() {
    std::unique_lock<std::mutex> lk(m_waitMutex);
    m_waitCV.wait(lk, [this] {
        return m_pendingCount.load(std::memory_order_acquire) == 0;
    });
}

void TaskPool::workerLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(m_queueMutex);
            m_queueCV.wait(lk, [this] {
                return !m_queue.empty() ||
                        !m_running.load(std::memory_order_relaxed);
            });
            if (!m_running.load(std::memory_order_relaxed) && m_queue.empty())
                return;
            task = std::move(m_queue.front());
            m_queue.pop();
        }

        task();

        if (m_pendingCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // This was the last pending task — wake waitAll
            m_waitCV.notify_all();
        }
    }
}