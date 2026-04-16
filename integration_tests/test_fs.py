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
    guard err we = fs.write(path, "hello") { return 1 }
    guard string content, err re = fs.read(path) { return 1 }
    if content == "hello" {
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
    guard err we = fs.write(path, "x") { return 1 }
    if fs.exists(path) {
        guard err re = fs.remove(path) { return 1 }
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
    guard err we = fs.write(src, "copy") { return 1 }
    guard err ce = fs.copy(src, dst) { return 1 }
    guard string content, err re = fs.read(dst) { return 1 }
    if content == "copy" {
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
    guard err me = fs.mkdir(path) { return 1 }
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
    guard err me = fs.mkdir_all(path) { return 1 }
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
    guard err me = fs.mkdir_all(fs.join(base, "a/b")) { return 1 }
    guard err we = fs.write(fs.join(base, "a/b/c.txt"), "x") { return 1 }
    guard err re = fs.remove_all(base) { return 1 }
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
    guard err me = fs.mkdir_all(base) { return 1 }
    guard err w1 = fs.write(fs.join(base, "a.txt"), "a") { return 1 }
    guard err w2 = fs.write(fs.join(base, "b.txt"), "b") { return 1 }
    guard err w3 = fs.write(fs.join(base, "c.md"), "c") { return 1 }
    array<string> matches = fs.glob(base, "*.txt")
    fs.remove_all(base)
    if matches.len() == 2 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class FsNewFunctionsTest(unittest.TestCase):
    """Tests for stem, app_dir, chdir, and is_valid_name"""

    def test_stem(self):
        code = '''import "fs"
fn main() -> i32 {
    if fs.stem("foo.png") != "foo" { return 1 }
    if fs.stem("/a/b/bar.tar.gz") != "bar.tar" { return 2 }
    if fs.stem("noext") != "noext" { return 3 }
    if fs.stem(".hidden") != ".hidden" { return 4 }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_app_dir(self):
        code = '''import "fs"
fn main() -> i32 {
    string d = fs.app_dir()
    if d.len() == 0 { return 1 }
    if !fs.is_dir(d) { return 2 }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_chdir(self):
        code = '''import "fs"
fn main() -> i32 {
    string orig = fs.cwd()
    string tmp = fs.temp_dir()
    guard err e1 = fs.chdir(tmp) { return 1 }
    guard err e2 = fs.chdir(orig) { return 2 }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_is_valid_name(self):
        code = '''import "fs"
fn main() -> i32 {
    if !fs.is_valid_name("hello.txt") { return 1 }
    if fs.is_valid_name("") { return 2 }
    if fs.is_valid_name("a/b") { return 3 }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
