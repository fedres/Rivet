// runtime/build/scheduler.hpp — critical-path parallel scheduler
#pragma once

#include "graph.hpp"
#include <functional>
#include <vector>

namespace rivet::build {

/// A ready-queue entry produced by the scheduler.
struct ScheduledTask {
    TaskId task_id;
    int    priority;     // higher = schedule sooner (critical path weight)
};

/// Scheduler produces an ordered list of TaskIds respecting:
///   1. All dependency constraints (topo order)
///   2. Critical-path priority (longest downstream chain first)
///   3. Maximum parallelism (up to `max_jobs` tasks simultaneously)
///
/// The scheduler is purely functional — it does NOT execute anything.
/// Feed the result to an Executor.
class Scheduler {
public:
    explicit Scheduler(const BuildGraph& graph, std::size_t max_jobs = 0);

    /// Returns tasks in the order they should be dispatched, respecting deps.
    /// Each inner vector is a "wave" — all tasks in a wave may run in parallel.
    [[nodiscard]] Result<std::vector<std::vector<TaskId>>> plan() const;

    /// Returns the next batch of tasks ready to run given `completed_ids`.
    /// Incremental version: call repeatedly as tasks finish.
    [[nodiscard]] std::vector<ScheduledTask>
    next_ready(const std::vector<TaskId>& completed_ids) const;

    std::size_t max_jobs() const noexcept { return max_jobs_; }

private:
    const BuildGraph& graph_;
    std::size_t       max_jobs_;
    std::unordered_map<TaskId, int> weights_;   // critical path weights
};

} // namespace rivet::build
