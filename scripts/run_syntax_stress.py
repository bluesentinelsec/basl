#!/usr/bin/env python3
"""Run compiler/stdlib stress test suites.

For each test project (directory containing main.vigil) under each suite dir:
  1. vigil check main.vigil
  2. vigil run main.vigil  (capture stdout)
  3. vigil transpile . -o <work-dir>/c-<name>

Then, per suite, all transpiled projects are built in a single cmake
invocation (sharing the vigil_rt library) and each binary is run and
compared against the interpreter output.

Use -j N to run tests in parallel (default: sequential).

Exit code is the number of failed tests (0 = all pass).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed


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
    exe_name = f"{target_name}.exe" if sys.platform == "win32" else target_name
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


# ── Phase 1: interpreter check + run (parallelisable per test) ────

def run_interpreter(vigil_bin, test_dir):
    """Check and run a test in the interpreter. Returns (ok, interp_stdout | error_detail)."""
    main_vigil = os.path.join(test_dir, "main.vigil")

    r = subprocess.run([vigil_bin, "check", main_vigil],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=30)
    if r.returncode != 0:
        return False, f"vigil check failed:\n{r.stderr}"

    r = subprocess.run([vigil_bin, "run", main_vigil],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=60, cwd=test_dir)
    if r.returncode != 0:
        return False, f"vigil run failed (exit {r.returncode}):\n{r.stderr}"
    return True, r.stdout


# ── Phase 2: transpile (parallelisable per test) ─────────────────

def transpile_test(vigil_bin, test_dir, c_dir):
    """Transpile a single test. Returns (ok, error_detail | None)."""
    if os.path.exists(c_dir):
        shutil.rmtree(c_dir)
    r = subprocess.run([vigil_bin, "transpile", test_dir, "-o", c_dir],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=60)
    if r.returncode != 0:
        return False, f"vigil transpile failed:\n{r.stderr}"
    return True, None


# ── Phase 3: batched cmake build (one per suite) ─────────────────

def generate_batched_cmake(suite_work_dir, test_c_dirs):
    """Generate a wrapper CMakeLists.txt that builds vigil_rt once and all
    test executables as separate targets sharing it.

    test_c_dirs: list of (test_name, c_dir) tuples
    Returns the path to the wrapper project directory.
    """
    batch_dir = os.path.join(suite_work_dir, "_batch")
    os.makedirs(batch_dir, exist_ok=True)

    # Use the vigil_rt from the first transpiled project (they're all identical)
    first_c_dir = test_c_dirs[0][1]
    rt_src = os.path.join(first_c_dir, "vigil_rt")
    rt_dst = os.path.join(batch_dir, "vigil_rt")
    if os.path.exists(rt_dst):
        shutil.rmtree(rt_dst)
    shutil.copytree(rt_src, rt_dst)

    # Copy each test's generated files into subdirectories
    test_targets = []
    for test_name, c_dir in test_c_dirs:
        safe_name = re.sub(r"[^a-zA-Z0-9_]", "_", test_name)
        test_sub = os.path.join(batch_dir, safe_name)
        os.makedirs(test_sub, exist_ok=True)
        for f in ("vigil_generated.c", "vigil_generated.h", "vigil_main.c"):
            src = os.path.join(c_dir, f)
            if os.path.exists(src):
                shutil.copy2(src, test_sub)
        test_targets.append((test_name, safe_name))

    # Read the original CMakeLists.txt and strip only the app executable lines
    orig_cmake = os.path.join(first_c_dir, "CMakeLists.txt")
    with open(orig_cmake, "r", encoding="utf-8") as f:
        orig_lines = f.readlines()

    # Keep everything except the main executable target lines
    rt_lines = []
    skip_app = False
    for line in orig_lines:
        stripped = line.strip()
        if stripped.startswith("# Main executable"):
            skip_app = True
            continue
        if skip_app:
            # Skip add_executable and target_ lines for vigil_app
            if stripped.startswith("add_executable(vigil_app") or \
               stripped.startswith("target_link_libraries(vigil_app") or \
               stripped.startswith("target_include_directories(vigil_app"):
                continue
            # Resume after the app block (next comment or blank after app lines)
            if stripped.startswith("#") or (stripped == "" and rt_lines and rt_lines[-1].strip().startswith("target_")):
                skip_app = False
            elif stripped == "":
                skip_app = False
        rt_lines.append(line)

    rt_cmake = "".join(rt_lines)

    # Generate the wrapper CMakeLists.txt
    lines = [rt_cmake, "", "# ── Batched test executables ──", ""]
    for test_name, safe_name in test_targets:
        lines.append(f"add_executable({safe_name} {safe_name}/vigil_main.c {safe_name}/vigil_generated.c)")
        lines.append(f"target_link_libraries({safe_name} PRIVATE vigil_rt)")
        lines.append(f"target_include_directories({safe_name} PRIVATE vigil_rt/include vigil_rt/src)")
        lines.append("")

    cmake_path = os.path.join(batch_dir, "CMakeLists.txt")
    with open(cmake_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    return batch_dir, test_targets


def build_batched(batch_dir):
    """Run cmake configure + build on the batched project. Returns (ok, error_detail)."""
    build_dir = os.path.join(batch_dir, "build")
    r = subprocess.run(["cmake", "-B", build_dir, "-S", batch_dir,
                        "-DCMAKE_BUILD_TYPE=Release"],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=300)
    if r.returncode != 0:
        return False, f"cmake configure failed:\n{r.stdout}\n{r.stderr}"

    r = subprocess.run(["cmake", "--build", build_dir, "--config", "Release",
                        "--parallel", "4"],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=300)
    if r.returncode != 0:
        return False, f"cmake build failed:\n{r.stdout}\n{r.stderr}"

    return True, build_dir


# ── Phase 4: run C binaries and compare ──────────────────────────

def compare_output(interp_out, c_out):
    """Compare interpreter and C binary output. Returns (match, detail)."""
    interp_lines = interp_out.rstrip().splitlines()
    c_lines = c_out.rstrip().splitlines()
    if interp_lines == c_lines:
        return True, f"{len(interp_lines)} lines match"
    detail = "output mismatch:\n--- interpreter\n"
    for line in interp_lines:
        detail += f"  {line}\n"
    detail += "+++ C binary\n"
    for line in c_lines:
        detail += f"  {line}\n"
    return False, detail


# ── Test collection ──────────────────────────────────────────────

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


def collect_suites(suite_dirs):
    """Group tests by suite. Returns {suite_name: [(test_name, test_dir), ...]}."""
    suites = {}
    for suite_dir in suite_dirs:
        suite_dir = os.path.abspath(suite_dir)
        suite_name = os.path.basename(suite_dir)
        entries = sorted(
            d for d in os.listdir(suite_dir)
            if os.path.isfile(os.path.join(suite_dir, d, "main.vigil"))
        )
        suites[suite_name] = [(d, os.path.join(suite_dir, d)) for d in entries]
    return suites


# ── Main orchestration ───────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vigil-bin", required=True)
    parser.add_argument("--suite-dir", required=True, action="append",
                        help="Test suite directory (may be repeated; processed in order)")
    parser.add_argument("--work-dir", default=None)
    parser.add_argument("-j", "--jobs", type=int, default=1,
                        help="Number of parallel test workers (default: 1, sequential)")
    args = parser.parse_args()

    vigil_bin = os.path.abspath(args.vigil_bin)
    work_dir = args.work_dir or tempfile.mkdtemp(prefix="syntax_stress_")
    os.makedirs(work_dir, exist_ok=True)

    suites = collect_suites(args.suite_dir)
    total_tests = sum(len(tests) for tests in suites.values())
    if total_tests == 0:
        print("ERROR: no test projects found", file=sys.stderr)
        return 1

    print(f"Running {total_tests} tests across {len(suites)} suites (batched cmake, -j {args.jobs})")

    passed = 0
    failed = 0

    for suite_name, tests in suites.items():
        print(f"\n-- {suite_name} --")
        suite_work = os.path.join(work_dir, suite_name)
        os.makedirs(suite_work, exist_ok=True)

        # Phase 1 + 2: interpreter run + transpile (parallel within suite)
        interp_results = {}  # test_name -> (ok, stdout)
        transpile_ok = {}    # test_name -> (ok, c_dir)

        def do_test(test_name, test_dir):
            iok, iout = run_interpreter(vigil_bin, test_dir)
            c_dir = os.path.join(suite_work, f"c-{test_name}")
            if iok:
                tok, terr = transpile_test(vigil_bin, test_dir, c_dir)
            else:
                tok, terr = False, "skipped (interpreter failed)"
            return test_name, iok, iout, tok, terr, c_dir

        if args.jobs > 1:
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = [pool.submit(do_test, tn, td) for tn, td in tests]
                for f in as_completed(futures):
                    tn, iok, iout, tok, terr, c_dir = f.result()
                    interp_results[tn] = (iok, iout)
                    transpile_ok[tn] = (tok, c_dir, terr)
        else:
            for test_name, test_dir in tests:
                tn, iok, iout, tok, terr, c_dir = do_test(test_name, test_dir)
                interp_results[tn] = (iok, iout)
                transpile_ok[tn] = (tok, c_dir, terr)

        # Collect successfully transpiled tests for batched build
        batch_tests = []
        for test_name, _ in tests:
            tok, c_dir, _ = transpile_ok[test_name]
            if tok:
                batch_tests.append((test_name, c_dir))

        # Phase 3: batched cmake build
        build_dir = None
        batch_targets = {}
        build_ok = False
        build_err = ""
        if batch_tests:
            batch_dir, test_targets = generate_batched_cmake(suite_work, batch_tests)
            build_ok, result = build_batched(batch_dir)
            if build_ok:
                build_dir = result
                batch_targets = {tn: safe for tn, safe in test_targets}
            else:
                build_err = result

        # Phase 4: run C binaries and compare (in test order)
        for test_name, _ in tests:
            iok, iout = interp_results[test_name]
            if not iok:
                failed += 1
                print(f"  FAIL  {test_name}")
                for line in iout.strip().splitlines():
                    print(f"        {line}")
                continue

            tok, c_dir, terr = transpile_ok[test_name]
            if not tok:
                failed += 1
                print(f"  FAIL  {test_name}")
                print(f"        {terr}")
                continue

            if not build_ok:
                failed += 1
                print(f"  FAIL  {test_name}")
                print(f"        batched cmake build failed")
                continue

            safe_name = batch_targets.get(test_name)
            if not safe_name:
                failed += 1
                print(f"  FAIL  {test_name}")
                print(f"        no build target")
                continue

            exe = find_executable(build_dir, safe_name)
            if exe is None:
                failed += 1
                print(f"  FAIL  {test_name}")
                print(f"        executable not found for {safe_name}")
                continue

            try:
                r = subprocess.run([exe], capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=30)
                c_out = r.stdout
            except subprocess.TimeoutExpired:
                failed += 1
                print(f"  FAIL  {test_name}")
                print(f"        C binary timeout")
                continue
            except Exception as e:
                failed += 1
                print(f"  FAIL  {test_name}")
                print(f"        C binary error: {e}")
                continue

            match, detail = compare_output(iout, c_out)
            if match:
                passed += 1
                print(f"  PASS  {test_name}  ({detail})")
            else:
                failed += 1
                print(f"  FAIL  {test_name}")
                for line in detail.strip().splitlines():
                    print(f"        {line}")

    print(f"\n{passed} passed, {failed} failed out of {total_tests}")
    return min(failed, 255)


if __name__ == "__main__":
    sys.exit(main())
