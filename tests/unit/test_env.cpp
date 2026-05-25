// tests/unit/test_env.cpp — Tests for rivet::env
#include "platform/interface/env.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace rivet;

TEST(Env, SetAndGet) {
    const char* key = "RIVET_TEST_VAR_42";
    env::unset(key);
    EXPECT_FALSE(env::get(key).has_value());

    auto r = env::set(key, "hello");
    ASSERT_TRUE(r.has_value());

    auto val = env::get(key);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "hello");

    env::unset(key);
    EXPECT_FALSE(env::get(key).has_value());
}

TEST(Env, HomeDir) {
    auto home = env::home_dir();
    ASSERT_TRUE(home.has_value());
    EXPECT_FALSE(home->empty());
}

TEST(Env, TempDir) {
    auto tmp = env::temp_dir();
    ASSERT_TRUE(tmp.has_value());
    EXPECT_FALSE(tmp->empty());
}

TEST(Env, RivetHome) {
    auto rh = env::rivet_home();
    ASSERT_TRUE(rh.has_value());
    // Should be inside home dir or an XDG override
    EXPECT_FALSE(rh->empty());
}

TEST(Env, HostTriple) {
    auto triple = env::host_triple();
    EXPECT_FALSE(triple.empty());
    // Should be one of the known triples documented in env.hpp
    // Just verify it contains a dash (e.g. "x86_64-linux-gnu")
    EXPECT_NE(triple.find('-'), std::string::npos);
}

TEST(Env, Snapshot) {
    const char* k = "RIVET_TEST_SNAP_99";
    env::set(k, "snap_value");

    auto snap = env::snapshot();
    EXPECT_TRUE(snap.count(k) > 0);
    EXPECT_EQ(snap.at(k), "snap_value");

    env::unset(k);
}
