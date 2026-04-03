#!/usr/bin/env python3
"""Integration tests for VIGIL time module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str, timeout: int = 10) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_time_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=timeout,
        )
        return result.returncode, result.stdout, result.stderr


class TimeNowTest(unittest.TestCase):
    def test_now_returns_timestamp(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.now()
    if ts > i64(1700000000) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_now_ms_greater_than_now(self):
        code = '''import "time"
fn main() -> i32 {
    i64 s = time.now()
    i64 ms = time.now_ms()
    if ms >= s * i64(1000) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_now_ns_greater_than_now_ms(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ms = time.now_ms()
    i64 ns = time.now_ns()
    if ns >= ms * i64(1000000) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class TimeComponentsTest(unittest.TestCase):
    def test_year_extraction(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 1, 15, 12, 0, 0)
    i32 y = time.year(ts)
    if y == 2024 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_month_extraction(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 1, 15, 12, 0, 0)
    i32 m = time.month(ts)
    if m == 1 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_day_extraction(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 1, 15, 12, 0, 0)
    i32 d = time.day(ts)
    if d == 15 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_hour_extraction(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 1, 15, 14, 30, 0)
    i32 h = time.hour(ts)
    if h == 14 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class TimeDateTest(unittest.TestCase):
    def test_date_creates_timestamp(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 6, 20, 10, 30, 45)
    i32 y = time.year(ts)
    i32 m = time.month(ts)
    i32 d = time.day(ts)
    if y == 2024 && m == 6 && d == 20 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class TimeArithmeticTest(unittest.TestCase):
    def test_add_days(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 1, 15, 12, 0, 0)
    i64 ts2 = time.add_days(ts, 10)
    i32 d = time.day(ts2)
    if d == 25 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_add_hours(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 1, 15, 10, 0, 0)
    i64 ts2 = time.add_hours(ts, 5)
    i32 h = time.hour(ts2)
    if h == 15 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_add_seconds(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 1, 15, 12, 0, 0)
    i64 ts2 = time.add_seconds(ts, i64(3600))
    i32 h = time.hour(ts2)
    if h == 13 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class TimeFormatTest(unittest.TestCase):
    def test_format_date(self):
        code = '''import "time"
fn main() -> i32 {
    i64 ts = time.date(2024, 1, 15, 12, 30, 45)
    string s = time.format(ts, "%Y-%m-%d")
    if s == "2024-01-15" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class TimeSleepTest(unittest.TestCase):
    def test_sleep_short(self):
        code = '''import "time"
fn main() -> i32 {
    i64 start = time.now_ms()
    time.sleep(i64(50))
    i64 end = time.now_ms()
    if end - start >= i64(40) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
