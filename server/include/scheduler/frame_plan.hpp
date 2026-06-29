#pragma once
// ======================================================================
// scheduler/frame_plan.hpp
// Builds and holds the per-tick phase / task list
// ======================================================================

#include "scheduler/task.hpp"
#include <vector>
#include <cstdint>

class FramePlan {
public:
    static constexpr int PHASE_COUNT = 6;

    void clear() {
        for (int i = 0; i < PHASE_COUNT; ++i) m_phases[i].clear();
        m_nextId = 0;
    }

    void addTask(Task task) {
        task.taskId = m_nextId++;
        int idx = static_cast<int>(task.phase);
        if (idx >= 0 && idx < PHASE_COUNT)
            m_phases[idx].push_back(std::move(task));
    }

    std::vector<Task>& tasksForPhase(TaskPhase phase) {
        return m_phases[static_cast<int>(phase)];
    }

    const std::vector<Task>& tasksForPhase(TaskPhase phase) const {
        return m_phases[static_cast<int>(phase)];
    }

    int totalTasks() const {
        int n = 0;
        for (int i = 0; i < PHASE_COUNT; ++i)
            n += static_cast<int>(m_phases[i].size());
        return n;
    }

private:
    std::vector<Task> m_phases[PHASE_COUNT];
    uint64_t          m_nextId = 0;
};