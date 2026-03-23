#!/usr/bin/env python3

import argparse
import csv
import json
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path


CSV_FIELDS = [
    "nloc",
    "ccn",
    "token",
    "param",
    "length",
    "location",
    "file",
    "function_name",
    "long_name",
    "start_line",
    "end_line",
]
METRICS = ("ccn", "length", "param")
SCAN_DIRS = ("src", "include", "tests")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare lizard metrics for the full codebase against origin/main."
    )
    parser.add_argument("--candidate-root", required=True)
    parser.add_argument("--baseline-root", required=True)
    parser.add_argument("--thresholds", required=True)
    parser.add_argument("--summary", required=True)
    parser.add_argument("--candidate-report")
    parser.add_argument("--baseline-report")
    return parser.parse_args()


def load_thresholds(path: Path) -> dict:
    data = json.loads(path.read_text())
    defaults = normalize_threshold_values(data.get("defaults", {}))
    new_function = normalize_threshold_values(data.get("new_function", {}))
    churn = data.get("churn", {})
    churn_tiers = []
    for tier in churn.get("tiers", []):
        churn_tiers.append(
            {
                "name": tier["name"],
                "min_commits": int(tier["min_commits"]),
                "thresholds": normalize_threshold_values(tier.get("thresholds", {})),
            }
        )
    churn_tiers.sort(key=lambda item: item["min_commits"], reverse=True)

    overrides = []
    for override in data.get("overrides", []):
        overrides.append(
            {
                "path_prefix": override.get("path_prefix"),
                "path": override.get("path"),
                "thresholds": normalize_threshold_values(override.get("thresholds", {})),
            }
        )

    return {
        "defaults": defaults,
        "new_function": new_function,
        "churn": {
            "since_days": int(churn.get("since_days", 180)),
            "tiers": churn_tiers,
        },
        "overrides": overrides,
    }


def normalize_threshold_values(raw: dict) -> dict[str, int]:
    return {metric: int(raw[metric]) for metric in METRICS if metric in raw}


def collect_file_churn(root: Path, since_days: int) -> dict[str, int]:
    since_date = (datetime.now(timezone.utc) - timedelta(days=since_days)).strftime("%Y-%m-%dT%H:%M:%SZ")
    cmd = [
        "git",
        "log",
        f"--since={since_date}",
        "--format=COMMIT:%H",
        "--name-only",
        "--",
        *SCAN_DIRS,
    ]
    result = subprocess.run(
        cmd,
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )

    file_commits: dict[str, set[str]] = {}
    current_commit = None
    for line in result.stdout.splitlines():
        if not line:
            continue
        if line.startswith("COMMIT:"):
            current_commit = line.removeprefix("COMMIT:")
            continue
        if current_commit is None:
            continue
        file_commits.setdefault(line, set()).add(current_commit)

    return {path: len(commits) for path, commits in file_commits.items()}


def run_lizard(root: Path) -> list[dict[str, str]]:
    cmd = [
        sys.executable,
        "-m",
        "lizard",
        *SCAN_DIRS,
        "-l",
        "c",
        "--csv",
    ]
    result = subprocess.run(
        cmd,
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    reader = csv.DictReader(result.stdout.splitlines(), fieldnames=CSV_FIELDS)
    rows = []
    for row in reader:
        if not row.get("file"):
            continue
        rows.append(row)
    return rows


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        for row in rows:
            writer.writerow(row)


def row_key(row: dict[str, str]) -> tuple[str, str]:
    return row["file"], row["long_name"]


def metric_map(row: dict[str, str]) -> dict[str, int]:
    return {metric: int(row[metric]) for metric in METRICS}


def merge_thresholds(base: dict[str, int], updates: dict[str, int]) -> dict[str, int]:
    merged = dict(base)
    merged.update(updates)
    return merged


def matching_override(overrides: list[dict], file_path: str) -> dict | None:
    best = None
    best_length = -1
    for override in overrides:
        exact_path = override.get("path")
        prefix = override.get("path_prefix")
        match_length = -1

        if exact_path is not None and file_path == exact_path:
            match_length = len(exact_path) + 100000
        elif prefix is not None and file_path.startswith(prefix):
            match_length = len(prefix)

        if match_length > best_length:
            best = override
            best_length = match_length
    return best


def threshold_context(
    threshold_config: dict,
    file_churn: dict[str, int],
    file_path: str,
    is_new_function: bool,
) -> dict:
    thresholds = dict(threshold_config["defaults"])
    churn_count = file_churn.get(file_path, 0)
    churn_tier = None

    for tier in threshold_config["churn"]["tiers"]:
        if churn_count >= tier["min_commits"]:
            thresholds = merge_thresholds(thresholds, tier["thresholds"])
            churn_tier = tier["name"]
            break

    override = matching_override(threshold_config["overrides"], file_path)

    if is_new_function:
        thresholds = merge_thresholds(thresholds, threshold_config["new_function"])

    if override is not None:
        thresholds = merge_thresholds(thresholds, override["thresholds"])

    return {
        "thresholds": thresholds,
        "churn_count": churn_count,
        "churn_tier": churn_tier,
        "override": override,
        "is_new_function": is_new_function,
    }


def summarize_rows(
    candidate_rows: list[dict[str, str]],
    baseline_rows: list[dict[str, str]],
    threshold_config: dict,
    file_churn: dict[str, int],
) -> dict:
    baseline_map = {row_key(row): row for row in baseline_rows}
    inherited = []
    regressions = []

    for row in candidate_rows:
        baseline_row = baseline_map.get(row_key(row))
        context = threshold_context(
            threshold_config,
            file_churn,
            row["file"],
            is_new_function=baseline_row is None,
        )
        actual = metric_map(row)
        exceeded = {
            metric: value for metric, value in actual.items() if value > context["thresholds"][metric]
        }
        if not exceeded:
            continue

        inherited_metrics = {}
        regression_metrics = {}

        for metric, value in exceeded.items():
            baseline_value = None
            if baseline_row is not None:
                baseline_value = int(baseline_row[metric])

            if (
                baseline_value is not None
                and baseline_value > context["thresholds"][metric]
                and value <= baseline_value
            ):
                inherited_metrics[metric] = {
                    "candidate": value,
                    "baseline": baseline_value,
                    "threshold": context["thresholds"][metric],
                }
            else:
                regression_metrics[metric] = {
                    "candidate": value,
                    "baseline": baseline_value,
                    "threshold": context["thresholds"][metric],
                }

        if inherited_metrics:
            inherited.append(
                {
                    "file": row["file"],
                    "function": row["function_name"],
                    "start_line": int(row["start_line"]),
                    "is_new_function": context["is_new_function"],
                    "churn_count": context["churn_count"],
                    "churn_tier": context["churn_tier"],
                    "metrics": inherited_metrics,
                }
            )
        if regression_metrics:
            regressions.append(
                {
                    "file": row["file"],
                    "function": row["function_name"],
                    "start_line": int(row["start_line"]),
                    "is_new_function": context["is_new_function"],
                    "churn_count": context["churn_count"],
                    "churn_tier": context["churn_tier"],
                    "metrics": regression_metrics,
                }
            )

    inherited.sort(key=lambda item: (item["file"], item["function"]))
    regressions.sort(key=lambda item: (item["file"], item["function"]))

    baseline_debt = 0
    for row in baseline_rows:
        context = threshold_context(
            threshold_config,
            file_churn,
            row["file"],
            is_new_function=False,
        )
        actual = metric_map(row)
        if any(actual[metric] > context["thresholds"][metric] for metric in METRICS):
            baseline_debt += 1

    return {
        "thresholds": threshold_config,
        "candidate_function_count": len(candidate_rows),
        "baseline_function_count": len(baseline_rows),
        "baseline_debt_count": baseline_debt,
        "candidate_inherited_debt_count": len(inherited),
        "regression_count": len(regressions),
        "inherited_debt": inherited,
        "regressions": regressions,
    }


def print_summary(summary: dict) -> None:
    defaults = summary["thresholds"]["defaults"]
    new_function = summary["thresholds"]["new_function"]
    print("## Complexity Report")
    print()
    print(
        f"Thresholds: defaults(ccn={defaults.get('ccn')}, length={defaults.get('length')}, "
        f"param={defaults.get('param')}), new-function ccn={new_function.get('ccn', defaults.get('ccn'))}"
    )
    print()
    print(
        f"Functions analyzed: candidate={summary['candidate_function_count']}, "
        f"baseline={summary['baseline_function_count']}"
    )
    print(
        f"Inherited debt: {summary['candidate_inherited_debt_count']} functions above thresholds"
    )
    print(f"Regressions: {summary['regression_count']}")
    print()

    if summary["regressions"]:
        print("New or worsened threshold violations:")
        for item in summary["regressions"]:
            metrics = ", ".join(
                f"{metric}={values['candidate']} (baseline={values['baseline']}, limit={values['threshold']})"
                for metric, values in item["metrics"].items()
            )
            details = []
            if item["is_new_function"]:
                details.append("new")
            if item["churn_tier"] is not None:
                details.append(f"churn={item['churn_tier']}:{item['churn_count']}")
            suffix = f" [{' '.join(details)}]" if details else ""
            print(f"- {item['file']}:{item['start_line']} {item['function']}: {metrics}{suffix}")
        print()

    if summary["candidate_inherited_debt_count"]:
        print("Inherited above-threshold functions:")
        for item in summary["inherited_debt"][:20]:
            metrics = ", ".join(
                f"{metric}={values['candidate']} (baseline={values['baseline']}, limit={values['threshold']})"
                for metric, values in item["metrics"].items()
            )
            details = []
            if item["churn_tier"] is not None:
                details.append(f"churn={item['churn_tier']}:{item['churn_count']}")
            suffix = f" [{' '.join(details)}]" if details else ""
            print(f"- {item['file']}:{item['start_line']} {item['function']}: {metrics}{suffix}")
        if summary["candidate_inherited_debt_count"] > 20:
            remaining = summary["candidate_inherited_debt_count"] - 20
            print(f"- ... {remaining} more inherited functions")


def main() -> int:
    args = parse_args()
    candidate_root = Path(args.candidate_root).resolve()
    baseline_root = Path(args.baseline_root).resolve()
    summary_path = Path(args.summary)
    summary_path.parent.mkdir(parents=True, exist_ok=True)

    threshold_config = load_thresholds(Path(args.thresholds))
    file_churn = collect_file_churn(candidate_root, threshold_config["churn"]["since_days"])
    candidate_rows = run_lizard(candidate_root)
    baseline_rows = run_lizard(baseline_root)

    if args.candidate_report:
        path = Path(args.candidate_report)
        path.parent.mkdir(parents=True, exist_ok=True)
        write_csv(path, candidate_rows)
    if args.baseline_report:
        path = Path(args.baseline_report)
        path.parent.mkdir(parents=True, exist_ok=True)
        write_csv(path, baseline_rows)

    summary = summarize_rows(candidate_rows, baseline_rows, threshold_config, file_churn)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print_summary(summary)
    return 1 if summary["regression_count"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
