#!/usr/bin/env python3
"""Integration tests for VIGIL regex module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    """Run VIGIL code and return (exit_code, stdout, stderr)."""
    with tempfile.TemporaryDirectory(prefix="vigil_regex_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class RegexMatchTest(unittest.TestCase):
    """Tests for regex.match()"""

    def test_match_literal(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("hello", "hello") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_literal_no_match(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("hello", "world") { return 1 }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_dot(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("h.llo", "hello") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_star(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("hel*o", "heo") && regex.match("hel*o", "hello") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_plus(self):
        code = '''import "regex"
fn main() -> i32 {
    if !regex.match("hel+o", "heo") && regex.match("hel+o", "hello") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_question(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("hel?o", "heo") && regex.match("hel?o", "helo") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_exact_zero_quantifier(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("a{0}", "") && !regex.match("a{0}", "a") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_open_brace_quantifiers(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("a{0,}", "")
        && regex.match("a{0,}", "aaaa")
        && regex.match("a{1,}", "a")
        && regex.match("a{1,}", "aaaa")
        && !regex.match("a{1,}", "") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_lazy_quantifiers(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("a*?", "aaaa")
        && regex.match("a+?", "aaaa")
        && regex.match("a??", "")
        && regex.match("a{0,}?", "aaaa") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_exact_one_quantifier(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("a{1}", "a") && !regex.match("a{1}", "") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_unsupported_brace_quantifiers_return_false(self):
        """Brace quantifiers that remain unsupported (bad syntax, too large)."""
        code = '''import "regex"
fn main() -> i32 {
    if !regex.match("a{1", "a")
        && !regex.match("a{101}", "a") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_brace_quantifiers(self):
        """Brace quantifiers that are now supported."""
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("a{2}", "aa")
        && regex.match("a{0,11}", "")
        && regex.match("a{2,}", "aa")
        && regex.match("a{1,2}", "a")
        && regex.match("a{1,2}", "aa")
        && !regex.match("a{2}", "a")
        && !regex.match("a{2,3}", "a") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_char_class(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("[abc]", "a") && !regex.match("[abc]", "d") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_char_class_range(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("[a-z]+", "hello") && !regex.match("[a-z]+", "HELLO") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_negated_class(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("[^0-9]+", "hello") && !regex.match("[^0-9]+", "123") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_alternation(self):
        code = '''import "regex"
fn main() -> i32 {
    if regex.match("cat|dog", "cat") && regex.match("cat|dog", "dog") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_digit(self):
        code = r'''import "regex"
fn main() -> i32 {
    if regex.match("\\d+", "123") && !regex.match("\\d+", "abc") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_word(self):
        code = r'''import "regex"
fn main() -> i32 {
    if regex.match("\\w+", "hello_123") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_match_whitespace(self):
        code = r'''import "regex"
fn main() -> i32 {
    if regex.match("\\s+", "   ") && !regex.match("\\s+", "abc") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RegexFindTest(unittest.TestCase):
    """Tests for regex.find()"""

    @unittest.skip("Tuple returns from native modules need work")
    def test_find_basic(self):
        code = '''import "regex"
fn main() -> i32 {
    string match, bool found = regex.find("[0-9]+", "abc123def")
    if found && match == "123" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_find_not_found(self):
        code = '''import "regex"
fn main() -> i32 {
    string match, bool found = regex.find("[0-9]+", "abcdef")
    if !found { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RegexFindAllTest(unittest.TestCase):
    """Tests for regex.find_all()"""

    def test_find_all_basic(self):
        code = '''import "regex"
fn main() -> i32 {
    array<string> matches = regex.find_all("[0-9]+", "a1b22c333")
    if matches.len() == 3 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_find_all_no_matches(self):
        code = '''import "regex"
fn main() -> i32 {
    array<string> matches = regex.find_all("[0-9]+", "abcdef")
    if matches.len() == 0 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RegexReplaceTest(unittest.TestCase):
    """Tests for regex.replace() and regex.replace_all()"""

    def test_replace_first(self):
        code = '''import "regex"
fn main() -> i32 {
    string result = regex.replace("[0-9]+", "a1b2c3", "X")
    if result == "aXb2c3" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_replace_all(self):
        code = '''import "regex"
fn main() -> i32 {
    string result = regex.replace_all("[0-9]+", "a1b2c3", "X")
    if result == "aXbXcX" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_replace_no_match(self):
        code = '''import "regex"
fn main() -> i32 {
    string result = regex.replace("[0-9]+", "abc", "X")
    if result == "abc" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RegexSplitTest(unittest.TestCase):
    """Tests for regex.split()"""

    def test_split_basic(self):
        code = '''import "regex"
fn main() -> i32 {
    array<string> parts = regex.split(",", "a,b,c")
    if parts.len() == 3 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_split_regex(self):
        code = r'''import "regex"
fn main() -> i32 {
    array<string> parts = regex.split("\\s+", "a  b   c")
    if parts.len() == 3 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_split_no_match(self):
        code = '''import "regex"
fn main() -> i32 {
    array<string> parts = regex.split(",", "abc")
    if parts.len() == 1 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
