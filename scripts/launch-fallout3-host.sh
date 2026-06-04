#!/usr/bin/env bash
# Launch Fallout 3 under a Wine-family host with SpockD3D9 env vars applied.
#
# Usage:
#   ./scripts/launch-fallout3-host.sh --game-dir "/path/to/Fallout 3"
#   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --exe Fallout3.exe
#   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --dry-run
#
# Requires: prepare-fallout3-host.sh run first (d3d9.dll + dxvk.conf + spockd3d9-host.env).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME_DIR=""
EXE_NAME="Fallout3.exe"
DRY_RUN=0
WINE_BIN="${WINE:-wine}"

usage() {
  cat <<EOF
Usage: $(basename "$0") --game-dir DIR [--exe NAME] [--dry-run]

Source spockd3d9-host.env and run \$WINE (default: wine) against EXE in DIR.
Set WINE to your host launcher if needed (e.g. a GPTK wine binary).

Does not install Steam or create prefixes — only runs the game executable.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --game-dir) shift; GAME_DIR="${1:-}" ;;
    --exe) shift; EXE_NAME="${1:-}" ;;
    --dry-run) DRY_RUN=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

if [ -z "$GAME_DIR" ]; then
  echo "error: --game-dir is required." >&2
  usage >&2
  exit 1
fi

ENV_FILE="$GAME_DIR/spockd3d9-host.env"
EXE_PATH="$GAME_DIR/$EXE_NAME"

if [ ! -f "$ENV_FILE" ]; then
  echo "error: $ENV_FILE not found. Run:" >&2
  echo "  ./scripts/prepare-fallout3-host.sh --game-dir \"$GAME_DIR\" --build" >&2
  exit 1
fi

if [ ! -f "$EXE_PATH" ]; then
  echo "error: executable not found: $EXE_PATH" >&2
  exit 1
fi

if [ ! -f "$GAME_DIR/d3d9.dll" ]; then
  echo "error: $GAME_DIR/d3d9.dll missing. Run prepare-fallout3-host.sh first." >&2
  exit 1
fi

# shellcheck source=/dev/null
source "$ENV_FILE"

BREW_PREFIX="${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || true)}"
if [ -n "$BREW_PREFIX" ]; then
  for icd in \
    "$BREW_PREFIX/share/vulkan/icd.d/MoltenVK_icd.json" \
    /opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json \
    /usr/local/share/vulkan/icd.d/MoltenVK_icd.json; do
    if [ -f "$icd" ]; then
      export VK_ICD_FILENAMES="$icd"
      export VK_DRIVER_FILES="$icd"
      break
    fi
  done
fi

LOG_FILE="${BOOT_LOG:-$GAME_DIR/fallout3-spockd3d9.log}"

echo "=== SpockD3D9 Fallout 3 launch ==="
echo "Game dir:  $GAME_DIR"
echo "Exe:       $EXE_PATH"
echo "Wine:      $WINE_BIN"
echo "DXVK conf: $DXVK_CONFIG_FILE"
echo "Log file:  $LOG_FILE"
echo ""

if [ "$DRY_RUN" -eq 1 ]; then
  echo "Dry run — environment:"
  env | grep -E '^(WINEDLLOVERRIDES|DXVK_|MVK_|VK_|DYLD_)' || true
  echo "Would run: cd \"$GAME_DIR\" && $WINE_BIN \"$EXE_NAME\""
  exit 0
fi

if ! command -v "$WINE_BIN" >/dev/null 2>&1; then
  echo "error: $WINE_BIN not found. Set WINE= to your host's wine binary." >&2
  exit 1
fi

cd "$GAME_DIR"
set -o pipefail
"$WINE_BIN" "$EXE_NAME" 2>&1 | tee "$LOG_FILE"
status=${PIPESTATUS[0]}

echo ""
echo "Exit code: $status"
echo "Check milestones: ./scripts/check-boot-logs.sh \"$LOG_FILE\""
exit "$status"
