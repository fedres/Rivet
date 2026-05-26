#!/usr/bin/env sh
# bootstrap/install.sh — Rivet one-line installer
# Usage: curl -fsSL https://releases.rivet.build/install.sh | sh
#   Or:  curl -fsSL https://releases.rivet.build/install.sh | RIVET_VERSION=0.2.0 sh
set -eu

RIVET_HOME="${RIVET_HOME:-$HOME/.rivet}"
BASE_URL="${RIVET_BASE_URL:-https://github.com/fedres/Rivet/releases/download}"
VERSION="${RIVET_VERSION:-}"   # empty → resolve latest from GitHub

# ─── Download helper ─────────────────────────────────────────────────────────
# Prefers curl; falls back to wget; errors out if neither is available.

_download() {  # _download <url> <dest>
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --progress-bar -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --show-progress -O "$2" "$1"
    else
        echo "error: neither curl nor wget found. Install one and retry." >&2
        exit 1
    fi
}

# Silent download — returns non-zero on failure without printing anything.
_download_quiet() {  # _download_quiet <url> <dest>
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$2" "$1" 2>/dev/null
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$2" "$1" 2>/dev/null
    else
        return 1
    fi
}

# ─── Platform detection ───────────────────────────────────────────────────────

_detect_os() {
    case "$(uname -s)" in
        Linux*)  echo linux ;;
        Darwin*) echo macos ;;
        *)       printf 'error: unsupported OS: %s\n' "$(uname -s)" >&2; exit 1 ;;
    esac
}

_detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)  echo x86_64 ;;
        aarch64|arm64) echo arm64 ;;
        *)             printf 'error: unsupported arch: %s\n' "$(uname -m)" >&2; exit 1 ;;
    esac
}

OS="$(_detect_os)"
ARCH="$(_detect_arch)"
TRIPLE="${OS}-${ARCH}"

# ─── Resolve version ──────────────────────────────────────────────────────────

if [ -z "$VERSION" ]; then
    VERSION="$(_download_quiet \
        "https://api.github.com/repos/fedres/Rivet/releases/latest" \
        /dev/stdout 2>/dev/null \
        | grep '"tag_name"' | head -1 \
        | sed 's/.*"tag_name": *"\([^"]*\)".*/\1/')" || true
fi

if [ -z "$VERSION" ]; then
    echo "error: could not determine latest Rivet version." >&2
    echo "       Set RIVET_VERSION=<version> and retry." >&2
    exit 1
fi

BUNDLE="rivet-${VERSION}-${TRIPLE}.tar.zst"
BUNDLE_URL="${BASE_URL}/${VERSION}/${BUNDLE}"
SHA_URL="${BUNDLE_URL}.sha256"

echo "Installing Rivet ${VERSION} (${TRIPLE}) → ${RIVET_HOME} ..."

# ─── Download ─────────────────────────────────────────────────────────────────

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

_download "$BUNDLE_URL" "$TMP/$BUNDLE"

# ─── Checksum verification ───────────────────────────────────────────────────

_sha256() {  # _sha256 <file>  → prints hex hash
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo ""
    fi
}

if _download_quiet "$SHA_URL" "$TMP/$BUNDLE.sha256"; then
    EXPECTED="$(awk '{print $1}' < "$TMP/$BUNDLE.sha256")"
    ACTUAL="$(_sha256 "$TMP/$BUNDLE")"
    if [ -z "$ACTUAL" ]; then
        echo "warning: no sha256 tool found — skipping checksum verification." >&2
    elif [ "$EXPECTED" != "$ACTUAL" ]; then
        echo "error: checksum mismatch (expected $EXPECTED, got $ACTUAL)." >&2
        echo "       The download may be corrupt or tampered with." >&2
        exit 1
    else
        echo "Checksum verified."
    fi
fi

# ─── Extract ─────────────────────────────────────────────────────────────────

mkdir -p "$RIVET_HOME"

# Try GNU/BSD tar with --zstd first; fall back to zstd pipe; error if neither works.
if tar --zstd -xf "$TMP/$BUNDLE" -C "$RIVET_HOME" 2>/dev/null; then
    :
elif command -v zstd >/dev/null 2>&1; then
    zstd -dc "$TMP/$BUNDLE" | tar -xf - -C "$RIVET_HOME"
else
    echo "error: cannot decompress .tar.zst — please install zstd:" >&2
    echo "         macOS:  brew install zstd" >&2
    echo "         Ubuntu: sudo apt install zstd" >&2
    echo "         Fedora: sudo dnf install zstd" >&2
    exit 1
fi

chmod +x "$RIVET_HOME/bin/rivet"

# ─── PATH update ─────────────────────────────────────────────────────────────

RIVET_BIN="$RIVET_HOME/bin"
EXPORT_LINE='export PATH="$HOME/.rivet/bin:$PATH"'

_add_to_profile() {
    local profile="$1"
    [ -f "$profile" ] || return 0
    grep -qF '.rivet/bin' "$profile" 2>/dev/null && return 0
    printf '\n# Added by Rivet installer\n%s\n' "$EXPORT_LINE" >> "$profile"
    echo "  Updated $(basename "$profile")"
}

case "$PATH" in
    *"$RIVET_BIN"*) ;;   # already in PATH — nothing to do
    *)
        _add_to_profile "$HOME/.bashrc"
        _add_to_profile "$HOME/.zshrc"
        _add_to_profile "$HOME/.profile"
        # Fish shell
        if [ -d "$HOME/.config/fish" ]; then
            FISH_CONF="$HOME/.config/fish/conf.d/rivet.fish"
            if [ ! -f "$FISH_CONF" ] || ! grep -qF '.rivet/bin' "$FISH_CONF" 2>/dev/null; then
                mkdir -p "$(dirname "$FISH_CONF")"
                printf 'fish_add_path "$HOME/.rivet/bin"\n' > "$FISH_CONF"
                echo "  Updated fish config"
            fi
        fi
        echo ""
        echo "Rivet added to PATH. Reload your shell or run:"
        echo "  source ~/.bashrc    # bash"
        echo "  source ~/.zshrc     # zsh"
        ;;
esac

# ─── LLVM toolchain bootstrap ────────────────────────────────────────────────
#
# A working Rivet needs a bundled LLVM. The published toolchain bundles live
# at github.com/fedres/Rivet/releases/tag/toolchain-<version> (uploaded by
# the publish-toolchain.yml Actions workflow).
#
# Set RIVET_SKIP_TOOLCHAIN=1 to skip; useful for CI where the toolchain
# is pre-cached or comes from a registry. RIVET_LLVM_VERSION overrides the
# version we'd otherwise install by default.

if [ "${RIVET_SKIP_TOOLCHAIN:-0}" = "0" ]; then
    DEFAULT_LLVM_VERSION="${RIVET_LLVM_VERSION:-18.1.8}"
    if "$RIVET_HOME/bin/rivet" toolchain list 2>/dev/null | grep -q 'clang-'; then
        echo ""
        echo "Toolchain already installed — skipping bootstrap."
    else
        echo ""
        echo "Installing LLVM toolchain ${DEFAULT_LLVM_VERSION}..."
        echo "  (set RIVET_SKIP_TOOLCHAIN=1 to skip)"
        if ! "$RIVET_HOME/bin/rivet" toolchain install "$DEFAULT_LLVM_VERSION"; then
            echo ""
            echo "warning: toolchain install failed. You can retry with:"
            echo "  rivet toolchain install $DEFAULT_LLVM_VERSION"
            echo "Or set RIVET_TOOLCHAIN_BASE_URL to point at a mirror."
        fi
    fi
fi

# ─── Done ─────────────────────────────────────────────────────────────────────

echo ""
echo "Rivet ${VERSION} installed."
echo "Run: rivet --version"
