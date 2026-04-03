#!/usr/bin/env python3
"""Integration tests for disabled stdlib module diagnostics."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def disabled_modules() -> list[str]:
    raw = os.environ.get("VIGIL_DISABLED_STDLIB", "")
    return [module.strip() for module in raw.split(",") if module.strip()]


def run_vigil(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_stdlib_availability_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class StdlibAvailabilityTest(unittest.TestCase):
    def test_disabled_stdlib_imports_fail_cleanly(self):
        modules = disabled_modules()
        if not modules:
            self.skipTest("no stdlib modules disabled in this build")

        for module in modules:
            with self.subTest(module=module):
                code = f'''import "{module}"
fn main() -> i32 {{
    return 0
}}'''
                rc, out, err = run_vigil(code)
                self.assertNotEqual(rc, 0, out)
                self.assertIn(
                    f"stdlib module '{module}' is not available in this build",
                    err,
                )


if __name__ == "__main__":
    unittest.main()
