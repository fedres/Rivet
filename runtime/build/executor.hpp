// runtime/build/executor.hpp — thread-pool build executor
#pragma once

#include "graph.hpp"
#include "../../platform/interface/result.hpp"
#include "../../platform/interface/process.hpp"
#include "../cache/store.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>

namespace rivet::build {

/// Outcome of running a single TaskNode.
struct TaskResult {
    TaskId      task_id;
    bool        cache_hit  = false;  // true if satisfied from local cache
    bool        success    = false;
    int         exit_code  = 0;
    std::string stdout_out;
    std::string stderr_out;
    std::chrono::nanoseconds elapsed{0};
};

/// Progress callback — invoked (from any thread) when a task completes.
using ProgressCallback = std::function<void(const TaskResult&)>;

/// Build summary returned by Executor::run().
struct BuildSummary {
    std::size_t total     = 0;
    std::size_t succeeded = 0;
    std::size_t failed    = 0;
    std::size_t cached    = 0;
    std::chrono::nanoseconds wall_time{0};
    std::vector<TaskResult>  failures;  // populated only for failed tasks
};

/// Executor drives the DAG to completion using a worker-thread pool.
///
/// Lifecycle:
///   1. Construct with a graph, thread count, and optional progress callback.
///   2. Call run() — blocks until all tasks complete or a fatal error occurs.
///   3. Inspect the returned BuildSummary.
///
/// Cancellation: call cancel() from any thread to abort the build.
/// In-flight processes are killed; pending tasks are skipped.
class Executor {
public:
    explicit Executor(BuildGraph&      graph,
                      std::size_t      thread_count = 0,  // 0 = hardware_concurrency
                      ProgressCallback on_progress  = {},
                      cache::Store*    cache        = nullptr);

    ~Executor();

    // Non-copyable, non-movable.
    Executor(const Executor&)            = delete;
    Executor& operator=(const Executor&) = delete;

    /// Run the build to completion. Blocking.
    [[nodiscard]] BuildSummary run();

    /// Signal all workers to stop after their current task. Thread-safe.
    void cancel();

    [[nodiscard]] bool is_cancelled() const noexcept {
        return cancel_.load(std::memory_order_relaxed);
    }

private:
    BuildGraph&       graph_;
    std::size_t       thread_count_;
    ProgressCallback  on_progress_;
    cache::Store*     cache_  = nullptr;

    std::atomic<bool> cancel_{false};

    // Worker thread pool.
    std::vector<std::thread>       workers_;
    std::mutex                     queue_mutex_;
    std::condition_variable        queue_cv_;
    std::queue<TaskId>             ready_queue_;
    bool                           done_{false};

    // Per-task in-degree tracking (mutable during execution).
    std::unordered_map<TaskId, int> in_degree_;
    std::mutex                     in_degree_mutex_;

    std::vector<TaskResult>        results_;
    std::mutex                     results_mutex_;

    void     worker_loop();
    TaskResult execute_task(const TaskNode& task);
    void     task_completed(const TaskResult& r);
    void     enqueue_ready_successors(TaskId completed_id);
};

} // namespace rivet::build
