#!/usr/bin/env python3
"""Integration tests for VIGIL fs module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    """Run VIGIL code and return (exit_code, stdout, stderr)."""
    with tempfile.TemporaryDirectory(prefix="vigil_fs_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class FsPathTest(unittest.TestCase):
    """Tests for path operations"""

    def test_join(self):
        code = '''import "fs"
fn main() -> i32 {
    if fs.join("a", "b") == "a/b" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_clean(self):
        code = '''import "fs"
fn main() -> i32 {
    if fs.clean("a/./b/../c") == "a/c" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_dir(self):
        code = '''import "fs"
fn main() -> i32 {
    if fs.dir("/foo/bar.txt") == "/foo" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_base(self):
        code = '''import "fs"
fn main() -> i32 {
    if fs.base("/foo/bar.txt") == "bar.txt" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_ext(self):
        code = '''import "fs"
fn main() -> i32 {
    if fs.ext("file.txt") == ".txt" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class FsFileTest(unittest.TestCase):
    """Tests for file operations"""

    def test_write_read(self):
        code = '''import "fs"
fn main() -> i32 {
    string tmp = fs.temp_dir()
    string path = fs.join(tmp, "vigil_test_wr.txt")
    fs.write(path, "hello")
    if fs.read(path) == "hello" {
        fs.remove(path)
        return 0
    }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_exists(self):
        code = '''import "fs"
fn main() -> i32 {
    string tmp = fs.temp_dir()
    string path = fs.join(tmp, "vigil_test_ex.txt")
    fs.write(path, "x")
    if fs.exists(path) {
        fs.remove(path)
        if !fs.exists(path) { return 0 }
    }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_copy(self):
        code = '''import "fs"
fn main() -> i32 {
    string tmp = fs.temp_dir()
    string src = fs.join(tmp, "vigil_test_cp1.txt")
    string dst = fs.join(tmp, "vigil_test_cp2.txt")
    fs.write(src, "copy")
    fs.copy(src, dst)
    if fs.read(dst) == "copy" {
        fs.remove(src)
        fs.remove(dst)
        return 0
    }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class FsDirTest(unittest.TestCase):
    """Tests for directory operations"""

    def test_mkdir(self):
        code = '''import "fs"
fn main() -> i32 {
    string tmp = fs.temp_dir()
    string path = fs.join(tmp, "vigil_test_mkdir")
    fs.mkdir(path)
    if fs.is_dir(path) {
        fs.remove(path)
        return 0
    }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_mkdir_all(self):
        code = '''import "fs"
fn main() -> i32 {
    string tmp = fs.temp_dir()
    string base = fs.join(tmp, "vigil_test_mkdirall")
    string path = fs.join(base, "a/b")
    fs.mkdir_all(path)
    if fs.is_dir(path) {
        fs.remove(path)
        fs.remove(fs.join(base, "a"))
        fs.remove(base)
        return 0
    }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class FsLocationTest(unittest.TestCase):
    """Tests for standard locations"""

    def test_home_dir(self):
        code = '''import "fs"
fn main() -> i32 {
    string home = fs.home_dir()
    if home.len() > 0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_temp_dir(self):
        code = '''import "fs"
fn main() -> i32 {
    string tmp = fs.temp_dir()
    if tmp.len() > 0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_cwd(self):
        code = '''import "fs"
fn main() -> i32 {
    string cwd = fs.cwd()
    if cwd.len() > 0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_remove_all(self):
        code = '''import "fs"
fn main() -> i32 {
    string base = fs.join(fs.temp_dir(), "vigil_it_rmall")
    fs.mkdir_all(fs.join(base, "a/b"))
    fs.write(fs.join(base, "a/b/c.txt"), "x")
    if !fs.remove_all(base) { return 1 }
    if fs.exists(base) { return 2 }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_glob(self):
        code = '''import "fs"
import "fmt"
fn main() -> i32 {
    string base = fs.join(fs.temp_dir(), "vigil_it_glob")
    fs.mkdir_all(base)
    fs.write(fs.join(base, "a.txt"), "a")
    fs.write(fs.join(base, "b.txt"), "b")
    fs.write(fs.join(base, "c.md"), "c")
    array<string> matches = fs.glob(base, "*.txt")
    fs.remove_all(base)
    if matches.len() == 2 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
