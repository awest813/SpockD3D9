#!/usr/bin/env bash
# SpockD3D9 environment readiness check.
#
# One command that tells you what is missing before real testing:
#   ./scripts/spock-doctor.sh
#
# PASS = ready, WARN = missing but only needed for some steps (with a hint),
# MISS = required for the native validation path. Exit 0 when no MISS.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=lib/spock-common.sh
source "$ROOT/scripts/lib/spock-common.sh"
# Self-heal for custom-prefix installs (e.g. ~/homebrew) on unconfigured
# shells; interactive users should still add the prefix to their PATH.
spock_ensure_brew_bin_on_path || true

hard_misses=0
soft_notes=0

pass() { printf '  PASS  %s\n' "$1"; }
warn() { printf '  WARN  %s\n' "$1"; soft_notes=$((soft_notes+1)); }
miss() { printf '  MISS  %s\n' "$1"; hard_misses=$((hard_misses+1)); }

echo "=== SpockD3D9 doctor ==="
echo ""

echo "-- Build tools (native path)"
for tool in meson ninja; do
  if command -v "$tool" >/dev/null 2>&1; then pass "$tool ($(command -v $tool))"
  else miss "$tool — brew install $tool"; fi
done
if command -v glslangValidator >/dev/null 2>&1 || command -v glslang >/dev/null 2>&1; then
  pass "glslang"
else
  miss "glslang — brew install glslang"
fi

echo "-- Vulkan runtime (Track A)"
if icd="$(spock_find_moltenvk_icd)"; then
  pass "MoltenVK ICD: $icd"
else
  miss "MoltenVK ICD — brew install molten-vk vulkan-loader"
fi
moltenvk_lib=""
while IFS= read -r p; do
  if [ -e "$p/lib/libMoltenVK.dylib" ]; then moltenvk_lib="$p/lib/libMoltenVK.dylib"; break; fi
done < <(spock_brew_prefixes)
if [ -n "$moltenvk_lib" ]; then pass "libMoltenVK.dylib: $moltenvk_lib"
else warn "libMoltenVK.dylib not found under any Homebrew prefix"; fi

echo "-- Windowing (pick one)"
wsi=0
if [ -e "$(spock_find_prefix_with_lib libSDL3.dylib 2>/dev/null || true)/lib/libSDL3.dylib" ]; then
  pass "SDL3"; wsi=1
fi
if p="$(spock_find_prefix_with_lib libSDL2-2.0.0.dylib 2>/dev/null || true)"; then
  if [ -n "$p" ]; then pass "SDL2 ($p)"; wsi=1; fi
fi
if [ "$wsi" -eq 0 ]; then warn "no SDL3/SDL2 — brew install sdl3 sdl2 (build requires one)"; fi

echo "-- PE cross-compile (hosted path)"
for t in i686-w64-mingw32-g++ x86_64-w64-mingw32-g++; do
  if command -v "$t" >/dev/null 2>&1; then pass "$t"
  else warn "$t — brew install mingw-w64 (only needed for ./scripts/build-pe-d3d9.sh)"; fi
done

echo "-- Wine-family host (hosted path)"
if [ -n "${WINE:-}" ] && command -v "$WINE" >/dev/null 2>&1; then
  pass "wine via \$WINE: $WINE"
elif command -v wine >/dev/null 2>&1; then
  pass "wine ($(command -v wine))"
elif [ -x "$HOME/Applications/CrossOver/bin/wine" ]; then
  pass "CrossOver wine: ~/Applications/CrossOver/bin/wine"
else
  warn "no wine on PATH — needed for run-pe-smoke.sh / title runs (CrossOver, Cosmos bottle, or brew wine)"
fi

echo "-- Built artifacts"
NATIVE_LIB="$(find "$ROOT/build-test" -name libdxvk_d3d9.dylib -type f 2>/dev/null | head -1 || true)"
if [ -n "$NATIVE_LIB" ]; then
  pass "native dylib: ${NATIVE_LIB#"$ROOT"/} (./scripts/test-macos-native.sh [--no-rebuild])"
else
  warn "native dylib not built — ./scripts/test-macos-native.sh"
fi
if [ -f "$ROOT/build-pe-d3d9-x86/d3d9.dll" ]; then
  pass "PE d3d9.dll (x86): build-pe-d3d9-x86/d3d9.dll"
  if [ -f "$ROOT/build-pe-d3d9-x86/d3d9-pe-smoke.exe" ]; then
    pass "PE smoke exe (x86): build-pe-d3d9-x86/d3d9-pe-smoke.exe"
  else
    warn "PE smoke exe missing — rebuild: ./scripts/build-pe-d3d9.sh --arch x86"
  fi
else
  warn "PE x86 artifacts not built — ./scripts/build-pe-d3d9.sh --arch x86"
fi
CORPUS="$(find "$ROOT/build-test" -name dxso-corpus -type f 2>/dev/null | head -1 || true)"
if [ -n "$CORPUS" ]; then
  pass "dxso-corpus: ${CORPUS#"$ROOT"/}"
else
  warn "dxso-corpus not built — ./scripts/test-macos-native.sh (shader-corpus pre-flight)"
fi
if command -v python3 >/dev/null 2>&1; then pass "python3 (tooling)"
else warn "python3 not found — conf-profile tests and plot-benchmark.py need it"; fi

echo ""
if [ -n "${HOMEBREW_PREFIX:-}" ]; then :; else
  first_prefix="$(spock_brew_prefixes | head -1 || true)"
  case "$first_prefix" in
    /opt/homebrew|/usr/local|"") ;;
    *) echo "note: Homebrew prefix is $first_prefix — add $first_prefix/bin to your shell PATH for interactive use." ;;
  esac
fi
echo "Homebrew prefixes probed: $(spock_brew_prefixes | tr '\n' ' ')"
if [ "$hard_misses" -gt 0 ]; then
  echo "Result: $hard_misses hard miss(es), $soft_notes warning(s). Fix the MISS lines first."
  exit 1
fi
echo "Result: ready. $soft_notes warning(s) (only needed for the steps they name)."
echo "Next: docs/MACOS_TESTING.md §4 pre-flight checklist."
exit 0
