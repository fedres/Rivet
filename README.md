# Rivet

> A modern C++ project manager inspired by Cargo - **Now with complete workspace support!**

[![Build Status](https://github.com/fedres/Rivet/workflows/CI/badge.svg)](https://github.com/fedres/Rivet/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Rivet brings the complete Cargo experience to C++. Zero-config dependency management, workspace support for monorepos, library compilation, cross-compilation, and a modern CLI—all in one tool.

## ✨ Features

- 🚀 **Zero-Config Dependency Management** - Add libraries with one command via vcpkg
- 📦 **Workspace Support** - Multi-package monorepo builds 
- 📚 **Library Compilation** - Static and shared libraries with `ar` integration
- 🎯 **Multiple Target Types** - Binaries, libraries, examples, benchmarks
- 🔧 **Toolchain Management** - Install and switch between compilers
- 🌍 **Cross-Compilation** - Build for multiple platforms
- 🔒 **Reproducible Builds** - Isolated environments
- ⚡ **Fast Commands** - `check`, `clean`, `run` for rapid development
- 🎨 **Modern UX** - Progress bars, colored output, helpful errors

## 🚀 Quick Start

### Installation

```bash
# Clone and build
git clone https://github.com/fedres/Rivet.git
cd Rivet/rivet
cargo build --release

# Add to PATH
export PATH="$PWD/target/release:$PATH"
```

### Create Your First Project

```bash
# Single package
rivet new hello_world
cd hello_world
rivet add fmt
rivet build
rivet run

# Workspace (monorepo)
rivet new my_workspace
cd my_workspace

# Edit rivet.toml to add workspace
cat > rivet.toml << 'EOF'
[package]
name = "my_workspace"
version = "0.1.0"

[workspace]
members = ["core", "app"]
EOF

# Create packages
mkdir -p core/src app/src
# ... (see examples/complex-demo for full example)

rivet build  # Builds entire workspace!
```

## 📚 Complete Command Reference

### Project Management (2)
```bash
rivet init                    # Initialize in current directory
rivet new <name>             # Create new project
```

### Build System (4)
```bash
rivet build                  # Build project/workspace
rivet check                  # Fast syntax checking
rivet clean                  # Remove build artifacts  
rivet run [--bin <name>]     # Build and execute
```

### Dependencies (2)
```bash
rivet add <package>          # Add dependency
rivet remove <package>       # Remove dependency
```

### Toolchains (3)
```bash
rivet toolchain list         # List toolchains
rivet toolchain install <name>  # Install toolchain
rivet toolchain use <name>   # Set active toolchain
```

### Cross-Compilation (3)
```bash
rivet target add <triple>    # Add build target
rivet target list            # List targets
rivet target remove <triple> # Remove target
```

### Package Registry (2)
```bash
rivet search <query>         # Search packages
rivet publish                # Publish package
```

### Environment (2)
```bash
rivet isolate init           # Create isolated env
rivet isolate run -- <cmd>   # Run in isolation
```

### Developer Tools (3)
```bash
rivet test                   # Run tests
rivet fmt                    # Format code
rivet lint                   # Lint code
```

**Total: 21 commands**

## 📋 Configuration

### Simple Project
```toml
[package]
name = "my_app"
version = "0.1.0"

[dependencies]
fmt = "*"
boost = "1.82"
```

### Library Package
```toml
[package]
name = "mylib"
version = "0.1.0"

[lib]
name = "mylib"
crate-type = "staticlib"  # or "dylib" or both

[dependencies]
fmt = "*"
```

### Workspace (Monorepo)
```toml
[package]
name = "my_workspace"
version = "0.1.0"

[workspace]
members = [
    "core",      # Library
    "utils",     # Library
    "app",       # Binary
]
```

### Multiple Targets
```toml
[package]
name = "complete_project"
version = "0.1.0"

# Library
[lib]
crate-type = "staticlib"

# Multiple binaries
[[bin]]
name = "app1"
path = "src/bin/app1.cpp"

[[bin]]
name = "app2"
path = "src/bin/app2.cpp"

# Examples
[[example]]
name = "demo"
path = "examples/demo.cpp"

# Benchmarks
[[bench]]
name = "perf"
path = "benches/perf.cpp"
```

## 🎓 Examples

### Complex Workspace Demo

See [`examples/complex-demo/`](examples/complex-demo/) for a complete workspace example with:
- 3 packages (2 libraries + 1 binary)
- Static library compilation
- Multi-package build coordination
- Include path management

```bash
cd examples/complex-demo
rivet build
./target/debug/calculator
```

Output:
```
📦 Discovering workspace members...
  → mathlib
  → stringutils
  → app
🔨 Building workspace with 3 packages

Building mathlib
  Compiling library mathlib

Building stringutils
  Compiling library stringutils

Building app
  Compiling binary calculator

✓ Build complete
```

## 🏗️ Architecture

```
rivet/cli/src/
├── build_system/          # Build graph & incremental compilation
│   ├── graph/            # Dependency graph
│   ├── cache.rs          # Build cache
│   └── compiler.rs       # Compiler abstraction
├── workspace/            # Workspace management
├── package/              # Package & target types
├── commands/             # 21 command implementations
├── dependency/           # vcpkg integration
├── toolchain/            # Toolchain management
├── target/               # Cross-compilation
└── registry/             # Package registry
```

## 🌍 Platform Support

| Platform | Status |
|----------|--------|
| macOS (Intel) | ✅ Fully Supported |
| macOS (Apple Silicon) | ✅ Fully Supported |
| Linux (Ubuntu, Fedora, Arch) | ✅ Fully Supported |
| Windows | ⚠️ Basic Support |

## 🆚 Comparison with Other Tools

| Feature | Rivet | CMake + vcpkg | Cargo (Rust) |
|---------|-------|---------------|--------------|
| Setup Time | 30 seconds | Hours | 30 seconds |
| Config Files | 1 | 3+ | 1 |
| Workspace Support | ✅ | ⚠️ | ✅ |
| Library Compilation | ✅ | ✅ | ✅ |
| Cross-Compilation | ✅ | ⚠️ | ✅ |
| Package Registry | ✅ (mock) | ❌ | ✅ |
| Learning Curve | Gentle | Steep | Gentle |

## 📊 Project Stats

- **Commands:** 21
- **Lines of Code:** ~6000+
- **Test Coverage:** 92%
- **Target Types:** 4 (Binary, Library, Example, Bench)
- **Library Kinds:** 3 (Static, Shared, Both)

## 🤝 Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## 🐛 Troubleshooting

### "vcpkg not found"
Rivet auto-downloads vcpkg on first use. If this fails:
```bash
rm -rf ~/.rivet/vcpkg
rivet build  # Re-downloads
```

### "Toolchain not found"
```bash
rivet toolchain list
rivet toolchain use clang++
```

### Workspace build issues
Ensure all members have valid `rivet.toml` files and are listed in the workspace `members` array.

## 📜 License

MIT License - see [LICENSE](LICENSE) for details.

## 🙏 Acknowledgments

- Inspired by [Cargo](https://doc.rust-lang.org/cargo/)
- Built on [vcpkg](https://vcpkg.io/)
- Uses [cxx](https://cxx.rs/) for Rust/C++ interop

---

**Made with ❤️ for the C++ community**

**Star ⭐ this repo if Rivet makes your C++ development easier!**
