#!/usr/bin/env sh
# bootstrap/install.sh — One-line installer for Rivet
# Usage: curl -fsSL https://rivet.build/install.sh | sh
set -eu

RIVET_REPO="https://github.com/fedres/Rivet"
RIVET_HOME="${RIVET_HOME:-$HOME/.rivet}"
RIVET_BIN="$RIVET_HOME/bin"

# ─── Detect OS ───────────────────────────────────────────────────────────────

detect_os() {
    case "$(uname -s)" in
        Linux*)  echo linux ;;
        Darwin*) echo macos ;;
        *)       echo "Unsupported OS: $(uname -s)" >&2; exit 1 ;;
    esac
}

# ─── Detect architecture ─────────────────────────────────────────────────────

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)  echo x86_64 ;;
        aarch64|arm64) echo arm64  ;;
        *)             echo "Unsupported arch: $(uname -m)" >&2; exit 1 ;;
    esac
}

OS="$(detect_os)"
ARCH="$(detect_arch)"
TRIPLE="${ARCH}-${OS}"

# ─── Resolve latest release tag ──────────────────────────────────────────────

LATEST_TAG="$(curl -fsSL "https://api.github.com/repos/fedres/Rivet/releases/latest" \
    | grep '"tag_name"' | head -1 | sed 's/.*"tag_name": "\(.*\)".*/\1/')"

if [ -z "$LATEST_TAG" ]; then
    echo "error: could not determine latest Rivet release" >&2
    exit 1
fi

ARCHIVE="rivet-${LATEST_TAG}-${TRIPLE}.tar.gz"
DOWNLOAD_URL="${RIVET_REPO}/releases/download/${LATEST_TAG}/${ARCHIVE}"

echo "Installing Rivet ${LATEST_TAG} (${TRIPLE}) to ${RIVET_HOME} ..."

# ─── Download ─────────────────────────────────────────────────────────────────

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

curl -fsSL --progress-bar -o "$TMP_DIR/$ARCHIVE" "$DOWNLOAD_URL"

# ─── Verify checksum (optional — if .sha256 exists) ─────────────────────────

if curl -fsSL -o "$TMP_DIR/${ARCHIVE}.sha256" "${DOWNLOAD_URL}.sha256" 2>/dev/null; then
    echo "Verifying checksum..."
    ( cd "$TMP_DIR" && sha256sum -c "${ARCHIVE}.sha256" )
fi

# ─── Install ─────────────────────────────────────────────────────────────────

mkdir -p "$RIVET_BIN"
tar -xzf "$TMP_DIR/$ARCHIVE" -C "$RIVET_HOME"
chmod +x "$RIVET_BIN/rivet"

echo "Rivet installed to $RIVET_BIN/rivet"

# ─── PATH hint ───────────────────────────────────────────────────────────────

if ! echo "$PATH" | grep -q "$RIVET_BIN"; then
    echo ""
    echo "Add Rivet to your PATH by adding the following to your shell profile:"
    echo ""
    echo '  export PATH="$HOME/.rivet/bin:$PATH"'
    echo ""
fi

echo "Done! Run 'rivet --version' to verify."
