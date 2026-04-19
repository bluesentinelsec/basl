#!/usr/bin/env python3
"""Integration tests for VIGIL log module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_log_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class LogLevelTest(unittest.TestCase):
    def test_set_level_info(self):
        code = '''import "log"
fn main() -> i32 {
    guard err e = log.set_level("info") { return 1 }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_set_level_invalid(self):
        code = '''import "log"
fn main() -> i32 {
    err e = log.set_level("bogus")
    if e != ok { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class LogOutputTest(unittest.TestCase):
    def test_info(self):
        code = '''import "log"
fn main() -> i32 {
    guard err e = log.set_level("info") { return 1 }
    log.info("test message")
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertIn("test message", err)

    def test_warn(self):
        code = '''import "log"
fn main() -> i32 {
    guard err e = log.set_level("warn") { return 1 }
    log.warn("warning message")
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertIn("warning message", err)

    def test_error(self):
        code = '''import "log"
fn main() -> i32 {
    guard err e = log.set_level("error") { return 1 }
    log.error("error message")
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertIn("error message", err)

    def test_debug_filtered(self):
        code = '''import "log"
fn main() -> i32 {
    guard err e = log.set_level("info") { return 1 }
    log.debug("debug message")
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertNotIn("debug message", err)

    def test_debug_shown(self):
        code = '''import "log"
fn main() -> i32 {
    guard err e = log.set_level("debug") { return 1 }
    log.debug("debug message")
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertIn("debug message", err)


if __name__ == "__main__":
    unittest.main()
