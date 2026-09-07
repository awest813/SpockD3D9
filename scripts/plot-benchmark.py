#!/usr/bin/env python3
"""Summarize a Wine FPS capture into benchmark numbers.

Parses the output of a Wine run with WINEDEBUG=-all,+timestamp,+fps (the
channel Wine's own fps counter uses) and reports avg FPS, 1%-low FPS, and
frametime percentiles as CSV — the quantitative block of the benchmark
report format in docs/WINDOWS_D3D9_BENCHMARKS.md.

Wine logs one line per second like:
    0110:0128:0.123s F:S fps:59 timestamp:1633023534.376 queue:00 ...
Only the `fps:` and `timestamp:` pairs are used; other tokens are ignored.
Any line containing `fps:<n>` is accepted, so logs with extra channels on
are tolerated.

Usage:
    ./scripts/plot-benchmark.py LOGFILE [--csv OUT.csv] [--chart OUT.png]

    --csv    write per-second rows + summary (default: print summary only)
    --chart  additionally render a PNG chart (requires matplotlib)

Exit codes: 0 ok, 1 no fps samples found, 2 usage error.
"""

import argparse
import csv
import math
import re
import sys

FPS_RE = re.compile(r"fps:(\d+)")
TS_RE = re.compile(r"timestamp:(\d+(?:\.\d+)?)")


def parse_samples(lines):
    """Return [(timestamp, fps)] from `fps:`/`timestamp:` line pairs."""
    samples = []
    pending_fps = None

    for line in lines:
        fps_match = FPS_RE.search(line)
        if not fps_match:
            continue
        ts_match = TS_RE.search(line)
        if ts_match:
            # Both on one line (modern Wine): direct sample.
            samples.append((float(ts_match.group(1)), int(fps_match.group(1))))
            pending_fps = None
        else:
            # fps seen; look for a timestamp on a nearby line.
            pending_fps = int(fps_match.group(1))

    if not samples and pending_fps is not None:
        samples.append((0.0, pending_fps))

    # Keep monotonic timestamps (Wine restarts the counter per process).
    cleaned = []
    last_ts = -1.0
    for ts, fps in samples:
        if ts < last_ts:
            ts = last_ts  # collapse restarts; samples stay order-stable
        cleaned.append((ts, fps))
        last_ts = ts
    return cleaned


def percentile(sorted_values, pct):
    if not sorted_values:
        return float("nan")
    idx = min(len(sorted_values) - 1, max(0, math.ceil(pct / 100.0 * len(sorted_values)) - 1))
    return sorted_values[idx]


def summarize(samples):
    fps_values = sorted(fps for _, fps in samples)
    frametimes = sorted(1000.0 / fps for fps in fps_values if fps > 0)
    n = len(fps_values)
    return {
        "samples": n,
        "duration_s": samples[-1][0] - samples[0][0] if n > 1 else 0.0,
        "avg_fps": sum(fps_values) / n if n else 0.0,
        "min_fps": fps_values[0] if n else 0.0,
        "max_fps": fps_values[-1] if n else 0.0,
        "fps_1p_low": percentile(fps_values, 1),
        "ft_avg_ms": (sum(frametimes) / len(frametimes)) if frametimes else 0.0,
        "ft_p99_ms": percentile(frametimes, 99),
        "ft_p95_ms": percentile(frametimes, 95),
    }


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("logfile")
    ap.add_argument("--csv", default=None)
    ap.add_argument("--chart", default=None)
    args = ap.parse_args()

    try:
        with open(args.logfile, "r", errors="replace") as f:
            samples = parse_samples(f)
    except OSError as e:
        print(f"error: cannot read {args.logfile}: {e}", file=sys.stderr)
        return 2

    if not samples:
        print(f"error: no `fps:` samples found in {args.logfile} "
              f"(capture with WINEDEBUG=-all,+timestamp,+fps)", file=sys.stderr)
        return 1

    summary = summarize(samples)

    print(f"samples:     {summary['samples']} ({summary['duration_s']:.0f}s span)")
    print(f"avg fps:     {summary['avg_fps']:.1f}")
    print(f"1% low fps:  {summary['fps_1p_low']:.0f}")
    print(f"min/max fps: {summary['min_fps']:.0f} / {summary['max_fps']:.0f}")
    print(f"ft avg:      {summary['ft_avg_ms']:.1f} ms")
    print(f"ft p95:      {summary['ft_p95_ms']:.1f} ms")
    print(f"ft p99:      {summary['ft_p99_ms']:.1f} ms")

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["timestamp_s", "fps"])
            for ts, fps in samples:
                w.writerow([f"{ts:.3f}", fps])
            w.writerow([])
            w.writerow(["metric", "value"])
            for key, value in summary.items():
                w.writerow([key, f"{value:.3f}"])
        print(f"csv:         {args.csv}")

    if args.chart:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            print("warning: matplotlib not installed; skipping chart", file=sys.stderr)
        else:
            times = [ts - samples[0][0] for ts, _ in samples]
            fps = [fps for _, fps in samples]
            fig, ax = plt.subplots(figsize=(10, 4))
            ax.plot(times, fps, linewidth=1)
            ax.set_xlabel("seconds")
            ax.set_ylabel("fps")
            ax.set_title(args.logfile)
            ax.grid(alpha=0.3)
            fig.tight_layout()
            fig.savefig(args.chart, dpi=120)
            print(f"chart:       {args.chart}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
