#!/usr/bin/env bash
# Run the PE host-boundary smoke test (d3d9-pe-smoke.exe) inside a Wine-family
# host — WITHOUT a game install.
#
# This validates the part of the hosted path that no native test covers:
#   native d3d9.dll override (WINEDLLOVERRIDES) -> Win32 HWND ->
#   winevulkan surface -> MoltenVK -> Metal -> Present
# If this passes V1-V3 (see docs/BOOT_TO_MENU.md), any later boot failure in a
# real game is a game/translator issue, not a host-boundary issue.
#
# Usage:
#   ./scripts/run-pe-smoke.sh                          # x86 build, $WINE (default: wine)
#   ./scripts/run-pe-smoke.sh --build                  # build the PE DLL + exe first
#   WINE=/path/to/bottle/wine WINEPREFIX=/path/to/bottle \
#     ./scripts/run-pe-smoke.sh                        # inside a Cosmos/CrossOver bottle
#   ./scripts/run-pe-smoke.sh --from ~/Downloads       # unzipped CI artifact dir
#
# Prerequisites: a working Wine-family host (Wine, CrossOver, Cosmos bottle,
# GPTK) with winevulkan reaching MoltenVK on the macOS side.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=lib/spock-common.sh
source "$ROOT/scripts/lib/spock-common.sh"
spock_ensure_brew_bin_on_path || true

ARCH="x86"
FRAMES=30
WINE_BIN="${WINE:-wine}"
WINE_PREFIX="${WINEPREFIX:-}"
FROM_DIR=""
BUILD=0
DRY_RUN=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run the PE host-boundary smoke test (d3d9-pe-smoke.exe) under a Wine host.

Options:
  --arch x86|x64   Architecture to test (default: x86 — the 32-bit path the
                   benchmark titles use; exercises the host's WoW64 support)
  --frames N       Frames to present (default: 30)
  --build          Run scripts/build-pe-d3d9.sh --arch <arch> first
  --from DIR       Directory containing d3d9.dll + d3d9-pe-smoke.exe
                   (default: build-pe-d3d9[-x86]/ from the repo build)
  --wine BIN       Wine binary (default: \$WINE or "wine")
  --prefix DIR     WINEPREFIX for the host (default: \$WINEPREFIX or wine default)
  --dry-run        Print the resolved command and environment, then exit

Examples:
  # Local build, default wine:
  ./scripts/run-pe-smoke.sh --build

  # CrossOver bottle (find the wine binary via 'cxrun' or the bottle's
  # 'Open in Finder'; the prefix is the bottle's drive_c parent):
  WINE="$HOME/Applications/CrossOver/bin/wine" \\
  WINEPREFIX="$HOME/Library/Application Support/CrossOver/Bottles/<bottle>" \\
    ./scripts/run-pe-smoke.sh
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --arch) shift; ARCH="${1:-}" ;;
    --arch=*) ARCH="${1#*=}" ;;
    --frames) shift; FRAMES="${1:-}" ;;
    --frames=*) FRAMES="${1#*=}" ;;
    --build) BUILD=1 ;;
    --from) shift; FROM_DIR="${1:-}" ;;
    --wine) shift; WINE_BIN="${1:-}" ;;
    --prefix) shift; WINE_PREFIX="${1:-}" ;;
    --dry-run) DRY_RUN=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

case "$ARCH" in
  x86|x64) ;;
  *) echo "error: --arch must be x86 or x64 (got: $ARCH)" >&2; exit 1 ;;
esac

if ! [[ "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: --frames must be a positive integer (got: $FRAMES)" >&2
  exit 1
fi

if [ "$BUILD" -eq 1 ]; then
  "$ROOT/scripts/build-pe-d3d9.sh" --arch "$ARCH"
fi

# Locate d3d9.dll + d3d9-pe-smoke.exe.
if [ -n "$FROM_DIR" ]; then
  DLL="$FROM_DIR/d3d9.dll"
  EXE="$FROM_DIR/d3d9-pe-smoke.exe"
else
  case "$ARCH" in
    x86) BUILD_DIR="$ROOT/build-pe-d3d9-x86" ;;
    x64) BUILD_DIR="$ROOT/build-pe-d3d9" ;;
  esac
  DLL="$BUILD_DIR/d3d9.dll"
  EXE="$BUILD_DIR/d3d9-pe-smoke.exe"
fi

for f in "$DLL" "$EXE"; do
  if [ ! -f "$f" ]; then
    echo "error: $f not found." >&2
    echo "Build it first: ./scripts/build-pe-d3d9.sh --arch $ARCH" >&2
    echo "Or pass --from DIR pointing at an unzipped CI artifact." >&2
    exit 1
  fi
done

# Sanity: the exe bitness must match the requested arch so a wrong-arch run
# fails here with a clear message instead of inside the host.
if [ "$ARCH" = "x86" ] && ! file "$EXE" | grep -q 'PE32 executable'; then
  echo "error: $EXE is not a 32-bit (PE32) executable — rebuild with --arch x86." >&2
  exit 1
fi
if [ "$ARCH" = "x64" ] && ! file "$EXE" | grep -q 'PE32+ executable'; then
  echo "error: $EXE is not a 64-bit (PE32+) executable — rebuild with --arch x64." >&2
  exit 1
fi

# Stage a clean run directory: the exe must load the override DLL from its own
# directory, and old d3d9.log files would confuse the milestone check.
RUN_DIR="$ROOT/build-pe-smoke-run-$ARCH"
rm -rf "$RUN_DIR"
mkdir -p "$RUN_DIR"
cp "$DLL" "$RUN_DIR/d3d9.dll"
cp "$EXE" "$RUN_DIR/d3d9-pe-smoke.exe"
if [ -f "$ROOT/tools/macos/macos.dxvk.conf" ]; then
  cp "$ROOT/tools/macos/macos.dxvk.conf" "$RUN_DIR/dxvk.conf"
fi

# --- Environment (mirrors launch-steam-d3d9-host.sh / BOOT_TO_MENU.md) ------
export WINEDLLOVERRIDES="d3d9=n,b${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-info}"
export DXVK_LOG_PATH="$RUN_DIR"
: "${MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS:=2}"   # macOS 26 / MoltenVK 1.4.x default
export MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS
# The PE d3d9.dll presents through the host's HWND; SDL/GLFW WSI must stay off.
unset DXVK_WSI_DRIVER

if [ -n "$WINE_PREFIX" ]; then
  export WINEPREFIX="$WINE_PREFIX"
fi

# Custom-prefix-aware MoltenVK ICD discovery (~/homebrew, HOMEBREW_PREFIX,
# /opt/homebrew, /usr/local); a pre-set VK_ICD_FILENAMES is respected.
if ! spock_export_moltenvk_icd; then
  echo "note: no MoltenVK ICD found via Homebrew — relying on host wiring" >&2
fi

LOG_FILE="$RUN_DIR/pe-smoke.log"

echo "=== SpockD3D9 PE host-boundary smoke test ($ARCH) ==="
echo "Wine:      $WINE_BIN"
[ -n "${WINEPREFIX:-}" ] && echo "Prefix:    $WINEPREFIX"
echo "Run dir:   $RUN_DIR"
echo "DLL:       $DLL"
echo "MoltenVK:  ${VK_ICD_FILENAMES:-<not found via brew — relying on host wiring>}"
echo ""

if [ "$DRY_RUN" -eq 1 ]; then
  echo "Dry run — environment:"
  env | grep -E '^(WINEDLLOVERRIDES|DXVK_|MVK_|VK_|WINEPREFIX|DYLD_)' || true
  echo "Would run: (cd $RUN_DIR && $WINE_BIN d3d9-pe-smoke.exe $FRAMES)"
  exit 0
fi

if ! command -v "$WINE_BIN" >/dev/null 2>&1; then
  echo "error: $WINE_BIN not found. Install a Wine-family host or pass --wine." >&2
  exit 1
fi

# Resolve wine to an absolute path — the run below cd's into the staging dir.
if ! WINE_BIN="$(spock_abs_cmd "$WINE_BIN")"; then
  echo "error: could not resolve wine binary: $WINE_BIN" >&2
  exit 1
fi

cd "$RUN_DIR"

set +e
set -o pipefail
"$WINE_BIN" d3d9-pe-smoke.exe "$FRAMES" 2>&1 | tee "$LOG_FILE"
SMOKE_STATUS=${PIPESTATUS[0]}
set -e

echo ""
echo "Exit code: $SMOKE_STATUS (0=OK 2=DLL load 3=window 4=Direct3DCreate9 5=adapter 6=CreateDevice 7=Present)"

# Milestone check: prefer the teed log (DLL stderr included); fall back to the
# DXVK file log if the banner went there instead.
CHECK_LOG="$LOG_FILE"
if ! grep -q 'DXVK:' "$CHECK_LOG" && [ -f "$RUN_DIR/d3d9.log" ]; then
  CHECK_LOG="$RUN_DIR/d3d9.log"
fi

echo "Boot milestones:"
if "$ROOT/scripts/check-boot-logs.sh" "$CHECK_LOG"; then
  CHECK_STATUS=0
else
  CHECK_STATUS=1
fi

echo ""
if [ "$SMOKE_STATUS" -eq 0 ] && [ "$CHECK_STATUS" -eq 0 ]; then
  echo "Host boundary healthy: override DLL, winevulkan -> MoltenVK, CreateDevice,"
  echo "and Present all work. Any later game boot failure is game/translator-side."
  echo "Next: docs/BOOT_TO_MENU.md"
  exit 0
fi

echo "Host boundary NOT healthy — see the exit code and milestone output above."
echo "Troubleshooting: docs/BOOT_TO_MENU.md § Troubleshooting,"
echo "and tools/dragonshard/README.md § Cosmos gotchas (32-bit / DLL shadowing)."
exit 1
