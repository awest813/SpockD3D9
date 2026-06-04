#!/usr/bin/env bash
# Build the experimental Windows PE d3d9.dll for use with Wine / CrossOver /
# Game Porting Toolkit via WINEDLLOVERRIDES="d3d9=n,b".
#
# This is NOT part of the blessed macOS native build. It cross-compiles a
# MinGW d3d9.dll using the same D3D9 sources as libdxvk_d3d9.dylib.
#
# Prerequisites (macOS):
#   brew install meson ninja mingw-w64 glslang
#
# Output:
#   build-pe-d3d9/d3d9.dll  (and dependent DLLs from the install tree)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-pe-d3d9"
CROSS_FILE="$ROOT/cross/pe-x86_64-w64-mingw32.txt"
MINGW_CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--wipe]

Build the experimental Windows PE d3d9.dll (x86_64-w64-mingw32).

Options:
  --wipe   Remove $BUILD_DIR before configuring
EOF
}

wipe=0
while [ $# -gt 0 ]; do
  case "$1" in
    --wipe) wipe=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

if ! command -v "$MINGW_CXX" >/dev/null 2>&1; then
  echo "error: $MINGW_CXX not found." >&2
  echo "Install a MinGW-w64 toolchain (macOS: brew install mingw-w64)." >&2
  exit 1
fi

if ! command -v meson >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
  echo "error: meson and ninja are required." >&2
  exit 1
fi

if ! command -v glslangValidator >/dev/null 2>&1 && ! command -v glslang >/dev/null 2>&1; then
  echo "error: glslangValidator (glslang) is required." >&2
  exit 1
fi

if [ "$wipe" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
  echo "Removing existing build directory: $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

MESON_ARGS=(
  --cross-file "$CROSS_FILE"
  --buildtype=release
  --prefix "$BUILD_DIR/install"
  --force-fallback-for=libdisplay-info
  -Denable_pe_d3d9=true
  -Denable_d3d9=true
  -Denable_d3d8=false
  -Denable_dxgi=false
  -Denable_d3d10=false
  -Denable_d3d11=false
)

if [ -n "${MESON_EXTRA_ARGS:-}" ]; then
  # shellcheck disable=SC2206
  extra=( ${MESON_EXTRA_ARGS} )
  MESON_ARGS+=("${extra[@]}")
fi

echo "=== SpockD3D9 experimental PE d3d9.dll (x86_64-w64-mingw32) ==="

if [ -d "$BUILD_DIR/meson-private" ]; then
  meson setup --reconfigure "$BUILD_DIR" "$ROOT" "${MESON_ARGS[@]}"
else
  meson setup "$BUILD_DIR" "$ROOT" "${MESON_ARGS[@]}"
fi

meson compile -C "$BUILD_DIR"
meson install -C "$BUILD_DIR" --destdir "$BUILD_DIR/staging"

DLL_PATH="$(find "$BUILD_DIR/staging" -name 'd3d9.dll' -type f | head -1)"
if [ -z "$DLL_PATH" ]; then
  echo "error: d3d9.dll not found under $BUILD_DIR/staging" >&2
  find "$BUILD_DIR/staging" -name '*.dll' 2>/dev/null || true
  exit 1
fi

cp "$DLL_PATH" "$BUILD_DIR/d3d9.dll"

echo ""
echo "Built: $BUILD_DIR/d3d9.dll"
file "$BUILD_DIR/d3d9.dll"
if ! file "$BUILD_DIR/d3d9.dll" | grep -Eq 'PE32\+ executable \(DLL\)|PE32 executable \(DLL\)'; then
  echo "error: output is not a Windows PE DLL" >&2
  exit 1
fi
echo "Install tree: $BUILD_DIR/staging"
echo ""
echo "Usage with an external Windows host (example):"
echo '  WINEDLLOVERRIDES="d3d9=n,b" wine Fallout3.exe'
echo ""
echo "See docs/MACOS_TESTING.md for the full macOS validation checklist."
