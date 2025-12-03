# Rivet Complex Demo - Workspace Example

This is a comprehensive example demonstrating Rivet's advanced build features.

## Project Structure

```
complex-demo/
├── rivet.toml          # Workspace root
├── mathlib/            # Static library package
│   ├── rivet.toml
│   ├── include/
│   │   └── mathlib/
│   │       └── math.hpp
│   └── src/
│       └── lib.cpp
├── stringutils/        # Static library package
│   ├── rivet.toml
│   ├── include/
│   │   └── stringutils/
│   │       └── strings.hpp
│   └── src/
│       └── lib.cpp
└── app/                # Binary application
    ├── rivet.toml
    └── src/
        └── main.cpp
```

## Features Demonstrated

✅ **Workspace Support**
- Multi-package workspace with 3 members
- Shared target directory
- Dependency resolution

✅ **Static Libraries**
- `mathlib` - Mathematical operations
- `stringutils` - String manipulation utilities

✅ **Binary Target**
- `calculator` - Application using both libraries

✅ **Library Compilation**
- Static library (`.a`) generation using `ar`
- Object file compilation
- Include path management

## Building

```bash
# Build entire workspace
rivet build

# Output:
# 📦 Discovering workspace members...
#   → mathlib
#   → stringutils
#   → app
# 🔨 Building workspace with 3 packages
# 
# Building mathlib
#   Compiling library mathlib
# 
# Building stringutils
#   Compiling library stringutils
# 
# Building app
#   Compiling binary calculator
# 
# ✓ Build complete
```

## Running

```bash
./target/debug/calculator
```

## Build Artifacts

After building, you'll find:
- `target/debug/libmathlib.a` - Math library
- `target/debug/libstringutils.a` - String utilities library
- `target/debug/calculator` - Executable binary
- `target/debug/mathlib/*.o` - Object files
- `target/debug/stringutils/*.o` - Object files

## Adding External Dependencies

To add external dependencies (e.g., boost, fmt), update the relevant `rivet.toml`:

```toml
[dependencies]
boost = "1.82"
fmt = "*"
```

Rivet will automatically download and configure these via vcpkg.

## Workspace Configuration

The workspace is defined in the root `rivet.toml`:

```toml
[workspace]
members = [
    "mathlib",
    "stringutils",
    "app",
]
```

Each member has its own `rivet.toml` defining its specific configuration.

## Commands

```bash
rivet build          # Build all packages
rivet clean          # Clean build artifacts
rivet check          # Fast syntax checking
```

## What This Demonstrates

1. **Workspace Management** - Multiple packages in one repository
2. **Library Types** - Static library compilation
3. **Build Order** - Correct dependency ordering
4. **Include Paths** - Proper header file discovery
5. **ar Integration** - Static library archiving
6. **Multi-Package Builds** - Coordinated compilation

This example shows that Rivet can handle complex, real-world C++ project structures with ease!
