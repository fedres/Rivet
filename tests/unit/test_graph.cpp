// tests/unit/test_graph.cpp — Build IR: BuildGraph + topological sort + critical path
#include "runtime/build/graph.hpp"
#include "runtime/build/scheduler.hpp"
#include <gtest/gtest.h>

using namespace rivet::build;

// Helper: create a named Phony node with no command.
static TaskNode phony(std::string name) {
    TaskNode n;
    n.name = std::move(name);
    n.kind = TaskKind::Phony;
    return n;
}

// ─── BuildGraph basics ────────────────────────────────────────────────────────

TEST(BuildGraph, AddAndFind) {
    BuildGraph g;
    TaskId id = g.add(phony("clean"));
    EXPECT_NE(id, kInvalidTaskId);
    ASSERT_NE(g.find(id), nullptr);
    EXPECT_EQ(g.find(id)->name, "clean");
    EXPECT_EQ(g.size(), 1u);
}

TEST(BuildGraph, AddDep) {
    BuildGraph g;
    TaskId a = g.add(phony("a"));
    TaskId b = g.add(phony("b"));
    ASSERT_TRUE(g.add_dep(a, b).has_value());
    // b depends on a → b.deps contains a
    EXPECT_EQ(g.at(b).deps.size(), 1u);
    EXPECT_EQ(g.at(b).deps[0], a);
}

TEST(BuildGraph, AddDepUnknownId) {
    BuildGraph g;
    auto r = g.add_dep(99, 100);
    EXPECT_FALSE(r.has_value());
}

// ─── Topological sort ────────────────────────────────────────────────────────

TEST(BuildGraph, TopoSortLinear) {
    // a → b → c  (a must finish before b, b before c)
    BuildGraph g;
    TaskId a = g.add(phony("a"));
    TaskId b = g.add(phony("b"));
    TaskId c = g.add(phony("c"));
    (void)g.add_dep(a, b);
    (void)g.add_dep(b, c);

    auto order = g.topo_sort();
    ASSERT_TRUE(order.has_value()) << order.error().message;
    ASSERT_EQ(order->size(), 3u);

    // a must come before b, b before c.
    auto pos = [&](TaskId id) -> std::size_t {
        for (std::size_t i = 0; i < order->size(); ++i)
            if ((*order)[i] == id) return i;
        return SIZE_MAX;
    };
    EXPECT_LT(pos(a), pos(b));
    EXPECT_LT(pos(b), pos(c));
}

TEST(BuildGraph, TopoSortCycleDetected) {
    BuildGraph g;
    TaskId a = g.add(phony("a"));
    TaskId b = g.add(phony("b"));
    (void)g.add_dep(a, b);
    (void)g.add_dep(b, a);   // cycle!

    auto order = g.topo_sort();
    EXPECT_FALSE(order.has_value());
    EXPECT_FALSE(order.error().message.empty());
}

TEST(BuildGraph, TopoSortDiamondDAG) {
    //     root
    //    /    \
    //   left  right
    //    \    /
    //     sink
    BuildGraph g;
    TaskId root  = g.add(phony("root"));
    TaskId left  = g.add(phony("left"));
    TaskId right = g.add(phony("right"));
    TaskId sink  = g.add(phony("sink"));

    (void)g.add_dep(root, left);
    (void)g.add_dep(root, right);
    (void)g.add_dep(left, sink);
    (void)g.add_dep(right, sink);

    auto order = g.topo_sort();
    ASSERT_TRUE(order.has_value()) << order.error().message;
    ASSERT_EQ(order->size(), 4u);

    auto pos = [&](TaskId id) -> std::size_t {
        for (std::size_t i = 0; i < order->size(); ++i)
            if ((*order)[i] == id) return i;
        return SIZE_MAX;
    };
    EXPECT_LT(pos(root),  pos(left));
    EXPECT_LT(pos(root),  pos(right));
    EXPECT_LT(pos(left),  pos(sink));
    EXPECT_LT(pos(right), pos(sink));
}

// ─── Critical path weights ────────────────────────────────────────────────────

TEST(BuildGraph, CriticalPathLinear) {
    //  a(1) → b(2) → c(3)
    // Critical path = 3 (from a's perspective)
    BuildGraph g;
    TaskId a = g.add(phony("a"));
    TaskId b = g.add(phony("b"));
    TaskId c = g.add(phony("c"));
    (void)g.add_dep(a, b);
    (void)g.add_dep(b, c);

    auto w = g.critical_path_weights();
    EXPECT_GT(w[a], w[b]);
    EXPECT_GT(w[b], w[c]);
    EXPECT_EQ(w[c], 1);
}

// ─── Ready tasks ─────────────────────────────────────────────────────────────

TEST(BuildGraph, ReadyTasksNoDeps) {
    BuildGraph g;
    (void)g.add(phony("a"));
    (void)g.add(phony("b"));
    // No deps: both are immediately ready.
    auto ready = g.ready_tasks();
    EXPECT_EQ(ready.size(), 2u);
}

TEST(BuildGraph, ReadyTasksWithDeps) {
    BuildGraph g;
    TaskId a = g.add(phony("a"));
    TaskId b = g.add(phony("b"));
    (void)g.add_dep(a, b);
    // Only a is ready (b needs a to be Done first).
    auto ready = g.ready_tasks();
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0], a);
}

// ─── Dependents ──────────────────────────────────────────────────────────────

TEST(BuildGraph, DependentsTransitive) {
    BuildGraph g;
    TaskId a = g.add(phony("a"));
    TaskId b = g.add(phony("b"));
    TaskId c = g.add(phony("c"));
    (void)g.add_dep(a, b);
    (void)g.add_dep(b, c);

    auto deps = g.dependents_of(a);
    ASSERT_EQ(deps.size(), 2u);
}

// ─── Scheduler: plan() ───────────────────────────────────────────────────────

TEST(Scheduler, PlanLinear) {
    BuildGraph g;
    TaskId a = g.add(phony("a"));
    TaskId b = g.add(phony("b"));
    (void)g.add_dep(a, b);

    Scheduler sched{g, 4};
    auto plan = sched.plan();
    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    // Two waves: [a], [b].
    ASSERT_EQ(plan->size(), 2u);
    EXPECT_EQ((*plan)[0].size(), 1u); // wave 0: a
    EXPECT_EQ((*plan)[1].size(), 1u); // wave 1: b
    EXPECT_EQ((*plan)[0][0], a);
    EXPECT_EQ((*plan)[1][0], b);
}

TEST(Scheduler, PlanParallelism) {
    // All independent: should be one wave.
    BuildGraph g;
    for (int i = 0; i < 5; ++i) (void)g.add(phony("task" + std::to_string(i)));

    Scheduler sched{g, 8};
    auto plan = sched.plan();
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->size(), 1u);
    EXPECT_EQ((*plan)[0].size(), 5u);
}
