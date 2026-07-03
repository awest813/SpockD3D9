#!/usr/bin/env bash
# Build the experimental Windows PE d3d9.dll for use with Wine / CrossOver /
# Game Porting Toolkit (and Whisky-family wrappers such as Cosmos) via
# WINEDLLOVERRIDES="d3d9=n,b".
#
# This is NOT part of the blessed macOS native build. It cross-compiles a
# MinGW d3d9.dll using the same D3D9 sources as libdxvk_d3d9.dylib.
#
# IMPORTANT: the DLL architecture must match the *game process* architecture.
# Most classic D3D9 titles (Fallout 3 / New Vegas, Oblivion, Dragon Age:
# Origins) are 32-bit — build with `--arch x86` for those. 64-bit games need
# `--arch x64` (the default). `--arch both` builds both.
#
# Prerequisites (macOS):
#   brew install meson ninja mingw-w64 glslang
#   (mingw-w64 from Homebrew provides both the x86_64 and i686 toolchains)
#
# Output:
#   build-pe-d3d9/d3d9.dll      (x64)
#   build-pe-d3d9-x86/d3d9.dll  (x86)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--arch x64|x86|both] [--wipe]

Build the experimental Windows PE d3d9.dll via a MinGW-w64 cross toolchain.

Options:
  --arch ARCH   Target architecture: x64 (default), x86, or both.
                Use x86 for 32-bit games (Fallout 3/NV, Oblivion, DA:O).
  --wipe        Remove the build directory before configuring
EOF
}

arch="x64"
wipe=0
while [ $# -gt 0 ]; do
  case "$1" in
    --arch) shift; arch="${1:-}" ;;
    --arch=*) arch="${1#*=}" ;;
    --wipe) wipe=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

case "$arch" in
  x64|x86|both) ;;
  *) echo "error: --arch must be x64, x86, or both (got: $arch)" >&2; exit 1 ;;
esac

if ! command -v meson >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
  echo "error: meson and ninja are required." >&2
  exit 1
fi

if ! command -v glslangValidator >/dev/null 2>&1 && ! command -v glslang >/dev/null 2>&1; then
  echo "error: glslangValidator (glslang) is required." >&2
  exit 1
fi

# Build one architecture. Args: <arch>
build_one() {
  local a="$1"
  local triple cross_file build_dir
  case "$a" in
    x64)
      triple="x86_64-w64-mingw32"
      cross_file="$ROOT/cross/pe-x86_64-w64-mingw32.txt"
      build_dir="$ROOT/build-pe-d3d9"
      ;;
    x86)
      triple="i686-w64-mingw32"
      cross_file="$ROOT/cross/pe-i686-w64-mingw32.txt"
      build_dir="$ROOT/build-pe-d3d9-x86"
      ;;
  esac

  local mingw_cxx="${triple}-g++"
  if ! command -v "$mingw_cxx" >/dev/null 2>&1; then
    echo "error: $mingw_cxx not found." >&2
    echo "Install a MinGW-w64 toolchain (macOS: brew install mingw-w64)." >&2
    exit 1
  fi

  if [ "$wipe" -eq 1 ] && [ -d "$build_dir" ]; then
    echo "Removing existing build directory: $build_dir"
    rm -rf "$build_dir"
  fi

  local meson_args=(
    --cross-file "$cross_file"
    --buildtype=release
    --prefix "$build_dir/install"
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
    local extra=( ${MESON_EXTRA_ARGS} )
    meson_args+=("${extra[@]}")
  fi

  echo "=== SpockD3D9 experimental PE d3d9.dll ($triple) ==="

  if [ -d "$build_dir/meson-private" ]; then
    meson setup --reconfigure "$build_dir" "$ROOT" "${meson_args[@]}"
  else
    meson setup "$build_dir" "$ROOT" "${meson_args[@]}"
  fi

  meson compile -C "$build_dir"
  meson install -C "$build_dir" --destdir "$build_dir/staging"

  local dll_path
  dll_path="$(find "$build_dir/staging" -name 'd3d9.dll' -type f | head -1)"
  if [ -z "$dll_path" ]; then
    echo "error: d3d9.dll not found under $build_dir/staging" >&2
    find "$build_dir/staging" -name '*.dll' 2>/dev/null || true
    exit 1
  fi

  cp "$dll_path" "$build_dir/d3d9.dll"

  echo ""
  echo "Built: $build_dir/d3d9.dll"
  file "$build_dir/d3d9.dll"
  if ! file "$build_dir/d3d9.dll" | grep -Eq 'PE32\+ executable \(DLL\)|PE32 executable \(DLL\)'; then
    echo "error: output is not a Windows PE DLL" >&2
    exit 1
  fi
  echo "Install tree: $build_dir/staging"
}

case "$arch" in
  both)
    build_one x64
    echo ""
    build_one x86
    ;;
  *)
    build_one "$arch"
    ;;
esac

echo ""
echo "Usage with an external Windows host (example):"
echo '  WINEDLLOVERRIDES="d3d9=n,b" wine Fallout3.exe   # 32-bit game -> build --arch x86'
echo ""
echo "See docs/MACOS_TESTING.md for the full macOS validation checklist,"
echo "including the Cosmos / Whisky-family bottle workflow."
