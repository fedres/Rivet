# Contributing to Rivet

Rivet builds rivet. Once you have a release binary, the dev loop is exactly
what end-users get — no cmake, no ninja, no manual toolchain hunting.

## 1. Get a rivet binary

```bash
# Linux / macOS
curl -fsSL https://github.com/fedres/Rivet/releases/latest/download/install.sh | sh

# Windows (PowerShell)
irm https://github.com/fedres/Rivet/releases/latest/download/install.ps1 | iex
```

The installer puts `rivet` on `PATH` and drops the LLVM toolchain under
`~/.rivet/toolchains/`. Windows ships with llvm-mingw bundled — no Build
Tools install, no winget prompt, no Windows SDK download. Restart your
shell after install so the new `PATH` is picked up.

```bash
rivet --version
# rivet 0.2.0 (...)
```

## 2. Clone the repo

```bash
git clone https://github.com/fedres/Rivet
cd Rivet
```

The repo root has a [`rivet.toml`](../rivet.toml) — that's rivet's own
manifest. Five targets (rivet_pal, sqlite_amalg, zstd_decomp, rivet_runtime,
rivet) declared via the multi-target schema, with cargo-style `cfg(os=...)`
overrides for the per-platform PAL code.

## 3. Build

```bash
rivet build
```

The multi-target engine compiles 49 sources into 4 archives + 1 binary in
~15s on a clean Linux/macOS/Windows CI runner. The output binary is at
`.rivet/build/debug/bin/rivet[.exe]`.

```bash
./.rivet/build/debug/bin/rivet --version
```

That's the version you just built. Drop it into your `PATH` (or
`~/.rivet/bin/`) if you want to dogfood it as the default.

## 4. Iterate

Standard Rust-cargo cadence:

```bash
rivet build               # incremental: only changed sources recompile
rivet build --profile=release
rivet build --profile=asan
```

Add a source: drop the file into the right directory and append its path
to the matching `sources = [...]` list in `rivet.toml`. No regen step.

Add an external dep:

```bash
rivet add nlohmann_json
rivet fetch
```

## The cmake bootstrap path

The repo still ships a `CMakeLists.txt`. Its only job is to bootstrap rivet
from a host system that has *no* rivet binary yet. Everything else — CI,
contributors, end users — uses `rivet build`. If you need the cmake path:

```bash
cmake --preset release
cmake --build --preset release
# → build/release/rivet[.exe]
```

We try to keep the two build paths in lockstep — adding a source means
updating both `rivet.toml` and `CMakeLists.txt`. The CI matrix exercises
both: every nightly smoke run goes through the cmake path (to produce a
fresh rivet binary), then through rivet's own self-build (to prove the
two stay aligned).

## Tests

```bash
rivet test                # build + run every [[test]] target
```

The rivet repo declares unit tests as `[[test]]` blocks in
[`tests/unit/CMakeLists.txt`](../tests/unit/CMakeLists.txt) (cmake path) and
under the same `[[test]]` schema in `rivet.toml` (rivet-build path; growing
test coverage in M1.polish). Smoke tests live under
[`tests/smoke/`](../tests/smoke/) and run via the `.github/workflows/smoke.yml`
nightly.

## Pull requests

* Open against `main`.
* `rivet build` + `rivet test` should pass locally on at least one OS.
* CI runs the full smoke matrix (Linux x64 + arm64, macOS arm64, Windows x64).
* PRs that touch `runtime/cli/cli.cpp` should keep the help text in sync
  with [`docs/cli.md`](cli.md).

## Working on toolchain bundles

If you're contributing changes to how the LLVM toolchain is packaged
(`.github/workflows/publish-toolchain.yml`), see
[Toolchain bundles](toolchain-bundles.md) for the publish flow and what
each bundle is supposed to contain.

## Where things live

| Area | Path | Notes |
|---|---|---|
| CLI dispatch + commands | `runtime/cli/cli.cpp` | build, fetch, add, exec, run, … |
| Multi-target build engine | `runtime/build/multi_target.{hpp,cpp}` | topo, cfg, per-source flags |
| Build IR + scheduler | `runtime/build/{graph,scheduler,executor}.cpp` | DAG + parallel executor |
| Pkg-config reader | `runtime/build/pkgconfig.cpp` | auto-link from vcpkg installs |
| Manifest parser | `runtime/package/manifest.cpp` | rivet.toml schema |
| Source registries | `runtime/package/sources/{vcpkg,git,local}.cpp` | package backends |
| Toolchain discovery | `runtime/toolchain/discovery.cpp` | locate active LLVM |
| Compile / link commands | `runtime/toolchain/compile.cpp` | clang/clang++/lld wrappers |
| SDK detection | `runtime/toolchain/sdk.cpp` | xcrun + vswhere |
| tar.zst extraction | `runtime/archive/tar_zst.cpp` | in-process libzstd |
| Platform Abstraction Layer | `platform/{interface,linux,macos,windows}/` | fs, process, env, net, ... |
| Vendored amalgamations | `vendor/{sqlite,zstd}/` | fetched at configure time |

## Style

* Roughly the existing style — explicit C++23, no `auto`-everything,
  `Result<T>` for fallible APIs, `// section dividers` between logical
  blocks of a file.
* Comments: explain the *why* and the non-obvious. Don't paraphrase what
  the code already says.
* Tests: prefer behavioural tests over implementation tests; gtest is the
  framework.
