#!/usr/bin/env python3
"""Integration test for vigil test --transpile."""

import os
import subprocess
import unittest

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")
PROJECT_DIR = os.path.join(os.path.dirname(__file__), "transpile_test_project")


class TranspileTestFlagTest(unittest.TestCase):
    def test_transpile_flag_passes(self):
        """vigil test --transpile should pass on the test project."""
        result = subprocess.run(
            [VIGIL_BIN, "test", "--transpile", "-v", "test"],
            capture_output=True, text=True, timeout=120,
            cwd=PROJECT_DIR,
        )
        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertIn("transpile: PASS", result.stdout)

    def test_interpreter_output_present(self):
        """Interpreter test output should be present."""
        result = subprocess.run(
            [VIGIL_BIN, "test", "--transpile", "-v", "test"],
            capture_output=True, text=True, timeout=120,
            cwd=PROJECT_DIR,
        )
        self.assertIn("arithmetic: pass", result.stdout)
        self.assertIn("strings: pass", result.stdout)
        self.assertIn("classes: pass", result.stdout)
        self.assertIn("csv_roundtrip: pass", result.stdout)

    def test_without_transpile_flag(self):
        """Without --transpile, tests should still pass normally."""
        result = subprocess.run(
            [VIGIL_BIN, "test", "-v", "test"],
            capture_output=True, text=True, timeout=30,
            cwd=PROJECT_DIR,
        )
        self.assertEqual(result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}")
        self.assertNotIn("transpile:", result.stdout)


if __name__ == "__main__":
    unittest.main()
