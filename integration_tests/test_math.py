#!/usr/bin/env python3
"""Integration tests for VIGIL math module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_math_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class MathConstantsTest(unittest.TestCase):
    def test_pi(self):
        code = '''import "math"
fn main() -> i32 {
    f64 pi = math.pi()
    if pi > 3.14 && pi < 3.15 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_e(self):
        code = '''import "math"
fn main() -> i32 {
    f64 e = math.e()
    if e > 2.71 && e < 2.72 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class MathRoundingTest(unittest.TestCase):
    def test_floor(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.floor(3.7)
    if r == 3.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_ceil(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.ceil(3.2)
    if r == 4.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_round(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.round(3.5)
    if r == 4.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_trunc(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.trunc(-3.7)
    if r == -3.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class MathBasicTest(unittest.TestCase):
    def test_abs(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.abs(-5.5)
    if r == 5.5 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_sqrt(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.sqrt(16.0)
    if r == 4.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_pow(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.pow(2.0, 3.0)
    if r == 8.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class MathMinMaxTest(unittest.TestCase):
    def test_min(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.min(3.0, 7.0)
    if r == 3.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_max(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.max(3.0, 7.0)
    if r == 7.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class MathTrigTest(unittest.TestCase):
    def test_sin_zero(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.sin(0.0)
    if r == 0.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_cos_zero(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.cos(0.0)
    if r == 1.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class MathLogExpTest(unittest.TestCase):
    def test_log(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.log(math.e())
    if r > 0.99 && r < 1.01 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_exp(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.exp(1.0)
    f64 e = math.e()
    if r > e - 0.01 && r < e + 0.01 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class MathSignTest(unittest.TestCase):
    def test_sign_positive(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.sign(5.0)
    if r == 1.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_sign_negative(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.sign(-5.0)
    if r == -1.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_sign_zero(self):
        code = '''import "math"
fn main() -> i32 {
    f64 r = math.sign(0.0)
    if r == 0.0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
