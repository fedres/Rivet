# CLI reference

All commands operate on the manifest found by walking up from the current
working directory.

## `rivet new <name>`

Scaffold a new project. Creates `<name>/{rivet.toml, src/main.cpp, .gitignore}`.

```bash
rivet new hello-fmt
rivet new mylib --template=lib   # (planned)
```

## `rivet add <pkg>[@version]`

Resolve `pkg` through the registered sources (vcpkg by default) and write
a `[dependencies]` entry into `rivet.toml`. Updates `rivet.lock`.

```bash
rivet add fmt
rivet add fmt@10.2.0
```

## `rivet remove <pkg>`

Drop `pkg` from `[dependencies]` + `rivet.lock`.

## `rivet fetch [--locked] [--frozen]`

Build every transitive dependency into a per-project install tree under
`~/.rivet/cache/deps/<project>/`. The path is fed back to `rivet build` so
headers / libraries / pkg-config files resolve automatically.

| Flag | Meaning |
|---|---|
| `--locked` | use `rivet.lock` verbatim; error if it would change |
| `--frozen` | `--locked` + no network access (full hermetic mode for CI) |

## `rivet build [--profile=<name>]`

Compile the project. Auto-discovers `src/`-laid-out single-binary projects;
dispatches to the multi-target engine if `rivet.toml` declares `[[lib]]` /
`[[bin]]` / `[[test]]` / `[[vendor]]`.

Built-in profiles: `debug` (default), `release`, `asan`, `tsan`, `msan`,
`ubsan`. Custom profiles via `[profiles.<name>]` in the manifest.

## `rivet run [<script>] [--profile=<name>] [-- <args>]`

Build, then either:
- run a `[scripts]` entry matching `<script>` (npm-style), or
- run the project's binary with `<args>` after `--`.

```bash
rivet run                      # → .rivet/build/debug/bin/myapp
rivet run -- --verbose --port 8080
rivet run fmt                  # runs the `fmt` script
```

## `rivet exec <name> [-- <args>]`

Run a binary that came in via the dependency graph (npm-exec / cargo-style).

```bash
rivet add protobuf
rivet fetch
rivet exec protoc -- --version
```

Discovery walks `~/.rivet/cache/deps/<project>/vcpkg-installed/<triplet>/
tools/<port>/<name>[.exe]`, falls back to `bin/<name>[.exe]`. The binary's
parent dir is prepended to `PATH` for the spawned child so co-located
sibling DLLs resolve on Windows.

## `rivet test`

Build the project (re-uses `rivet build`), then build and run every
`[[test]]` target. Test discovery is being expanded to also pick up
`tests/*.cpp` for single-binary projects (planned).

## `rivet toolchain <subcommand>`

Manage the bundled LLVM toolchains under `~/.rivet/toolchains/`.

```bash
rivet toolchain install 19.1.7    # pull from releases
rivet toolchain list              # show installed versions
rivet toolchain use 19.1.7        # set active
rivet toolchain active            # print active version
```

Override the download base with `RIVET_TOOLCHAIN_BASE_URL` for air-gapped
or private-mirror setups.

## `rivet cache`

```bash
rivet cache stats          # size, hit rate, oldest entry
rivet cache clean          # drop everything
rivet cache gc             # drop entries older than the threshold
```

## `rivet self-update`

Download the latest release of rivet itself and atomic-rename it into
place. Honours `RIVET_BASE_URL` for private mirrors.

## `rivet version` / `rivet --version`

Print the version, target triple, and git hash if injected at build time.

## `rivet help [<command>]`

Print the top-level help or per-command help.

## Environment variables

| Variable | Purpose |
|---|---|
| `RIVET_HOME` | root of rivet's data dir (default `~/.rivet`) |
| `RIVET_BASE_URL` | release-download base for `install.sh` |
| `RIVET_VERSION` | pin a specific version in `install.sh` |
| `RIVET_TOOLCHAIN_BASE_URL` | private mirror for LLVM bundles |
| `RIVET_LLVM_VERSION` | default LLVM version for `install.sh` |
| `RIVET_SKIP_TOOLCHAIN` | skip auto-toolchain in `install.sh` |
| `RIVET_SKIP_SDK_CHECK` | (Windows) bypass VS Build Tools probe |
| `RIVET_AUTO_INSTALL_VS` | (Windows) auto-approve winget install |
| `RIVET_REGISTRY_TOKEN` | auth for `rivet publish` |
| `RIVET_REGISTRY_URL` | custom registry base |
