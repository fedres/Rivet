#!/bin/sh
set -e

# Rivet Installer
# Installs Rivet from GitHub Releases

REPO="fedres/Rivet"
INSTALL_DIR="$HOME/.rivet"
BIN_DIR="$INSTALL_DIR/bin"
EXE="rivet"

# Detect OS and Architecture
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Linux)
        PLATFORM="linux"
        ;;
    Darwin)
        PLATFORM="macos"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        PLATFORM="windows"
        EXE="rivet.exe"
        ;;
    *)
        echo "Unsupported operating system: $OS"
        exit 1
        ;;
esac

case "$ARCH" in
    x86_64)
        ARCH="x86_64"
        ;;
    arm64|aarch64)
        ARCH="aarch64"
        ;;
    *)
        echo "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

# Construct download URL (assuming standard naming convention)
# rivet-x86_64-unknown-linux-gnu
# rivet-aarch64-apple-darwin
if [ "$PLATFORM" = "macos" ]; then
    TARGET="$ARCH-apple-darwin"
elif [ "$PLATFORM" = "linux" ]; then
    TARGET="$ARCH-unknown-linux-gnu"
else
    TARGET="$ARCH-pc-windows-msvc"
fi

ASSET_NAME="rivet-$TARGET"
DOWNLOAD_URL="https://github.com/$REPO/releases/latest/download/$ASSET_NAME"

echo "Installing Rivet..."
echo "  • Detected platform: $PLATFORM ($ARCH)"
echo "  • Install directory: $INSTALL_DIR"

# Create directory
mkdir -p "$BIN_DIR"

# Download
echo "  • Downloading from GitHub..."
if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$DOWNLOAD_URL" -o "$BIN_DIR/$EXE"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$BIN_DIR/$EXE" "$DOWNLOAD_URL"
else
    echo "Error: curl or wget is required to install Rivet."
    exit 1
fi

# Make executable
chmod +x "$BIN_DIR/$EXE"

# Setup PATH
SHELL_NAME=$(basename "$SHELL")
RC_FILE=""

case "$SHELL_NAME" in
    bash) RC_FILE="$HOME/.bashrc" ;;
    zsh)  RC_FILE="$HOME/.zshrc" ;;
    fish) RC_FILE="$HOME/.config/fish/config.fish" ;;
esac

echo "  • Setting up PATH..."
if [ -n "$RC_FILE" ]; then
    if ! grep -q "$BIN_DIR" "$RC_FILE"; then
        if [ "$SHELL_NAME" = "fish" ]; then
            echo "set -gx PATH \"$BIN_DIR\" \$PATH" >> "$RC_FILE"
        else
            echo "export PATH=\"$BIN_DIR:\$PATH\"" >> "$RC_FILE"
        fi
        echo "    Added to $RC_FILE"
    else
        echo "    Already in $RC_FILE"
    fi
fi

echo ""
echo "✨ Rivet installed successfully!"
echo "Please restart your terminal or run:"
echo "    export PATH=\"$BIN_DIR:\$PATH\""
echo ""
echo "Run 'rivet --help' to get started."
