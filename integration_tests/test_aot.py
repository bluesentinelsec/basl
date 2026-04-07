#!/usr/bin/env python3
"""Integration tests for CLI AOT controls."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def write_program(tmpdir: str) -> Path:
    path = Path(tmpdir) / "main.vigil"
    path.write_text(
        "fn fib(i32 n) -> i32 {\n"
        "    if n < 2 { return n }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "fn main() -> i32 { return fib(8) - 21 }\n",
        encoding="utf-8",
    )
    return path


class AotCliTest(unittest.TestCase):
    def test_run_no_aot_flag(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_aot_flag_") as tmpdir:
            program = write_program(tmpdir)
            result = subprocess.run(
                [VIGIL_BIN, "run", "--no-aot", str(program)],
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_run_no_aot_env(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_aot_env_") as tmpdir:
            program = write_program(tmpdir)
            env = os.environ.copy()
            env["VIGIL_NO_AOT"] = "1"
            result = subprocess.run(
                [VIGIL_BIN, "run", str(program)],
                capture_output=True,
                text=True,
                timeout=10,
                env=env,
            )
            self.assertEqual(result.returncode, 0, msg=result.stderr)


if __name__ == "__main__":
    unittest.main()
