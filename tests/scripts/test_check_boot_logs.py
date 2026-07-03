#!/usr/bin/env python3
"""Hermetic tests for the Fallout 3 boot-log milestone checker."""

import os
import subprocess
import sys
import tempfile


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO_ROOT, "scripts", "check-boot-logs.sh")

HEALTHY_LOG = """\
info:  DXVK: v2.7.1
info:  D3D9: Detected GPU: MoltenVK
info:  D3D9: CreateDeviceEx OK (1920x1080)
"""


def run_checker(args, stdin=None):
    return subprocess.run(
        [SCRIPT, *args],
        cwd=REPO_ROOT,
        input=stdin,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def assert_contains(haystack, needle):
    if needle not in haystack:
        raise AssertionError(f"expected to find {needle!r} in output:\n{haystack}")


def test_file_input_passes():
    with tempfile.NamedTemporaryFile("w", delete=False) as handle:
        handle.write(HEALTHY_LOG)
        path = handle.name

    try:
        result = run_checker([path])
    finally:
        os.unlink(path)

    if result.returncode != 0:
        raise AssertionError(result.stderr + result.stdout)

    assert_contains(result.stdout, "Log: " + path)
    assert_contains(result.stdout, "[PASS] V1")
    assert_contains(result.stdout, "[PASS] V3")
    assert_contains(result.stdout, "Translator path looks healthy")


def test_stdin_input_passes():
    result = run_checker(["-"], stdin=HEALTHY_LOG)

    if result.returncode != 0:
        raise AssertionError(result.stderr + result.stdout)

    assert_contains(result.stdout, "Log: stdin")
    assert_contains(result.stdout, "V1 library:  yes")
    assert_contains(result.stdout, "V3 device:   yes")


def test_missing_file_fails():
    result = run_checker(["/tmp/spockd3d9-missing-log-does-not-exist.log"])

    if result.returncode == 0:
        raise AssertionError("missing log unexpectedly passed")

    assert_contains(result.stderr, "error: log file not found")


def main():
    tests = [
        test_file_input_passes,
        test_stdin_input_passes,
        test_missing_file_fails,
    ]

    failures = []
    for test in tests:
        try:
            test()
            print(f"OK: {test.__name__}")
        except Exception as exc:  # noqa: BLE001 - plain script, report all failures
            failures.append((test.__name__, exc))
            print(f"FAIL: {test.__name__}: {exc}", file=sys.stderr)

    if failures:
        return 1

    print("\ncheck-boot-logs tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
