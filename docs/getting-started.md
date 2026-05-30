# Getting Started

Five minutes from "I've never run rivet" to "I have a binary".

## 1. Install

```bash
# Linux / macOS
curl -fsSL https://github.com/fedres/Rivet/releases/latest/download/install.sh | sh

# Windows (PowerShell)
irm https://github.com/fedres/Rivet/releases/latest/download/install.ps1 | iex
```

The installer drops a `rivet` binary into `~/.rivet/bin/` (Linux/macOS) or
`%USERPROFILE%\.rivet\bin\` (Windows) and adds it to your `PATH`. Restart
your shell so the change takes effect.

```bash
rivet --version
# rivet 0.1.0 (...)
```

### Platform prereqs

**No special prereqs on any platform** — rivet ships its own clang, libc++,
linker, and (on Windows) MinGW-w64 SDK pieces.

| Platform | What rivet needs | Notes |
|---|---|---|
| **Linux** | nothing | glibc + kernel headers ship with every distro |
| **Windows** | nothing | rivet bundles llvm-mingw (LLVM + MinGW-w64 + libc++) — no Build Tools, no Windows SDK install, no winget prompt |
| **macOS** | Xcode CLI tools | the baseline every C/C++ toolchain on macOS needs (cargo, brew, gcc, anything). If you've ever run `cc` on a Mac, you already have them. If not: `xcode-select --install` (one-time, ~1 GB). Apple's licence doesn't let any third-party tool redistribute the frameworks. |

## 2. Install a toolchain

```bash
rivet toolchain install 19.1.7
```

Pulls a ~600 MB platform-specific LLVM bundle (clang, clang++, lld, libc++,
llvm-rc on Windows) from the project's release page and extracts it under
`~/.rivet/toolchains/19.1.7/`. Subsequent commands use it automatically.

## 3. New project

```bash
rivet new hello-fmt
cd hello-fmt
```

This creates:

```
hello-fmt/
├── rivet.toml      # package manifest
├── src/
│   └── main.cpp    # "Hello, World!" stub
└── .gitignore
```

## 4. Add a dependency

```bash
rivet add fmt
```

`fmt` resolves through vcpkg as a backend. You'll never need to type `vcpkg`.
The manifest gains a `[dependencies]` entry and `rivet.lock` is created.

## 5. Use it

```cpp
// src/main.cpp
#include <fmt/core.h>
int main() {
    fmt::print("hello from fmt {}\n", 42);
    return 0;
}
```

## 6. Build + run

```bash
rivet fetch    # vcpkg builds fmt against the bundled clang (slow first time)
rivet build    # compiles main.cpp against fmt, produces the binary
rivet run      # → hello from fmt 42
```

The binary lives at `.rivet/build/debug/bin/hello-fmt`.

## Where to next?

- [Manifest reference](manifest.md) — every field in `rivet.toml`
- [CLI reference](cli.md) — every subcommand
- [Building rivet itself](building.md) — for contributors
- [Toolchain bundles](toolchain-bundles.md) — how the LLVM bundles are
  produced and published
