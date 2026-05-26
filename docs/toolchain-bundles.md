# Publishing LLVM toolchain bundles

Rivet bundles a deterministic LLVM toolchain so reproducible builds don't
depend on whatever system compiler happens to be installed. The bundles
are repacked from upstream LLVM official prebuilts and hosted as GitHub
release assets at `github.com/fedres/Rivet/releases/tag/toolchain-<ver>`.

## Publishing a new version

1. Go to **Actions → "Publish LLVM Toolchain Bundle"**.
2. Click **"Run workflow"**.
3. Enter the LLVM version (must match an upstream release tag — see
   [llvm/llvm-project/releases](https://github.com/llvm/llvm-project/releases)).
4. Pick whether to **strip unused** parts (default `true`; cuts bundle
   size by ~50% by removing docs, opt/llc, and clang static libs).
5. Run.

The workflow downloads each supported platform's upstream `.tar.xz`,
unpacks it, optionally strips, then recompresses with zstd and uploads
to a release tagged `toolchain-<ver>`. If a platform has no upstream
prebuilt for the requested version, that target is skipped without
failing the whole run.

Each release contains up to four bundles:

| Triple                       | Filename                                                  |
|------------------------------|-----------------------------------------------------------|
| `x86_64-linux-gnu`           | `rivet-toolchain-clang-<ver>-x86_64-linux-gnu.tar.zst`    |
| `arm64-linux-gnu`            | `rivet-toolchain-clang-<ver>-arm64-linux-gnu.tar.zst`     |
| `arm64-apple-macos`          | `rivet-toolchain-clang-<ver>-arm64-apple-macos.tar.zst`   |
| `x86_64-windows-msvc`        | `rivet-toolchain-clang-<ver>-x86_64-windows-msvc.tar.zst` |

Each is accompanied by a `.sha256` sidecar; the CLI verifies it
automatically on download.

## How the CLI consumes them

```sh
rivet toolchain install 19.1.7
```

resolves to:

```
https://github.com/fedres/Rivet/releases/download/toolchain-19.1.7/
    rivet-toolchain-clang-19.1.7-<triple>.tar.zst
```

Override the host with `RIVET_TOOLCHAIN_BASE_URL` for air-gapped
environments or private mirrors:

```sh
RIVET_TOOLCHAIN_BASE_URL=https://artifacts.acme.corp/rivet \
    rivet toolchain install 19.1.7
```

## Auto-bootstrap during install

When users run the one-line installer:

```sh
curl -fsSL .../install.sh | sh
```

it now also fetches a toolchain on first install. Override:

| Variable                | Effect                                             |
|-------------------------|----------------------------------------------------|
| `RIVET_SKIP_TOOLCHAIN=1`| Skip the toolchain bootstrap entirely.             |
| `RIVET_LLVM_VERSION=v`  | Install LLVM `v` instead of the default.           |
| `RIVET_TOOLCHAIN_BASE_URL=...` | Pull bundles from a custom URL.             |

## Picking a default LLVM version

The install scripts default to `RIVET_LLVM_VERSION=19.1.7`. To change
the default for new installs:

1. Pick a version with full platform coverage (most recent LLVM releases
   ship Linux X64/ARM64, macOS ARM64, and Windows X64 prebuilts).
2. Dispatch the workflow above to publish bundles for that version.
3. Update `bootstrap/install.sh` and `bootstrap/install.ps1`:
   change `DEFAULT_LLVM_VERSION="19.1.7"` (and the PowerShell equivalent).

## Why we don't compile LLVM ourselves

Compiling LLVM from source on GitHub Actions takes 6+ hours per platform
and would dominate our CI budget. Upstream LLVM already publishes
binaries built with their own hardening profile — repacking is a
straightforward way to get a deterministic bundle without recompiling.
