#!/usr/bin/env python3
"""Integration tests for VIGIL yaml module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    """Run VIGIL code and return (exit_code, stdout, stderr)."""
    with tempfile.TemporaryDirectory(prefix="vigil_yaml_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class YamlParseTest(unittest.TestCase):
    """Tests for yaml.parse()"""

    def test_parse_mapping(self):
        code = '''import "yaml";
import "fmt";
fn main() -> i32 {
    string y = "name: test\\ncount: 42";
    string j = yaml.parse(y);
    if j == "{\\"name\\":\\"test\\",\\"count\\":42}" { return 0; }
    fmt.println(j);
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stdout: {out}, stderr: {err}")

    def test_parse_sequence(self):
        code = '''import "yaml";
import "fmt";
fn main() -> i32 {
    string y = "- a\\n- b\\n- c";
    string j = yaml.parse(y);
    if j == "[\\"a\\",\\"b\\",\\"c\\"]" { return 0; }
    fmt.println(j);
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stdout: {out}, stderr: {err}")

    def test_parse_nested(self):
        code = '''import "yaml";
import "fmt";
fn main() -> i32 {
    string y = "items:\\n  - x\\n  - y";
    string j = yaml.parse(y);
    if j == "{\\"items\\":[\\"x\\",\\"y\\"]}" { return 0; }
    fmt.println(j);
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stdout: {out}, stderr: {err}")


class YamlGetTest(unittest.TestCase):
    """Tests for yaml.get()"""

    def test_get_string(self):
        code = '''import "yaml";
fn main() -> i32 {
    string y = "name: test";
    if yaml.get(y, "name") == "test" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_get_nested(self):
        code = '''import "yaml";
fn main() -> i32 {
    string y = "server:\\n  host: localhost";
    if yaml.get(y, "server.host") == "localhost" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_get_array_index(self):
        code = '''import "yaml";
fn main() -> i32 {
    string y = "items:\\n  - a\\n  - b\\n  - c";
    if yaml.get(y, "items[1]") == "b" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_get_number(self):
        code = '''import "yaml";
fn main() -> i32 {
    string y = "count: 42";
    if yaml.get(y, "count") == "42" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_get_bool(self):
        code = '''import "yaml";
fn main() -> i32 {
    string y = "enabled: true";
    if yaml.get(y, "enabled") == "true" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class YamlFeaturesTest(unittest.TestCase):
    """Tests for YAML features"""

    def test_comments(self):
        code = '''import "yaml";
fn main() -> i32 {
    string y = "# comment\\nname: test  # inline";
    if yaml.get(y, "name") == "test" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_quoted_string(self):
        code = '''import "yaml";
fn main() -> i32 {
    string y = "msg: \\"hello world\\"";
    if yaml.get(y, "msg") == "hello world" { return 0; }
    return 1;
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
