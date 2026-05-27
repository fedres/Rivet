# Rivet

A self-contained, cross-platform C++ build system and package manager —
cargo for C++, in spirit and ergonomics.

```bash
# Linux / macOS
curl -fsSL https://github.com/fedres/Rivet/releases/latest/download/install.sh | sh

# Windows (PowerShell)
irm https://github.com/fedres/Rivet/releases/latest/download/install.ps1 | iex
```

## Documentation

- **[Getting Started](getting-started.md)** — 5-minute tour from install
  to running binary.
- **[`rivet.toml` reference](manifest.md)** — every section, every field,
  with examples.
- **[CLI reference](cli.md)** — every subcommand, every flag.
- **[Building rivet itself](building.md)** — for contributors.
- **[Toolchain bundles](toolchain-bundles.md)** — how the LLVM bundles are
  produced and published.

## Why rivet?

C++ tooling is fractured. To onboard a contributor today you typically
need:

- a system C++ compiler (the right version)
- CMake (the right version)
- Ninja, possibly Make
- vcpkg or Conan, configured against the right toolchain
- pkg-config, possibly Boost, possibly Qt's qmake
- platform-specific build tools (MSVC's `vcvarsall.bat`, Xcode's
  Command Line Tools)
- a separate sanitizer-instrumented toolchain for asan/tsan/ubsan

Cargo solved this for Rust by shipping one binary that owns the entire
flow. Rivet does the same for C++:

- ships its own clang + libc++ per-platform (`rivet toolchain install`),
- uses vcpkg as a *backend* (`rivet add fmt` — users never type `vcpkg`),
- builds dependencies hermetically into a per-project install tree,
- auto-links via pkg-config metadata so adding a dep is one line,
- emits the same project layout (`src/`, `include/`, `tests/`) cargo did,
- self-hosts: every release of rivet is built by rivet, from a
  `rivet.toml`, with no cmake in the trace.

The single non-redistributable prereq is the platform SDK — Apple's
frameworks and Microsoft's Windows Kits are licensed and cannot be
shipped. The installer detects + auto-installs on Windows (`winget`),
prompts on macOS (`xcode-select --install`), and is a no-op on Linux.

## Status

* **Multi-platform**: Linux x64 + arm64, macOS arm64, Windows x64 all
  pass the nightly smoke (hello-fmt project + multi-target lib+bin +
  rivet builds rivet end-to-end).
* **Self-hosting**: rivet builds itself from `rivet.toml` on every CI run.
* **vcpkg-backed**: hundreds of C++ packages are accessible via
  `rivet add` already.
* Public release: `v0.1.0` — `curl install.sh | sh` works.

The release page tracks platform support and the current bundle versions.

## License

Apache 2.0.
