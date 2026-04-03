#!/usr/bin/env python3
"""Integration tests for VIGIL atomic module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_atomic_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class AtomicNewTest(unittest.TestCase):
    def test_new_returns_handle(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(0))
    if a >= i64(0) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_new_with_initial_value(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(42))
    i64 val = atomic.load(a)
    if val == i64(42) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class AtomicLoadStoreTest(unittest.TestCase):
    def test_store_and_load(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(0))
    atomic.store(a, i64(100))
    i64 val = atomic.load(a)
    if val == i64(100) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class AtomicAddSubTest(unittest.TestCase):
    def test_add(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(10))
    i64 old = atomic.add(a, i64(5))
    i64 new_val = atomic.load(a)
    if old == i64(10) && new_val == i64(15) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_sub(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(20))
    i64 old = atomic.sub(a, i64(7))
    i64 new_val = atomic.load(a)
    if old == i64(20) && new_val == i64(13) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class AtomicCasTest(unittest.TestCase):
    def test_cas_success(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(50))
    bool ok = atomic.cas(a, i64(50), i64(60))
    i64 val = atomic.load(a)
    if ok && val == i64(60) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_cas_failure(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(50))
    bool ok = atomic.cas(a, i64(99), i64(60))
    i64 val = atomic.load(a)
    if !ok && val == i64(50) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class AtomicIncDecTest(unittest.TestCase):
    def test_inc(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(5))
    i64 old = atomic.inc(a)
    i64 new_val = atomic.load(a)
    if old == i64(5) && new_val == i64(6) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_dec(self):
        code = '''import "atomic"
fn main() -> i32 {
    i64 a = atomic.new(i64(5))
    i64 old = atomic.dec(a)
    i64 new_val = atomic.load(a)
    if old == i64(5) && new_val == i64(4) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class AtomicGrowthTest(unittest.TestCase):
    def test_allocate_past_initial_capacity(self):
        code = '''import "atomic"
fn main() -> i32 {
    array<i64> handles = []
    for (i64 i = i64(0); i < i64(1500); i = i + i64(1)) {
        handles.push(atomic.new(i))
    }
    for (i32 i = 0; i < handles.len(); i = i + 1) {
        if atomic.load(handles[i]) != i64(i) { return 1 }
    }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
