#!/usr/bin/env python3
"""Hermetic validation for shipped dxvk.conf profiles.

A title profile (e.g. tools/fallout3/fallout3.dxvk.conf) must only set keys that
SpockD3D9 actually understands. The canonical list of understood keys is the set
documented in the repository-root ``dxvk.conf`` (every option appears there as a
commented ``# section.key = value`` line). This test discovers every
``tools/**/*.dxvk.conf`` profile with no dependencies beyond the Python standard
library and fails if a profile sets a key that the reference file does not
document, is otherwise malformed, or if a required benchmark profile is missing.

Run directly:  python3 tests/conf/test_dxvk_conf_profiles.py
"""

import os
import re
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

REFERENCE_CONF = os.path.join(REPO_ROOT, "dxvk.conf")

# Benchmark profiles that must ship while Fallout 3, Fallout: New Vegas, and
# Dragon Age: Origins are the Windows D3D9/macOS compatibility targets.
REQUIRED_PROFILES = {
    os.path.join("tools", "fallout3", "fallout3.dxvk.conf"),
    os.path.join("tools", "fallout-new-vegas", "fallout-new-vegas.dxvk.conf"),
    os.path.join("tools", "dragon-age-origins", "dragon-age-origins.dxvk.conf"),
}

# A "section.key" token, e.g. d3d9.shaderModel or dxvk.tilerMode.
KEY_RE = re.compile(r"^([A-Za-z0-9_]+\.[A-Za-z0-9_]+)\s*=")


def documented_keys(path):
    """Keys documented in the reference conf, taken from commented option lines."""
    keys = set()
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if not stripped.startswith("#"):
                # Reference conf documents options as comments; an uncommented
                # assignment would be an active default, which we also accept.
                body = stripped
            else:
                body = stripped.lstrip("#").strip()
            match = KEY_RE.match(body)
            if match:
                keys.add(match.group(1))
    return keys


def active_assignments(path):
    """(key, lineno) for every uncommented ``key = value`` line in a profile."""
    assignments = []
    with open(path, "r", encoding="utf-8") as handle:
        for lineno, line in enumerate(handle, start=1):
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            match = KEY_RE.match(stripped)
            if not match:
                raise AssertionError(
                    f"{path}:{lineno}: line is not a 'section.key = value' "
                    f"assignment or comment: {stripped!r}"
                )
            assignments.append((match.group(1), lineno))
    return assignments


def discover_profiles():
    """Return all shipped title profiles under tools/, as absolute paths."""
    profiles = []
    tools_dir = os.path.join(REPO_ROOT, "tools")

    for root, _, files in os.walk(tools_dir):
        for filename in files:
            if filename.endswith(".dxvk.conf"):
                profiles.append(os.path.join(root, filename))

    return sorted(profiles)


def main():
    failures = []

    if not os.path.isfile(REFERENCE_CONF):
        print(f"FAIL: reference conf not found: {REFERENCE_CONF}")
        return 1

    known = documented_keys(REFERENCE_CONF)
    if not known:
        print(f"FAIL: no documented keys parsed from {REFERENCE_CONF}")
        return 1

    print(f"Reference dxvk.conf documents {len(known)} option keys.")

    profiles = discover_profiles()
    if not profiles:
        print("FAIL: no shipped dxvk.conf profiles discovered under tools/")
        return 1

    discovered_rel = {
        os.path.relpath(profile, REPO_ROOT)
        for profile in profiles
    }

    for required in sorted(REQUIRED_PROFILES - discovered_rel):
        failures.append(f"required benchmark profile not found: {required}")

    for profile in profiles:
        if not os.path.isfile(profile):
            failures.append(f"profile not found: {profile}")
            continue

        assignments = active_assignments(profile)
        rel = os.path.relpath(profile, REPO_ROOT)
        if not assignments:
            failures.append(f"{rel}: sets no options (expected at least one)")
            continue

        for key, lineno in assignments:
            if key not in known:
                failures.append(
                    f"{rel}:{lineno}: unknown option '{key}' "
                    f"(not documented in dxvk.conf)"
                )

        print(f"OK: {rel} - {len(assignments)} active option(s), all recognized.")

    if failures:
        print("\nFAILURES:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("\nAll profiles valid.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
