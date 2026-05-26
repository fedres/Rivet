// tests/platform/test_fs.cpp — Platform-level filesystem edge case tests
#include "platform/interface/fs.hpp"
#include "platform/interface/env.hpp"
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

using namespace rivet;

class FsTest : public ::testing::Test {
protected:
    Path test_dir;

    void SetUp() override {
        auto tmp = env::temp_dir();
        ASSERT_TRUE(tmp.has_value());
        test_dir = *tmp / "rivet_test_fs";
        auto r = fs::create_dirs(test_dir);
        ASSERT_TRUE(r.has_value()) << r.error().message;
    }

    void TearDown() override {
        (void)fs::remove_all(test_dir);
    }

    Path p(const std::string& rel) const { return test_dir / rel; }
};

// ─── Basic existence ──────────────────────────────────────────────────────────

TEST_F(FsTest, ExistsAndNotExists) {
    EXPECT_TRUE(fs::exists(test_dir).value_or(false));
    EXPECT_FALSE(fs::exists(p("no_such_file")).value_or(true));
}

// ─── Atomic write / read ─────────────────────────────────────────────────────

TEST_F(FsTest, WriteAtomicAndReadBack) {
    auto dest = p("atomic.txt");
    std::string content = "hello rivet\n";

    auto wr = fs::write_atomic(dest,
        rivet::ByteSpan{reinterpret_cast<const std::byte*>(content.data()), content.size()});
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto rd = fs::read_file(dest);
    ASSERT_TRUE(rd.has_value()) << rd.error().message;
    std::string back(reinterpret_cast<const char*>(rd->data()), rd->size());
    EXPECT_EQ(back, content);
}

// ─── Atomic rename ───────────────────────────────────────────────────────────

TEST_F(FsTest, RenameAtomicReplacesTarget) {
    auto src  = p("src.txt");
    auto dst  = p("dst.txt");

    std::vector<std::byte> data_a{std::byte{'a'}};
    std::vector<std::byte> data_b{std::byte{'b'}};
    ASSERT_TRUE(fs::write_atomic(src, data_a).has_value());
    ASSERT_TRUE(fs::write_atomic(dst, data_b).has_value());

    ASSERT_TRUE(fs::rename_atomic(src, dst).has_value());
    EXPECT_FALSE(fs::exists(src).value_or(true));

    auto rd = fs::read_file(dst);
    ASSERT_TRUE(rd.has_value());
    EXPECT_EQ((*rd)[0], std::byte{'a'});
}

// ─── Directory listing ───────────────────────────────────────────────────────

TEST_F(FsTest, ListDir) {
    ASSERT_TRUE(fs::write_atomic(p("a.txt"), {}).has_value());
    ASSERT_TRUE(fs::write_atomic(p("b.txt"), {}).has_value());
    ASSERT_TRUE(fs::create_dir(p("sub")).has_value());

    auto entries = fs::list_dir(test_dir);
    ASSERT_TRUE(entries.has_value()) << entries.error().message;
    EXPECT_GE(entries->size(), 3u);
}

// ─── Symlinks ────────────────────────────────────────────────────────────────

TEST_F(FsTest, CreateAndReadSymlink) {
    auto target = p("target.txt");
    auto link   = p("link.txt");
    std::vector<std::byte> data{std::byte{'x'}};
    ASSERT_TRUE(fs::write_atomic(target, data).has_value());

    auto rc = fs::create_symlink(target, link);
    ASSERT_TRUE(rc.has_value()) << rc.error().message;

    auto resolved = fs::read_symlink(link);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    // The resolved target should end with "target.txt"
    EXPECT_EQ(resolved->filename(), "target.txt");
}

// ─── Unicode paths ───────────────────────────────────────────────────────────

TEST_F(FsTest, UnicodePath) {
    // U+4E2D U+6587 = "中文"
    Path upath = test_dir / u8"中文_file.txt";
    std::vector<std::byte> data{std::byte{'u'}, std::byte{'8'}};
    auto wr = fs::write_atomic(upath, data);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;
    EXPECT_TRUE(fs::exists(upath).value_or(false));
}

// ─── File stat ───────────────────────────────────────────────────────────────

TEST_F(FsTest, StatReturnsSize) {
    auto f = p("sized.bin");
    std::vector<std::byte> data(128, std::byte{0xAB});
    ASSERT_TRUE(fs::write_atomic(f, data).has_value());

    auto st = fs::stat(f);
    ASSERT_TRUE(st.has_value()) << st.error().message;
    EXPECT_EQ(st->size_bytes, 128u);
}

// ─── Concurrent writes (no data corruption) ──────────────────────────────────

TEST_F(FsTest, ConcurrentAtomicWrites) {
    constexpr int N = 8;
    std::vector<std::thread> threads;
    threads.reserve(N);

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&, i]() {
            auto f = p("concurrent_" + std::to_string(i) + ".txt");
            std::vector<std::byte> data{static_cast<std::byte>(i)};
            auto r = fs::write_atomic(f, data);
            EXPECT_TRUE(r.has_value()) << r.error().message;
        });
    }
    for (auto& t : threads) t.join();

    // All files should exist
    for (int i = 0; i < N; ++i) {
        EXPECT_TRUE(fs::exists(p("concurrent_" + std::to_string(i) + ".txt")).value_or(false));
    }
}

// ─── remove_all ──────────────────────────────────────────────────────────────

TEST_F(FsTest, RemoveAllRecursive) {
    auto sub = p("deep/a/b/c");
    ASSERT_TRUE(fs::create_dirs(sub).has_value());
    ASSERT_TRUE(fs::write_atomic(sub / "x.txt", {}).has_value());

    auto r = fs::remove_all(p("deep"));
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_FALSE(fs::exists(p("deep")).value_or(true));
}
