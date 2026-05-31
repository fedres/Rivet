// platform/linux/sandbox.cpp -- Linux sandboxing backend (Landlock).
//
// Uses Landlock (LSM, kernel 5.13+) to enforce per-process filesystem
// access policy. The restriction is installed in the child process
// between fork() and execve() via SpawnOptions::pre_exec_hook -- once a
// task is restricted it cannot regain access, and execve(2) inherits
// the restriction unchanged.
//
// Gracefully degrades on older kernels: is_supported() returns false
// and spawn_sandboxed falls through to a plain process::spawn. Callers
// who care about enforcement should check is_supported() up front.
//
// Network isolation (DenyAll / DenyOutbound) is *not* yet wired here --
// that's D3. Network policy in the SandboxPolicy is currently ignored
// on Linux and silently behaves as Allow.
//
// References:
//   - Documentation/userspace-api/landlock.rst in the kernel tree
//   - https://landlock.io/
#include "../interface/sandbox.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

// ---- Landlock ABI ----------------------------------------------------------
//
// Vendor the smallest set of types + numbers Landlock needs rather than
// depend on a glibc that's new enough to ship <linux/landlock.h>. Values
// are stable kernel UAPI and match the upstream header exactly.
#ifdef __has_include
#  if __has_include(<linux/landlock.h>)
#    include <linux/landlock.h>
#    define RIVET_HAVE_LANDLOCK_HEADER 1
#  endif
#endif

#ifndef RIVET_HAVE_LANDLOCK_HEADER

struct landlock_ruleset_attr {
    uint64_t handled_access_fs;
};

struct landlock_path_beneath_attr {
    uint64_t allowed_access;
    int32_t  parent_fd;
} __attribute__((packed));

#define LANDLOCK_RULE_PATH_BENEATH 1

#define LANDLOCK_ACCESS_FS_EXECUTE      (1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE   (1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE    (1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR     (1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR   (1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE  (1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR    (1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR     (1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG     (1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK    (1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO    (1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK   (1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM     (1ULL << 12)
#endif

// Syscall numbers (stable across architectures since kernel 5.13).
#ifndef __NR_landlock_create_ruleset
#  define __NR_landlock_create_ruleset 444
#  define __NR_landlock_add_rule       445
#  define __NR_landlock_restrict_self  446
#endif

#define RIVET_FS_READ \
    (LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR | \
     LANDLOCK_ACCESS_FS_EXECUTE)

#define RIVET_FS_WRITE \
    (LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR | \
     LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_CHAR | \
     LANDLOCK_ACCESS_FS_MAKE_DIR    | LANDLOCK_ACCESS_FS_MAKE_REG  | \
     LANDLOCK_ACCESS_FS_MAKE_SOCK   | LANDLOCK_ACCESS_FS_MAKE_FIFO | \
     LANDLOCK_ACCESS_FS_MAKE_BLOCK  | LANDLOCK_ACCESS_FS_MAKE_SYM)

#define RIVET_FS_ALL (RIVET_FS_READ | RIVET_FS_WRITE)

namespace rivet::sandbox {

namespace {

// Thin syscall wrappers. We can't use glibc wrappers because they only
// exist in very recent glibc; raw syscall() is async-signal-safe and
// portable across every Linux libc.
inline long landlock_create_ruleset(const landlock_ruleset_attr* attr,
                                     size_t size, uint32_t flags) {
    return ::syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
inline long landlock_add_rule(int ruleset_fd, int rule_type,
                               const void* rule_attr, uint32_t flags) {
    return ::syscall(__NR_landlock_add_rule, ruleset_fd, rule_type,
                     rule_attr, flags);
}
inline long landlock_restrict_self(int ruleset_fd, uint32_t flags) {
    return ::syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}

} // namespace

bool is_supported() {
    // Probe by asking for the kernel's max supported ABI version. The
    // documented incantation per landlock.io: a null attr + size=0 + the
    // VERSION flag returns the ABI integer (1+), or ENOSYS on a kernel
    // without Landlock. The probe is side-effect-free (no ruleset is
    // created) and very cheap.
    constexpr uint32_t LANDLOCK_CREATE_RULESET_VERSION = 1U << 0;
    long abi = landlock_create_ruleset(nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
    return abi >= 1;
}

rivet::Result<rivet::process::ChildProcess>
spawn_sandboxed(rivet::process::SpawnOptions opts, SandboxPolicy policy) {
    if (!is_supported() || policy.path_rules.empty()) {
        return rivet::process::spawn(std::move(opts));
    }

    // The hook runs in the child between fork() and execve(). It must be
    // async-signal-safe -- no malloc, no exceptions, no std:: container
    // mutation. We only ever read from `policy`, whose backing memory is
    // COW-inherited from the parent at fork.
    opts.pre_exec_hook = [pol = std::move(policy)]() -> int {
        // 1. PR_SET_NO_NEW_PRIVS prevents setuid binaries that the child
        //    might exec from clearing the sandbox. Required by Landlock.
        if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
            return errno;  // hard fail; abort the child
        }

        // 2. Build the ruleset advertising every fs op we want to control.
        landlock_ruleset_attr rs_attr{};
        rs_attr.handled_access_fs = RIVET_FS_ALL;
        int ruleset_fd = static_cast<int>(landlock_create_ruleset(
            &rs_attr, sizeof(rs_attr), 0));
        if (ruleset_fd < 0) {
            return errno == ENOSYS ? 0 : errno;
        }

        // 3. For each PathRule, open(path) and call landlock_add_rule.
        for (size_t i = 0; i < pol.path_rules.size(); ++i) {
            const auto& r = pol.path_rules[i];
            if (r.access == PathRule::Access::Deny) continue;

            int pfd = ::open(r.path.c_str(), O_PATH | O_CLOEXEC);
            if (pfd < 0) {
                // Missing path on disk is non-fatal -- just don't add the
                // rule. Common during fresh builds (output dirs created
                // later by the compiler).
                continue;
            }
            landlock_path_beneath_attr p_attr{};
            p_attr.allowed_access = (r.access == PathRule::Access::ReadOnly)
                                     ? RIVET_FS_READ
                                     : RIVET_FS_ALL;
            p_attr.parent_fd = pfd;
            (void)landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
                                     &p_attr, 0);
            ::close(pfd);
        }

        // 4. Lock the child to this ruleset for the rest of its lifetime.
        if (landlock_restrict_self(ruleset_fd, 0) != 0) {
            int saved = errno;
            ::close(ruleset_fd);
            return saved;
        }
        ::close(ruleset_fd);
        return 0;
    };

    return rivet::process::spawn(std::move(opts));
}

} // namespace rivet::sandbox
