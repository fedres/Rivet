# Contributing to Rivet

Thank you for your interest in contributing to Rivet! This document provides guidelines for contributing.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/yourusername/rivet`
3. Create a branch: `git checkout -b feature/my-feature`
4. Make your changes
5. Run tests: `cd rivet && cargo test`
6. Commit: `git commit -am 'Add my feature'`
7. Push: `git push origin feature/my-feature`
8. Create a Pull Request

## Development Setup

### Prerequisites
- Rust 1.70+ (`rustup` recommended)
- Git
- C++17 compiler (clang++ or g++)

### Building
```bash
cd rivet
cargo build
```

### Running Tests
```bash
cargo test
```

### Running with Logging
```bash
RIVET_LOG=debug cargo run -- build
```

## Code Style

- Run `cargo fmt` before committing
- Run `cargo clippy` and fix warnings
- Write tests for new features
- Update documentation

## Pull Request Guidelines

- Keep PRs focused on a single feature/fix
- Write clear commit messages
- Add tests for new functionality
- Update README if adding user-facing features
- Ensure CI passes

## Testing

- Unit tests: Test individual functions
- Integration tests: Test CLI commands end-to-end
- Add tests in `rivet/cli/tests/`

Example:
```rust
#[test]
fn test_my_feature() {
    // Test code
}
```

## Reporting Issues

- Check existing issues first
- Provide clear reproduction steps
- Include system information (OS, Rust version)
- Include error messages and logs

## Code of Conduct

- Be respectful and inclusive
- Focus on constructive feedback
- Help others learn

## Questions?

Open an issue with the "question" label or start a discussion.

Thank you for contributing! 🎉
