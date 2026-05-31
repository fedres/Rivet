// runtime/build/executor.cpp — thread-pool build executor
#include "executor.hpp"
#include "dep_file.hpp"
#include "../cache/key.hpp"
#include "../../platform/interface/process.hpp"
#include "../../platform/interface/sandbox.hpp"
#include "../../platform/interface/env.hpp"
#include "../../platform/interface/time.hpp"
#include "../../platform/interface/fs.hpp"

#include <algorithm>
#include <format>
#include <thread>
#include <unordered_set>

namespace rivet::build {

// ─── Construction ────────────────────────────────────────────────────────────

Executor::Executor(BuildGraph& graph, std::size_t thread_count,
                   ProgressCallback on_progress, cache::Store* cache)
    : graph_(graph)
    , thread_count_(thread_count == 0 ? std::thread::hardware_concurrency() : thread_count)
    , on_progress_(std::move(on_progress))
    , cache_(cache)
{}

Executor::~Executor() {
    cancel();
    done_ = true;
    queue_cv_.notify_all();
}

// ─── run() ───────────────────────────────────────────────────────────────────

BuildSummary Executor::run() {
    auto start = rivet::time::now();

    // Compute initial in-degrees from node.deps.
    {
        std::lock_guard lock(in_degree_mutex_);
        for (const auto& node : graph_.nodes())
            in_degree_[node.id] = static_cast<int>(node.deps.size());
    }

    // Seed the ready queue with zero-dep tasks.
    {
        std::lock_guard lock(queue_mutex_);
        for (const auto& node : graph_.nodes()) {
            if (node.deps.empty())
                ready_queue_.push(node.id);
        }
    }

    // Spawn worker threads.
    workers_.reserve(thread_count_);
    for (std::size_t i = 0; i < thread_count_; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }

    for (auto& w : workers_) w.join();
    workers_.clear();

    // Build summary.
    BuildSummary summary;
    summary.wall_time = rivet::time::elapsed(start);

    std::lock_guard lock(results_mutex_);
    summary.total = results_.size();
    for (const auto& r : results_) {
        if (r.cache_hit)  ++summary.cached;
        if (r.success)    ++summary.succeeded;
        else {
            ++summary.failed;
            summary.failures.push_back(r);
        }
    }
    return summary;
}

// ─── cancel() ────────────────────────────────────────────────────────────────

void Executor::cancel() {
    cancel_.store(true, std::memory_order_relaxed);
    queue_cv_.notify_all();
}

// ─── Worker loop ─────────────────────────────────────────────────────────────

void Executor::worker_loop() {
    while (!cancel_.load(std::memory_order_relaxed)) {
        TaskId id = kInvalidTaskId;

        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [&] {
                return !ready_queue_.empty() || done_ || cancel_.load();
            });

            if (cancel_.load() || (done_ && ready_queue_.empty())) return;
            if (ready_queue_.empty()) continue;

            id = ready_queue_.front();
            ready_queue_.pop();
        }

        if (id == kInvalidTaskId) continue;

        graph_.at(id).state = TaskNode::State::Running;

        const TaskNode& node = graph_.at(id);
        TaskResult result = execute_task(node);

        graph_.at(id).state = result.success
            ? TaskNode::State::Done
            : TaskNode::State::Failed;

        task_completed(result);

        if (!result.success && !cancel_.load()) {
            cancel();
            return;
        }

        enqueue_ready_successors(id);
    }
}

// ─── execute_task() ──────────────────────────────────────────────────────────

TaskResult Executor::execute_task(const TaskNode& task) {
    TaskResult r;
    r.task_id = task.id;
    auto t_start = rivet::time::now();

    if (task.kind == TaskKind::Phony) {
        r.success = true;
        r.elapsed = rivet::time::elapsed(t_start);
        return r;
    }

    // ── Cache lookup ─────────────────────────────────────────────────────────
    if (cache_ && task.cache_key && !task.cache_key->empty()) {
        bool all_outputs_exist = !task.outputs.empty();
        for (const auto& out : task.outputs) {
            if (out.primary && !rivet::fs::exists(out.path).value_or(false)) {
                all_outputs_exist = false;
                break;
            }
        }

        if (!all_outputs_exist) {
            // 1. Try local cache.
            bool hit_local = cache_->has(*task.cache_key);
            if (hit_local) {
                for (const auto& out : task.outputs) {
                    if (out.primary) {
                        if (auto pr = rivet::fs::create_dirs(out.path.parent_path()); !pr)
                            goto recompile;
                        if (auto fr = cache_->fetch(*task.cache_key, out.path); !fr)
                            goto recompile;
                    }
                }
                r.cache_hit = true;
                r.success   = true;
                r.elapsed   = rivet::time::elapsed(t_start);
                return r;
            }

            // 2. Try remote cache (only for primary output tasks).
            if (!task.outputs.empty()) {
                const auto& primary_out = [&]() -> const OutputFile* {
                    for (const auto& o : task.outputs)
                        if (o.primary) return &o;
                    return nullptr;
                }();
                if (primary_out) {
                    if (auto pr = rivet::fs::create_dirs(primary_out->path.parent_path()); pr) {
                        if (auto rr = cache_->fetch_remote(*task.cache_key, primary_out->path); rr) {
                            r.cache_hit = true;
                            r.success   = true;
                            r.elapsed   = rivet::time::elapsed(t_start);
                            return r;
                        }
                    }
                }
            }
        }
    }

    recompile:
    if (!task.command && task.raw_command.empty()) {
        r.success = true;
        r.elapsed = rivet::time::elapsed(t_start);
        return r;
    }

    // ── Ensure output directories exist ──────────────────────────────────────
    for (const auto& out : task.outputs) {
        if (out.primary) (void)rivet::fs::create_dirs(out.path.parent_path());
    }

    // ── Build argument vector ─────────────────────────────────────────────────
    std::vector<std::string> argv;
    std::string exe;

    if (task.command) {
        exe  = task.command->executable;
        argv = task.command->args;
    } else {
        exe  = task.raw_command[0];
        argv = {task.raw_command.begin() + 1, task.raw_command.end()};
    }

    rivet::process::SpawnOptions opts;
    opts.capture_stdout = true;
    opts.capture_stderr = true;
    opts.inherit_env    = true;   // inherit PATH so clang can find system headers

    if (task.command && !task.command->working_dir.empty())
        opts.working_dir = task.command->working_dir;

    argv.insert(argv.begin(), exe);
    opts.args = std::move(argv);

    // ── D2: optionally route through the sandbox backend ────────────────
    // RIVET_SANDBOX=1 enables hermetic compile sandboxing. The policy
    // allows reads on the toolchain root, /usr (system headers on Linux),
    // and every input file's parent dir, plus writes to every output dir
    // and the TMPDIR blanket. Compile tasks only -- linker / archive
    // tasks aren't gated yet.
    auto sandbox_env = rivet::env::get("RIVET_SANDBOX");
    bool want_sandbox = sandbox_env.has_value() && *sandbox_env == "1"
                        && task.command
                        && task.kind == TaskKind::Compile;

    auto child_r = [&]() {
        if (want_sandbox && rivet::sandbox::is_supported()) {
            rivet::sandbox::SandboxPolicy policy;
            policy.allow_tmpdir = true;
            policy.network      = rivet::sandbox::NetworkPolicy::DenyAll;

            // Toolchain root: parent of the exe's bin/ dir.
            Path exe_path{task.command->executable};
            Path toolchain_root = exe_path.parent_path().parent_path();
            if (!toolchain_root.empty()) {
                policy.path_rules.push_back({toolchain_root,
                    rivet::sandbox::PathRule::Access::ReadOnly, true});
            }
            // System headers (no-op on macOS thanks to the baked profile).
            policy.path_rules.push_back({Path{"/usr"},
                rivet::sandbox::PathRule::Access::ReadOnly, true});

            // Input parents (covers source dir + transitive header roots).
            for (const auto& in : task.inputs) {
                Path d = in.path.parent_path();
                if (!d.empty()) policy.path_rules.push_back({d,
                    rivet::sandbox::PathRule::Access::ReadOnly, true});
            }
            // Output parents.
            for (const auto& out : task.outputs) {
                Path d = out.path.parent_path();
                if (!d.empty()) policy.path_rules.push_back({d,
                    rivet::sandbox::PathRule::Access::ReadWrite, true});
            }
            return rivet::sandbox::spawn_sandboxed(std::move(opts), std::move(policy));
        }
        return rivet::process::spawn(std::move(opts));
    }();
    if (!child_r) {
        r.success    = false;
        r.stderr_out = std::format("spawn failed: {}", child_r.error().message);
        r.elapsed    = rivet::time::elapsed(t_start);
        return r;
    }

    auto& child  = *child_r;
    auto  wait_r = child.wait();
    r.exit_code  = wait_r ? *wait_r : -1;
    r.success    = (r.exit_code == 0);
    r.elapsed    = rivet::time::elapsed(t_start);
    r.stdout_out = child.stdout_output();
    r.stderr_out = child.stderr_output();

    // ── D1: augment cache key with transitive header hashes (post-compile) ──
    //
    // The construction-time cache_key was derived from the source file alone --
    // it's the right key for the FIRST build (no .d file yet) but a stale key
    // on subsequent builds, because clang's .d file lists every header the
    // translation unit pulled in. We re-derive after compile using the
    // populated .d file so the same logical inputs hash to the same key on
    // every subsequent build (which is exactly what `derive_key` with the
    // augmented input list will produce at construction time on the next run).
    std::optional<build::CacheKey> augmented_key;
    if (r.success && task.command && task.cache_key && !task.cache_key->empty()) {
        // Locate the .d file argument: clang emits `-MF <path>`.
        const auto& args = task.command->args;
        Path dep_path;
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "-MF") { dep_path = Path{args[i + 1]}; break; }
        }
        if (!dep_path.empty() && rivet::fs::exists(dep_path).value_or(false)) {
            TaskNode aug = task;  // mutate a copy; the graph stays frozen.
            augment_inputs_with_dep_file(aug, dep_path);
            // Reuse the same derive_key overload the graph builder used at
            // construction time so the augmented key is bit-identical to what
            // a future graph build will compute on its first lookup.
            if (auto kr = cache::derive_key(aug, aug.tool_version, aug.target_triple)) {
                augmented_key = std::move(*kr);
            }
        }
    }

    // ── Store successful output in local + remote cache ──────────────────────
    //
    // Store under BOTH the original key (so this run is internally consistent
    // and re-runs without a .d file still hit) AND the augmented key (so the
    // next build's construction-time lookup -- which re-parses the .d -- can
    // hit without compiling).
    if (r.success && cache_ && task.cache_key && !task.cache_key->empty()) {
        for (const auto& out : task.outputs) {
            if (out.primary && rivet::fs::exists(out.path).value_or(false)) {
                (void)cache_->put(*task.cache_key, out.path);
                cache_->push_remote(*task.cache_key, out.path);
                if (augmented_key && !augmented_key->empty() &&
                    *augmented_key != *task.cache_key) {
                    (void)cache_->put(*augmented_key, out.path);
                    cache_->push_remote(*augmented_key, out.path);
                }
            }
        }
    }

    return r;
}

// ─── task_completed() ────────────────────────────────────────────────────────

void Executor::task_completed(const TaskResult& r) {
    {
        std::lock_guard lock(results_mutex_);
        results_.push_back(r);
    }
    if (on_progress_) on_progress_(r);
}

// ─── enqueue_ready_successors() ──────────────────────────────────────────────

void Executor::enqueue_ready_successors(TaskId completed_id) {
    // O(successors) per call — uses the adjacency list instead of O(n) scan.
    for (TaskId succ_id : graph_.successors_of(completed_id)) {
        bool ready = false;
        {
            std::lock_guard lock(in_degree_mutex_);
            auto it = in_degree_.find(succ_id);
            if (it != in_degree_.end() && --it->second == 0)
                ready = true;
        }

        if (ready) {
            auto* succ = graph_.find(succ_id);
            if (succ && succ->state == TaskNode::State::Pending) {
                std::lock_guard lock(queue_mutex_);
                ready_queue_.push(succ_id);
                queue_cv_.notify_one();
            }
        }
    }

    // Signal done when all tasks have reached a terminal state.
    bool all_finished = true;
    for (const auto& n : graph_.nodes()) {
        if (n.state == TaskNode::State::Pending ||
            n.state == TaskNode::State::Ready   ||
            n.state == TaskNode::State::Running) {
            all_finished = false;
            break;
        }
    }
    if (all_finished) {
        done_ = true;
        queue_cv_.notify_all();
    }
}

} // namespace rivet::build
