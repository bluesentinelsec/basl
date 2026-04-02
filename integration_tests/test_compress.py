#!/usr/bin/env python3
"""Integration tests for VIGIL compress module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_compress_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class ZlibTest(unittest.TestCase):
    def test_zlib_roundtrip(self):
        code = '''import "compress";
fn main() -> i32 {
    string data = "hello world hello world hello world";
    string compressed = compress.zlib_compress(data);
    string decompressed = compress.zlib_decompress(compressed);
    if decompressed == data { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_zlib_compresses(self):
        code = '''import "compress";
fn main() -> i32 {
    string data = "hello world hello world hello world";
    string compressed = compress.zlib_compress(data);
    // Just verify we got some output
    if compressed != "" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class DeflateTest(unittest.TestCase):
    def test_deflate_roundtrip(self):
        code = '''import "compress";
fn main() -> i32 {
    string data = "test data for deflate compression";
    string compressed = compress.deflate_compress(data);
    string decompressed = compress.deflate_decompress(compressed);
    if decompressed == data { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class GzipTest(unittest.TestCase):
    def test_gzip_roundtrip(self):
        code = '''import "compress";
fn main() -> i32 {
    string data = "gzip test data with some content";
    string compressed = compress.gzip_compress(data);
    string decompressed = compress.gzip_decompress(compressed);
    if decompressed == data { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class Lz4Test(unittest.TestCase):
    def test_lz4_roundtrip(self):
        code = '''import "compress";
fn main() -> i32 {
    string data = "lz4 compression test data here";
    string compressed = compress.lz4_compress(data);
    string decompressed = compress.lz4_decompress(compressed);
    if decompressed == data { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class EmptyDataTest(unittest.TestCase):
    def test_zlib_empty(self):
        code = '''import "compress";
fn main() -> i32 {
    string data = "";
    string compressed = compress.zlib_compress(data);
    string decompressed = compress.zlib_decompress(compressed);
    if decompressed == data { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
