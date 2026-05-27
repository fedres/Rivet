# `rivet.toml` reference

Every rivet project has a `rivet.toml` at its root. The schema is TOML 1.0.

## Single-binary project (minimal)

```toml
[package]
name    = "myapp"
version = "0.1.0"

[dependencies]
fmt = "*"
```

`rivet build` walks `src/` recursively, compiles every `.cpp`/`.cxx`/`.cc`,
links the result into `.rivet/build/<profile>/bin/myapp`. Headers are
resolved through `src/`, `include/` (if it exists), and dependency includes
from vcpkg.

## Multi-target project

Real projects need libraries, multiple binaries, vendored C amalgamations,
per-platform sources, and per-source compile flags. All of that is
expressible in one manifest.

```toml
[package]
name    = "myapp"
version = "0.2.0"

[build]
cxx_std = "c++23"

# A static library.
[[lib]]
name         = "myapp_core"
sources      = ["src/core/types.cpp", "src/core/io.cpp"]
include_dirs = ["include", "src/core"]

# A binary that depends on the lib.
[[bin]]
name       = "myapp"
path       = "src/main.cpp"
depends_on = ["myapp_core"]

# A test binary.
[[test]]
name       = "test_core"
path       = "tests/test_core.cpp"
depends_on = ["myapp_core"]
```

## Sections

### `[package]`

| Key | Required | Notes |
|---|---|---|
| `name` | yes | letters, digits, `_`, `-` |
| `version` | yes | SemVer 2.0 |
| `description` | no | one-line summary |
| `license` | no | SPDX identifier |
| `repository` | no | URL |
| `authors` | no | array of strings |
| `keywords` | no | array of strings |

### `[build]`

| Key | Default | Notes |
|---|---|---|
| `cxx_std` | `"c++23"` | passed as `-std=` |

### `[dependencies]` / `[dev-dependencies]`

```toml
[dependencies]
# Registry (vcpkg-backed): version is a SemVer constraint.
fmt        = "10.2.0"
spdlog     = "^1.13"
nlohmann_json = "*"        # any version

# Path: useful for monorepo / local development.
mylib = { path = "../mylib" }

# Git: clone a repo at a ref.
mylib = { git = "https://github.com/me/mylib", tag = "v0.3.1" }
mylib = { git = "https://github.com/me/mylib", branch = "main" }
mylib = { git = "https://github.com/me/mylib", rev  = "ab1234c" }

# Optional features (cargo-style).
boost = { version = "1.85", features = ["filesystem", "system"] }
```

`dev-dependencies` are only built when running `rivet test`.

### `[[lib]]` — static library

```toml
[[lib]]
name         = "mylib"
sources      = ["src/lib/foo.cpp", "src/lib/bar.cpp"]
include_dirs = ["include", "src/lib"]
depends_on   = ["other_lib"]               # intra-manifest deps
compile_flags = ["-Wall", "-Wextra"]
defines      = ["MYLIB_VERSION=\"1.0\""]
link_libs    = []                          # raw -l tokens for consumers
```

`sources` accepts simple globs: `src/lib/*.cpp`, `vendor/zstd/common/*.c`.

### `[[bin]]` — executable

```toml
[[bin]]
name       = "myapp"
path       = "src/main.cpp"
depends_on = ["mylib"]
```

`depends_on` causes every transitive `[[lib]]` / `[[vendor]]` archive to
appear on the link command in topological order (POSIX `ld` is
order-sensitive on static archives).

### `[[test]]` — test executable

Identical shape to `[[bin]]`. `rivet test` builds and runs every `[[test]]`
target.

### `[[vendor]]` — vendored C amalgamation

```toml
[[vendor]]
name         = "sqlite"
sources      = ["vendor/sqlite/sqlite3.c"]
include_dirs = ["vendor/sqlite"]
defines      = ["SQLITE_THREADSAFE=1", "SQLITE_OMIT_LOAD_EXTENSION=1"]

[vendor.per_source_flags]
"vendor/sqlite/sqlite3.c" = ["-Wno-all", "-Wno-extra", "-Wno-pedantic"]
```

Vendor targets are static archives the rest of the manifest can
`depends_on`. They differ from `[[lib]]` only by convention; the build
engine treats them the same way. `per_source_flags` is the escape hatch
for silencing warnings on third-party C that you wouldn't fix yourself.

### Cfg-conditional overrides

Cargo-style `cfg(os = ...)` gates. Each appears as an array-of-tables
under the target it modifies.

```toml
[[lib]]
name = "myapp_pal"
sources = ["src/pal/common.cpp"]

[[lib.cfg]]
os = "linux"
sources       = ["src/pal/linux/fs.cpp", "src/pal/linux/process.cpp"]
link_libs     = ["-lssl", "-lcrypto"]
compile_flags = ["-DPAL_LINUX=1"]

[[lib.cfg]]
os = "macos"
sources       = ["src/pal/macos/fs.cpp", "src/pal/macos/process.cpp"]
link_libs     = ["-framework", "CoreFoundation"]

[[lib.cfg]]
os = "windows"
sources       = ["src/pal/windows/fs.cpp", "src/pal/windows/process.cpp"]
link_libs     = ["-lws2_32", "-lkernel32", "-lshell32"]
compile_flags = ["-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX"]
```

The cfg block's lists are *additive* — they merge with the parent target's
top-level `sources` / `compile_flags` / `link_libs`. Only matching cfgs are
applied at build time.

Supported predicates: `os = "linux" | "macos" | "windows"`.
Negation, conjunctions, and `target_arch` are tracked but not yet
implemented.

### `[scripts]` — npm/bun-style commands

```toml
[scripts]
fmt    = "clang-format -i src/**/*.cpp"
lint   = "rivet exec clang-tidy src/main.cpp"
bench  = "./.rivet/build/release/bin/bench --runs=10"
```

`rivet run fmt` runs the value as a shell command with the manifest dir as
cwd and `~/.rivet/bin` prepended to `PATH`.

### `[profiles.*]` — build profiles

```toml
[profiles.debug]
opt_level = 0
debug     = true

[profiles.release]
opt_level = 2
debug     = false
lto       = true

[profiles.asan]
opt_level  = 0
debug      = true
sanitizers = ["address", "undefined"]
```

Built-in profiles: `debug`, `release`, `asan`, `tsan`, `msan`, `ubsan`.
Any of them can be overridden; `[profiles.<name>]` adds a new profile.
Select with `rivet build --profile=<name>`.

## Output layout

```
.rivet/build/<profile>/
├── obj/<target>/<rel-path>.o
├── lib/lib<target>.a              # POSIX
├── lib/<target>.lib               # Windows
├── bin/<target>[.exe]
└── tests/<target>[.exe]
```

## See also

- [`rivet.toml` at the repo root](../rivet.toml) — rivet's own manifest,
  demonstrates every feature on a real project.
- [CLI reference](cli.md) — command-line surface.
