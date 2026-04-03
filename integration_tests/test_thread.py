#!/usr/bin/env python3
"""Integration tests for VIGIL thread module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str, timeout: int = 10) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_thread_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=timeout,
        )
        return result.returncode, result.stdout, result.stderr


class ThreadCurrentIdTest(unittest.TestCase):
    def test_current_id_positive(self):
        code = '''import "thread"
fn main() -> i32 {
    i64 id = thread.current_id()
    if id > i64(0) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class ThreadYieldTest(unittest.TestCase):
    def test_yield_returns_true(self):
        code = '''import "thread"
fn main() -> i32 {
    bool ok = thread.yield()
    if ok { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class ThreadSleepTest(unittest.TestCase):
    def test_sleep_short(self):
        code = '''import "thread"
import "time"
fn main() -> i32 {
    i64 start = time.now_ms()
    thread.sleep(i64(50))
    i64 end = time.now_ms()
    if end - start >= i64(40) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class MutexTest(unittest.TestCase):
    def test_mutex_create(self):
        code = '''import "thread"
fn main() -> i32 {
    i64 m = thread.mutex()
    if m >= i64(0) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_mutex_lock_unlock(self):
        code = '''import "thread"
fn main() -> i32 {
    i64 m = thread.mutex()
    bool locked = thread.lock(m)
    bool unlocked = thread.unlock(m)
    if locked && unlocked { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_mutex_try_lock(self):
        code = '''import "thread"
fn main() -> i32 {
    i64 m = thread.mutex()
    bool got = thread.try_lock(m)
    thread.unlock(m)
    if got { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RwlockTest(unittest.TestCase):
    def test_rwlock_create(self):
        code = '''import "thread"
fn main() -> i32 {
    i64 rw = thread.rwlock()
    if rw >= i64(0) { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_rwlock_read(self):
        code = '''import "thread"
fn main() -> i32 {
    i64 rw = thread.rwlock()
    bool locked = thread.read_lock(rw)
    bool unlocked = thread.rw_unlock(rw)
    if locked && unlocked { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_rwlock_write(self):
        code = '''import "thread"
fn main() -> i32 {
    i64 rw = thread.rwlock()
    bool locked = thread.write_lock(rw)
    bool unlocked = thread.rw_unlock(rw)
    if locked && unlocked { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
