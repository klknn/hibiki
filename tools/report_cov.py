#!/usr/bin/env python3
"""Parse Bazel LCOV coverage report and display per-file coverage for hibiki/ui Java sources.

Usage:
    bazel coverage -c opt --combined_report=lcov :component_initialization_test :theme_test ...
    python3 tools/report_cov.py
"""

import glob
import os
import re
import sys
import subprocess

_DEFAULT_BAZEL_COVERAGE_REPORT = "bazel-out/_coverage/_coverage_report.dat"

def find_lcov_report() -> str:
    """Find the combined LCOV report from the Bazel cache."""
    if os.path.exists(_DEFAULT_BAZEL_COVERAGE_REPORT):
        return _DEFAULT_BAZEL_COVERAGE_REPORT
    candidates = glob.glob(
        os.path.expanduser(
            "~/.cache/bazel/_bazel_*/*/execroot/_main/bazel-out/_coverage/_coverage_report.dat"
        )
    )
    if not candidates:
        print("ERROR: Could not find combined LCOV report.", file=sys.stderr)
        print("Run 'bazel coverage -c opt --combined_report=lcov <targets>' first.", file=sys.stderr)
        sys.exit(1)
    # Use most recently modified
    candidates.sort(key=os.path.getmtime, reverse=True)
    return candidates[0]


def parse_lcov(path: str) -> dict[str, tuple[int, int]]:
    """Parse LCOV file and return per-file (hit, total) for hibiki/ui sources."""
    with open(path) as f:
        content = f.read()

    files = {}
    for record in content.strip().split("end_of_record"):
        sf_match = re.search(r"SF:(.*)", record)
        if not sf_match:
            continue
        source = sf_match.group(1)
        if "/hibiki/ui/" not in source or source.endswith("Test.java"):
            continue

        basename = os.path.basename(source)
        lh = sum(int(line.split(":")[1]) for line in record.split("\n") if line.startswith("LH:"))
        lf = sum(int(line.split(":")[1]) for line in record.split("\n") if line.startswith("LF:"))

        if lf > 0:
            if basename in files:
                files[basename] = (files[basename][0] + lh, files[basename][1] + lf)
            else:
                files[basename] = (lh, lf)
    return files


def main():
    cmd = "bazel coverage -c opt --enable_platform_specific_config --combined_report=lcov :all"
    print(f"Running: {cmd}")
    subprocess.check_call(cmd, shell=True)
    cmd = f"genhtml --output genhtml {_DEFAULT_BAZEL_COVERAGE_REPORT} --ignore-errors inconsistent"
    subprocess.call(cmd, shell=True)

    lcov_path = find_lcov_report()
    print(f"Reading: {lcov_path}\n")

    files = parse_lcov(lcov_path)
    if not files:
        print("No hibiki/ui source coverage data found.")
        sys.exit(1)

    total_lh = 0
    total_lf = 0

    print(f"{'File':<40} {'Hit':>5} {'Total':>5} {'Coverage':>8}")
    print("-" * 62)

    for fname in sorted(files):
        lh, lf = files[fname]
        pct = 100.0 * lh / lf if lf > 0 else 0
        marker = "\u274c" if pct < 25.0 else "\u2705"
        print(f"{fname:<40} {lh:>5} {lf:>5} {pct:>7.1f}% {marker}")
        total_lh += lh
        total_lf += lf

    total_pct = 100.0 * total_lh / total_lf if total_lf > 0 else 0
    print("-" * 62)
    marker = "\u274c" if total_pct < 30.0 else "\u2705"
    print(f"{'TOTAL':<40} {total_lh:>5} {total_lf:>5} {total_pct:>7.1f}% {marker}")


if __name__ == "__main__":
    main()
