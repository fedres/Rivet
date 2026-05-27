// runtime/toolchain/compile.cpp — CompileJob → command translation
#include "compile.hpp"
#include "../../platform/interface/process.hpp"

#include <algorithm>
#include <format>
#include <mutex>

namespace rivet::toolchain {

// ─── Shared helpers ──────────────────────────────────────────────────────────

#if defined(__APPLE__)
// Apple's headers (stdio.h, stdlib.h, string.h, ...) live inside the Xcode
// SDK at /Applications/Xcode*.app/Contents/Developer/Platforms/MacOSX.platform/
// Developer/SDKs/MacOSX.sdk — the bundled clang has no idea where that is.
// Without -isysroot pointing at it, every translation unit fails on the
// first <stdio.h> with "unknown type name 'size_t'" etc. (Caught by the
// macOS smoke test at step 7.) Probe xcrun once, cache forever.
static const std::string& macos_sdk_path() {
    static std::once_flag once;
    static std::string cached;
    std::call_once(once, [] {
        rivet::process::SpawnOptions opts;
        opts.args = {"xcrun", "--show-sdk-path"};
        opts.inherit_env    = true;
        opts.capture_stdout = true;
        opts.capture_stderr = true;
        auto r = rivet::process::run(std::move(opts));
        if (!r || r->exit_code != 0) return;
        std::string out = r->stdout_output;
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
            out.pop_back();
        cached = std::move(out);
    });
    return cached;
}
#endif

static std::string opt_flag(build::OptLevel opt) {
    switch (opt) {
        case build::OptLevel::Debug: return "-O0";
        case build::OptLevel::O1:    return "-O1";
        case build::OptLevel::O2:    return "-O2";
        case build::OptLevel::O3:    return "-O3";
        case build::OptLevel::Size:  return "-Os";
    }
    return "-O0";
}

// ─── make_compile_command() ──────────────────────────────────────────────────

Result<build::CompileCommand> make_compile_command(const CompileJob& job,
                                                    const ToolchainInfo& tc) {
    build::CompileCommand cmd;
    // Dispatch on source extension: `.c` → C compiler (clang), everything
    // else → C++ compiler (clang++). Without this the bundled clang++ drives
    // C amalgamations (vendored sqlite, libzstd) as if they were C++, which
    // breaks on K&R prototypes / typedef redefinitions / `register` use.
    auto ext = job.source.extension().string();
    bool is_c = (ext == ".c");
    cmd.executable   = (is_c ? tc.clang() : tc.clangpp()).string();
    cmd.working_dir  = job.source.parent_path();

    auto& args = cmd.args;

    // Standard language options. `-std=c++NN` is meaningless to clang in
    // C mode — let .c sources go in with the toolchain default (C17 since
    // clang-17), explicitly tag the compile as C/C++ via -x so a file with
    // an unrecognised extension (e.g. .S, .cxx) still routes correctly.
    if (is_c) {
        args.push_back("-x"); args.push_back("c");
    } else {
        args.push_back("-x"); args.push_back("c++");
        args.push_back("-std=" + job.cxx_std);
    }
    args.push_back(opt_flag(job.opt));
    if (job.debug) args.push_back("-g");
    if (job.lto)   args.push_back("-flto=thin");

#if defined(__APPLE__)
    if (const auto& sdk = macos_sdk_path(); !sdk.empty()) {
        args.push_back("-isysroot");
        args.push_back(sdk);
    }
#endif

    // Target triple.
    if (!job.target_triple.empty()) {
        args.push_back("--target=" + job.target_triple);
    }

    // Sanitizers.
    if (!job.sanitizers.empty()) {
        std::string san_list;
        for (std::size_t i = 0; i < job.sanitizers.size(); ++i) {
            if (i) san_list += ',';
            san_list += job.sanitizers[i];
        }
        args.push_back("-fsanitize=" + san_list);
        args.push_back("-fno-omit-frame-pointer");
    }

    // Include paths.
    for (const auto& inc : job.include_paths)
        args.push_back("-I" + inc.string());

    // Defines.
    for (const auto& def : job.defines)
        args.push_back("-D" + def);

    // Extra flags (passthrough).
    for (const auto& f : job.flags)
        args.push_back(f);

    // Dep file.
    if (job.generate_deps && !job.dep_file.empty()) {
        args.push_back("-MD");
        args.push_back("-MF");
        args.push_back(job.dep_file.string());
    }

    // Compile-only, output.
    args.push_back("-c");
    args.push_back("-o"); args.push_back(job.output.string());
    args.push_back(job.source.string());

    return cmd;
}

// ─── make_link_command() ─────────────────────────────────────────────────────

Result<build::CompileCommand> make_link_command(const LinkJob& job,
                                                  const ToolchainInfo& tc) {
    build::CompileCommand cmd;
    cmd.executable = tc.clangpp().string();

    auto& args = cmd.args;

    if (!job.target_triple.empty())
        args.push_back("--target=" + job.target_triple);

    if (job.shared) args.push_back("-shared");
    if (job.lto)    args.push_back("-flto=thin");

    // Use lld.
    args.push_back("-fuse-ld=lld");

#if defined(__APPLE__)
    if (const auto& sdk = macos_sdk_path(); !sdk.empty()) {
        args.push_back("-isysroot");
        args.push_back(sdk);
    }
#endif

    // Sanitizers.
    if (!job.sanitizers.empty()) {
        std::string san_list;
        for (std::size_t i = 0; i < job.sanitizers.size(); ++i) {
            if (i) san_list += ',';
            san_list += job.sanitizers[i];
        }
        args.push_back("-fsanitize=" + san_list);
    }

    // Input object files.
    for (const auto& inp : job.inputs)
        args.push_back(inp.string());

    // Library search paths.
    for (const auto& lsp : job.lib_search_paths)
        args.push_back("-L" + lsp);

    // Link libraries.
    for (const auto& lib : job.link_libs)
        args.push_back(lib.string());

    // Extra linker flags.
    for (const auto& f : job.flags)
        args.push_back(f);

    args.push_back("-o"); args.push_back(job.output.string());

    return cmd;
}

// ─── make_archive_command() ──────────────────────────────────────────────────

Result<build::CompileCommand> make_archive_command(const ArchiveJob& job,
                                                     const ToolchainInfo& tc) {
    build::CompileCommand cmd;
    cmd.executable = tc.llvm_ar().string();

    cmd.args.push_back("rcs");
    cmd.args.push_back(job.output.string());
    for (const auto& inp : job.inputs)
        cmd.args.push_back(inp.string());

    return cmd;
}

// ─── compile_job_from() ──────────────────────────────────────────────────────

CompileJob compile_job_from(const Path& source, const Path& output,
                             const build::BuildConfig& cfg,
                             const ToolchainInfo& /*tc*/) {
    CompileJob job;
    job.source         = source;
    job.output         = output;
    job.dep_file       = output.parent_path() / (output.filename().string() + ".d");
    job.target_triple  = cfg.target_triple;
    job.cxx_std        = cfg.cxx_std;
    job.opt            = cfg.opt;
    job.debug          = cfg.debug;
    job.lto            = cfg.lto;
    job.flags          = cfg.extra_flags;
    job.include_paths  = cfg.include_paths;
    job.defines        = cfg.defines;
    job.sanitizers     = cfg.sanitizers;
    return job;
}

} // namespace rivet::toolchain
