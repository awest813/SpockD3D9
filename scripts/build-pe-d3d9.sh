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

echo "=== SpockD3D9 experimental PE d3d9.dll (x86_64-w64-mingw32) ==="

meson setup "$BUILD_DIR" "$ROOT" \
  --cross-file "$CROSS_FILE" \
  --buildtype=release \
  --prefix "$BUILD_DIR/install" \
  -Denable_pe_d3d9=true \
  -Denable_d3d9=true \
  -Denable_d3d8=false \
  -Denable_dxgi=false \
  -Denable_d3d10=false \
  -Denable_d3d11=false \
  ${MESON_EXTRA_ARGS:-}

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
echo "Install tree: $BUILD_DIR/staging"
echo ""
echo "Usage with an external Windows host (example):"
echo '  WINEDLLOVERRIDES="d3d9=n,b" wine Fallout3.exe'
