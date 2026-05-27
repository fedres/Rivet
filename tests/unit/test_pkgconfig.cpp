// tests/unit/test_pkgconfig.cpp — pkg-config parser + resolver
#include "runtime/build/pkgconfig.hpp"
#include "platform/interface/fs.hpp"
#include "platform/interface/env.hpp"

#include <gtest/gtest.h>

using namespace rivet::build;

namespace {

class PkgConfigTest : public ::testing::Test {
protected:
    rivet::Path tmp;
    void SetUp() override {
        auto t = rivet::env::temp_dir();
        ASSERT_TRUE(t.has_value());
        tmp = *t / "rivet_pkgconfig_test";
        (void)rivet::fs::remove_all(tmp);
        (void)rivet::fs::create_dirs(tmp);
    }
    void TearDown() override { (void)rivet::fs::remove_all(tmp); }

    void write_pc(const std::string& name, const std::string& content) {
        rivet::Path p = tmp / (name + ".pc");
        rivet::ByteSpan span{reinterpret_cast<const std::byte*>(content.data()), content.size()};
        (void)rivet::fs::write_atomic(p, span);
    }
};

} // namespace

TEST_F(PkgConfigTest, ParsesSimpleLibrary) {
    write_pc("fmt",
        "prefix=/opt/fmt\n"
        "includedir=${prefix}/include\n"
        "libdir=${prefix}/lib\n"
        "\n"
        "Name: fmt\n"
        "Description: A modern formatting library\n"
        "Version: 12.1.0\n"
        "Cflags: -I${includedir}\n"
        "Libs: -L${libdir} -lfmt\n");

    auto r = parse_pc(tmp / "fmt.pc");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->name, "fmt");
    EXPECT_EQ(r->version, "12.1.0");
    ASSERT_EQ(r->cflags.size(), 1u);
    EXPECT_EQ(r->cflags[0], "-I/opt/fmt/include");
    ASSERT_EQ(r->libs.size(), 2u);
    EXPECT_EQ(r->libs[0], "-L/opt/fmt/lib");
    EXPECT_EQ(r->libs[1], "-lfmt");
}

TEST_F(PkgConfigTest, ResolvesRequiresTransitively) {
    write_pc("base",
        "Name: base\n"
        "Version: 1.0\n"
        "Cflags: -I/base/include\n"
        "Libs: -lbase\n");
    write_pc("mid",
        "Name: mid\n"
        "Version: 1.0\n"
        "Requires: base\n"
        "Cflags: -I/mid/include\n"
        "Libs: -lmid\n");
    write_pc("top",
        "Name: top\n"
        "Version: 1.0\n"
        "Requires: mid\n"
        "Cflags: -I/top/include\n"
        "Libs: -ltop\n");

    auto r = resolve_pkgs({"top"}, {tmp}, /*static_link=*/false);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->resolved.size(), 3u);

    // Cflags from all three present.
    EXPECT_NE(std::find(r->cflags.begin(), r->cflags.end(), "-I/base/include"),
              r->cflags.end());
    EXPECT_NE(std::find(r->cflags.begin(), r->cflags.end(), "-I/top/include"),
              r->cflags.end());

    // Linker order: dependent (top) must appear BEFORE its dependencies
    // (mid, base) on POSIX ld's command line.
    auto idx_top  = std::find(r->libs.begin(), r->libs.end(), "-ltop")  - r->libs.begin();
    auto idx_mid  = std::find(r->libs.begin(), r->libs.end(), "-lmid")  - r->libs.begin();
    auto idx_base = std::find(r->libs.begin(), r->libs.end(), "-lbase") - r->libs.begin();
    EXPECT_LT(idx_top,  idx_mid);
    EXPECT_LT(idx_mid,  idx_base);
}

TEST_F(PkgConfigTest, IncludesPrivateOnlyOnStaticLink) {
    write_pc("openssl",
        "Name: openssl\n"
        "Version: 3.0\n"
        "Libs: -lssl -lcrypto\n"
        "Libs.private: -ldl -lpthread\n"
        "Requires.private: zlib\n");
    write_pc("zlib",
        "Name: zlib\n"
        "Version: 1.2\n"
        "Libs: -lz\n");

    auto shared = resolve_pkgs({"openssl"}, {tmp}, /*static_link=*/false);
    ASSERT_TRUE(shared.has_value());
    // Requires.private not pulled in for shared link.
    EXPECT_EQ(std::find(shared->resolved.begin(), shared->resolved.end(), "zlib"),
              shared->resolved.end());

    auto stat = resolve_pkgs({"openssl"}, {tmp}, /*static_link=*/true);
    ASSERT_TRUE(stat.has_value());
    EXPECT_NE(std::find(stat->resolved.begin(), stat->resolved.end(), "zlib"),
              stat->resolved.end());
}

TEST_F(PkgConfigTest, ReportsUnresolved) {
    write_pc("alpha",
        "Name: alpha\n"
        "Version: 1.0\n"
        "Requires: beta\n");
    // No beta.pc — should appear in `unresolved`.

    auto r = resolve_pkgs({"alpha"}, {tmp}, false);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->unresolved.size(), 1u);
    EXPECT_EQ(r->unresolved[0], "beta");
}

TEST_F(PkgConfigTest, TokenisesQuotedPaths) {
    write_pc("space",
        "Name: space\n"
        "Version: 1.0\n"
        "Cflags: -I\"/path with space/include\"\n"
        "Libs: -L\"/path with space/lib\" -lthing\n");

    auto r = parse_pc(tmp / "space.pc");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->cflags.size(), 1u);
    EXPECT_EQ(r->cflags[0], "-I/path with space/include");
    ASSERT_EQ(r->libs.size(), 2u);
    EXPECT_EQ(r->libs[0], "-L/path with space/lib");
    EXPECT_EQ(r->libs[1], "-lthing");
}
