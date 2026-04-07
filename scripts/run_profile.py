#!/usr/bin/env python3
"""Run VIGIL benchmarks under gprof (check_*) or perf+flamegraph (run_*)."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vigil-bin", required=True, help="Path to the vigil binary")
    parser.add_argument("--manifest", required=True, help="Path to benchmarks/manifest.json")
    parser.add_argument("--output", required=True, help="Directory to write profiling artifacts into")
    parser.add_argument("--flamegraph-dir", default=None, help="Path to FlameGraph repo (for perf profiling)")
    return parser.parse_args()


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if data.get("version") != 1:
        raise ValueError(f"unsupported manifest version in {path}")
    return data


def require_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise RuntimeError(f"required tool not found on PATH: {name}")


def profile_one_gprof(
    repo_root: Path,
    vigil_bin: Path,
    entry: dict[str, Any],
    output_dir: Path,
) -> None:
    name: str = entry["name"]
    argv = [str(vigil_bin)] + list(entry["command"])
    timeout = int(entry.get("timeout_seconds", 20))

    gmon_path = repo_root / "gmon.out"
    gmon_path.unlink(missing_ok=True)

    print(f"[profile/gprof] running: {name}", flush=True)
    subprocess.run(
        argv,
        cwd=str(repo_root),
        stdin=subprocess.DEVNULL,
        capture_output=True,
        timeout=timeout,
        check=True,
    )

    if not gmon_path.exists():
        print(f"[profile/gprof] WARNING: gmon.out not produced for {name}", flush=True)
        return

    gmon_dest = output_dir / f"gmon_{name}.out"
    shutil.move(str(gmon_path), str(gmon_dest))

    flat_path = output_dir / f"{name}_flat.txt"
    with flat_path.open("w", encoding="utf-8") as out:
        subprocess.run(
            ["gprof", "-b", str(vigil_bin), str(gmon_dest)],
            stdout=out,
            check=True,
        )
    print(f"[profile/gprof]   flat profile  -> {flat_path.name}", flush=True)

    gprof_result = subprocess.run(
        ["gprof", str(vigil_bin), str(gmon_dest)],
        capture_output=True,
        text=True,
        check=True,
    )
    dot_result = subprocess.run(
        ["gprof2dot"],
        input=gprof_result.stdout,
        capture_output=True,
        text=True,
    )
    if dot_result.returncode != 0:
        print(f"[profile/gprof]   WARNING: gprof2dot failed for {name}", flush=True)
        return

    svg_result = subprocess.run(
        ["dot", "-Tsvg"],
        input=dot_result.stdout,
        capture_output=True,
        text=True,
    )
    if svg_result.returncode != 0:
        print(f"[profile/gprof]   WARNING: dot failed for {name}", flush=True)
        return

    svg_path = output_dir / f"{name}_callgraph.svg"
    svg_path.write_text(svg_result.stdout, encoding="utf-8")
    print(f"[profile/gprof]   call graph    -> {svg_path.name}", flush=True)


def profile_one_perf(
    repo_root: Path,
    vigil_bin: Path,
    entry: dict[str, Any],
    output_dir: Path,
    flamegraph_dir: Path,
) -> None:
    name: str = entry["name"]
    iterations = int(entry.get("profile_iterations", 10))
    timeout = int(entry.get("timeout_seconds", 20)) * iterations + 30

    # Build a shell command that runs the benchmark N times.
    argv = [str(vigil_bin)] + list(entry["command"])
    inner_cmd = " ".join(argv)
    loop_cmd = f"for i in $(seq 1 {iterations}); do {inner_cmd}; done"

    perf_data = output_dir / f"{name}_perf.data"

    print(f"[profile/perf] running: {name} ({iterations} iterations)", flush=True)
    subprocess.run(
        ["perf", "record", "-F", "999", "-g", "--call-graph", "fp",
         "-o", str(perf_data), "--", "sh", "-c", loop_cmd],
        cwd=str(repo_root),
        stdin=subprocess.DEVNULL,
        timeout=timeout,
        check=True,
    )

    # Flamegraph SVG.
    stackcollapse = flamegraph_dir / "stackcollapse-perf.pl"
    flamegraph_pl = flamegraph_dir / "flamegraph.pl"

    perf_script = subprocess.run(
        ["perf", "script", "-i", str(perf_data)],
        capture_output=True,
        text=True,
        check=True,
    )
    collapsed = subprocess.run(
        ["perl", str(stackcollapse)],
        input=perf_script.stdout,
        capture_output=True,
        text=True,
        check=True,
    )
    svg = subprocess.run(
        ["perl", str(flamegraph_pl)],
        input=collapsed.stdout,
        capture_output=True,
        text=True,
        check=True,
    )
    svg_path = output_dir / f"{name}_flamegraph.svg"
    svg_path.write_text(svg.stdout, encoding="utf-8")
    print(f"[profile/perf]   flamegraph    -> {svg_path.name}", flush=True)

    # perf annotate for the VM dispatch function.
    annotate_path = output_dir / f"{name}_annotate.txt"
    with annotate_path.open("w", encoding="utf-8") as out:
        subprocess.run(
            ["perf", "annotate", "--stdio", "-i", str(perf_data),
             "--symbol=vigil_regvm_execute"],
            stdout=out,
            stderr=subprocess.DEVNULL,
        )
    print(f"[profile/perf]   annotate      -> {annotate_path.name}", flush=True)

    # Clean up large perf.data file.
    perf_data.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()

    manifest_path = Path(args.manifest).resolve()
    repo_root = manifest_path.parent.parent
    vigil_bin = Path(args.vigil_bin).resolve()
    output_dir = Path(args.output).resolve()
    flamegraph_dir = Path(args.flamegraph_dir).resolve() if args.flamegraph_dir else None

    if not vigil_bin.exists():
        raise FileNotFoundError(f"vigil binary not found: {vigil_bin}")

    output_dir.mkdir(parents=True, exist_ok=True)

    manifest = load_manifest(manifest_path)

    has_gprof = all(shutil.which(t) for t in ("gprof", "gprof2dot", "dot"))
    has_perf = shutil.which("perf") is not None and flamegraph_dir is not None

    for entry in manifest["benchmarks"]:
        name = entry["name"]
        if name.startswith("run_"):
            if has_perf:
                profile_one_perf(repo_root, vigil_bin, entry, output_dir, flamegraph_dir)
            else:
                print(f"[profile] SKIP {name}: perf or flamegraph not available", flush=True)
        elif name.startswith("check_"):
            if has_gprof:
                profile_one_gprof(repo_root, vigil_bin, entry, output_dir)
            else:
                print(f"[profile] SKIP {name}: gprof tools not available", flush=True)
        else:
            print(f"[profile] SKIP {name}: unknown prefix", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
