#!/usr/bin/env python3
"""Compare benchmark result sets and fail on material regressions."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, help="Path to baseline benchmark results JSON")
    parser.add_argument("--candidate", required=True, help="Path to candidate benchmark results JSON")
    parser.add_argument("--thresholds", required=True, help="Path to thresholds JSON")
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def pct_delta(baseline: int, candidate: int) -> float:
    if baseline <= 0:
        return 0.0
    return ((candidate - baseline) / baseline) * 100.0


def ms_from_ns(value: int) -> float:
    return value / 1_000_000.0


def merged_thresholds(all_thresholds: dict[str, Any], name: str) -> dict[str, float]:
    merged = dict(all_thresholds["defaults"])
    merged.update(all_thresholds.get("benchmarks", {}).get(name, {}))
    return merged


def _extract_metrics(entry: dict[str, Any]) -> tuple[int, int]:
    """Extract median_elapsed_ns and median_max_rss_kb from either flat or nested format."""
    if "median_elapsed_ns" in entry:
        return int(entry["median_elapsed_ns"]), int(entry["median_max_rss_kb"])
    if "aot" in entry:
        aot = entry["aot"]
        return int(aot["median_elapsed_ns"]), int(aot["median_max_rss_kb"])
    raise KeyError("benchmark entry has neither 'median_elapsed_ns' nor 'aot' key")


def summarize_row(
    name: str,
    baseline: dict[str, Any],
    candidate: dict[str, Any],
    limits: dict[str, float],
) -> tuple[list[str], list[str]]:
    baseline_time, baseline_rss = _extract_metrics(baseline)
    candidate_time, candidate_rss = _extract_metrics(candidate)

    time_delta_ns = candidate_time - baseline_time
    rss_delta_kb = candidate_rss - baseline_rss
    time_pct = pct_delta(baseline_time, candidate_time)
    rss_pct = pct_delta(baseline_rss, candidate_rss)

    failures = []
    if (
        time_delta_ns > 0
        and time_pct > float(limits["time_regress_pct"])
        and ms_from_ns(time_delta_ns) > float(limits["time_regress_abs_ms"])
    ):
        failures.append(
            f"time +{time_pct:.1f}% (+{ms_from_ns(time_delta_ns):.2f} ms) exceeds "
            f"{limits['time_regress_pct']}% and {limits['time_regress_abs_ms']} ms"
        )

    if (
        rss_delta_kb > 0
        and rss_pct > float(limits["rss_regress_pct"])
        and rss_delta_kb > int(limits["rss_regress_abs_kb"])
    ):
        failures.append(
            f"rss +{rss_pct:.1f}% (+{rss_delta_kb} KB) exceeds "
            f"{limits['rss_regress_pct']}% and {limits['rss_regress_abs_kb']} KB"
        )

    row = [
        name,
        f"{ms_from_ns(baseline_time):.2f}",
        f"{ms_from_ns(candidate_time):.2f}",
        f"{time_pct:+.1f}%",
        str(baseline_rss),
        str(candidate_rss),
        f"{rss_pct:+.1f}%",
        "FAIL" if failures else "PASS",
    ]
    return row, failures


def write_summary(markdown: str) -> None:
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(markdown)
        if not markdown.endswith("\n"):
            handle.write("\n")


def main() -> int:
    args = parse_args()
    baseline_data = load_json(Path(args.baseline))
    candidate_data = load_json(Path(args.candidate))
    thresholds = load_json(Path(args.thresholds))

    baseline_map = {entry["name"]: entry for entry in baseline_data["benchmarks"]}
    candidate_map = {entry["name"]: entry for entry in candidate_data["benchmarks"]}

    missing = sorted(set(candidate_map) - set(baseline_map))
    if missing:
        raise SystemExit(f"baseline is missing benchmark(s): {', '.join(missing)}")

    headers = ["benchmark", "main_ms", "pr_ms", "time_delta", "main_rss_kb", "pr_rss_kb", "rss_delta", "result"]
    markdown_lines = [
        "## Performance Regression Report",
        "",
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]

    failures_by_benchmark: list[tuple[str, list[str]]] = []
    for name in sorted(candidate_map):
        limits = merged_thresholds(thresholds, name)
        row, failures = summarize_row(name, baseline_map[name], candidate_map[name], limits)
        markdown_lines.append("| " + " | ".join(row) + " |")
        if failures:
            failures_by_benchmark.append((name, failures))

    markdown_lines.append("")
    if failures_by_benchmark:
        markdown_lines.append("### Regressions")
        markdown_lines.append("")
        for name, failures in failures_by_benchmark:
            markdown_lines.append(f"- `{name}`")
            for failure in failures:
                markdown_lines.append(f"  - {failure}")
    else:
        markdown_lines.append("No material performance regressions detected.")

    markdown = "\n".join(markdown_lines) + "\n"
    print(markdown, end="")
    write_summary(markdown)

    if failures_by_benchmark:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
