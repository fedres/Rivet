// tests/unit/test_json.cpp — JSON reader smoke tests
#include "runtime/common/json.hpp"
#include <gtest/gtest.h>

using namespace rivet::json;

TEST(Json, ParseString) {
    auto v = parse(R"("hello")");
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(v->is_string());
    EXPECT_EQ(v->as_string(), "hello");
}

TEST(Json, ParseEscapes) {
    auto v = parse(R"("a\nb\tc\"d")");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_string(), "a\nb\tc\"d");
}

TEST(Json, ParseNumber) {
    auto v = parse("42.5");
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(v->is_number());
    EXPECT_DOUBLE_EQ(v->as_number(), 42.5);
}

TEST(Json, ParseBoolNull) {
    EXPECT_TRUE (parse("true")->as_bool());
    EXPECT_FALSE(parse("false")->as_bool());
    EXPECT_TRUE (parse("null")->is_null());
}

TEST(Json, ParseArray) {
    auto v = parse(R"([1, 2, "three", true])");
    ASSERT_TRUE(v.has_value());
    ASSERT_TRUE(v->is_array());
    ASSERT_EQ(v->as_array().size(), 4u);
    EXPECT_DOUBLE_EQ((*v)[0].as_number(), 1.0);
    EXPECT_EQ((*v)[2].as_string(), "three");
    EXPECT_TRUE((*v)[3].as_bool());
}

TEST(Json, ParseObject) {
    auto v = parse(R"({"name": "fmt", "version": "11.0.2"})");
    ASSERT_TRUE(v.has_value());
    ASSERT_TRUE(v->is_object());
    EXPECT_EQ((*v)["name"].as_string(),    "fmt");
    EXPECT_EQ((*v)["version"].as_string(), "11.0.2");
    EXPECT_TRUE((*v)["missing"].is_null());
}

TEST(Json, ParseNested) {
    // Realistic vcpkg.json fragment.
    auto v = parse(R"({
        "name": "spdlog",
        "version-semver": "1.14.1",
        "dependencies": [
            "fmt",
            {"name": "gtest", "host": true, "features": ["foo"]}
        ]
    })");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ((*v)["name"].as_string(), "spdlog");
    EXPECT_EQ((*v)["version-semver"].as_string(), "1.14.1");

    const auto& deps = (*v)["dependencies"].as_array();
    ASSERT_EQ(deps.size(), 2u);
    EXPECT_EQ(deps[0].as_string(), "fmt");
    EXPECT_EQ(deps[1]["name"].as_string(), "gtest");
    EXPECT_TRUE(deps[1]["host"].as_bool());
    EXPECT_EQ(deps[1]["features"].as_array()[0].as_string(), "foo");
}

TEST(Json, RejectsTrailingGarbage) {
    EXPECT_FALSE(parse(R"({"k":1} junk)").has_value());
}

TEST(Json, RejectsUnterminatedString) {
    EXPECT_FALSE(parse(R"("hello)").has_value());
}
