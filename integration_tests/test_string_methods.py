#!/usr/bin/env python3
"""Integration tests for string methods."""

import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def resolve_vigil_command() -> list[str]:
    configured_bin = os.environ.get("VIGIL_BIN")
    native_bin = REPO_ROOT / "build" / ("vigil.exe" if os.name == "nt" else "vigil")
    wasm_bin = REPO_ROOT / "build" / "vigil.js"

    if configured_bin:
        return [configured_bin]
    if native_bin.exists():
        return [str(native_bin)]
    if wasm_bin.exists():
        return [os.environ.get("EMSDK_NODE", "node"), str(wasm_bin)]

    raise FileNotFoundError("could not locate VIGIL CLI executable in build output")


def write_sources(root: Path, sources: dict[str, str]) -> None:
    for relative_path, text in sources.items():
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(text).strip() + "\n", encoding="utf-8")


def run_vigil(root: Path, entrypoint: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [*resolve_vigil_command(), "run", str(root / entrypoint)],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        check=False,
    )


class StringMethodsTest(unittest.TestCase):
    """Tests for new string methods: fields, join, cut, equal_fold."""

    def test_fields_splits_on_whitespace(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_str_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "main.vigil": """
                    import "fmt";
                    fn main() -> i32 {
                        string s = "  hello   world  ";
                        array<string> parts = s.fields();
                        if parts.len() != 2 { return 1; }
                        if parts[0] != "hello" { return 2; }
                        if parts[1] != "world" { return 3; }
                        return 0;
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_fields_empty_string(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_str_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "main.vigil": """
                    fn main() -> i32 {
                        string s = "";
                        array<string> parts = s.fields();
                        return parts.len();
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_join_array_of_strings(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_str_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "main.vigil": """
                    import "fmt";
                    fn main() -> i32 {
                        array<string> parts = ["a", "b", "c"];
                        string joined = ",".join(parts);
                        if joined != "a,b,c" { return 1; }
                        
                        string joined2 = "".join(parts);
                        if joined2 != "abc" { return 2; }
                        
                        return 0;
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_join_empty_array(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_str_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "main.vigil": """
                    fn main() -> i32 {
                        array<string> empty = [];
                        string joined = ",".join(empty);
                        if joined != "" { return 1; }
                        return 0;
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_cut_found(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_str_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "main.vigil": """
                    fn main() -> i32 {
                        string s = "key=value";
                        string before, string after, bool found = s.cut("=");
                        if !found { return 1; }
                        if before != "key" { return 2; }
                        if after != "value" { return 3; }
                        return 0;
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_cut_not_found(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_str_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "main.vigil": """
                    fn main() -> i32 {
                        string s = "no separator here";
                        string before, string after, bool found = s.cut("=");
                        if found { return 1; }
                        if before != "no separator here" { return 2; }
                        if after != "" { return 3; }
                        return 0;
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)

    def test_equal_fold_case_insensitive(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_str_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "main.vigil": """
                    fn main() -> i32 {
                        if !"Go".equal_fold("go") { return 1; }
                        if !"HELLO".equal_fold("hello") { return 2; }
                        if !"MixedCase".equal_fold("MIXEDCASE") { return 3; }
                        if "abc".equal_fold("abcd") { return 4; }
                        if "".equal_fold("x") { return 5; }
                        if !"".equal_fold("") { return 6; }
                        return 0;
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)


if __name__ == "__main__":
    unittest.main()
