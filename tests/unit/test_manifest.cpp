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
    LockFile lf;
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
