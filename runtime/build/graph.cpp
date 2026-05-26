// runtime/build/graph.cpp — BuildGraph implementation
#include "graph.hpp"

#include <algorithm>
#include <format>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace rivet::build {

// ─── Mutation ────────────────────────────────────────────────────────────────

TaskId BuildGraph::add(TaskNode node) {
    const TaskId id = next_id_++;
    node.id = id;

    // Wire pre-declared deps into successors_ so the node can be added with
    // deps already set (e.g. link nodes that depend on all compile node IDs).
    std::vector<TaskId> pre_deps = node.deps;   // capture before move
    index_[id] = nodes_.size();
    nodes_.push_back(std::move(node));
    successors_.emplace(id, std::vector<TaskId>{});

    for (TaskId dep : pre_deps) {
        if (successors_.count(dep))
            successors_[dep].push_back(id);
    }
    return id;
}

Result<void> BuildGraph::add_dep(TaskId from, TaskId to) {
    if (!index_.count(from))
        return make_error<void>(std::format("unknown task id {}", from));
    if (!index_.count(to))
        return make_error<void>(std::format("unknown task id {}", to));

    nodes_[index_[to]].deps.push_back(from);
    successors_[from].push_back(to);
    return {};
}

// ─── Queries ─────────────────────────────────────────────────────────────────

const TaskNode* BuildGraph::find(TaskId id) const noexcept {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &nodes_[it->second];
}

TaskNode* BuildGraph::find(TaskId id) noexcept {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &nodes_[it->second];
}

const TaskNode& BuildGraph::at(TaskId id) const {
    auto* p = find(id);
    if (!p) throw std::out_of_range(std::format("BuildGraph::at({}): not found", id));
    return *p;
}

TaskNode& BuildGraph::at(TaskId id) {
    auto* p = find(id);
    if (!p) throw std::out_of_range(std::format("BuildGraph::at({}): not found", id));
    return *p;
}

// ─── Topological sort (Kahn's algorithm) ─────────────────────────────────────

Result<std::vector<TaskId>> BuildGraph::topo_sort() const {
    // Compute in-degrees.
    std::unordered_map<TaskId, int> in_deg;
    for (const auto& n : nodes_) {
        in_deg.emplace(n.id, 0);
    }
    for (const auto& n : nodes_) {
        for (TaskId dep : n.deps) {
            in_deg[n.id]++;  // n depends on dep → n has one more in-edge
            (void)dep;
        }
    }
    // Re-derive from successors_ for correctness.
    in_deg.clear();
    for (const auto& n : nodes_) in_deg[n.id] = static_cast<int>(n.deps.size());

    std::queue<TaskId> q;
    for (const auto& [id, deg] : in_deg)
        if (deg == 0) q.push(id);

    std::vector<TaskId> order;
    order.reserve(nodes_.size());

    while (!q.empty()) {
        TaskId cur = q.front(); q.pop();
        order.push_back(cur);
        for (TaskId succ : successors_.at(cur)) {
            if (--in_deg[succ] == 0)
                q.push(succ);
        }
    }

    if (order.size() != nodes_.size()) {
        // Cycle exists — find and report it.
        std::string cycle_nodes;
        for (const auto& [id, deg] : in_deg) {
            if (deg > 0) {
                cycle_nodes += std::format(" {}", id);
            }
        }
        return make_error<std::vector<TaskId>>(
            "build graph contains a dependency cycle involving tasks:" + cycle_nodes);
    }

    return order;
}

// ─── Critical path ───────────────────────────────────────────────────────────

std::unordered_map<TaskId, int> BuildGraph::critical_path_weights() const {
    // Longest-path-first: weight[n] = 1 + max(weight[successor]).
    // Compute in reverse topo order.
    auto sorted_r = topo_sort();
    if (!sorted_r) return {};

    std::unordered_map<TaskId, int> w;
    for (const auto& n : nodes_) w[n.id] = 0;

    // Iterate in reverse topo order (roots last in topo → process roots first
    // in reverse gives leaves first, which is what we want for bottom-up DP).
    const auto& sorted = *sorted_r;
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        TaskId id = *it;
        int max_succ = 0;
        for (TaskId s : successors_.at(id))
            max_succ = std::max(max_succ, w[s]);
        w[id] = 1 + max_succ;
    }
    return w;
}

// ─── Ready tasks ─────────────────────────────────────────────────────────────

std::vector<TaskId> BuildGraph::ready_tasks() const {
    std::vector<TaskId> result;
    for (const auto& n : nodes_) {
        if (n.state != TaskNode::State::Pending) continue;
        bool all_done = true;
        for (TaskId dep : n.deps) {
            const auto* d = find(dep);
            if (!d || d->state != TaskNode::State::Done) {
                all_done = false;
                break;
            }
        }
        if (all_done) result.push_back(n.id);
    }
    return result;
}

// ─── Dependents ──────────────────────────────────────────────────────────────

std::vector<TaskId> BuildGraph::dependents_of(TaskId id) const {
    std::vector<TaskId> result;
    std::queue<TaskId>  q;
    std::unordered_set<TaskId> visited;

    q.push(id);
    visited.insert(id);

    while (!q.empty()) {
        TaskId cur = q.front(); q.pop();
        auto it = successors_.find(cur);
        if (it == successors_.end()) continue;
        for (TaskId s : it->second) {
            if (!visited.count(s)) {
                visited.insert(s);
                result.push_back(s);
                q.push(s);
            }
        }
    }
    return result;
}

} // namespace rivet::build
