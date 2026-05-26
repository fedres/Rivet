// runtime/build/ir.hpp — Rivet Build Intermediate Representation
//
// All build logic is compiled down to a Build IR DAG before execution.
// The IR is platform-neutral: no platform-specific types appear here.
//
// DAG flow:
//   cx.toml / rivet.build
//         |
//         v
//   [Parser / Evaluator]  -->  BuildGraph (this file)
//         |
//         v
//   [Scheduler]           -->  ordered TaskNode list with parallelism info
//         |
//         v
//   [Executor]            -->  dispatches CompileJobs / LinkJobs to worker pool

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../../platform/interface/types.hpp"

namespace rivet::build {

// ─── Identification ───────────────────────────────────────────────────────────

using TaskId   = uint32_t;
constexpr TaskId kInvalidTaskId = 0;

// ─── Task kinds ───────────────────────────────────────────────────────────────

enum class TaskKind : uint8_t {
    Compile,        // C++ → .o
    Link,           // .o* → binary or shared lib
    Archive,        // .o* → static lib
    CompileModule,  // C++20 module unit → .bmi
    CopyFile,       // verbatim file copy (resources, headers)
    CustomCommand,  // arbitrary shell command
    Phony,          // pure ordering dependency (like Makefile PHONY)
};

[[nodiscard]] constexpr std::string_view task_kind_name(TaskKind k) noexcept {
    switch (k) {
        case TaskKind::Compile:       return "compile";
        case TaskKind::Link:          return "link";
        case TaskKind::Archive:       return "archive";
        case TaskKind::CompileModule: return "compile-module";
        case TaskKind::CopyFile:      return "copy";
        case TaskKind::CustomCommand: return "custom";
        case TaskKind::Phony:         return "phony";
    }
    return "unknown";
}

// ─── File descriptors ────────────────────────────────────────────────────────

struct InputFile {
    Path        path;
    std::string content_hash;   // SHA-256 hex; populated during graph prep
};

struct OutputFile {
    Path        path;
    bool        primary = true;  // false for side outputs (e.g. .d files)
};

// ─── Cache key ───────────────────────────────────────────────────────────────

// Derived from: tool_version + target_triple + flags + input hashes + defines.
// Timestamps are NEVER part of the key — only content.
struct CacheKey {
    std::string hex;    // 64-char hex SHA-256 of the full key material

    bool operator==(const CacheKey& o) const noexcept { return hex == o.hex; }
    bool operator!=(const CacheKey& o) const noexcept { return hex != o.hex; }
    bool empty() const noexcept { return hex.empty(); }
};

// ─── Compile command ─────────────────────────────────────────────────────────

struct CompileCommand {
    std::string              executable;        // absolute path to clang/clang++
    std::vector<std::string> args;              // full argument list (no exe)
    Path                     working_dir;

    // Rendered as a string for compile_commands.json and debugging.
    [[nodiscard]] std::string to_string() const;
};

// ─── Task node (the fundamental unit of the Build IR) ────────────────────────

struct TaskNode {
    TaskId                   id      = kInvalidTaskId;
    std::string              name;               // human-readable (e.g. "src/foo.cpp")
    TaskKind                 kind    = TaskKind::CustomCommand;
    std::vector<TaskId>      deps;               // all tasks that must complete first

    std::vector<InputFile>   inputs;
    std::vector<OutputFile>  outputs;

    // For Compile / CompileModule / Link / Archive tasks:
    std::optional<CompileCommand> command;

    // For CustomCommand tasks:
    std::vector<std::string> raw_command;        // argv split

    // Populated during graph preparation; empty means "not yet computed".
    std::optional<CacheKey>  cache_key;

    // Execution state (set by Executor — treat as mutable scheduler data).
    enum class State : uint8_t { Pending, Ready, Running, Done, Failed, Skipped };
    State state = State::Pending;
};

// ─── Build graph ──────────────────────────────────────────────────────────────

// Forward-declared here; full definition in graph.hpp.
class BuildGraph;

// ─── ABI tag (§18) ───────────────────────────────────────────────────────────

struct ABITag {
    std::string compiler;       // "clang-18.1.0"
    std::string stdlib;         // "libc++-18"
    std::string target_triple;  // "x86_64-linux-gnu"
    std::string cxx_std;        // "c++23"
    bool        debug    = true;
    bool        lto      = false;
    std::vector<std::string> sanitizers;  // ["address", "undefined"]

    [[nodiscard]] bool compatible_with(const ABITag& other) const noexcept;
};

// ─── Build configuration (mirrors rivet.toml [build] section) ────────────────

enum class OptLevel { Debug = 0, O1 = 1, O2 = 2, O3 = 3, Size };

struct BuildConfig {
    std::string  target_triple;
    OptLevel     opt     = OptLevel::Debug;
    bool         debug   = true;
    bool         lto     = false;
    std::vector<std::string> extra_flags;
    std::vector<std::string> sanitizers;
    std::vector<Path>        include_paths;
    std::vector<std::string> defines;
    std::string              cxx_std = "c++23";
};

// ─── Inline helpers ───────────────────────────────────────────────────────────

inline bool ABITag::compatible_with(const ABITag& o) const noexcept {
    // Extract major version from "clang-18.1.0" → "18"
    auto major = [](const std::string& ver) -> std::string {
        auto dot = ver.find('.');
        return dot != std::string::npos ? ver.substr(0, dot) : ver;
    };
    return major(compiler)  == major(o.compiler)  &&
           stdlib            == o.stdlib           &&
           target_triple     == o.target_triple    &&
           cxx_std           == o.cxx_std          &&
           debug             == o.debug            &&
           lto               == o.lto              &&
           sanitizers        == o.sanitizers;
}

inline std::string CompileCommand::to_string() const {
    std::string s = executable;
    for (const auto& a : args) { s += ' '; s += a; }
    return s;
}

} // namespace rivet::build

// Hash support for CacheKey (for use in std::unordered_map).
template <>
struct std::hash<rivet::build::CacheKey> {
    std::size_t operator()(const rivet::build::CacheKey& k) const noexcept {
        return std::hash<std::string>{}(k.hex);
    }
};
