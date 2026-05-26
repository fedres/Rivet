// tests/unit/test_source.cpp — PackageSource registry + LocalSource roundtrip
#include "runtime/package/source.hpp"
#include "runtime/package/sources/local.hpp"
#include "platform/interface/env.hpp"
#include "platform/interface/fs.hpp"

#include <gtest/gtest.h>

using namespace rivet::pkg;

class SourceTest : public ::testing::Test {
protected:
    rivet::Path tmp_dir;

    void SetUp() override {
        auto t = rivet::env::temp_dir();
        ASSERT_TRUE(t.has_value());
        tmp_dir = *t / "rivet_source_test";
        (void)rivet::fs::remove_all(tmp_dir);
        (void)rivet::fs::create_dirs(tmp_dir);
    }
    void TearDown() override { (void)rivet::fs::remove_all(tmp_dir); }

    rivet::Path make_pkg(const std::string& name, const std::string& version) {
        auto dir = tmp_dir / name;
        (void)rivet::fs::create_dirs(dir);
        std::string toml =
            "[package]\n"
            "name    = \"" + name + "\"\n"
            "version = \"" + version + "\"\n";
        (void)rivet::fs::write_atomic(dir / "rivet.toml",
            rivet::ByteSpan{reinterpret_cast<const std::byte*>(toml.data()), toml.size()});
        return dir;
    }
};

// ─── LocalSource ─────────────────────────────────────────────────────────────

TEST_F(SourceTest, LocalResolvesByPath) {
    auto pkg_dir = make_pkg("hello", "1.2.3");
    LocalSource src;

    PackageRef ref;
    ref.name       = "hello";
    ref.local_path = pkg_dir;
    ASSERT_TRUE(src.handles(ref));

    auto r = src.resolve(ref);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->name,    "hello");
    EXPECT_EQ(r->version, "1.2.3");
    EXPECT_EQ(r->source_id, "path");
}

TEST_F(SourceTest, LocalRejectsMissingPath) {
    LocalSource src;
    PackageRef ref;
    ref.name       = "ghost";
    ref.local_path = tmp_dir / "does-not-exist";
    auto r = src.resolve(ref);
    EXPECT_FALSE(r.has_value());
}

TEST_F(SourceTest, LocalFetchReturnsRecipe) {
    auto pkg_dir = make_pkg("hello", "1.0.0");
    LocalSource src;
    PackageRef ref;
    ref.local_path = pkg_dir;
    auto r = src.resolve(ref);
    ASSERT_TRUE(r.has_value());

    auto rec = src.fetch(*r, tmp_dir / "cache");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->source_dir, pkg_dir);
    EXPECT_EQ(rec->driver, BuildDriver::Rivet);
}

// ─── SourceRegistry ──────────────────────────────────────────────────────────

TEST_F(SourceTest, RegistryRoutesByExplicitSource) {
    auto pkg_dir = make_pkg("foo", "0.1.0");

    SourceRegistry reg;
    reg.add(std::make_unique<LocalSource>());

    PackageRef ref;
    ref.name       = "foo";
    ref.source_id  = "path";
    ref.local_path = pkg_dir;

    auto r = reg.resolve(ref);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->name, "foo");
}

TEST_F(SourceTest, RegistryFailsWhenNoSourceHandles) {
    SourceRegistry reg;
    reg.add(std::make_unique<LocalSource>());

    PackageRef ref;
    ref.name = "nowhere";
    // No local_path → LocalSource won't handle it.
    auto r = reg.resolve(ref);
    EXPECT_FALSE(r.has_value());
}

TEST_F(SourceTest, RegistryFailsForUnknownExplicitSource) {
    SourceRegistry reg;
    reg.add(std::make_unique<LocalSource>());

    PackageRef ref;
    ref.name      = "foo";
    ref.source_id = "vcpkg";   // not registered
    auto r = reg.resolve(ref);
    EXPECT_FALSE(r.has_value());
}
