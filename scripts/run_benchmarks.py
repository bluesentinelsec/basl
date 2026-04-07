#!/usr/bin/env python3
"""Run VIGIL performance benchmarks and emit JSON results."""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vigil-bin", required=True, help="Path to the vigil executable")
    parser.add_argument("--manifest", required=True, help="Path to benchmarks/manifest.json")
    parser.add_argument("--output", required=True, help="Where to write JSON results")
    parser.add_argument("--case", action="append", default=[], help="Run only the named benchmark case")
    return parser.parse_args()


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if data.get("version") != 1:
        raise ValueError(f"unsupported manifest version in {path}")
    return data


def rss_kb_from_rusage(rusage: os.wait4_result) -> int:
    rss = int(rusage.ru_maxrss)
    if sys.platform == "darwin":
        return rss // 1024
    return rss


def wait_for_process(proc: subprocess.Popen[Any], timeout_seconds: int) -> tuple[int, int]:
    start_ns = time.perf_counter_ns()
    deadline = time.monotonic() + timeout_seconds
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            proc.kill()
            _, status, rusage = os.wait4(proc.pid, 0)
            raise TimeoutError(f"benchmark timed out after {timeout_seconds}s (status={status}, rss={rss_kb_from_rusage(rusage)}KB)")
        pid, status, rusage = os.wait4(proc.pid, os.WNOHANG)
        if pid == proc.pid:
            elapsed_ns = time.perf_counter_ns() - start_ns
            return os.waitstatus_to_exitcode(status), rss_kb_from_rusage(rusage), elapsed_ns
        time.sleep(0.01)


def run_once(argv: list[str], cwd: Path, timeout_seconds: int, env: dict[str, str] | None = None) -> tuple[int, int]:
    with tempfile.TemporaryFile() as stdout_handle, tempfile.TemporaryFile() as stderr_handle:
        proc = subprocess.Popen(
            argv,
            cwd=str(cwd),
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=stdout_handle,
            stderr=stderr_handle,
            text=False,
        )
        returncode, max_rss_kb, elapsed_ns = wait_for_process(proc, timeout_seconds)
        if returncode != 0:
            stderr_handle.seek(0)
            stderr_text = stderr_handle.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"benchmark command failed ({returncode}): {' '.join(argv)}\n{stderr_text}")
        return elapsed_ns, max_rss_kb


def median_int(values: list[int]) -> int:
    return int(statistics.median(values))


def format_ms(elapsed_ns: int) -> str:
    return f"{elapsed_ns / 1_000_000.0:.2f}"


def format_rss(rss_kb: int) -> str:
    return f"{rss_kb} KB"


def run_variant(repo_root: Path, argv: list[str], entry: dict[str, Any], env: dict[str, str] | None) -> dict[str, Any]:
    warmups = int(entry.get("warmups", 1))
    iterations = int(entry.get("iterations", 5))
    timeout_seconds = int(entry.get("timeout_seconds", 20))

    for _ in range(warmups):
        run_once(argv, repo_root, timeout_seconds, env=env)

    runs = []
    elapsed_values = []
    rss_values = []
    for _ in range(iterations):
        elapsed_ns, max_rss_kb = run_once(argv, repo_root, timeout_seconds, env=env)
        runs.append({
            "elapsed_ns": elapsed_ns,
            "max_rss_kb": max_rss_kb,
        })
        elapsed_values.append(elapsed_ns)
        rss_values.append(max_rss_kb)

    median_elapsed_ns = median_int(elapsed_values)
    median_max_rss_kb = median_int(rss_values)

    return {
        "warmups": warmups,
        "iterations": iterations,
        "timeout_seconds": timeout_seconds,
        "median_elapsed_ns": median_elapsed_ns,
        "median_max_rss_kb": median_max_rss_kb,
        "runs": runs,
    }


def run_benchmark(repo_root: Path, vigil_bin: Path, entry: dict[str, Any]) -> dict[str, Any]:
    argv = [str(vigil_bin)] + list(entry["command"])
    aot = run_variant(repo_root, argv, entry, env=None)
    no_aot_env = os.environ.copy()
    no_aot_env["VIGIL_NO_AOT"] = "1"
    interpreter = run_variant(repo_root, argv, entry, env=no_aot_env)

    print(
        f"{entry['name']:20s}  aot {format_ms(aot['median_elapsed_ns']):>8s} ms  interp {format_ms(interpreter['median_elapsed_ns']):>8s} ms",
        flush=True,
    )

    return {
        "name": entry["name"],
        "command": entry["command"],
        "aot": aot,
        "interpreter": interpreter,
    }


def main() -> int:
    args = parse_args()
    manifest_path = Path(args.manifest).resolve()
    repo_root = manifest_path.parent.parent
    vigil_bin = Path(args.vigil_bin).resolve()
    output_path = Path(args.output).resolve()

    if not vigil_bin.exists():
        raise FileNotFoundError(f"vigil binary not found: {vigil_bin}")

    manifest = load_manifest(manifest_path)
    selected = set(args.case)
    benchmarks = []
    for entry in manifest["benchmarks"]:
        if selected and entry["name"] not in selected:
            continue
        benchmarks.append(run_benchmark(repo_root, vigil_bin, entry))

    if selected:
        missing = sorted(selected - {entry["name"] for entry in manifest["benchmarks"]})
        if missing:
            raise ValueError(f"unknown benchmark case(s): {', '.join(missing)}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "manifest": str(manifest_path),
        "vigil_bin": str(vigil_bin),
        "benchmarks": benchmarks,
    }
    with output_path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
