#!/usr/bin/env bash
# Launch The Elder Scrolls IV: Oblivion under a Wine-family host with SpockD3D9.
#
# Thin wrapper around launch-steam-d3d9-host.sh with Oblivion defaults.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

GAME_DIR=""
EXE_NAME="Oblivion.exe"
DRY_RUN=0
STEAM_MODE=0
APPID="22330"
STEAM_EXE="${STEAM_EXE:-}"
NO_OVERLAY=0

usage() {
  cat <<EOF
Usage: $(basename "$0") --game-dir DIR [options]

Source spockd3d9-host.env and launch Oblivion under \$WINE (default: wine)
with the SpockD3D9 d3d9.dll override applied.

Options:
  --exe NAME      Game executable for direct launch (default: Oblivion.exe)
  --steam         Launch through Steam (default for retail builds).
  --appid N       Steam App ID (default: $APPID = Oblivion GOTY; 4500 = original)
  --steam-exe P   Path to Steam.exe in the prefix.
  --no-overlay    Disable the Steam overlay for clean D3D9 diagnostics.
  --dry-run       Print the resolved command and environment, then exit.
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

ARGS=(
  --game-dir "$GAME_DIR"
  --exe "$EXE_NAME"
  --title "The Elder Scrolls IV: Oblivion"
  --log-slug oblivion
)

[ "$STEAM_MODE" -eq 1 ] && ARGS+=(--steam --appid "$APPID")
[ -n "$STEAM_EXE" ] && ARGS+=(--steam-exe "$STEAM_EXE")
[ "$NO_OVERLAY" -eq 1 ] && ARGS+=(--no-overlay)
[ "$DRY_RUN" -eq 1 ] && ARGS+=(--dry-run)

exec "$ROOT/scripts/launch-steam-d3d9-host.sh" "${ARGS[@]}"
