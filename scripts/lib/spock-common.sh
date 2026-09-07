#!/usr/bin/env bash
# Shared helpers for SpockD3D9 shell tools. Source, don't execute:
#   source "$(dirname "$0")/lib/spock-common.sh"
#
# Mirrors the native prefix discovery in src/util/util_env.cpp
# (getHomebrewPrefixes): Homebrew is not always in /opt/homebrew or
# /usr/local — custom prefixes such as ~/homebrew must be probed too, and
# `brew` itself may not be on a non-interactive shell's PATH.

# Print existing Homebrew prefix candidates, most-specific first, deduped.
spock_brew_prefixes() {
  local -a candidates=()
  [ -n "${HOMEBREW_PREFIX:-}" ] && candidates+=("$HOMEBREW_PREFIX")

  local via_brew=""
  if command -v brew >/dev/null 2>&1; then
    via_brew="$(brew --prefix 2>/dev/null || true)"
  elif [ -x "$HOME/homebrew/bin/brew" ]; then
    via_brew="$("$HOME/homebrew/bin/brew" --prefix 2>/dev/null || true)"
  fi
  [ -n "$via_brew" ] && candidates+=("$via_brew")

  candidates+=("$HOME/homebrew" /opt/homebrew /usr/local)

  local seen="" p
  for p in "${candidates[@]}"; do
    [ -n "$p" ] && [ -d "$p" ] || continue
    case ":$seen:" in
      *":$p:"*) ;;
      *) seen="${seen:+$seen:}$p"; printf '%s\n' "$p" ;;
    esac
  done
}

# Echo the first MoltenVK ICD manifest found under a Homebrew prefix.
spock_find_moltenvk_icd() {
  local p icd
  while IFS= read -r p; do
    for icd in "$p/share/vulkan/icd.d/MoltenVK_icd.json" \
               "$p/etc/vulkan/icd.d/MoltenVK_icd.json"; do
      if [ -f "$icd" ]; then
        printf '%s\n' "$icd"
        return 0
      fi
    done
  done < <(spock_brew_prefixes)
  return 1
}

# Export VK_ICD_FILENAMES/VK_DRIVER_FILES at the first MoltenVK ICD found.
# An explicitly pre-set VK_ICD_FILENAMES is respected (no override).
spock_export_moltenvk_icd() {
  [ -n "${VK_ICD_FILENAMES:-}" ] && return 0
  local icd
  if icd="$(spock_find_moltenvk_icd)"; then
    export VK_ICD_FILENAMES="$icd"
    export VK_DRIVER_FILES="$icd"
    return 0
  fi
  return 1
}

# Echo an existing prefix containing lib/<lib> for DYLD_LIBRARY_PATH use.
spock_find_prefix_with_lib() {
  local lib="$1" p
  while IFS= read -r p; do
    if [ -e "$p/lib/$lib" ]; then
      printf '%s\n' "$p"
      return 0
    fi
  done < <(spock_brew_prefixes)
  return 1
}

# Resolve a command to an absolute path so a later cd cannot break it.
# Usage: abs="$(spock_abs_cmd "$cmd")" — echoes nothing if not found.
spock_abs_cmd() {
  local cmd="$1" found
  if ! command -v "$cmd" >/dev/null 2>&1; then
    return 1
  fi
  found="$(command -v "$cmd")"
  case "$found" in
    /*) printf '%s\n' "$found" ;;
    *)  printf '%s\n' "$PWD/$found" ;;
  esac
}

# Ensure a Homebrew prefix's bin dir is on PATH when it carries build tools
# but the shell was not configured for the custom prefix (e.g. ~/homebrew
# installed without shell PATH setup). Idempotent.
spock_ensure_brew_bin_on_path() {
  local p
  while IFS= read -r p; do
    if [ -x "$p/bin/meson" ] || [ -x "$p/bin/ninja" ] || [ -x "$p/bin/brew" ]; then
      case ":$PATH:" in
        *":$p/bin:"*) ;;
        *) export PATH="$p/bin:$PATH" ;;
      esac
      return 0
    fi
  done < <(spock_brew_prefixes)
  return 1
}
