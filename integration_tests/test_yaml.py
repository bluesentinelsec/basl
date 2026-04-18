#!/usr/bin/env python3
"""Integration tests for VIGIL yaml module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_yaml_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class YamlParseTest(unittest.TestCase):
    def test_parse_mapping(self):
        code = r'''import "yaml"
import "fmt"
fn main() -> i32 {
    string j, err e = yaml.parse("name: test\ncount: 42")
    if e != ok { return 1 }
    if j == "{\"name\":\"test\",\"count\":42}" { return 0 }
    fmt.println(j)
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stdout: {out}, stderr: {err}")

    def test_parse_sequence(self):
        code = r'''import "yaml"
import "fmt"
fn main() -> i32 {
    string j, err e = yaml.parse("- a\n- b\n- c")
    if e != ok { return 1 }
    if j == "[\"a\",\"b\",\"c\"]" { return 0 }
    fmt.println(j)
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stdout: {out}, stderr: {err}")

    def test_parse_nested(self):
        code = r'''import "yaml"
import "fmt"
fn main() -> i32 {
    string j, err e = yaml.parse("items:\n  - x\n  - y")
    if e != ok { return 1 }
    if j == "{\"items\":[\"x\",\"y\"]}" { return 0 }
    fmt.println(j)
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stdout: {out}, stderr: {err}")


class YamlGetTest(unittest.TestCase):
    def test_get_string(self):
        code = r'''import "yaml"
fn main() -> i32 {
    string v, err e = yaml.get("name: test", "name")
    if v == "test" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_get_nested(self):
        code = r'''import "yaml"
fn main() -> i32 {
    string v, err e = yaml.get("server:\n  host: localhost", "server.host")
    if v == "localhost" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_get_array_index(self):
        code = r'''import "yaml"
fn main() -> i32 {
    string v, err e = yaml.get("items:\n  - a\n  - b\n  - c", "items[1]")
    if v == "b" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_get_number(self):
        code = r'''import "yaml"
fn main() -> i32 {
    string v, err e = yaml.get("count: 42", "count")
    if v == "42" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_get_bool(self):
        code = r'''import "yaml"
fn main() -> i32 {
    string v, err e = yaml.get("enabled: true", "enabled")
    if v == "true" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class YamlFeaturesTest(unittest.TestCase):
    def test_comments(self):
        code = r'''import "yaml"
fn main() -> i32 {
    string v, err e = yaml.get("# comment\nname: test  # inline", "name")
    if v == "test" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_quoted_string(self):
        code = r'''import "yaml"
fn main() -> i32 {
    string v, err e = yaml.get("msg: \"hello world\"", "msg")
    if v == "hello world" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
