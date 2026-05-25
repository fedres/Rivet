# Rivet

> A self-contained, cross-platform C++ build system and runtime.

```bash
curl -fsSL https://rivet.build/install.sh | sh
```

No sudo. No prior compiler. No assumptions.

---

## What is Rivet?

Rivet is a **complete, batteries-included C++ toolchain** — installer, compiler runtime, build system, package manager, and caching engine — distributed as a single self-contained bundle per platform.

It aims to be for C++ what `cargo` is for Rust: the obvious, default, correct choice.

---

## Design Philosophy

- **Bundled LLVM toolchain** — never depends on a system compiler
- **Hermetic, reproducible builds** — content-addressed, not timestamp-based
- **Strict platform abstraction** — no `#ifdef _WIN32` in core logic
- **SQLite-style engineering** — assume hostile platforms, handle partial writes, crash-consistency throughout
- **Self-hosting target** — `rivet build rivet`

---

## Architecture Overview

```
bootstrap/          Tiny installer scripts (sh / ps1)
platform/           Platform Abstraction Layer
  interface/        Pure C++ PAL API headers
  linux/            Linux backend implementations
  macos/            macOS backend implementations
  windows/          Windows backend implementations
runtime/            Core runtime (CLI, build engine, cache, packages)
toolchain/          Bundled LLVM integration layer
vendor/             Vendored third-party libraries (pinned)
tests/              Unit, integration, platform, stress, e2e
ci/                 CI configuration helpers
.github/workflows/  GitHub Actions CI matrix
```

---

## Platform Support

| Platform      | Architecture | Status      |
|---------------|-------------|-------------|
| Linux         | x64         | Primary     |
| Linux         | arm64       | Primary     |
| Linux (musl)  | x64         | Long-term   |
| macOS         | arm64       | Primary     |
| macOS         | x64         | Primary     |
| Windows       | x64         | Primary     |

---

## Building from Source

> Requires CMake 3.25+ and a C++20-capable compiler (clang 16+ recommended).

```bash
cmake --preset release
cmake --build --preset release
```

See [docs/building.md](docs/building.md) for full instructions.

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
