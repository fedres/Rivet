// tests/unit/test_resolver.cpp — end-to-end resolver smoke test (LocalSource only)
#include "runtime/package/resolver.hpp"
#include "runtime/package/sources/local.hpp"
#include "platform/interface/env.hpp"
#include "platform/interface/fs.hpp"

#include <gtest/gtest.h>

using namespace rivet::pkg;

class ResolverTest : public ::testing::Test {
protected:
    rivet::Path tmp_dir;

    void SetUp() override {
        auto t = rivet::env::temp_dir();
        ASSERT_TRUE(t.has_value());
        tmp_dir = *t / "rivet_resolver_test";
        (void)rivet::fs::remove_all(tmp_dir);
        (void)rivet::fs::create_dirs(tmp_dir);
    }
    void TearDown() override { (void)rivet::fs::remove_all(tmp_dir); }

    rivet::Path make_pkg(const std::string& name, const std::string& version,
                         const std::string& extra = {}) {
        auto dir = tmp_dir / name;
        (void)rivet::fs::create_dirs(dir);
        std::string toml =
            "[package]\n"
            "name    = \"" + name + "\"\n"
            "version = \"" + version + "\"\n" + extra;
        (void)rivet::fs::write_atomic(dir / "rivet.toml",
            rivet::ByteSpan{reinterpret_cast<const std::byte*>(toml.data()), toml.size()});
        return dir;
    }
};

TEST_F(ResolverTest, ResolvesSingleDep) {
    auto leaf = make_pkg("leaf", "1.0.0");

    Manifest root;
    root.name = "root";
    root.version = "0.1.0";
    DepSpec spec;
    spec.kind = DepKind::Path;
    spec.local_path = leaf;
    root.dependencies["leaf"] = spec;

    SourceRegistry reg;
    reg.add(std::make_unique<LocalSource>());

    Resolver r{reg};
    auto lf = r.resolve(root);
    ASSERT_TRUE(lf.has_value()) << lf.error().message;
    EXPECT_EQ(lf->root_name, "root");
    ASSERT_EQ(lf->packages.size(), 1u);
    EXPECT_EQ(lf->packages[0].name,    "leaf");
    EXPECT_EQ(lf->packages[0].version, "1.0.0");
    EXPECT_EQ(lf->packages[0].source,  "path");
}

TEST_F(ResolverTest, FollowsTransitiveDeps) {
    auto leaf = make_pkg("leaf", "1.0.0");
    auto mid_toml =
        "[dependencies]\n"
        "leaf = { path = '" + leaf.string() + "' }\n";
    auto mid = make_pkg("mid", "2.0.0", mid_toml);

    Manifest root;
    root.name = "root";
    root.version = "0.1.0";
    DepSpec spec;
    spec.kind = DepKind::Path;
    spec.local_path = mid;
    root.dependencies["mid"] = spec;

    SourceRegistry reg;
    reg.add(std::make_unique<LocalSource>());

    Resolver r{reg};
    auto lf = r.resolve(root);
    ASSERT_TRUE(lf.has_value()) << lf.error().message;
    ASSERT_EQ(lf->packages.size(), 2u);
    // Sorted alphabetically.
    EXPECT_EQ(lf->packages[0].name, "leaf");
    EXPECT_EQ(lf->packages[1].name, "mid");
}

TEST_F(ResolverTest, DeduplicatesSharedDep) {
    auto leaf = make_pkg("leaf", "1.0.0");
    auto leaf_dep =
        "[dependencies]\n"
        "leaf = { path = '" + leaf.string() + "' }\n";
    auto mid_a = make_pkg("mid_a", "1.0.0", leaf_dep);
    auto mid_b = make_pkg("mid_b", "1.0.0", leaf_dep);

    Manifest root;
    root.name = "root";
    root.version = "0.1.0";
    {
        DepSpec s; s.kind = DepKind::Path; s.local_path = mid_a;
        root.dependencies["mid_a"] = s;
    }
    {
        DepSpec s; s.kind = DepKind::Path; s.local_path = mid_b;
        root.dependencies["mid_b"] = s;
    }

    SourceRegistry reg;
    reg.add(std::make_unique<LocalSource>());
    Resolver r{reg};
    auto lf = r.resolve(root);
    ASSERT_TRUE(lf.has_value()) << lf.error().message;
    // leaf appears once despite being depended on by both mids.
    ASSERT_EQ(lf->packages.size(), 3u);
    EXPECT_EQ(lf->packages[0].name, "leaf");
}

TEST_F(ResolverTest, ErrorsOnUnresolvableDep) {
    Manifest root;
    root.name = "root";
    root.version = "0.1.0";
    DepSpec spec;
    spec.kind = DepKind::Path;
    spec.local_path = tmp_dir / "ghost";  // never created
    root.dependencies["ghost"] = spec;

    SourceRegistry reg;
    reg.add(std::make_unique<LocalSource>());
    Resolver r{reg};
    auto lf = r.resolve(root);
    EXPECT_FALSE(lf.has_value());
}
