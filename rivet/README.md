# Rivet

> A modern C++ project manager that makes dependency management boring (in the best way)

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()

Rivet brings the Cargo experience to C++. No more CMake wrangling, no dependency hell, no "works on my machine" — just code.

## Features

- 🚀 **Zero-config dependency management** - Add libraries with one command
- 🔧 **Toolchain management** - Install and switch between compilers easily
- 📦 **vcpkg integration** - Transparent access to thousands of packages
- 🔒 **Reproducible builds** - Isolated environments ensure consistency
- 🎯 **Developer-friendly** - Test, format, and lint with simple commands

## Quick Start

### Installation

```bash
# Clone and build
git clone https://github.com/yourusername/rivet
cd rivet
cargo build --release

# Add to PATH
export PATH="$PWD/target/release:$PATH"
```

### Create Your First Project

```bash
# Create a new project
rivet new my_app
cd my_app

# Add a dependency
rivet add fmt

# Write some code
cat > src/main.cpp << 'EOF'
#include <fmt/core.h>

int main() {
    fmt::print("Hello from Rivet!\n");
    return 0;
}
EOF

# Build and run
rivet build
./target/debug/my_app
```

Output:
```
Hello from Rivet!
```

## Commands

### Project Management
```bash
rivet init                    # Initialize in current directory
rivet new <name>             # Create new project
rivet build                  # Build project
```

### Dependencies
```bash
rivet add <package>          # Add dependency
rivet remove <package>       # Remove dependency
```

### Toolchains
```bash
rivet toolchain list         # List available toolchains
rivet toolchain install <name>  # Install toolchain
rivet toolchain use <name>   # Set active toolchain
```

### Environment
```bash
rivet isolate init           # Create isolated environment
rivet isolate run -- <cmd>   # Run command in isolation
```

### Development
```bash
rivet test                   # Run tests
rivet fmt                    # Format code
rivet fmt --check            # Check formatting
rivet lint                   # Lint code
```

## Configuration

Rivet uses a simple `rivet.toml` file:

```toml
[package]
name = "my_app"
version = "0.1.0"
edition = "2023"

[dependencies]
fmt = "*"
boost = "1.82"

[toolchain]
compiler = "clang++"
version = "auto"
```

## How It Works

1. **Dependencies**: Rivet uses vcpkg under the hood, but you never need to know that
2. **Toolchains**: Install compilers to `~/.rivet/toolchains` without sudo
3. **Isolation**: Each project gets a `.rivet/` directory with its own environment
4. **Build**: Rivet invokes the compiler with the right flags automatically

## Comparison

| Feature | Rivet | CMake + vcpkg | Conan |
|---------|-------|---------------|-------|
| Setup time | 30 seconds | Hours | Hours |
| Config files | 1 (rivet.toml) | 3+ (CMakeLists, vcpkg.json, etc.) | 2+ |
| Learning curve | Gentle | Steep | Moderate |
| Toolchain mgmt | Built-in | Manual | Manual |

## Requirements

- Rust 1.70+ (for building Rivet)
- C++17 compiler (clang++ or g++)
- Git (for vcpkg)

Optional:
- `clang-format` (for `rivet fmt`)
- `clang-tidy` (for `rivet lint`)

## Platform Support

- ✅ macOS (Intel & Apple Silicon)
- ✅ Linux (Ubuntu, Fedora, Arch)
- ⚠️ Windows (basic support)

## Examples

### Using Boost

```bash
rivet new boost_example
cd boost_example
rivet add boost[asio]
```

```cpp
#include <boost/asio.hpp>
#include <iostream>

int main() {
    boost::asio::io_context io;
    std::cout << "Boost.Asio works!\n";
    return 0;
}
```

```bash
rivet build
./target/debug/boost_example
```

### Running Tests

```bash
# Add Catch2
rivet add catch2

# Create test
mkdir tests
cat > tests/test_main.cpp << 'EOF'
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Example test", "[example]") {
    REQUIRE(1 + 1 == 2);
}
EOF

# Run tests
rivet test
```

## Troubleshooting

### "vcpkg not found"
Rivet will automatically download vcpkg on first use. If this fails:
```bash
rm -rf ~/.rivet/vcpkg
rivet build  # Will re-download
```

### "Toolchain not found"
```bash
rivet toolchain list  # See available toolchains
rivet toolchain use clang++  # Use system compiler
```

### Build fails with linking errors
Check that the dependency name matches the library name:
```bash
rivet add nlohmann-json  # Package name
# Link with: -lnlohmann_json (library name)
```

## Contributing

Contributions welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) first.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- Inspired by [Cargo](https://doc.rust-lang.org/cargo/) (Rust)
- Built on [vcpkg](https://vcpkg.io/) (Microsoft)
- Uses [cxx](https://cxx.rs/) for Rust/C++ interop

## Roadmap

- [x] Basic dependency management
- [x] Toolchain management
- [x] Environment isolation
- [x] Developer workflow commands
- [ ] Workspace support (monorepos)
- [ ] Cross-compilation
- [ ] Package registry
- [ ] IDE integration

---

Made with ❤️ for the C++ community
