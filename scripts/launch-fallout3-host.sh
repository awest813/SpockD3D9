#!/usr/bin/env bash
# Launch Fallout 3 under a Wine-family host with SpockD3D9 env vars applied.
#
# Usage:
#   # Direct executable launch (no Steam DRM):
#   ./scripts/launch-fallout3-host.sh --game-dir "/path/to/Fallout 3"
#   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --exe Fallout3.exe
#
#   # Steam launch (the retail target — satisfies Steam DRM + overlay):
#   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --steam
#   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --steam --appid 22300
#   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --steam --no-overlay
#
#   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --dry-run
#
# Requires: prepare-fallout3-host.sh run first (d3d9.dll + dxvk.conf + spockd3d9-host.env).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME_DIR=""
EXE_NAME="Fallout3.exe"
DRY_RUN=0
STEAM_MODE=0
# Steam App IDs: 22370 = Fallout 3 GOTY, 22300 = Fallout 3 (original).
APPID="22370"
STEAM_EXE="${STEAM_EXE:-}"
NO_OVERLAY=0
WINE_BIN="${WINE:-wine}"

usage() {
  cat <<EOF
Usage: $(basename "$0") --game-dir DIR [options]

Source spockd3d9-host.env and launch Fallout 3 under \$WINE (default: wine)
with the SpockD3D9 d3d9.dll override applied. Set WINE to your host launcher
if needed (e.g. a GPTK wine binary).

Options:
  --exe NAME      Game executable for direct launch (default: Fallout3.exe)
  --steam         Launch through Steam instead of running the exe directly.
                  Required for the retail Steam build (DRM + overlay). Steam
                  must already be installed in the prefix and logged in.
  --appid N       Steam App ID (default: $APPID = Fallout 3 GOTY; 22300 = base)
  --steam-exe P   Path to Steam.exe in the prefix; uses 'Steam.exe -applaunch N'.
                  Without it, falls back to 'start steam://rungameid/N'.
  --no-overlay    Disable the Steam overlay (gameoverlayrenderer) for clean
                  D3D9 diagnostics — it also hooks D3D9 Present.
  --dry-run       Print the resolved command and environment, then exit.

Does not install Steam or create prefixes.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --game-dir) shift; GAME_DIR="${1:-}" ;;
    --exe) shift; EXE_NAME="${1:-}" ;;
    --steam) STEAM_MODE=1 ;;
    --appid) shift; APPID="${1:-}" ;;
    --steam-exe) shift; STEAM_EXE="${1:-}" ;;
    --no-overlay) NO_OVERLAY=1 ;;
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

# In direct mode the exe must exist; in Steam mode the game is launched by
# Steam (which may live in a different prefix path), so the exe check is skipped.
if [ "$STEAM_MODE" -eq 0 ] && [ ! -f "$EXE_PATH" ]; then
  echo "error: executable not found: $EXE_PATH" >&2
  echo "(use --steam to launch the retail Steam build via Steam instead)" >&2
  exit 1
fi

if [ ! -f "$GAME_DIR/d3d9.dll" ]; then
  echo "error: $GAME_DIR/d3d9.dll missing. Run prepare-fallout3-host.sh first." >&2
  exit 1
fi

# shellcheck source=/dev/null
source "$ENV_FILE"

# Disable the Steam overlay's own D3D9 hook for clean diagnostics. Appended
# after the sourced WINEDLLOVERRIDES so the d3d9=n,b override is preserved.
if [ "$NO_OVERLAY" -eq 1 ]; then
  export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}gameoverlayrenderer=d;gameoverlayrenderer64=d"
fi

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
# DXVK writes d3d9.log here (set by spockd3d9-host.env). In Steam mode this is
# the authoritative log since the game's stdout is not captured by this script.
DXVK_LOG_FILE="${DXVK_LOG_PATH:-$GAME_DIR}/d3d9.log"

# Resolve the launch command.
if [ "$STEAM_MODE" -eq 1 ]; then
  if [ -n "$STEAM_EXE" ]; then
    LAUNCH_DESC="$WINE_BIN \"$STEAM_EXE\" -applaunch $APPID"
    set -- "$WINE_BIN" "$STEAM_EXE" -applaunch "$APPID"
  else
    LAUNCH_DESC="$WINE_BIN start steam://rungameid/$APPID"
    set -- "$WINE_BIN" start "steam://rungameid/$APPID"
  fi
else
  LAUNCH_DESC="cd \"$GAME_DIR\" && $WINE_BIN \"$EXE_NAME\""
  set -- "$WINE_BIN" "$EXE_NAME"
fi

echo "=== SpockD3D9 Fallout 3 launch ==="
echo "Game dir:  $GAME_DIR"
if [ "$STEAM_MODE" -eq 1 ]; then
  echo "Mode:      Steam (appid $APPID)"
  echo "Overlay:   $([ "$NO_OVERLAY" -eq 1 ] && echo disabled || echo enabled)"
else
  echo "Mode:      direct exe"
  echo "Exe:       $EXE_PATH"
fi
echo "Wine:      $WINE_BIN"
echo "DXVK conf: $DXVK_CONFIG_FILE"
echo "DXVK log:  $DXVK_LOG_FILE"
echo ""

if [ "$DRY_RUN" -eq 1 ]; then
  echo "Dry run — environment:"
  env | grep -E '^(WINEDLLOVERRIDES|DXVK_|MVK_|VK_|DYLD_)' || true
  echo "Would run: $LAUNCH_DESC"
  exit 0
fi

if ! command -v "$WINE_BIN" >/dev/null 2>&1; then
  echo "error: $WINE_BIN not found. Set WINE= to your host's wine binary." >&2
  exit 1
fi

cd "$GAME_DIR"

if [ "$STEAM_MODE" -eq 1 ]; then
  # Steam launches the game as a detached child; its stdout is not ours to
  # capture. Rely on DXVK_LOG_PATH/d3d9.log for milestone checking.
  echo "Launching via Steam. Steam must be running and logged in."
  echo "Boot logs will appear in: $DXVK_LOG_FILE"
  "$@"
  status=$?
  echo ""
  echo "Steam launcher exit code: $status (game runs detached under Steam)"
  echo "Check milestones once the menu loads or exits:"
  echo "  ./scripts/check-boot-logs.sh \"$DXVK_LOG_FILE\""
  exit "$status"
fi

set -o pipefail
"$@" 2>&1 | tee "$LOG_FILE"
status=${PIPESTATUS[0]}

echo ""
echo "Exit code: $status"
echo "Check milestones: ./scripts/check-boot-logs.sh \"$LOG_FILE\""
echo "  (or the DXVK file log: ./scripts/check-boot-logs.sh \"$DXVK_LOG_FILE\")"
exit "$status"
