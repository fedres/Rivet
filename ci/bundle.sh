#!/usr/bin/env sh
# ci/bundle.sh — Create a Rivet release bundle (.tar.zst) for Linux/macOS.
#
# Usage:
#   ci/bundle.sh <binary> <version> <out_dir>
#
# Example:
#   ci/bundle.sh build/release/rivet v0.1.0 dist/
#
# Produces:
#   dist/rivet-v0.1.0-linux-x86_64.tar.zst
#   dist/rivet-v0.1.0-linux-x86_64.tar.zst.sha256

set -eu

BINARY="${1:?Usage: ci/bundle.sh <binary> <version> <out_dir>}"
VERSION="${2:?Usage: ci/bundle.sh <binary> <version> <out_dir>}"
OUT_DIR="${3:?Usage: ci/bundle.sh <binary> <version> <out_dir>}"

# ─── Detect platform ──────────────────────────────────────────────────────────

case "$(uname -s)" in
    Linux*)  OS="linux" ;;
    Darwin*) OS="macos" ;;
    *)       printf 'error: unsupported OS: %s\n' "$(uname -s)" >&2; exit 1 ;;
esac

case "$(uname -m)" in
    x86_64|amd64)  ARCH="x86_64" ;;
    aarch64|arm64) ARCH="arm64" ;;
    *)             printf 'error: unsupported arch: %s\n' "$(uname -m)" >&2; exit 1 ;;
esac

TRIPLE="${OS}-${ARCH}"
BUNDLE_NAME="rivet-${VERSION}-${TRIPLE}"

# ─── Stage bundle layout ──────────────────────────────────────────────────────
#
# bin/
#   rivet
# meta/
#   version.json
#   platform.json

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

STAGE="$TMP/$BUNDLE_NAME"
mkdir -p "$STAGE/bin" "$STAGE/meta"

cp "$BINARY" "$STAGE/bin/rivet"
chmod 0755 "$STAGE/bin/rivet"

# Resolve git hash: prefer CI-provided env var, then git, then "unknown".
GIT_HASH="${GITHUB_SHA:-}"
if [ -z "$GIT_HASH" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    GIT_HASH="$(git -C "$SCRIPT_DIR/.." rev-parse --short HEAD 2>/dev/null || echo "unknown")"
fi

BUILD_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

cat > "$STAGE/meta/version.json" <<EOF
{
  "version": "${VERSION}",
  "triple": "${TRIPLE}",
  "git_hash": "${GIT_HASH}",
  "build_date": "${BUILD_DATE}"
}
EOF

cat > "$STAGE/meta/platform.json" <<EOF
{
  "os": "${OS}",
  "arch": "${ARCH}"
}
EOF

# ─── Package ──────────────────────────────────────────────────────────────────

mkdir -p "$OUT_DIR"
ARCHIVE="${OUT_DIR}/${BUNDLE_NAME}.tar.zst"

# Try GNU/BSD tar --zstd first; fall back to explicit zstd pipe.
if tar --zstd -cf "$ARCHIVE" -C "$TMP" "$BUNDLE_NAME" 2>/dev/null; then
    :
elif command -v zstd >/dev/null 2>&1; then
    tar -cf - -C "$TMP" "$BUNDLE_NAME" | zstd -T0 -19 -o "$ARCHIVE"
else
    echo "error: zstd not found. Install: apt install zstd / brew install zstd" >&2
    exit 1
fi

# ─── Checksum ─────────────────────────────────────────────────────────────────

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$ARCHIVE" > "${ARCHIVE}.sha256"
elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$ARCHIVE" > "${ARCHIVE}.sha256"
else
    echo "warning: no sha256 tool — checksum file not generated." >&2
fi

printf 'Bundle : %s\n' "$ARCHIVE"
[ -f "${ARCHIVE}.sha256" ] && printf 'SHA256 : %s\n' "${ARCHIVE}.sha256"
