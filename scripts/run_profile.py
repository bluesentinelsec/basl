#!/usr/bin/env python3
"""Run VIGIL benchmarks under gprof and emit flat profiles and call-graph SVGs."""

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
    parser.add_argument("--vigil-bin", required=True, help="Path to the gprof-instrumented vigil binary")
    parser.add_argument("--manifest", required=True, help="Path to benchmarks/manifest.json")
    parser.add_argument("--output", required=True, help="Directory to write profiling artifacts into")
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


def profile_one(
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

    print(f"[profile] running: {name}", flush=True)
    subprocess.run(
        argv,
        cwd=str(repo_root),
        stdin=subprocess.DEVNULL,
        capture_output=True,
        timeout=timeout,
        check=True,
    )

    if not gmon_path.exists():
        print(f"[profile] WARNING: gmon.out not produced for {name} — binary may not be -pg instrumented", flush=True)
        return

    gmon_dest = output_dir / f"gmon_{name}.out"
    shutil.move(str(gmon_path), str(gmon_dest))

    # Flat profile: functions ranked by self and cumulative time.
    flat_path = output_dir / f"{name}_flat.txt"
    with flat_path.open("w", encoding="utf-8") as out:
        subprocess.run(
            ["gprof", "-b", str(vigil_bin), str(gmon_dest)],
            stdout=out,
            check=True,
        )
    print(f"[profile]   flat profile  -> {flat_path.name}", flush=True)

    # Call graph SVG: gprof output piped through gprof2dot then dot.
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
        print(f"[profile]   WARNING: gprof2dot failed for {name}", flush=True)
        return

    svg_result = subprocess.run(
        ["dot", "-Tsvg"],
        input=dot_result.stdout,
        capture_output=True,
        text=True,
    )
    if svg_result.returncode != 0:
        print(f"[profile]   WARNING: dot failed for {name}", flush=True)
        return

    svg_path = output_dir / f"{name}_callgraph.svg"
    svg_path.write_text(svg_result.stdout, encoding="utf-8")
    print(f"[profile]   call graph    -> {svg_path.name}", flush=True)


def main() -> int:
    args = parse_args()

    for tool in ("gprof", "gprof2dot", "dot"):
        require_tool(tool)

    manifest_path = Path(args.manifest).resolve()
    repo_root = manifest_path.parent.parent
    vigil_bin = Path(args.vigil_bin).resolve()
    output_dir = Path(args.output).resolve()

    if not vigil_bin.exists():
        raise FileNotFoundError(f"vigil binary not found: {vigil_bin}")

    output_dir.mkdir(parents=True, exist_ok=True)

    manifest = load_manifest(manifest_path)
    for entry in manifest["benchmarks"]:
        profile_one(repo_root, vigil_bin, entry, output_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
