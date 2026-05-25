// tests/unit/test_result.cpp — Tests for the Result<T> / Error type
#include "platform/interface/result.hpp"
#include <gtest/gtest.h>

using namespace rivet;

TEST(Result, SuccessHasValue) {
    Result<int> r{42};
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(Result, ErrorHasError) {
    Result<int> r = make_error<int>("something failed", 42);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "something failed");
    EXPECT_EQ(r.error().code, 42);
}

TEST(Result, VoidSuccess) {
    Result<void> r{};
    EXPECT_TRUE(r.has_value());
}

TEST(Result, VoidError) {
    Result<void> r = make_error("oops");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "oops");
}

TEST(Result, Propagate) {
    Result<int> source = make_error<int>("original", 5);
    Result<std::string> propagated = propagate<std::string>(source);
    ASSERT_FALSE(propagated.has_value());
    EXPECT_EQ(propagated.error().message, "original");
    EXPECT_EQ(propagated.error().code, 5);
}

TEST(Error, DefaultCode) {
    Error e{"msg"};
    EXPECT_EQ(e.code, 0);
    EXPECT_EQ(e.message, "msg");
}
