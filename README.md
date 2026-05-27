# Rivet

> A self-contained, cross-platform C++ build system and package manager.

```bash
# Linux / macOS
curl -fsSL https://github.com/fedres/Rivet/releases/latest/download/install.sh | sh

# Windows (PowerShell)
irm https://github.com/fedres/Rivet/releases/latest/download/install.ps1 | iex
```

No prior compiler. No system package manager. No `apt install build-essential` /
`brew install cmake` / `vcpkg integrate`. Rivet ships its own clang, links its
own libc++, fetches its own dependencies, builds them in a hermetic per-project
tree, and produces a working binary.

The single non-redistributable prereq is the platform SDK (Xcode on macOS,
Visual Studio Build Tools on Windows). On Windows the installer auto-detects
and offers a `winget install` of the Build Tools — same flow as `rustup-init`.
On macOS it points you at `xcode-select --install`. Linux has no SDK to
install: glibc + kernel headers come with every distro.

---

## What works today

* **`rivet build`** — compiles a project against the bundled clang, with
  per-project `.rivet/build/<profile>/` output.
* **`rivet add <pkg>`** — resolves a dependency through vcpkg as a backend.
  No vcpkg knowledge required from the user.
* **`rivet fetch`** — builds every transitive dep with the bundled clang into
  a per-project install tree. `--locked` / `--frozen` for CI.
* **`rivet run`** — build + run the project binary, or invoke a
  `[scripts]` entry (npm-style).
* **`rivet exec <bin>`** — run a binary that came in via a dependency
  (npm-exec-style; works for `protoc`, `flatc`, etc.).
* **`rivet new <name>`** — scaffold a new project.
* **`rivet toolchain install <ver>`** — pull a bundled LLVM toolchain from
  the project's release page.
* **`rivet self-update`** — replace the running binary with the latest
  released version, in place.

Rivet **builds itself** from a hand-written [`rivet.toml`](rivet.toml) on
all three platforms — `49 sources → 5 artefacts → 1 binary, no cmake in the
trace`. The CI smoke job at
[`.github/workflows/smoke.yml`](.github/workflows/smoke.yml) re-proves this
on every dispatch.

---

## Quick start

```bash
rivet new hello-fmt && cd hello-fmt
echo '#include <fmt/core.h>
int main() { fmt::print("hello from fmt {}\n", 42); }' > src/main.cpp
rivet add fmt           # registers fmt in rivet.toml + lockfile
rivet fetch             # vcpkg builds fmt with the bundled clang
rivet build             # compiles main.cpp against fmt
rivet run               # → "hello from fmt 42"
```

A minimal `rivet.toml` for a single-binary project:

```toml
[package]
name    = "hello-fmt"
version = "0.1.0"

[dependencies]
fmt = "*"
```

For multi-target projects (libs, multiple binaries, vendored amalgamations,
per-platform sources), see the [`rivet.toml` at the repo root](rivet.toml) —
that's rivet's own build manifest and showcases every feature.

---

## Design philosophy

- **Bundled LLVM toolchain** — never depends on a system compiler.
  `rivet toolchain install 19.1.7` pulls a ~600 MB platform bundle on first
  use. Vendored libzstd extracts it in-process; no host `tar`/`zstd` needed.
- **vcpkg as a backend, not a UX** — `rivet add fmt` works without the user
  ever typing `vcpkg`. Per-project install trees, pkg-config-driven auto-link
  (`-I`, `-L`, `-l`, `-framework`, `/MT` static CRT), no shared global state.
- **Multi-target schema in `rivet.toml`** — cargo-style `[[lib]]`,
  `[[bin]]`, `[[test]]`, `[[vendor]]` arrays with cfg-conditional
  `[[lib.cfg]] os = "linux"` / `"macos"` / `"windows"` overrides and
  per-source flag pinning for vendored amalgamations.
- **Strict platform abstraction** — no `#ifdef _WIN32` in core logic.
- **Self-hosting** — every release is built from rivet's own `rivet.toml`
  via the multi-target engine; cmake is the alternate path for fresh-clone
  bootstrapping.

---

## Platform support

| Platform      | Architecture | Status       | Notes |
|---------------|--------------|--------------|-------|
| Linux         | x86_64       | **Primary**  | smoke green, self-build green |
| Linux         | arm64        | **Primary**  | smoke green |
| macOS         | arm64        | **Primary**  | smoke green, self-build green |
| Windows       | x86_64       | **Primary**  | smoke green, self-build green |
| macOS         | x86_64       | Not shipped  | Intel runner queue is unworkable; reintroduce on user ask |
| Linux (musl)  | x86_64       | Long-term    | not yet shipped |

---

## Architecture overview

```
bootstrap/          One-line installer scripts (sh / ps1) with cargo-style
                    VS Build Tools auto-detect on Windows.
platform/           Platform Abstraction Layer
  interface/        Pure C++ PAL API headers
  linux/  macos/  windows/   per-OS backends (fs, process, net, env, ...)
runtime/            Core runtime
  archive/          In-process tar.zst extraction (vendored libzstd)
  build/            Build IR, scheduler, executor, pkg-config reader,
                    multi-target engine
  cache/            Content-addressed compile + binary cache
  cli/              CLI dispatch (build, fetch, add, exec, ...)
  package/          Manifest parser, resolver, vcpkg/git/local sources
  toolchain/        Bundled-LLVM discovery + compile/link command builders
                    + SDK detection (xcrun, VS Build Tools)
vendor/             Pinned vendored C amalgamations (sqlite, libzstd)
.github/workflows/  CI (ci.yml), nightly smoke (smoke.yml), publish + release
```

---

## Building from source

For developers contributing to rivet itself. Once you have a release rivet
installed, `rivet build` against the repo-root `rivet.toml` builds it too —
that's the self-build path.

### Via the bundled-rivet path (no cmake)

```bash
git clone https://github.com/fedres/Rivet && cd Rivet
rivet toolchain install 19.1.7
rivet build
./.rivet/build/debug/bin/rivet --version
```

### Via CMake (bootstrap path, no rivet required)

> Requires CMake 3.25+ and a C++23-capable compiler.

```bash
cmake --preset release
cmake --build --preset release
```

See [docs/building.md](docs/building.md) for full instructions.

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
