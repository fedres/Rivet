// runtime/build/graph.hpp — Build IR graph: DAG, topological sort, critical path
#pragma once

#include "ir.hpp"
#include "../../platform/interface/result.hpp"

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace rivet::build {

/// BuildGraph owns all TaskNodes and provides:
///   - Cycle detection (reports the cycle path on error)
///   - Topological sort (Kahn's algorithm)
///   - Critical-path analysis (longest-path-first scheduling hint)
///   - Dependency fan-in / fan-out queries
class BuildGraph {
public:
    BuildGraph() = default;

    // ─── Mutation ──────────────────────────────────────────────────────────

    /// Add a task and return its auto-assigned TaskId.
    TaskId add(TaskNode node);

    /// Add a directed edge: `from` must complete before `to` can start.
    /// Returns error if either id is unknown.
    Result<void> add_dep(TaskId from, TaskId to);

    // ─── Queries ───────────────────────────────────────────────────────────

    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
    [[nodiscard]] bool empty() const noexcept { return nodes_.empty(); }

    [[nodiscard]] const TaskNode* find(TaskId id) const noexcept;
    [[nodiscard]]       TaskNode* find(TaskId id)       noexcept;

    [[nodiscard]] const TaskNode& at(TaskId id) const;
    [[nodiscard]]       TaskNode& at(TaskId id);

    /// Returns all task ids in topological order (leaves first, roots last).
    /// Returns error if a cycle is detected (includes the cycle in the message).
    [[nodiscard]] Result<std::vector<TaskId>> topo_sort() const;

    /// Compute per-task critical-path weight (longest chain through this node).
    /// Higher = more urgent to schedule first.
    [[nodiscard]] std::unordered_map<TaskId, int> critical_path_weights() const;

    /// Tasks with no remaining unsatisfied dependencies (ready to run now).
    [[nodiscard]] std::vector<TaskId> ready_tasks() const;

    /// Transitive dependents of `id` — used to mark downstream as dirty.
    [[nodiscard]] std::vector<TaskId> dependents_of(TaskId id) const;

    // ─── Iteration ─────────────────────────────────────────────────────────

    [[nodiscard]] const std::vector<TaskNode>& nodes() const noexcept { return nodes_; }
    [[nodiscard]]       std::vector<TaskNode>& nodes()       noexcept { return nodes_; }

    /// Direct successors of `id` (tasks that depend on it completing first).
    [[nodiscard]] const std::vector<TaskId>& successors_of(TaskId id) const noexcept {
        static const std::vector<TaskId> kEmpty;
        auto it = successors_.find(id);
        return it != successors_.end() ? it->second : kEmpty;
    }

private:
    std::vector<TaskNode>                      nodes_;
    std::unordered_map<TaskId, std::size_t>    index_;   // id → nodes_ index
    TaskId                                     next_id_ = 1;

    // Adjacency list: successors_[id] = set of task ids that depend on id.
    std::unordered_map<TaskId, std::vector<TaskId>> successors_;
};

} // namespace rivet::build
