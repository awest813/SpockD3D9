#!/usr/bin/env bash
# Launch a Windows D3D9 title under a Wine-family host with SpockD3D9 env vars.
#
# Usage:
#   ./scripts/launch-steam-d3d9-host.sh \
#     --game-dir "/path/to/game" \
#     --exe Game.exe \
#     --title "My Game" \
#     --steam --appid 12345
#
# Requires: prepare-steam-d3d9-host.sh (or a title wrapper) run first.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME_DIR=""
EXE_NAME=""
TITLE="D3D9 game"
LOG_SLUG=""
DRY_RUN=0
STEAM_MODE=0
APPID=""
STEAM_EXE="${STEAM_EXE:-}"
NO_OVERLAY=0
WINE_BIN="${WINE:-wine}"

usage() {
  cat <<EOF
Usage: $(basename "$0") --game-dir DIR --exe NAME [options]

Source spockd3d9-host.env and launch a hosted Windows D3D9 title under \$WINE
(default: wine) with the SpockD3D9 d3d9.dll override applied.

Options:
  --title NAME    Human-readable title for banners (default: "$TITLE")
  --log-slug S    Boot log filename prefix (default: derived from --title)
  --exe NAME      Game executable for direct launch (required unless --steam only)
  --steam         Launch through Steam instead of running the exe directly.
  --appid N       Steam App ID (required with --steam unless set by a wrapper)
  --steam-exe P   Path to Steam.exe in the prefix; uses 'Steam.exe -applaunch N'.
  --no-overlay    Disable the Steam overlay for clean D3D9 diagnostics.
  --dry-run       Print the resolved command and environment, then exit.

Does not install Steam or create prefixes.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --game-dir) shift; GAME_DIR="${1:-}" ;;
    --exe) shift; EXE_NAME="${1:-}" ;;
    --title) shift; TITLE="${1:-}" ;;
    --log-slug) shift; LOG_SLUG="${1:-}" ;;
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

if [ "$STEAM_MODE" -eq 1 ] && [ -z "$APPID" ]; then
  echo "error: --appid is required with --steam." >&2
  usage >&2
  exit 1
fi

if [ "$STEAM_MODE" -eq 0 ] && [ -z "$EXE_NAME" ]; then
  echo "error: --exe is required for direct launch (or pass --steam)." >&2
  usage >&2
  exit 1
fi

if [ -z "$LOG_SLUG" ]; then
  LOG_SLUG="$(printf '%s' "$TITLE" | tr '[:upper:]' '[:lower:]' | tr -cs 'a-z0-9' '-' | sed 's/^-//;s/-$//')"
  [ -z "$LOG_SLUG" ] && LOG_SLUG="game"
fi

ENV_FILE="$GAME_DIR/spockd3d9-host.env"
EXE_PATH=""
if [ -n "$EXE_NAME" ]; then
  EXE_PATH="$GAME_DIR/$EXE_NAME"
fi

if [ ! -f "$ENV_FILE" ]; then
  echo "error: $ENV_FILE not found. Run prepare-steam-d3d9-host.sh first." >&2
  exit 1
fi

if [ "$STEAM_MODE" -eq 0 ] && [ ! -f "$EXE_PATH" ]; then
  echo "error: executable not found: $EXE_PATH" >&2
  echo "(use --steam to launch the retail Steam build via Steam instead)" >&2
  exit 1
fi

if [ ! -f "$GAME_DIR/d3d9.dll" ]; then
  echo "error: $GAME_DIR/d3d9.dll missing. Run prepare-steam-d3d9-host.sh first." >&2
  exit 1
fi

# shellcheck source=/dev/null
source "$ENV_FILE"

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

LOG_FILE="${BOOT_LOG:-$GAME_DIR/${LOG_SLUG}-spockd3d9.log}"
DXVK_LOG_FILE="${DXVK_LOG_PATH:-$GAME_DIR}/d3d9.log"

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

echo "=== SpockD3D9 $TITLE launch ==="
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
