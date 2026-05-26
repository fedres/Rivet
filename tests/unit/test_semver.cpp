// tests/unit/test_semver.cpp — SemVer parser + constraint matcher
#include "runtime/package/source.hpp"
#include <gtest/gtest.h>

using namespace rivet::pkg;

// ─── parse_semver ────────────────────────────────────────────────────────────

TEST(SemVer, ParseFullVersion) {
    auto v = parse_semver("1.2.3");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->major, 1);
    EXPECT_EQ(v->minor, 2);
    EXPECT_EQ(v->patch, 3);
}

TEST(SemVer, ParseWithVPrefix) {
    auto v = parse_semver("v2.0.0");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->major, 2);
}

TEST(SemVer, ParseShort) {
    auto v = parse_semver("1.2");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->minor, 2);
    EXPECT_EQ(v->patch, 0);
}

TEST(SemVer, ParseMajorOnly) {
    auto v = parse_semver("3");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->major, 3);
}

TEST(SemVer, ParsePrerelease) {
    auto v = parse_semver("1.0.0-rc.1");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->prerelease, "rc.1");
}

TEST(SemVer, ParseRejectsBogus) {
    EXPECT_FALSE(parse_semver("not-a-version").has_value());
    EXPECT_FALSE(parse_semver("").has_value());
}

// ─── satisfies ───────────────────────────────────────────────────────────────

static SemVer v(int M, int m, int p) { return SemVer{M, m, p, {}}; }

TEST(SemVer, Wildcard) {
    EXPECT_TRUE(satisfies("*",   v(1, 0, 0)));
    EXPECT_TRUE(satisfies("any", v(99, 0, 0)));
    EXPECT_TRUE(satisfies("",    v(0, 0, 1)));
}

TEST(SemVer, Caret) {
    EXPECT_TRUE (satisfies("^1.2.3", v(1, 2, 3)));
    EXPECT_TRUE (satisfies("^1.2.3", v(1, 9, 0)));
    EXPECT_FALSE(satisfies("^1.2.3", v(2, 0, 0)));
    EXPECT_FALSE(satisfies("^1.2.3", v(1, 2, 2)));
    // 0.x.y: caret pins minor.
    EXPECT_TRUE (satisfies("^0.2.3", v(0, 2, 9)));
    EXPECT_FALSE(satisfies("^0.2.3", v(0, 3, 0)));
}

TEST(SemVer, Tilde) {
    EXPECT_TRUE (satisfies("~1.2.3", v(1, 2, 9)));
    EXPECT_FALSE(satisfies("~1.2.3", v(1, 3, 0)));
}

TEST(SemVer, Range) {
    EXPECT_TRUE (satisfies(">=1.0.0 <2.0.0", v(1, 5, 0)));
    EXPECT_FALSE(satisfies(">=1.0.0 <2.0.0", v(2, 0, 0)));
    EXPECT_FALSE(satisfies(">=1.0.0 <2.0.0", v(0, 9, 0)));
}

TEST(SemVer, EqualityAndComparison) {
    EXPECT_TRUE (satisfies("=2.3.4",  v(2, 3, 4)));
    EXPECT_FALSE(satisfies("=2.3.4",  v(2, 3, 5)));
    EXPECT_TRUE (satisfies(">1.0.0",  v(1, 0, 1)));
    EXPECT_FALSE(satisfies(">=2.0.0", v(1, 9, 9)));
}

TEST(SemVer, MajorOnly) {
    // "1" → any 1.x.y
    EXPECT_TRUE (satisfies("1",  v(1, 2, 3)));
    EXPECT_FALSE(satisfies("1",  v(2, 0, 0)));
}

TEST(SemVer, MinorOnly) {
    // "1.2" → any 1.2.x
    EXPECT_TRUE (satisfies("1.2", v(1, 2, 9)));
    EXPECT_FALSE(satisfies("1.2", v(1, 3, 0)));
}
