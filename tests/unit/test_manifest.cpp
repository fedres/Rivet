// tests/unit/test_manifest.cpp — Package manifest parsing and validation
#include "runtime/package/manifest.hpp"
#include "runtime/package/lockfile.hpp"
#include "platform/interface/env.hpp"
#include "platform/interface/fs.hpp"
#include <gtest/gtest.h>
#include <format>

using namespace rivet::pkg;

// ─── Helpers ─────────────────────────────────────────────────────────────────

class ManifestTest : public ::testing::Test {
protected:
    rivet::Path tmp_dir;

    void SetUp() override {
        auto t = rivet::env::temp_dir();
        ASSERT_TRUE(t.has_value());
        tmp_dir = *t / "rivet_manifest_test";
        (void)rivet::fs::create_dirs(tmp_dir);
    }

    void TearDown() override {
        (void)rivet::fs::remove_all(tmp_dir);
    }

    rivet::Path write_toml(const std::string& content) {
        auto path = tmp_dir / "rivet.toml";
        (void)rivet::fs::write_atomic(path,
            rivet::ByteSpan{reinterpret_cast<const std::byte*>(content.data()), content.size()});
        return path;
    }
};

// ─── Basic parsing ────────────────────────────────────────────────────────────

TEST_F(ManifestTest, ParseMinimalManifest) {
    auto path = write_toml(
        "[package]\n"
        "name    = \"myapp\"\n"
        "version = \"0.1.0\"\n"
    );

    auto r = parse_manifest(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->name,    "myapp");
    EXPECT_EQ(r->version, "0.1.0");
}

TEST_F(ManifestTest, ParseWithDescription) {
    auto path = write_toml(
        "[package]\n"
        "name        = \"libfoo\"\n"
        "version     = \"1.2.3\"\n"
        "description = \"A foo library\"\n"
        "license     = \"MIT\"\n"
    );

    auto r = parse_manifest(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->description, "A foo library");
    EXPECT_EQ(r->license,     "MIT");
}

TEST_F(ManifestTest, ParseSetsRootDir) {
    auto path = write_toml("[package]\nname=\"x\"\nversion=\"0.0.1\"\n");
    auto r = parse_manifest(path);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->root_dir, tmp_dir);
}

TEST_F(ManifestTest, NotFoundError) {
    auto r = parse_manifest(tmp_dir / "nonexistent.toml");
    EXPECT_FALSE(r.has_value());
}

// ─── Validation ───────────────────────────────────────────────────────────────

TEST_F(ManifestTest, ValidateMissingName) {
    Manifest m;
    m.version = "1.0.0";
    auto r = validate(m);
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(r.error().message.empty());
}

TEST_F(ManifestTest, ValidateMissingVersion) {
    Manifest m;
    m.name = "myapp";
    auto r = validate(m);
    EXPECT_FALSE(r.has_value());
}

TEST_F(ManifestTest, ValidateBadVersion) {
    Manifest m;
    m.name    = "myapp";
    m.version = "notaversion";  // no dots
    auto r = validate(m);
    EXPECT_FALSE(r.has_value());
}

TEST_F(ManifestTest, ValidateBadName) {
    Manifest m;
    m.name    = "My App";  // spaces not allowed
    m.version = "1.0.0";
    auto r = validate(m);
    EXPECT_FALSE(r.has_value());
}

TEST_F(ManifestTest, ValidateOK) {
    Manifest m;
    m.name    = "my-app_v2";
    m.version = "1.0.0";
    auto r = validate(m);
    EXPECT_TRUE(r.has_value()) << r.error().message;
}

// ─── Serialize ────────────────────────────────────────────────────────────────

TEST_F(ManifestTest, SerializeRoundTrip) {
    Manifest m;
    m.name        = "roundtrip";
    m.version     = "2.3.4";
    m.description = "testing serialize";

    auto text = serialize(m);
    EXPECT_NE(text.find("roundtrip"), std::string::npos);
    EXPECT_NE(text.find("2.3.4"),     std::string::npos);
}

// ─── LockFile ─────────────────────────────────────────────────────────────────

TEST_F(ManifestTest, LockfileResolveNoDeps) {
    Manifest m;
    m.name    = "app";
    m.version = "0.1.0";

    auto lf = resolve(m);
    ASSERT_TRUE(lf.has_value()) << lf.error().message;
    EXPECT_TRUE(lf->packages.empty());
    EXPECT_TRUE(is_up_to_date(*lf, m));
}

TEST_F(ManifestTest, LockfileWriteAndRead) {
    rivet::pkg::LockFile lf;
    lf.root_name    = "myapp";
    lf.root_version = "1.0.0";

    LockedDep dep;
    dep.name     = "libbar";
    dep.version  = "2.0.0";
    dep.source   = "registry";
    dep.checksum = "deadbeef";
    lf.packages.push_back(dep);

    auto lock_path = tmp_dir / "rivet.lock";
    auto wr = write_lockfile(lf, lock_path);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto rd = parse_lockfile(lock_path);
    ASSERT_TRUE(rd.has_value()) << rd.error().message;
    ASSERT_EQ(rd->packages.size(), 1u);
    EXPECT_EQ(rd->packages[0].name,     "libbar");
    EXPECT_EQ(rd->packages[0].version,  "2.0.0");
    EXPECT_EQ(rd->packages[0].checksum, "deadbeef");
}

// ─── Multi-target schema (M1: rivet-self-build) ──────────────────────────────

TEST_F(ManifestTest, ParsesMultiTargetSchema) {
    auto path = write_toml(R"(
[package]
name = "rivet"
version = "0.1.0"

[[lib]]
name = "rivet_pal"
sources = ["platform/common/types.cpp"]
include_dirs = [".", "platform/common"]

[[lib]]
name = "rivet_runtime"
sources = ["runtime/package/manifest.cpp", "runtime/build/graph.cpp"]
depends_on = ["rivet_pal"]
defines = ["RIVET_VERSION=\"0.1.0\""]

[[bin]]
name = "rivet"
path = "runtime/main.cpp"
depends_on = ["rivet_runtime"]

[[test]]
name = "test_manifest"
path = "tests/unit/test_manifest.cpp"
depends_on = ["rivet_runtime"]

[[vendor]]
name = "sqlite"
sources = ["vendor/sqlite/sqlite3.c"]
compile_flags = ["-Wno-all", "-Wno-extra"]
defines = ["SQLITE_THREADSAFE=1"]
)");
    auto r = parse_manifest(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->name, "rivet");
    ASSERT_EQ(r->targets.size(), 5u);

    EXPECT_EQ(r->targets[0].kind, TargetKind::Lib);
    EXPECT_EQ(r->targets[0].name, "rivet_pal");
    ASSERT_EQ(r->targets[0].sources.size(), 1u);
    EXPECT_EQ(r->targets[0].sources[0], "platform/common/types.cpp");
    ASSERT_EQ(r->targets[0].include_dirs.size(), 2u);

    EXPECT_EQ(r->targets[1].kind, TargetKind::Lib);
    EXPECT_EQ(r->targets[1].name, "rivet_runtime");
    ASSERT_EQ(r->targets[1].depends_on.size(), 1u);
    EXPECT_EQ(r->targets[1].depends_on[0], "rivet_pal");
    ASSERT_EQ(r->targets[1].defines.size(), 1u);

    EXPECT_EQ(r->targets[2].kind, TargetKind::Bin);
    EXPECT_EQ(r->targets[2].name, "rivet");
    EXPECT_EQ(r->targets[2].path, "runtime/main.cpp");

    EXPECT_EQ(r->targets[3].kind, TargetKind::Test);
    EXPECT_EQ(r->targets[3].name, "test_manifest");

    EXPECT_EQ(r->targets[4].kind, TargetKind::Vendor);
    EXPECT_EQ(r->targets[4].name, "sqlite");
    ASSERT_EQ(r->targets[4].compile_flags.size(), 2u);
}

TEST_F(ManifestTest, ParsesCfgConditionalSources) {
    auto path = write_toml(R"(
[package]
name = "demo"
version = "0.1.0"

[[lib]]
name = "demo_pal"
sources = ["platform/common/types.cpp"]

[[lib.cfg]]
os = "linux"
sources = ["platform/linux/fs.cpp", "platform/linux/process.cpp"]

[[lib.cfg]]
os = "macos"
sources = ["platform/macos/fs.cpp"]
link_libs = ["-framework", "CoreFoundation"]

[[lib.cfg]]
os = "windows"
sources = ["platform/windows/fs.cpp"]
link_libs = ["ws2_32.lib"]
)");
    auto r = parse_manifest(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->targets.size(), 1u);
    const auto& t = r->targets[0];
    ASSERT_EQ(t.cfg_overrides.size(), 3u);

    EXPECT_EQ(t.cfg_overrides[0].cfg.os, "linux");
    EXPECT_EQ(t.cfg_overrides[0].extra_sources.size(), 2u);

    EXPECT_EQ(t.cfg_overrides[1].cfg.os, "macos");
    ASSERT_EQ(t.cfg_overrides[1].extra_link_libs.size(), 2u);
    EXPECT_EQ(t.cfg_overrides[1].extra_link_libs[0], "-framework");

    EXPECT_EQ(t.cfg_overrides[2].cfg.os, "windows");
}

TEST_F(ManifestTest, ParsesWorkspaceSection) {
    auto path = write_toml(R"(
[workspace]
members = ["crates/foo", "crates/bar"]
exclude = ["crates/wip-baz"]

[workspace.dependencies]
fmt = "10.2"
spdlog = { version = "1.13", static = true }
)");
    auto r = parse_manifest(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_TRUE(r->workspace.has_value());
    const auto& w = *r->workspace;
    ASSERT_EQ(w.members.size(), 2u);
    EXPECT_EQ(w.members[0], "crates/foo");
    EXPECT_EQ(w.members[1], "crates/bar");
    ASSERT_EQ(w.exclude.size(), 1u);
    EXPECT_EQ(w.exclude[0], "crates/wip-baz");
    ASSERT_EQ(w.dependencies.size(), 2u);
    EXPECT_EQ(w.dependencies.at("fmt").version, "10.2");
    EXPECT_EQ(w.dependencies.at("spdlog").version, "1.13");
    EXPECT_TRUE(w.dependencies.at("spdlog").static_link);
}

TEST_F(ManifestTest, ParsesPerSourceCompileFlags) {
    auto path = write_toml(R"(
[package]
name = "demo"
version = "0.1.0"

[[vendor]]
name = "sqlite"
sources = ["vendor/sqlite/sqlite3.c"]

[vendor.per_source_flags]
"vendor/sqlite/sqlite3.c" = ["-Wno-all", "-Wno-extra"]
)");
    auto r = parse_manifest(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->targets.size(), 1u);
    const auto& t = r->targets[0];
    EXPECT_EQ(t.kind, TargetKind::Vendor);
    ASSERT_EQ(t.per_source_flags.size(), 1u);
    auto it = t.per_source_flags.find("vendor/sqlite/sqlite3.c");
    ASSERT_NE(it, t.per_source_flags.end());
    ASSERT_EQ(it->second.size(), 2u);
    EXPECT_EQ(it->second[0], "-Wno-all");
}
