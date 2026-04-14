#!/usr/bin/env python3
"""Run compiler/stdlib stress test suites.

For each test project (directory containing main.vigil) under each suite dir:
  1. vigil check main.vigil
  2. vigil run main.vigil  (capture stdout)
  3. vigil transpile . -o <work-dir>/c-<name>
  4. cmake configure + build the transpiled C project
  5. Run the C binary  (capture stdout)
  6. Compare interpreter vs C output (must match exactly)

Suites are processed in the order given. Within each suite, tests are
sorted alphabetically for deterministic execution.

Exit code is the number of failed tests (0 = all pass).
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def find_executable(build_dir, name):
    """Find the built executable inside a CMake build directory."""
    candidates = [
        os.path.join(build_dir, name),
        os.path.join(build_dir, f"{name}.exe"),
        os.path.join(build_dir, "Release", name),
        os.path.join(build_dir, "Release", f"{name}.exe"),
        os.path.join(build_dir, "Debug", name),
        os.path.join(build_dir, "Debug", f"{name}.exe"),
    ]
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    for root, _dirs, files in os.walk(build_dir):
        if "CMakeFiles" in root:
            continue
        for f in files:
            path = os.path.join(root, f)
            if os.access(path, os.X_OK) and not f.endswith((".cmake", ".txt")):
                return path
    return None


def run_test(vigil_bin, test_dir, work_dir):
    """Run a single test project. Returns (passed, detail)."""
    name = os.path.basename(test_dir)
    main_vigil = os.path.join(test_dir, "main.vigil")

    r = subprocess.run([vigil_bin, "check", main_vigil],
                       capture_output=True, text=True, timeout=30)
    if r.returncode != 0:
        return False, f"vigil check failed:\n{r.stderr}"

    r = subprocess.run([vigil_bin, "run", main_vigil],
                       capture_output=True, text=True, timeout=60, cwd=test_dir)
    if r.returncode != 0:
        return False, f"vigil run failed (exit {r.returncode}):\n{r.stderr}"
    interp_out = r.stdout

    c_dir = os.path.join(work_dir, f"c-{name}")
    if os.path.exists(c_dir):
        shutil.rmtree(c_dir)
    r = subprocess.run([vigil_bin, "transpile", test_dir, "-o", c_dir],
                       capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        return False, f"vigil transpile failed:\n{r.stderr}"

    build_dir = os.path.join(c_dir, "build")
    r = subprocess.run(["cmake", "-B", build_dir, "-S", c_dir,
                        "-DCMAKE_BUILD_TYPE=Release"],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return False, f"cmake configure failed:\n{r.stdout}\n{r.stderr}"

    r = subprocess.run(["cmake", "--build", build_dir, "--config", "Release"],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return False, f"cmake build failed:\n{r.stdout}\n{r.stderr}"

    exe = find_executable(build_dir, name)
    if exe is None:
        return False, f"no executable found in {build_dir}"

    r = subprocess.run([exe], capture_output=True, text=True, timeout=30)
    c_out = r.stdout

    interp_lines = interp_out.rstrip().splitlines()
    c_lines = c_out.rstrip().splitlines()
    if interp_lines != c_lines:
        detail = "output mismatch:\n--- interpreter\n"
        for line in interp_lines:
            detail += f"  {line}\n"
        detail += "+++ C binary\n"
        for line in c_lines:
            detail += f"  {line}\n"
        return False, detail

    return True, f"{len(interp_lines)} lines match"


def collect_tests(suite_dirs):
    """Collect test directories from all suites, in order."""
    tests = []
    for suite_dir in suite_dirs:
        suite_dir = os.path.abspath(suite_dir)
        entries = sorted(
            d for d in os.listdir(suite_dir)
            if os.path.isfile(os.path.join(suite_dir, d, "main.vigil"))
        )
        for d in entries:
            tests.append((os.path.basename(suite_dir), os.path.join(suite_dir, d)))
    return tests


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vigil-bin", required=True)
    parser.add_argument("--suite-dir", required=True, action="append",
                        help="Test suite directory (may be repeated; processed in order)")
    parser.add_argument("--work-dir", default=None)
    args = parser.parse_args()

    vigil_bin = os.path.abspath(args.vigil_bin)
    work_dir = args.work_dir or tempfile.mkdtemp(prefix="syntax_stress_")
    os.makedirs(work_dir, exist_ok=True)

    tests = collect_tests(args.suite_dir)
    if not tests:
        print("ERROR: no test projects found", file=sys.stderr)
        return 1

    passed = failed = 0
    current_suite = None

    for suite_name, test_dir in tests:
        if suite_name != current_suite:
            current_suite = suite_name
            print(f"\n── {suite_name} ──")

        name = os.path.basename(test_dir)
        try:
            ok, detail = run_test(vigil_bin, test_dir, work_dir)
        except subprocess.TimeoutExpired:
            ok, detail = False, "timeout"
        except Exception as e:
            ok, detail = False, str(e)

        if ok:
            passed += 1
            print(f"  PASS  {name}  ({detail})")
        else:
            failed += 1
            print(f"  FAIL  {name}")
            for line in detail.strip().splitlines():
                print(f"        {line}")

    print(f"\n{passed} passed, {failed} failed out of {len(tests)}")
    return failed


if __name__ == "__main__":
    sys.exit(main())
