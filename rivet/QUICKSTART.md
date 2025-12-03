# Rivet - Quick Start Guide

Rivet is a modern C++ project manager that makes dependency management and builds simple.

## Installation

```bash
cd /Users/karthikt/Rivet/rivet
cargo build --release
# Add to PATH
export PATH="/Users/karthikt/Rivet/rivet/target/release:$PATH"
```

## Quick Start

### Create a New Project

```bash
rivet new my_project
cd my_project
```

This creates:
```
my_project/
├── rivet.toml
└── src/
    └── main.cpp
```

### Add Dependencies

```bash
rivet add fmt
```

### Build and Run

```bash
rivet build
./target/debug/my_project
```

## Common Commands

```bash
# Project Management
rivet init                    # Initialize in current directory
rivet new <name>             # Create new project

# Dependencies
rivet add <package>          # Add dependency
rivet remove <package>       # Remove dependency
rivet build                  # Build project

# Toolchains
rivet toolchain list         # List available toolchains
rivet toolchain install <name>  # Install toolchain
rivet toolchain use <name>   # Set active toolchain

# Environment Isolation
rivet isolate init           # Create isolated environment
rivet isolate run -- <cmd>   # Run command in isolation

# Development
rivet test                   # Run tests
rivet fmt                    # Format code
rivet lint                   # Lint code
```

## Configuration (rivet.toml)

```toml
[package]
name = "my_project"
version = "0.1.0"
edition = "2023"

[dependencies]
fmt = "*"

[toolchain]
compiler = "clang++"
version = "auto"
```

## Example: Using fmt Library

```cpp
// src/main.cpp
#include <fmt/core.h>

int main() {
    fmt::print("Hello, Rivet!\n");
    return 0;
}
```

```bash
rivet add fmt
rivet build
./target/debug/my_project
```

## Next Steps

- Read the full documentation in `IMPLEMENTATION_SUMMARY.md`
- Check the walkthrough in `.gemini/antigravity/brain/.../walkthrough.md`
- Explore the task list in `.gemini/antigravity/brain/.../task.md`
