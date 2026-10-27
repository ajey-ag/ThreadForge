#!/usr/bin/env python3
"""Summarize raw benchmark rows without third-party packages."""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python3 scripts/summarize_benchmarks.py results.csv")
    with Path(sys.argv[1]).open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise SystemExit("No benchmark rows")
    groups = defaultdict(list)
    for row in rows:
        groups[(row["workload"], int(row["workers"]), row["policy"], row["timing"])].append(float(row["elapsed_s"]))
    maximum = max(int(row["workers"]) for row in rows)
    print("# Measured benchmark summary\n")
    print(f"Source: `{Path(sys.argv[1]).name}`; {len(rows)} raw rows. Times below are median milliseconds.")
    print("Speedup is serial median / parallel median, not a guaranteed or theoretical speedup.\n")
    print(f"| Workload | Serial ms | FIFO {maximum} workers ms | Stealing {maximum} workers ms | FIFO speedup | Stealing speedup |")
    print("|---|---:|---:|---:|---:|---:|")
    for workload in ("cpu", "short", "mixed", "blocking", "batch"):
        serial = statistics.median(groups[(workload, 0, "serial", "0")])
        fifo = statistics.median(groups[(workload, maximum, "fifo", "1")])
        stealing_values = groups[(workload, maximum, "stealing", "1")]
        if not stealing_values:
            raise SystemExit("This summary requires the final work-stealing benchmark sweep")
        stealing = statistics.median(stealing_values)
        print(f"| {workload} | {serial * 1000:.3f} | {fifo * 1000:.3f} | {stealing * 1000:.3f} | {serial / fifo:.3f}x | {serial / stealing:.3f}x |")
    print("\n## Timing instrumentation comparison\n")
    print("FIFO elapsed medians; on/off differences include measurement noise and scheduling effects. Counts and benchmark latency sampling remain enabled in both modes.\n")
    print("| Workload | Workers | Timing on ms | Timing off ms | On/off ratio |")
    print("|---|---:|---:|---:|---:|")
    for workload in ("cpu", "short", "mixed", "blocking", "batch"):
        for workers in sorted({1, maximum}):
            on = statistics.median(groups[(workload, workers, "fifo", "1")])
            off = statistics.median(groups[(workload, workers, "fifo", "0")])
            print(f"| {workload} | {workers} | {on * 1000:.3f} | {off * 1000:.3f} | {on / off:.3f} |")
    print("\n## Spread at the maximum worker count\n")
    print("| Workload | Policy | Min ms | Median ms | Max ms |")
    print("|---|---|---:|---:|---:|")
    for workload in ("cpu", "short", "mixed", "blocking", "batch"):
        for policy in ("fifo", "priority", "stealing"):
            values = groups[(workload, maximum, policy, "1")]
            print(f"| {workload} | {policy} | {min(values)*1000:.3f} | {statistics.median(values)*1000:.3f} | {max(values)*1000:.3f} |")
    print("\nThese short runs on shared WSL hardware are validation measurements, not publication-grade performance claims. No affinity pinning or power/thermal control was applied. Repeat on an idle machine with larger batches before optimizing.")


if __name__ == "__main__":
    main()
