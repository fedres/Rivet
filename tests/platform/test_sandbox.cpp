// tests/platform/test_sandbox.cpp -- end-to-end sandbox enforcement.
//
// D2 acceptance: a sandboxed child can read explicitly-allowed paths and
// cannot read paths outside the allow-list. Platform-gated:
//
//   macOS  -- sandbox-exec(1)
//   Linux  -- Landlock (kernel 5.13+); test skips when unsupported
//   Win    -- not yet implemented; test currently expects no-op spawn
//
// The Linux + Windows arms are skipped or expect-no-op until those
// backends land; macOS proves the policy plumbing end-to-end.
#include "platform/interface/sandbox.hpp"
#include "platform/interface/process.hpp"

#include <gtest/gtest.h>

namespace {

// Run a child with the given argv + policy and return its exit code.
// Stdout/stderr are captured.
int run_under_sandbox(std::vector<std::string> argv,
                       rivet::sandbox::SandboxPolicy policy) {
    rivet::process::SpawnOptions opts;
    opts.args           = std::move(argv);
    opts.inherit_env    = true;
    opts.capture_stdout = true;
    opts.capture_stderr = true;
    auto child = rivet::sandbox::spawn_sandboxed(std::move(opts), std::move(policy));
    if (!child) return -1;
    auto rc = child->wait();
    return rc.value_or(-1);
}

} // namespace

#if defined(__APPLE__)

TEST(MacosSandbox, AllowsExplicitlyListedReads) {
    // Allow /etc as ReadOnly; expect `/usr/bin/test -r /etc/hosts` to succeed.
    rivet::sandbox::SandboxPolicy policy;
    policy.path_rules.push_back({rivet::Path{"/etc"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    int rc = run_under_sandbox(
        {"/usr/bin/test", "-r", "/etc/hosts"}, std::move(policy));
    EXPECT_EQ(rc, 0) << "sandbox should permit a read on an allow-listed path";
}

TEST(MacosSandbox, BlocksUnlistedWrites) {
    // No write rule -> cat to a fresh tmp path outside the allowed
    // tmpdir scope is denied. Use a write outside /private/tmp (which the
    // policy's allow_tmpdir would have unblocked) by routing through an
    // explicit literal that was never granted.
    rivet::sandbox::SandboxPolicy policy;
    policy.allow_tmpdir = false;  // strip the tmpdir blanket
    int rc = run_under_sandbox(
        {"/bin/sh", "-c", "echo x > /tmp/rivet_sandbox_should_block.$$"},
        std::move(policy));
    EXPECT_NE(rc, 0) << "without an allow rule, writing to /tmp should fail";
}

// Forward-declare the impl-internal builder inside its own namespace so the
// linker resolves rivet::sandbox::build_macos_profile, not ::build_macos_profile.
namespace rivet::sandbox {
std::string build_macos_profile(const SandboxPolicy&);
}

TEST(MacosSandbox, ProfileBuilderHasDenyDefault) {
    rivet::sandbox::SandboxPolicy p;
    p.path_rules.push_back(
        {rivet::Path{"/tmp/allowed"},
         rivet::sandbox::PathRule::Access::ReadWrite,
         true});
    std::string text = rivet::sandbox::build_macos_profile(p);

    EXPECT_NE(text.find("(deny default)"),  std::string::npos);
    EXPECT_NE(text.find("/tmp/allowed"),    std::string::npos);
    EXPECT_NE(text.find("(allow file*"),    std::string::npos);
}

#endif  // __APPLE__

#if defined(__linux__)

TEST(LinuxSandbox, IsSupportedReturnsBoolWithoutCrashing) {
    // Probe shouldn't crash regardless of kernel version. On runners with
    // Landlock (5.13+) returns true; on older runners returns false.
    bool s = rivet::sandbox::is_supported();
    (void)s;
}

TEST(LinuxSandbox, ReadOnlyRuleBlocksWriteToSamePath) {
    if (!rivet::sandbox::is_supported()) {
        GTEST_SKIP() << "Landlock not available on this kernel";
    }
    // Allow /tmp ReadOnly. Spawn `sh -c 'echo hi > /tmp/rivet_lock_test.$$'`.
    // The write should fail; the shell's exit code is non-zero.
    rivet::sandbox::SandboxPolicy policy;
    policy.path_rules.push_back({rivet::Path{"/tmp"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    // We also need to allow read+exec on the directories the shell needs
    // (binary + glibc), otherwise execve itself fails. Allow /usr ReadOnly.
    policy.path_rules.push_back({rivet::Path{"/usr"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    policy.path_rules.push_back({rivet::Path{"/bin"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    policy.path_rules.push_back({rivet::Path{"/lib"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    policy.path_rules.push_back({rivet::Path{"/lib64"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    int rc = run_under_sandbox(
        {"/bin/sh", "-c", "echo hi > /tmp/rivet_landlock_should_block.$$"},
        std::move(policy));
    EXPECT_NE(rc, 0) << "ReadOnly rule should have blocked the write";
}

TEST(LinuxSandbox, ReadWriteRuleAllowsBothWaysOnPath) {
    if (!rivet::sandbox::is_supported()) {
        GTEST_SKIP() << "Landlock not available on this kernel";
    }
    // Allow /tmp ReadWrite. The same write that fails in the test above
    // should succeed here.
    rivet::sandbox::SandboxPolicy policy;
    policy.path_rules.push_back({rivet::Path{"/tmp"},
                                  rivet::sandbox::PathRule::Access::ReadWrite,
                                  true});
    policy.path_rules.push_back({rivet::Path{"/usr"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    policy.path_rules.push_back({rivet::Path{"/bin"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    policy.path_rules.push_back({rivet::Path{"/lib"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    policy.path_rules.push_back({rivet::Path{"/lib64"},
                                  rivet::sandbox::PathRule::Access::ReadOnly,
                                  true});
    int rc = run_under_sandbox(
        {"/bin/sh", "-c", "echo hi > /tmp/rivet_landlock_should_allow.$$ && rm -f /tmp/rivet_landlock_should_allow.$$"},
        std::move(policy));
    EXPECT_EQ(rc, 0) << "ReadWrite rule should have permitted the write";
}

#endif  // __linux__

// On platforms whose sandbox backend hasn't been wired yet,
// spawn_sandboxed should still return a running child -- the API
// promises graceful degradation.
TEST(SandboxFallback, SpawnSucceedsEvenWithStubBackend) {
    rivet::sandbox::SandboxPolicy policy;
#if defined(_WIN32)
    int rc = run_under_sandbox({"cmd.exe", "/c", "exit", "0"}, std::move(policy));
#else
    int rc = run_under_sandbox({"/bin/sh", "-c", "exit 0"}, std::move(policy));
#endif
    EXPECT_EQ(rc, 0);
}
