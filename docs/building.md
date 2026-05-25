# Building Rivet from Source

## Prerequisites

| Tool        | Minimum version | Notes                                    |
|-------------|----------------|------------------------------------------|
| C++ compiler | GCC 13, Clang 16, MSVC 19.35 | C++23 required                |
| CMake        | 3.25           | CMakePresets.json support               |
| Ninja        | 1.11           | Recommended generator                   |
| Git          | 2.30           | Used by FetchContent for GoogleTest     |

### Linux

```sh
# Ubuntu / Debian
sudo apt-get install cmake ninja-build gcc-13 g++-13 libseccomp-dev

# Fedora
sudo dnf install cmake ninja-build gcc g++ libseccomp-devel

# Arch
sudo pacman -S cmake ninja gcc libseccomp
```

### macOS

```sh
brew install cmake ninja
# System clang (Xcode 14.3+) or brew install llvm
```

### Windows

Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with the
**Desktop development with C++** workload. CMake and Ninja are included.

## Quick start

```sh
# Clone
git clone https://github.com/fedres/Rivet.git && cd Rivet

# Configure (debug build with assertions)
cmake --preset debug

# Build
cmake --build --preset debug

# Run tests
ctest --preset debug --output-on-failure

# Run the binary
./build/debug/rivet --version
```

## Available presets

| Preset    | Optimization | Sanitizers                    |
|-----------|-------------|-------------------------------|
| `debug`   | `-O0 -g`    | none                          |
| `release` | `-O3`       | none                          |
| `asan`    | `-O1 -g`    | AddressSanitizer + UBSan      |
| `tsan`    | `-O1 -g`    | ThreadSanitizer               |

## Build flags reference

| CMake variable           | Default | Meaning                              |
|--------------------------|---------|--------------------------------------|
| `RIVET_BUILD_TESTS`      | `ON`    | Build the test suite                 |
| `RIVET_STATIC_RUNTIME`   | `ON`    | Link libstdc++/libgcc statically (Linux) |
| `CMAKE_INSTALL_PREFIX`   | `/usr/local` | Installation prefix            |

## Install

```sh
cmake --preset release
cmake --build --preset release
sudo cmake --install build/release
```

## Cross-compilation

Cross-compilation toolchain files are planned in `toolchain/`. For now, build
natively on each target platform using the provided CI matrix as reference.

## Vendored dependencies

All third-party code lives in `vendor/`. See [vendor/README.md](../vendor/README.md)
for the version and license inventory. No internet access is required after the
initial `FetchContent` download of GoogleTest (only needed for testing).

## Troubleshooting

**`fatal error: bits/c++config.h: No such file`** — Install the 64-bit GCC
multilib on Ubuntu: `sudo apt-get install gcc-multilib g++-multilib`.

**`cmake: command not found`** — CMake 3.25+ is required. Check your distribution's
package name (`cmake3` on some older RHEL/CentOS systems).

**Long paths on Windows** — Enable long path support:
```powershell
Set-ItemProperty -Path 'HKLM:\System\CurrentControlSet\Control\FileSystem' `
    -Name LongPathsEnabled -Value 1
```

**ASAN reports false positives on macOS** — Run with
`MallocNanoZone=0 ASAN_OPTIONS=detect_leaks=0 ./build/asan/rivet`.
