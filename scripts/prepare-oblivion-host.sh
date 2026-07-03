#!/usr/bin/env bash
# Install SpockD3D9 PE d3d9.dll + dxvk.conf into an Oblivion game directory.
#
# Thin wrapper around prepare-steam-d3d9-host.sh with Oblivion defaults.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

GAME_DIR=""
DLL_PATH=""
PROFILE_PATH="$ROOT/tools/oblivion/oblivion.dxvk.conf"
DO_BUILD=0

usage() {
  cat <<EOF
Usage: $(basename "$0") --game-dir DIR [--dll PATH] [--profile PATH] [--build]

Copy SpockD3D9 d3d9.dll and oblivion.dxvk.conf (as dxvk.conf) into DIR.
Writes spockd3d9-host.env for launch-oblivion-host.sh.

Oblivion is 32-bit — this wrapper always uses the x86 PE build.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --game-dir) shift; GAME_DIR="${1:-}" ;;
    --dll) shift; DLL_PATH="${1:-}" ;;
    --profile) shift; PROFILE_PATH="${1:-}" ;;
    --build) DO_BUILD=1 ;;
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
  --profile "$PROFILE_PATH"
  --title "The Elder Scrolls IV: Oblivion"
  --arch x86
)

[ -n "$DLL_PATH" ] && ARGS+=(--dll "$DLL_PATH")
[ "$DO_BUILD" -eq 1 ] && ARGS+=(--build)

exec "$ROOT/scripts/prepare-steam-d3d9-host.sh" "${ARGS[@]}"
