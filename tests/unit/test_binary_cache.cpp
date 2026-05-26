// tests/unit/test_binary_cache.cpp — PackageCacheKey determinism + store roundtrip
#include "runtime/cache/binary_cache.hpp"
#include "platform/interface/env.hpp"
#include "platform/interface/fs.hpp"

#include <gtest/gtest.h>

using namespace rivet::cache;

class BinaryCacheTest : public ::testing::Test {
protected:
    rivet::Path root;
    void SetUp() override {
        auto t = rivet::env::temp_dir();
        ASSERT_TRUE(t.has_value());
        root = *t / "rivet_binary_cache_test";
        (void)rivet::fs::remove_all(root);
        (void)rivet::fs::create_dirs(root);
    }
    void TearDown() override { (void)rivet::fs::remove_all(root); }
};

static PackageCacheKey make_key(std::string name = "fmt",
                                 std::string version = "11.0.2",
                                 std::vector<std::string> features = {}) {
    PackageCacheKey k;
    k.name         = std::move(name);
    k.version      = std::move(version);
    k.triple       = "x86_64-linux-gnu";
    k.toolchain_id = "clang-18.1.0";
    k.features     = std::move(features);
    k.deps_hash    = "0000000000000000000000000000000000000000000000000000000000000000";
    return k;
}

TEST_F(BinaryCacheTest, DigestIsDeterministic) {
    auto a = make_key();
    auto b = make_key();
    EXPECT_EQ(a.digest(), b.digest());
    EXPECT_EQ(a.digest().size(), 64u);
}

TEST_F(BinaryCacheTest, DigestChangesWithVersion) {
    auto a = make_key("fmt", "11.0.2");
    auto b = make_key("fmt", "11.0.3");
    EXPECT_NE(a.digest(), b.digest());
}

TEST_F(BinaryCacheTest, DigestChangesWithToolchain) {
    auto a = make_key();
    auto b = make_key();
    b.toolchain_id = "clang-19.0.0";
    EXPECT_NE(a.digest(), b.digest());
}

TEST_F(BinaryCacheTest, FeaturesAreOrderIndependent) {
    auto a = make_key("fmt", "11.0.2", {"foo", "bar", "baz"});
    auto b = make_key("fmt", "11.0.2", {"baz", "foo", "bar"});
    EXPECT_EQ(a.digest(), b.digest());
}

TEST_F(BinaryCacheTest, RollDepsHashIsOrderIndependent) {
    std::vector<std::string> a{"abc", "def", "ghi"};
    std::vector<std::string> b{"ghi", "abc", "def"};
    EXPECT_EQ(roll_deps_hash(a), roll_deps_hash(b));
}

TEST_F(BinaryCacheTest, RollDepsHashDistinguishesContents) {
    EXPECT_NE(roll_deps_hash({"abc", "def"}), roll_deps_hash({"abc", "xyz"}));
}

TEST_F(BinaryCacheTest, LookupMissesForEmptyCache) {
    BinaryCache cache{root};
    auto k = make_key();
    EXPECT_FALSE(cache.has(k));
    EXPECT_FALSE(cache.lookup(k).has_value());
}

TEST_F(BinaryCacheTest, StoreThenLookupRoundtrip) {
    BinaryCache cache{root};
    auto k = make_key();

    // Stand-in artifact tarball.
    auto src = root / "fake.tar.zst";
    std::string blob = "pretend this is a zstd-compressed tarball";
    ASSERT_TRUE(rivet::fs::write_atomic(src,
        rivet::ByteSpan{reinterpret_cast<const std::byte*>(blob.data()), blob.size()}
    ).has_value());

    auto r = cache.store(k, src);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    EXPECT_TRUE(cache.has(k));
    auto p = cache.lookup(k);
    ASSERT_TRUE(p.has_value());
    // Cached path must round-trip the original bytes.
    auto rb = rivet::fs::read_file(*p);
    ASSERT_TRUE(rb.has_value());
    std::string back(reinterpret_cast<const char*>(rb->data()), rb->size());
    EXPECT_EQ(back, blob);
}

TEST_F(BinaryCacheTest, PathLayoutUsesDigestPrefix) {
    BinaryCache cache{root};
    auto k = make_key();
    auto p = cache.artifact_path(k);
    auto digest = k.digest();
    // .../binary/<dd>/<digest>.tar.zst
    EXPECT_EQ(p.filename().string(), digest + ".tar.zst");
    EXPECT_EQ(p.parent_path().filename().string(), digest.substr(0, 2));
}
