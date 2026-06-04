#!/usr/bin/env bash
# Quick local macOS validation: build the native dylib and run d3d9-clear.
#
# Prerequisites:
#   brew install meson ninja glslang sdl3 sdl2 molten-vk vulkan-loader
#
# Usage:
#   ./scripts/test-macos-native.sh [--arch arm64|x86_64] [--frames N]

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="$ROOT/build-test"
VERSION="local-test"
ARCH=""
FRAMES=60

usage() {
  cat <<EOF
Usage: $(basename "$0") [--arch arm64|x86_64] [--frames N]

Build SpockD3D9 natively and run the d3d9-clear smoke test.
Per-arch only; universal lipo is not supported here (see package-native.sh).
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --arch) shift; ARCH="$1" ;;
    --frames) shift; FRAMES="$1" ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

if [ -z "$ARCH" ]; then
  ARCH="$(uname -m)"
fi

if ! command -v meson >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
  echo "error: meson and ninja are required." >&2
  exit 1
fi

if ! command -v glslangValidator >/dev/null 2>&1 && ! command -v glslang >/dev/null 2>&1; then
  echo "error: glslangValidator (glslang) is required." >&2
  exit 1
fi

rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"

echo "=== SpockD3D9 native macOS smoke test ($ARCH) ==="

build_args=(--no-package --dev-build --arch "$ARCH")
"$ROOT/package-native.sh" "$VERSION" "$BUILD_ROOT" "${build_args[@]}"

LIB_DIR="$BUILD_ROOT/spockd3d9-$VERSION/usr/lib"
if [ ! -d "$LIB_DIR" ]; then
  echo "error: install lib dir not found: $LIB_DIR" >&2
  exit 1
fi

BREW_PREFIX="${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || true)}"
export DYLD_LIBRARY_PATH="$LIB_DIR${BREW_PREFIX:+:$BREW_PREFIX/lib}${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export DXVK_LOG_LEVEL=info
export MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0

if [ -n "$BREW_PREFIX" ]; then
  for icd in \
    "$BREW_PREFIX/share/vulkan/icd.d/MoltenVK_icd.json" \
    /opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json \
    /usr/local/share/vulkan/icd.d/MoltenVK_icd.json; do
    if [ -f "$icd" ]; then
      export VK_ICD_FILENAMES="$icd"
      export VK_DRIVER_FILES="$icd"
      echo "Using MoltenVK ICD: $icd"
      break
    fi
  done
fi

run_smoke() {
  local binary="$1"
  local bin_path="$LIB_DIR/$binary"
  if [ ! -x "$bin_path" ]; then
    echo "warning: $binary not built; skipping."
    return 0
  fi
  echo "=== Smoke test: $binary ($FRAMES frames) ==="
  "$bin_path" "$FRAMES"
}

run_smoke d3d9-clear
run_smoke d3d9-clear-sdl2

echo ""
echo "Native macOS smoke test passed."
echo "Library: $LIB_DIR/libdxvk_d3d9.dylib"
echo "Next: ./scripts/build-pe-d3d9.sh for the experimental Windows d3d9.dll."
echo "Full checklist: docs/MACOS_TESTING.md"
