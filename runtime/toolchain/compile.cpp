// runtime/toolchain/compile.cpp — CompileJob → command translation
#include "compile.hpp"
#include "../../platform/interface/process.hpp"

#include <algorithm>
#include <format>
#include <iostream>
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

// Classify a source-file extension into a compile mode. Centralised so the
// dispatch table is one place to read instead of a chain of conditionals.
//
// Hardened against two real footguns:
//   1. A header file (.h/.hpp/.hh/.hxx/.h++/.inl/.ipp) accidentally listed
//      under `sources = [...]`. clang will dutifully drive the header as a
//      TU, emit a redundant .o, and produce confusing duplicate-symbol
//      errors at link time.
//   2. An unrecognised extension silently routing to C++ — the previous
//      "everything else → c++" rule worked for .cpp/.cc but quietly
//      mis-compiled .s/.S as C++, and gave no signal for typos.
namespace {

enum class SourceKind { C, CXX, Header, Unknown };

SourceKind classify_extension(const std::string& ext) {
    if (ext == ".c")                              return SourceKind::C;
    if (ext == ".cpp" || ext == ".cc"  ||
        ext == ".cxx" || ext == ".C"   ||
        ext == ".c++")                            return SourceKind::CXX;
    if (ext == ".h"   || ext == ".hpp" ||
        ext == ".hh"  || ext == ".hxx" ||
        ext == ".h++" || ext == ".inl" ||
        ext == ".ipp" || ext == ".tpp")           return SourceKind::Header;
    return SourceKind::Unknown;
}

} // namespace

Result<build::CompileCommand> make_compile_command(const CompileJob& job,
                                                    const ToolchainInfo& tc) {
    build::CompileCommand cmd;

    auto ext  = job.source.extension().string();
    auto kind = classify_extension(ext);

    if (kind == SourceKind::Header) {
        return make_error<build::CompileCommand>(std::format(
            "source '{}' is a header file ({}) — headers must not appear in "
            "`sources = [...]`; remove it from the manifest target",
            job.source.string(), ext));
    }
    if (kind == SourceKind::Unknown) {
        std::cerr << "warning: source '" << job.source.string()
                  << "' has unrecognised extension '" << ext
                  << "' — compiling as C++ (rename to .cpp/.cc/.cxx, or "
                  << "add .c for a C source)\n";
        kind = SourceKind::CXX;
    }

    bool is_c = (kind == SourceKind::C);
    cmd.executable   = (is_c ? tc.clang() : tc.clangpp()).string();
    cmd.working_dir  = job.source.parent_path();

    auto& args = cmd.args;

    // Standard language options. `-std=c++NN` is meaningless to clang in
    // C mode — let .c sources go in with the toolchain default (C17 since
    // clang-17), explicitly tag the compile as C/C++ via -x.
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

    // Target triple. On Windows, force the MinGW target if the caller
    // didn't supply one — bundled llvm-mingw is configured for the GNU
    // ABI and silently picks msvc otherwise.
#if defined(_WIN32)
    if (job.target_triple.empty()) {
        args.push_back("--target=x86_64-w64-mingw32");
    } else {
        args.push_back("--target=" + job.target_triple);
    }
#else
    if (!job.target_triple.empty()) {
        args.push_back("--target=" + job.target_triple);
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
#if defined(_WIN32)
    // MinGW ABI: target triple + GNU-style static link of the C++ runtime
    // libs (libc++.a, libunwind.a, the mingw CRT). Without the target
    // override, llvm-mingw's clang defaults to msvc.
    if (job.target_triple.empty()) {
        args.push_back("--target=x86_64-w64-mingw32");
    }
    args.push_back("-static-libstdc++");
    args.push_back("-static-libgcc");
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
