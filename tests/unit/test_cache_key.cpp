// tests/unit/test_cache_key.cpp — Cache key derivation
#include "runtime/cache/key.hpp"
#include <gtest/gtest.h>

using namespace rivet::cache;

TEST(CacheKey, SHA256KnownVector) {
    // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    auto r = sha256_string("abc");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(*r, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(CacheKey, SHA256EmptyString) {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    auto r = sha256_string("");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(CacheKey, DeriveKeyDeterministic) {
    KeyMaterial m1;
    m1.tool_version  = "clang-18.1.0";
    m1.target_triple = "x86_64-linux-gnu";
    m1.cxx_std       = "c++23";
    m1.flags         = {"-O2", "-DNDEBUG"};
    m1.input_hashes  = {"aaaa", "bbbb"};

    KeyMaterial m2 = m1;
    // Reverse the order of flags and hashes — result must be identical.
    std::reverse(m2.flags.begin(), m2.flags.end());
    std::reverse(m2.input_hashes.begin(), m2.input_hashes.end());

    auto k1 = derive_key(std::move(m1));
    auto k2 = derive_key(std::move(m2));

    ASSERT_TRUE(k1.has_value());
    ASSERT_TRUE(k2.has_value());
    EXPECT_EQ(k1->hex, k2->hex);  // order-independent
}

TEST(CacheKey, DeriveKeyDifferentFlags) {
    KeyMaterial m1;
    m1.tool_version  = "clang-18.1.0";
    m1.target_triple = "x86_64-linux-gnu";
    m1.cxx_std       = "c++23";
    m1.flags         = {"-O2"};
    m1.input_hashes  = {"aaaa"};

    KeyMaterial m2 = m1;
    m2.flags = {"-O0"};  // different optimization

    auto k1 = derive_key(std::move(m1));
    auto k2 = derive_key(std::move(m2));

    ASSERT_TRUE(k1.has_value());
    ASSERT_TRUE(k2.has_value());
    EXPECT_NE(k1->hex, k2->hex);  // must differ
}

TEST(CacheKey, DeriveKeyDifferentTriples) {
    KeyMaterial m;
    m.tool_version  = "clang-18.1.0";
    m.cxx_std       = "c++23";
    m.flags         = {"-O2"};
    m.input_hashes  = {"aaaa"};

    m.target_triple = "x86_64-linux-gnu";
    auto k_linux = derive_key(m);

    m.target_triple = "arm64-apple-macos11";
    auto k_macos = derive_key(std::move(m));

    ASSERT_TRUE(k_linux.has_value());
    ASSERT_TRUE(k_macos.has_value());
    EXPECT_NE(k_linux->hex, k_macos->hex);
}

TEST(CacheKey, EmptyKeyIsEmpty) {
    rivet::build::CacheKey k;
    EXPECT_TRUE(k.empty());
}

TEST(CacheKey, KeyLength) {
    KeyMaterial m;
    m.tool_version  = "clang-18.1.0";
    m.target_triple = "x86_64-linux-gnu";
    m.cxx_std       = "c++23";
    auto k = derive_key(std::move(m));
    ASSERT_TRUE(k.has_value());
    EXPECT_EQ(k->hex.size(), 64u);  // SHA-256 hex = 64 chars
}
