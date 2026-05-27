// tests/unit/test_compile.cpp — source extension dispatch
//
// Locks down the C/C++/header/unknown classification in make_compile_command.
// The codebase used to dispatch with a bare `ext == ".c"` check; anything
// else routed to clang++. That mis-compiled vendored .c amalgs as C++ and
// silently accepted headers in `sources = [...]` (linking would later fail
// with confusing duplicate-symbol errors).
#include "runtime/toolchain/compile.hpp"
#include "runtime/toolchain/discovery.hpp"

#include <gtest/gtest.h>

using namespace rivet::toolchain;

static ToolchainInfo fake_tc() {
    ToolchainInfo tc;
    tc.root    = rivet::Path{"/tmp/rivet-fake-toolchain"};
    tc.version = "fake-19.1.0";
    return tc;
}

static CompileJob job_for(const std::string& source_path) {
    CompileJob cj;
    cj.source  = rivet::Path{source_path};
    cj.output  = rivet::Path{source_path + ".o"};
    cj.cxx_std = "c++23";
    return cj;  // opt defaults to OptLevel::Debug
}

TEST(CompileDispatch, CSourceRoutesToCCompiler) {
    auto tc = fake_tc();
    auto r  = make_compile_command(job_for("/proj/vendor/sqlite/sqlite3.c"), tc);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_NE(r->executable.find("clang"), std::string::npos);
    EXPECT_EQ(r->executable.find("clang++"), std::string::npos);  // not clang++
    // -x c, no -std= flag
    bool has_x_c = false, has_std = false;
    for (size_t i = 0; i + 1 < r->args.size(); ++i) {
        if (r->args[i] == "-x" && r->args[i + 1] == "c") has_x_c = true;
        if (r->args[i].rfind("-std=", 0) == 0) has_std = true;
    }
    EXPECT_TRUE(has_x_c);
    EXPECT_FALSE(has_std) << "C source must not get a -std=c++ flag";
}

TEST(CompileDispatch, CppExtensionsAllRouteToCxx) {
    auto tc = fake_tc();
    for (auto ext : {".cpp", ".cc", ".cxx", ".C", ".c++"}) {
        auto r = make_compile_command(job_for(std::string("/proj/foo") + ext), tc);
        ASSERT_TRUE(r.has_value()) << "extension " << ext << ": " << r.error().message;
        EXPECT_NE(r->executable.find("clang++"), std::string::npos) << "ext=" << ext;
        bool has_x_cxx = false;
        for (size_t i = 0; i + 1 < r->args.size(); ++i)
            if (r->args[i] == "-x" && r->args[i + 1] == "c++") has_x_cxx = true;
        EXPECT_TRUE(has_x_cxx) << "ext=" << ext;
    }
}

TEST(CompileDispatch, HeaderInSourcesIsHardError) {
    auto tc = fake_tc();
    for (auto ext : {".h", ".hpp", ".hh", ".hxx", ".h++", ".inl", ".ipp", ".tpp"}) {
        auto r = make_compile_command(job_for(std::string("/proj/foo") + ext), tc);
        ASSERT_FALSE(r.has_value()) << "expected hard error for header ext " << ext;
        EXPECT_NE(r.error().message.find("header file"), std::string::npos)
            << "ext=" << ext << " msg=" << r.error().message;
    }
}

TEST(CompileDispatch, UnknownExtensionWarnsAndDefaultsToCxx) {
    auto tc = fake_tc();
    // Unknown extension should warn (stderr — not asserted) and fall back to
    // C++ so legacy behaviour is preserved.
    auto r = make_compile_command(job_for("/proj/weird.xyz"), tc);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_NE(r->executable.find("clang++"), std::string::npos);
}
