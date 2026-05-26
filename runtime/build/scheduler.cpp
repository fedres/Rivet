// runtime/build/scheduler.cpp — critical-path parallel scheduler
#include "scheduler.hpp"

#include <algorithm>
#include <thread>
#include <queue>
#include <unordered_set>

namespace rivet::build {

Scheduler::Scheduler(const BuildGraph& graph, std::size_t max_jobs)
    : graph_(graph)
    , max_jobs_(max_jobs == 0 ? std::thread::hardware_concurrency() : max_jobs)
    , weights_(graph.critical_path_weights())
{}

// ─── plan() — full static schedule ───────────────────────────────────────────

Result<std::vector<std::vector<TaskId>>> Scheduler::plan() const {
    auto sorted = graph_.topo_sort();
    if (!sorted) return propagate<std::vector<std::vector<TaskId>>>(sorted);

    // Group tasks into parallel waves using level-based BFS.
    // Level of a task = 1 + max(level of deps). Level 0 = no deps.
    std::unordered_map<TaskId, int> level;
    int max_level = 0;

    for (TaskId id : *sorted) {
        const TaskNode& n = graph_.at(id);
        int lv = 0;
        for (TaskId dep : n.deps)
            lv = std::max(lv, level[dep] + 1);
        level[id] = lv;
        max_level = std::max(max_level, lv);
    }

    std::vector<std::vector<TaskId>> waves(max_level + 1);
    for (TaskId id : *sorted) {
        waves[level[id]].push_back(id);
    }

    // Within each wave, sort by critical-path weight descending.
    for (auto& wave : waves) {
        std::sort(wave.begin(), wave.end(), [&](TaskId a, TaskId b) {
            return weights_.count(a) && weights_.count(b)
                   ? weights_.at(a) > weights_.at(b)
                   : false;
        });
    }

    return waves;
}

// ─── next_ready() — incremental ready-queue ──────────────────────────────────

std::vector<ScheduledTask>
Scheduler::next_ready(const std::vector<TaskId>& completed_ids) const {
    // Build a set of completed ids for fast lookup.
    std::unordered_set<TaskId> done(completed_ids.begin(), completed_ids.end());

    std::vector<ScheduledTask> result;

    for (const TaskNode& n : graph_.nodes()) {
        if (n.state != TaskNode::State::Pending) continue;

        bool ready = true;
        for (TaskId dep : n.deps) {
            if (!done.count(dep)) { ready = false; break; }
        }
        if (ready) {
            int w = weights_.count(n.id) ? weights_.at(n.id) : 0;
            result.push_back({n.id, w});
        }
    }

    // Sort by priority descending.
    std::sort(result.begin(), result.end(), [](const ScheduledTask& a, const ScheduledTask& b) {
        return a.priority > b.priority;
    });

    // Cap at max_jobs_.
    if (result.size() > max_jobs_)
        result.resize(max_jobs_);

    return result;
}

} // namespace rivet::build
