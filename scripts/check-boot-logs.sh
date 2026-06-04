#!/usr/bin/env bash
# Parse a SpockD3D9 / Wine log for boot-to-menu milestones (V1–V4).
#
# Usage:
#   ./scripts/check-boot-logs.sh fallout3-spockd3d9.log
#   ./scripts/check-boot-logs.sh d3d9.log

set -euo pipefail

if [ $# -lt 1 ]; then
  echo "Usage: $(basename "$0") LOG_FILE" >&2
  exit 1
fi

LOG="$1"
if [ ! -f "$LOG" ]; then
  echo "error: log file not found: $LOG" >&2
  exit 1
fi

pass() { echo "  [PASS] $1"; }
fail() { echo "  [FAIL] $1"; }
warn() { echo "  [WARN] $1"; }
info() { echo "  [----] $1"; }

echo "=== SpockD3D9 boot milestone check ==="
echo "Log: $LOG"
echo ""

v1=0 v2=0 v3=0 v4_hint=0

if grep -qE 'DXVK: |info:.*DXVK:' "$LOG" 2>/dev/null || grep -q 'DXVK:' "$LOG"; then
  pass "V1 — DXVK/SpockD3D9 translator loaded"
  v1=1
  grep -m1 'DXVK:' "$LOG" | sed 's/^/         /' || true
else
  fail "V1 — no DXVK version line (override not loaded?)"
fi

if grep -qE 'D3D9: Detected GPU|D3D9Adapter|adapterCount|Found device' "$LOG" 2>/dev/null \
   || grep -qE 'DXVK.*adapter|Enumerated.*adapter' "$LOG" 2>/dev/null; then
  pass "V2 — adapter / GPU enumeration seen"
  v2=1
elif [ "$v1" -eq 1 ]; then
  warn "V2 — inconclusive (no adapter line; may still be OK if CreateDevice succeeded)"
  v2=1
else
  fail "V2 — no adapter enumeration"
fi

if grep -q 'D3D9: CreateDeviceEx OK' "$LOG"; then
  pass "V3 — CreateDeviceEx succeeded"
  v3=1
  grep -m1 'D3D9: CreateDeviceEx OK' "$LOG" | sed 's/^/         /' || true
else
  fail "V3 — CreateDeviceEx OK not found"
fi

if grep -qE 'Device lost|Device reset' "$LOG"; then
  count=$(grep -cE 'Device lost|Device reset' "$LOG" || true)
  if [ "${count:-0}" -gt 3 ]; then
    warn "Repeated device lost/reset ($count lines) — menu may not stay up"
  else
    info "Occasional device lost/reset ($count) — verify focus handling"
  fi
fi

if grep -qE 'err:.*shader|Failed to create.*pipeline|DXSO' "$LOG"; then
  warn "Shader/pipeline errors present — check DXVK_LOG_LEVEL=debug"
else
  if [ "$v3" -eq 1 ]; then
    pass "V4 hint — no obvious shader fatal errors (confirm menu visually)"
    v4_hint=1
  fi
fi

echo ""
echo "Summary:"
echo "  V1 library:  $([ "$v1" -eq 1 ] && echo yes || echo NO)"
echo "  V2 adapter:  $([ "$v2" -eq 1 ] && echo yes || echo NO)"
echo "  V3 device:   $([ "$v3" -eq 1 ] && echo yes || echo NO)"
echo "  V4 menu:     manual — see docs/BOOT_TO_MENU.md"

if [ "$v1" -eq 1 ] && [ "$v3" -eq 1 ]; then
  echo ""
  echo "Translator path looks healthy. If the screen is still black, capture"
  echo "DXVK_LOG_LEVEL=debug and file a macOS bug report."
  exit 0
fi

exit 1
