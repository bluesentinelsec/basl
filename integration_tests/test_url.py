#!/usr/bin/env python3
"""Integration tests for VIGIL url module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    """Run VIGIL code and return (exit_code, stdout, stderr)."""
    with tempfile.TemporaryDirectory(prefix="vigil_url_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class UrlSchemeTest(unittest.TestCase):
    """Tests for url.scheme()"""

    def test_scheme_https(self):
        code = '''import "url"
fn main() -> i32 {
    if url.scheme("https://example.com") == "https" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_scheme_http(self):
        code = '''import "url"
fn main() -> i32 {
    if url.scheme("http://example.com") == "http" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class UrlHostTest(unittest.TestCase):
    """Tests for url.host()"""

    def test_host_simple(self):
        code = '''import "url"
fn main() -> i32 {
    if url.host("https://example.com/path") == "example.com" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_host_with_port(self):
        code = '''import "url"
fn main() -> i32 {
    if url.host("https://example.com:8080/path") == "example.com" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class UrlPortTest(unittest.TestCase):
    """Tests for url.port()"""

    def test_port_present(self):
        code = '''import "url"
fn main() -> i32 {
    if url.port("https://example.com:8080") == "8080" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_port_absent(self):
        code = '''import "url"
fn main() -> i32 {
    if url.port("https://example.com") == "" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class UrlPathTest(unittest.TestCase):
    """Tests for url.path()"""

    def test_path_simple(self):
        code = '''import "url"
fn main() -> i32 {
    if url.path("https://example.com/foo/bar") == "/foo/bar" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class UrlQueryTest(unittest.TestCase):
    """Tests for url.query()"""

    def test_query_simple(self):
        code = '''import "url"
fn main() -> i32 {
    if url.query("https://example.com?a=1&b=2") == "a=1&b=2" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class UrlEncodeDecodeTest(unittest.TestCase):
    """Tests for url.encode() and url.decode()"""

    def test_encode_spaces(self):
        code = '''import "url"
fn main() -> i32 {
    if url.encode("hello world") == "hello+world" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_decode_percent(self):
        code = '''import "url"
fn main() -> i32 {
    if url.decode("hello%20world") == "hello world" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_decode_plus(self):
        code = '''import "url"
fn main() -> i32 {
    if url.decode("hello+world") == "hello world" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
