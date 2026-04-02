#!/usr/bin/env python3
"""Integration tests for VIGIL args module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str, args: list[str] = None) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_args_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        cmd = [VIGIL_BIN, "run", str(path)]
        if args:
            cmd.extend(args)
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class ArgsCountTest(unittest.TestCase):
    def test_count_no_args(self):
        code = '''import "args";
fn main() -> i32 {
    i32 c = args.count();
    if c == 0 { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_count_with_args(self):
        code = '''import "args";
fn main() -> i32 {
    i32 c = args.count();
    if c == 3 { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code, ["one", "two", "three"])
        self.assertEqual(rc, 0, f"stderr: {err}")


class ArgsAtTest(unittest.TestCase):
    def test_at_first(self):
        code = '''import "args";
fn main() -> i32 {
    string a = args.at(0);
    if a == "hello" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code, ["hello", "world"])
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_at_second(self):
        code = '''import "args";
fn main() -> i32 {
    string a = args.at(1);
    if a == "world" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code, ["hello", "world"])
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_at_out_of_bounds(self):
        code = '''import "args";
fn main() -> i32 {
    string a = args.at(99);
    if a == "" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code, ["hello"])
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_at_negative(self):
        code = '''import "args";
fn main() -> i32 {
    string a = args.at(-1);
    if a == "" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code, ["hello"])
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
