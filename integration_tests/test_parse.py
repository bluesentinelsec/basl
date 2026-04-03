#!/usr/bin/env python3
"""Integration tests for the VIGIL parse module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_parse_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class ParseI32Test(unittest.TestCase):
    def test_i32_success(self):
        code = '''import "parse"
fn main() -> i32 {
    i32 value, err e = parse.i32("42")
    if e == ok && value == 42 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_i32_failure(self):
        code = '''import "parse"
fn main() -> i32 {
    i32 value, err e = parse.i32("oops")
    if e != ok && value == 0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_i32_empty(self):
        code = '''import "parse"
fn main() -> i32 {
    i32 value, err e = parse.i32("")
    if e != ok && value == 0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class ParseF64Test(unittest.TestCase):
    def test_f64_success(self):
        code = '''import "parse"
fn main() -> i32 {
    f64 value, err e = parse.f64("3.5")
    if e == ok && value > 3.49 && value < 3.51 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_f64_failure(self):
        code = '''import "parse"
fn main() -> i32 {
    f64 value, err e = parse.f64("abc")
    if e != ok && value == 0.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class ParseBoolTest(unittest.TestCase):
    def test_bool_success(self):
        code = '''import "parse"
fn main() -> i32 {
    bool value, err e = parse.bool("true")
    if e == ok && value { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_bool_false(self):
        code = '''import "parse"
fn main() -> i32 {
    bool value, err e = parse.bool("false")
    if e == ok && !value { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_bool_failure(self):
        code = '''import "parse"
fn main() -> i32 {
    bool value, err e = parse.bool("not-bool")
    if e != ok && !value { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
