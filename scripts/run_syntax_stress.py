#!/usr/bin/env python3
"""Run the compiler syntax stress test suite.

For each test project under --suite-dir:
  1. vigil check main.vigil
  2. vigil run main.vigil  (capture stdout)
  3. vigil transpile . -o <work-dir>/c-<name>
  4. cmake configure + build the transpiled C project
  5. Run the C binary  (capture stdout)
  6. Compare interpreter vs C output (must match exactly)

Exit code is the number of failed tests (0 = all pass).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile


def cmake_parallelism():
    """Return a stable parallel job count for CMake builds."""
    cpu_count = os.cpu_count()
    if cpu_count is None or cpu_count < 1:
        return "1"
    return str(cpu_count)


def transpiled_executable_target(c_dir):
    """Read the generated CMakeLists.txt to determine the app target name."""
    cmake_lists = os.path.join(c_dir, "CMakeLists.txt")
    with open(cmake_lists, "r", encoding="utf-8") as f:
        content = f.read()
    match = re.search(r"add_executable\(\s*([A-Za-z0-9_+-]+)\b", content)
    if match is None:
        raise ValueError(f"no add_executable target found in {cmake_lists}")
    return match.group(1)


def find_executable(build_dir, target_name):
    """Find the built executable for a known CMake target."""
    exe_name = f"{target_name}.exe" if os.name == "nt" else target_name
    candidates = [
        os.path.join(build_dir, exe_name),
        os.path.join(build_dir, "Release", exe_name),
        os.path.join(build_dir, "Debug", exe_name),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c

    for root, _dirs, files in os.walk(build_dir):
        if "CMakeFiles" in root:
            continue
        for f in files:
            if f == exe_name:
                return os.path.join(root, f)
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

    r = subprocess.run(["cmake", "--build", build_dir, "--config", "Release",
                        "--parallel", cmake_parallelism()],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return False, f"cmake build failed:\n{r.stdout}\n{r.stderr}"

    target_name = transpiled_executable_target(c_dir)
    exe = find_executable(build_dir, target_name)
    if exe is None:
        return False, f"no executable found for target {target_name} in {build_dir}"

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vigil-bin", required=True)
    parser.add_argument("--suite-dir", required=True)
    parser.add_argument("--work-dir", default=None)
    args = parser.parse_args()

    vigil_bin = os.path.abspath(args.vigil_bin)
    suite_dir = os.path.abspath(args.suite_dir)
    work_dir = args.work_dir or tempfile.mkdtemp(prefix="syntax_stress_")
    os.makedirs(work_dir, exist_ok=True)

    test_dirs = sorted(
        os.path.join(suite_dir, d)
        for d in os.listdir(suite_dir)
        if os.path.isfile(os.path.join(suite_dir, d, "main.vigil"))
    )
    if not test_dirs:
        print(f"ERROR: no test projects found in {suite_dir}", file=sys.stderr)
        return 1

    passed = failed = 0
    failures = []

    for test_dir in test_dirs:
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
            failures.append((name, detail))
            print(f"  FAIL  {name}")
            for line in detail.strip().splitlines():
                print(f"        {line}")

    print(f"\n{passed} passed, {failed} failed out of {len(test_dirs)}")
    return failed


if __name__ == "__main__":
    sys.exit(main())
