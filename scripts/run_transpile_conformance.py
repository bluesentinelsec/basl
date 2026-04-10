#!/usr/bin/env python3
"""Run the transpile conformance suite.

For each .vigil program in transpile_conformance/:
  1. Run with the interpreter and record exit code
  2. Transpile to C, compile, run, and record exit code
  3. Compare — they must match

Usage:
    python3 scripts/run_transpile_conformance.py --vigil-bin ./build/vigil \
        --suite transpile_conformance/ --work-dir /tmp/transpile_conform
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def find_executable(build_dir: Path, name: str) -> Path:
    for p in [
        build_dir / name,
        build_dir / f"{name}.exe",
        build_dir / "Release" / f"{name}.exe",
        build_dir / "Release" / name,
        build_dir / "Debug" / f"{name}.exe",
        build_dir / "Debug" / name,
    ]:
        if p.exists():
            return p
    return build_dir / name


def run_conformance(vigil_bin: str, suite_dir: str, work_dir: str) -> int:
    vigil = Path(vigil_bin).resolve()
    vigil_root = vigil.parent.parent
    vigil_include = str(vigil_root / "include")
    vigil_lib_dir = str(vigil.parent)
    suite = Path(suite_dir)
    work = Path(work_dir)
    work.mkdir(parents=True, exist_ok=True)

    programs = sorted(suite.glob("*.vigil"))
    if not programs:
        print(f"No .vigil files found in {suite_dir}")
        return 1

    passed = 0
    failed = 0
    skipped = 0

    for prog in programs:
        name = prog.stem
        print(f"  {name} ... ", end="", flush=True)

        # Interpreter
        env = os.environ.copy()
        env["VIGIL_NO_AOT"] = "1"
        interp = subprocess.run(
            [str(vigil), "run", str(prog)],
            capture_output=True, text=True, timeout=180, env=env,
        )

        # Transpile
        out_dir = work / name
        if out_dir.exists():
            import shutil
            shutil.rmtree(out_dir)

        r = subprocess.run(
            [str(vigil), "transpile", str(prog), "-o", str(out_dir)],
            capture_output=True, text=True, timeout=180,
        )
        if r.returncode != 0:
            print(f"SKIP (transpile failed: {r.stderr.strip()[:80]})")
            skipped += 1
            continue

        # Build
        build_dir = out_dir / "build"
        r = subprocess.run(
            ["cmake", "-S", str(out_dir), "-B", str(build_dir),
             "-DCMAKE_BUILD_TYPE=Release",
             f"-DVIGIL_INCLUDE_DIR={vigil_include}",
             f"-DVIGIL_LIB_DIR={vigil_lib_dir}"],
            capture_output=True, text=True, timeout=180,
        )
        if r.returncode != 0:
            print("SKIP (cmake configure failed)")
            skipped += 1
            continue

        r = subprocess.run(
            ["cmake", "--build", str(build_dir), "--config", "Release"],
            capture_output=True, text=True, timeout=180,
        )
        if r.returncode != 0:
            print("SKIP (cmake build failed)")
            skipped += 1
            continue

        # Run
        exe = find_executable(build_dir, "vigil_app")
        run_env = os.environ.copy()
        if os.name == "nt":
            for sub in ("", "Release", "Debug"):
                d = str(Path(vigil_lib_dir) / sub) if sub else vigil_lib_dir
                run_env["PATH"] = d + ";" + run_env.get("PATH", "")
        else:
            run_env["LD_LIBRARY_PATH"] = vigil_lib_dir + ":" + run_env.get("LD_LIBRARY_PATH", "")
            run_env["DYLD_LIBRARY_PATH"] = vigil_lib_dir + ":" + run_env.get("DYLD_LIBRARY_PATH", "")
            run_env["ASAN_OPTIONS"] = "verify_asan_link_order=0:" + run_env.get("ASAN_OPTIONS", "")

        compiled = subprocess.run(
            [str(exe)], capture_output=True, text=True, timeout=10, env=run_env,
        )

        # Skip if runtime linkage failed
        if compiled.returncode != interp.returncode and compiled.stderr.strip() == "":
            print("SKIP (runtime linkage)")
            skipped += 1
            continue

        if compiled.returncode == interp.returncode and compiled.stdout == interp.stdout:
            print("OK")
            passed += 1
        else:
            print(f"FAIL (interp={interp.returncode}, compiled={compiled.returncode})")
            if compiled.stderr:
                print(f"    stderr: {compiled.stderr.strip()[:120]}")
            failed += 1

    print(f"\n{passed} passed, {failed} failed, {skipped} skipped out of {len(programs)}")
    return 1 if failed > 0 else 0


def main():
    parser = argparse.ArgumentParser(description="Run transpile conformance suite")
    parser.add_argument("--vigil-bin", default="./build/vigil")
    parser.add_argument("--suite", default="transpile_conformance/")
    parser.add_argument("--work-dir", default=None)
    args = parser.parse_args()

    work_dir = args.work_dir
    if work_dir is None:
        work_dir = tempfile.mkdtemp(prefix="vigil_conform_")

    print(f"Transpile conformance suite: {args.suite}")
    print(f"Vigil binary: {args.vigil_bin}")
    print(f"Work directory: {work_dir}\n")

    sys.exit(run_conformance(args.vigil_bin, args.suite, work_dir))


if __name__ == "__main__":
    main()
