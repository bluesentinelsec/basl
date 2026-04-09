#!/usr/bin/env python3
"""Integration tests for vigil transpile command."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(args, **kwargs):
    return subprocess.run(
        [VIGIL_BIN] + args, capture_output=True, text=True, timeout=30, **kwargs
    )


def find_executable(build_dir: Path, name: str) -> Path:
    """Find the built executable, handling MSVC multi-config output dirs."""
    direct = build_dir / name
    if direct.exists():
        return direct
    # Windows: .exe extension
    direct_exe = build_dir / f"{name}.exe"
    if direct_exe.exists():
        return direct_exe
    # MSVC multi-config: Release/ or Debug/ subdirectory
    for sub in ("Release", "Debug", "RelWithDebInfo", "MinSizeRel"):
        p = build_dir / sub / f"{name}.exe"
        if p.exists():
            return p
        p = build_dir / sub / name
        if p.exists():
            return p
    return direct  # fallback


def transpile_compile_run(source: str, tmpdir: str) -> subprocess.CompletedProcess:
    """Transpile a Vigil program, compile the generated C, and run it."""
    src = Path(tmpdir) / "main.vigil"
    src.write_text(source, encoding="utf-8")
    out_dir = Path(tmpdir) / "c_out"

    # Transpile
    r = run_vigil(["transpile", str(src), "-o", str(out_dir)])
    if r.returncode != 0:
        return r

    # Determine vigil project root for include/lib paths
    vigil_bin = Path(VIGIL_BIN).resolve()
    vigil_root = vigil_bin.parent.parent  # build/vigil -> project root
    vigil_include = str(vigil_root / "include")
    vigil_lib_dir = str(vigil_bin.parent)

    # Build generated C
    build_dir = out_dir / "build"
    r = subprocess.run(
        ["cmake", "-S", str(out_dir), "-B", str(build_dir),
         "-DCMAKE_BUILD_TYPE=Release",
         f"-DVIGIL_INCLUDE_DIR={vigil_include}",
         f"-DVIGIL_LIB_DIR={vigil_lib_dir}"],
        capture_output=True, text=True, timeout=30,
    )
    if r.returncode != 0:
        return r

    r = subprocess.run(
        ["cmake", "--build", str(build_dir), "--config", "Release"],
        capture_output=True, text=True, timeout=30,
    )
    if r.returncode != 0:
        return r

    # Run — set library path for shared lib
    env = os.environ.copy()
    if os.name == "nt":
        env["PATH"] = vigil_lib_dir + ";" + env.get("PATH", "")
    else:
        env["LD_LIBRARY_PATH"] = vigil_lib_dir + ":" + env.get("LD_LIBRARY_PATH", "")
        env["DYLD_LIBRARY_PATH"] = vigil_lib_dir + ":" + env.get("DYLD_LIBRARY_PATH", "")
        # Suppress ASAN issues when linking against ASAN-instrumented libvigil
        env["ASAN_OPTIONS"] = "verify_asan_link_order=0:" + env.get("ASAN_OPTIONS", "")

    exe = find_executable(build_dir, "vigil_app")
    return subprocess.run(
        [str(exe)],
        capture_output=True, text=True, timeout=10,
        env=env,
    )


class TranspileCliTest(unittest.TestCase):
    def test_help(self) -> None:
        r = run_vigil(["transpile", "--help"])
        self.assertEqual(r.returncode, 0)
        self.assertIn("transpile", r.stdout.lower())

    def test_missing_input(self) -> None:
        r = run_vigil(["transpile"])
        self.assertEqual(r.returncode, 0)  # shows help

    def test_missing_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_transpile_") as tmpdir:
            src = Path(tmpdir) / "main.vigil"
            src.write_text("fn main() -> i32 { return 0 }\n", encoding="utf-8")
            r = run_vigil(["transpile", str(src)])
            self.assertNotEqual(r.returncode, 0)

    def test_generates_files(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_transpile_") as tmpdir:
            src = Path(tmpdir) / "main.vigil"
            src.write_text("fn main() -> i32 { return 0 }\n", encoding="utf-8")
            out_dir = Path(tmpdir) / "c_out"
            r = run_vigil(["transpile", str(src), "-o", str(out_dir)])
            self.assertEqual(r.returncode, 0, msg=r.stderr)
            self.assertTrue((out_dir / "vigil_generated.c").exists())
            self.assertTrue((out_dir / "vigil_generated.h").exists())
            self.assertTrue((out_dir / "vigil_main.c").exists())
            self.assertTrue((out_dir / "CMakeLists.txt").exists())


class TranspileConformanceTest(unittest.TestCase):
    """Run programs through both interpreter and transpile-compile-run, compare exit codes."""

    def _conformance(self, source: str, expected_exit: int = 0) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_conform_") as tmpdir:
            src = Path(tmpdir) / "main.vigil"
            src.write_text(source, encoding="utf-8")

            # Interpreter
            env = os.environ.copy()
            env["VIGIL_NO_AOT"] = "1"
            interp = run_vigil(["run", str(src)], env=env)
            self.assertEqual(interp.returncode, expected_exit,
                             msg=f"Interpreter: {interp.stderr}")

            # Transpile-compile-run
            compiled = transpile_compile_run(source, tmpdir)
            self.assertEqual(compiled.returncode, expected_exit,
                             msg=f"Compiled: {compiled.stderr}")

    def test_return_constant(self) -> None:
        self._conformance("fn main() -> i32 { return 0 }\n")

    def test_return_42(self) -> None:
        self._conformance("fn main() -> i32 { return 42 }\n", expected_exit=42)

    def test_fibonacci(self) -> None:
        self._conformance(
            "fn fib(i32 n) -> i32 {\n"
            "    if n < 2 { return n }\n"
            "    return fib(n - 1) + fib(n - 2)\n"
            "}\n"
            "fn main() -> i32 { return fib(10) - 55 }\n"
        )

    def test_arithmetic(self) -> None:
        self._conformance(
            "fn main() -> i32 {\n"
            "    i32 a = 10\n"
            "    i32 b = 3\n"
            "    return (a + b) - 13\n"
            "}\n"
        )

    def test_recursive_function(self) -> None:
        self._conformance(
            "fn sum(i32 n) -> i32 {\n"
            "    if n < 1 { return 0 }\n"
            "    return n + sum(n - 1)\n"
            "}\n"
            "fn main() -> i32 { return sum(10) - 55 }\n"
        )

    def test_string_println(self) -> None:
        """Transpiled program using fmt.println with a string constant."""
        with tempfile.TemporaryDirectory(prefix="vigil_conform_") as tmpdir:
            source = (
                'import "fmt"\n'
                "fn main() -> i32 {\n"
                '    fmt.println("hello")\n'
                "    return 0\n"
                "}\n"
            )
            src = Path(tmpdir) / "main.vigil"
            src.write_text(source, encoding="utf-8")

            # Interpreter
            env = os.environ.copy()
            env["VIGIL_NO_AOT"] = "1"
            interp = run_vigil(["run", str(src)], env=env)
            self.assertEqual(interp.returncode, 0, msg=interp.stderr)
            self.assertEqual(interp.stdout.strip(), "hello")

            # Transpile-compile-run
            compiled = transpile_compile_run(source, tmpdir)
            self.assertEqual(compiled.returncode, 0,
                             msg=f"Compiled: {compiled.stderr}")
            self.assertEqual(compiled.stdout.strip(), "hello")


if __name__ == "__main__":
    unittest.main()
