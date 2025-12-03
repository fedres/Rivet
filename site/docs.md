# Rivet Documentation

Welcome to the complete Rivet documentation!

## Table of Contents

- [Getting Started](#getting-started)
- [Configuration](#configuration)
- [Commands](#commands)
- [Workspace](#workspace)
- [Target Types](#target-types)
- [Examples](#examples)

## Getting Started

### Installation

```bash
git clone https://github.com/fedres/Rivet.git
cd Rivet/rivet
cargo build --release
export PATH="$PWD/target/release:$PATH"
```

### Quick Start

```bash
# Create a new project
rivet new my_project
cd my_project

# Add a dependency
rivet add boost

# Build and run
rivet build
rivet run
```

## Configuration

Rivet projects are configured via `rivet.toml`:

### Basic Configuration

```toml
[package]
name = "my_project"
version = "0.1.0"
edition = "2023"

[dependencies]
boost = "1.82"
fmt = "*"
```

### Library Configuration

```toml
[package]
name = "mylib"
version = "0.1.0"

[lib]
name = "mylib"
path = "src/lib.cpp"
crate-type = "staticlib"  # Options: "staticlib", "dylib", or both
```

### Workspace Configuration

```toml
[package]
name = "my_workspace"
version = "0.1.0"

[workspace]
members = [
    "core",
    "utils",
    "app",
]
```

### Multiple Binaries

```toml
[[bin]]
name = "app1"
path = "src/bin/app1.cpp"

[[bin]]
name = "app2"
path = "src/bin/app2.cpp"
```

### Examples and Benchmarks

```toml
[[example]]
name = "demo"
path = "examples/demo.cpp"

[[bench]]
name = "performance"
path = "benches/perf.cpp"
```

## Commands

### Project Management

- `rivet init` - Initialize project in current directory
- `rivet new <name>` - Create new project in new directory

### Build System

- `rivet build` - Build project or workspace
- `rivet check` - Fast syntax checking without linking
- `rivet clean` - Remove all build artifacts
- `rivet run [--bin <name>]` - Build and execute binary

### Dependencies

- `rivet add <package>` - Add dependency to rivet.toml
- `rivet remove <package>` - Remove dependency

### Toolchains

- `rivet toolchain list` - List installed toolchains
- `rivet toolchain install <name>` - Install toolchain
- `rivet toolchain use <name>` - Set active toolchain

### Cross-Compilation

- `rivet target add <triple>` - Add build target
- `rivet target list` - List configured targets
- `rivet target remove <triple>` - Remove target

### Package Registry

- `rivet search <query>` - Search for packages
- `rivet publish` - Publish package to registry

### Environment

- `rivet isolate init` - Create isolated environment
- `rivet isolate run -- <cmd>` - Run command in isolation

### Developer Tools

- `rivet test` - Run tests
- `rivet fmt` - Format source files
- `rivet lint` - Lint source files

## Workspace

Workspaces allow you to manage multiple packages in a single repository.

### Structure

```
my_workspace/
├── rivet.toml          # Workspace root
├── core/
│   ├── rivet.toml
│   └── src/
├── utils/
│   ├── rivet.toml
│   └── src/
└── app/
    ├── rivet.toml
    └── src/
```

### Building Workspaces

```bash
# Build all packages
rivet build

# Output shows:
# 📦 Discovering workspace members...
#   → core
#   → utils
#   → app
# 🔨 Building workspace with 3 packages
```

## Target Types

Rivet supports four target types:

### 1. Binary

Executable programs:

```toml
[[bin]]
name = "my_app"
path = "src/main.cpp"
```

### 2. Library

Static or shared libraries:

```toml
[lib]
name = "mylib"
crate-type = "staticlib"  # Generates lib{name}.a
```

### 3. Example

Example programs:

```toml
[[example]]
name = "demo"
path = "examples/demo.cpp"
```

### 4. Benchmark

Benchmark programs:

```toml
[[bench]]
name = "perf"
path = "benches/perf.cpp"
```

## Examples

### Simple Binary with Dependencies

```bash
rivet new calculator
cd calculator

# Add dependencies
rivet add boost
rivet add fmt

# Edit src/main.cpp
# ... compile and run
rivet run
```

### Library Project

```bash
rivet new --lib mathlib
cd mathlib

# Configure as static library in rivet.toml
# [lib]
# crate-type = "staticlib"

rivet build
# Generates target/debug/libmathlib.a
```

### Workspace Project

See `examples/complex-demo/` in the repository for a complete workspace example with:
- 2 library packages
- 1 binary package
- Dependency management
- Build coordination

## Platform Support

| Platform | Status |
|----------|--------|
| macOS (Intel) | ✅ Fully Supported |
| macOS (Apple Silicon) | ✅ Fully Supported |
| Linux | ✅ Fully Supported |
| Windows | ⚠️ Basic Support |

## Troubleshooting

### "vcpkg not found"

Rivet auto-downloads vcpkg on first use:

```bash
rm -rf ~/.rivet/vcpkg
rivet build  # Re-downloads
```

### "Toolchain not found"

```bash
rivet toolchain list
rivet toolchain use clang++
```

### Build Issues

```bash
# Clean and rebuild
rivet clean
rivet build

# Check syntax only
rivet check
```

## Contributing

See [CONTRIBUTING.md](https://github.com/fedres/Rivet/blob/production/CONTRIBUTING.md) for guidelines.

## License

MIT License - see [LICENSE](https://github.com/fedres/Rivet/blob/production/LICENSE).
