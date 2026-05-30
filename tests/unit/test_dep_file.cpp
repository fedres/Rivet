// tests/unit/test_dep_file.cpp -- make-style dep file parser
#include "runtime/build/dep_file.hpp"
#include "platform/interface/env.hpp"
#include "platform/interface/fs.hpp"

#include <gtest/gtest.h>
#include <string>

using rivet::build::parse_dep_file;
using rivet::Path;

class DepFileTest : public ::testing::Test {
protected:
    Path tmp;

    void SetUp() override {
        auto t = rivet::env::temp_dir();
        ASSERT_TRUE(t.has_value());
        tmp = *t / "rivet_dep_file_test";
        (void)rivet::fs::create_dirs(tmp);
    }
    void TearDown() override {
        (void)rivet::fs::remove_all(tmp);
    }

    Path write(const std::string& body) {
        auto p = tmp / "foo.o.d";
        (void)rivet::fs::write_atomic(p,
            rivet::ByteSpan{reinterpret_cast<const std::byte*>(body.data()),
                            body.size()});
        return p;
    }
};

TEST_F(DepFileTest, SinglePrerequisite) {
    auto p = write("out/foo.o: src/foo.cpp\n");
    auto deps = parse_dep_file(p);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], Path{"src/foo.cpp"});
}

TEST_F(DepFileTest, BackslashContinuation) {
    auto p = write(
        "out/foo.o: src/foo.cpp \\\n"
        "  include/bar.h \\\n"
        "  include/baz.h\n");
    auto deps = parse_dep_file(p);
    ASSERT_EQ(deps.size(), 3u);
    EXPECT_EQ(deps[0], Path{"src/foo.cpp"});
    EXPECT_EQ(deps[1], Path{"include/bar.h"});
    EXPECT_EQ(deps[2], Path{"include/baz.h"});
}

TEST_F(DepFileTest, CRLFLineEndings) {
    // clang on Windows emits CRLF in .d files. Confirm the parser
    // tolerates that without producing phantom dependencies.
    auto p = write(
        "out/foo.o: src/foo.cpp \\\r\n"
        "  include/bar.h\r\n");
    auto deps = parse_dep_file(p);
    ASSERT_EQ(deps.size(), 2u);
    EXPECT_EQ(deps[0], Path{"src/foo.cpp"});
    EXPECT_EQ(deps[1], Path{"include/bar.h"});
}

TEST_F(DepFileTest, EscapedSpaceInPath) {
    // GNU make convention: a literal space inside a path is escaped as `\ `.
    auto p = write("out/foo.o: src/path\\ with\\ spaces/foo.cpp\n");
    auto deps = parse_dep_file(p);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], Path{"src/path with spaces/foo.cpp"});
}

TEST_F(DepFileTest, EmptyOnMissingFile) {
    auto deps = parse_dep_file(tmp / "nonexistent.d");
    EXPECT_TRUE(deps.empty());
}

TEST_F(DepFileTest, MultipleRules) {
    // Less common but defensible: two .o targets in one .d. Both prerequisite
    // lists should be folded together (the caller cares about the union).
    auto p = write(
        "out/foo.o: src/foo.cpp include/bar.h\n"
        "out/baz.o: src/baz.cpp include/bar.h\n");
    auto deps = parse_dep_file(p);
    ASSERT_EQ(deps.size(), 4u);
    EXPECT_EQ(deps[0], Path{"src/foo.cpp"});
    EXPECT_EQ(deps[1], Path{"include/bar.h"});
    EXPECT_EQ(deps[2], Path{"src/baz.cpp"});
    EXPECT_EQ(deps[3], Path{"include/bar.h"});
}

TEST_F(DepFileTest, IgnoresStrayBackslashToken) {
    // After joining backslash-newline pairs to spaces, a defensive parser
    // shouldn't emit a stray "\" as a token. Construct a case where the
    // backslash sat between continuations and ensure it doesn't leak.
    auto p = write(
        "out/foo.o: src/foo.cpp \\\n"
        "\\\n"
        "  include/bar.h\n");
    auto deps = parse_dep_file(p);
    // Tokens should be src/foo.cpp + include/bar.h. The dangling `\` from
    // the second continuation line is fused into the whitespace.
    ASSERT_EQ(deps.size(), 2u);
    EXPECT_EQ(deps[0], Path{"src/foo.cpp"});
    EXPECT_EQ(deps[1], Path{"include/bar.h"});
}
