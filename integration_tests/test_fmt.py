"""Integration tests for vigil fmt."""

import os
import subprocess
import sys
import tempfile
import unittest

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")


def fmt(source, *extra_args):
    """Write source to a temp file, run vigil fmt, return the result."""
    with tempfile.NamedTemporaryFile(suffix=".vigil", mode="wb", delete=False) as f:
        f.write(source.encode("utf-8"))
        path = f.name
    try:
        result = subprocess.run(
            [VIGIL_BIN, "fmt"] + list(extra_args) + [path],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode != 0 and "--check" not in extra_args:
            raise RuntimeError(f"vigil fmt failed: {result.stderr}")
        with open(path, "rb") as f:
            return f.read().decode("utf-8"), result
    finally:
        os.unlink(path)


class TestVigilFmt(unittest.TestCase):

    def test_import_sorting(self):
        src = 'import "io";\nimport "fmt";\n'
        got, _ = fmt(src)
        self.assertEqual(got, 'import "fmt";\nimport "io";\n')

    def test_function_indent(self):
        src = 'fn main() -> i32 {\nreturn 0;\n}\n'
        got, _ = fmt(src)
        self.assertIn("    return 0;", got)

    def test_if_else(self):
        src = ('fn main() -> i32 {\n'
               'if x > 0 {\nreturn 1;\n} else {\nreturn 0;\n}\n}\n')
        got, _ = fmt(src)
        self.assertIn("} else {", got)
        self.assertIn("        return 1;", got)

    def test_for_loop(self):
        src = ('fn main() -> i32 {\n'
               'for (i32 i = 0; i < 10; i++) {\nfmt.println(i);\n}\nreturn 0;\n}\n')
        got, _ = fmt(src)
        self.assertIn("for (i32 i = 0; i < 10; i++) {", got)

    def test_switch(self):
        src = ('fn main() -> i32 {\nswitch (x) {\n'
               'case 1: return 1;\ndefault: return 0;\n}\n}\n')
        got, _ = fmt(src)
        self.assertIn("    switch x {", got)
        self.assertIn("        case 1:", got)
        self.assertIn("        default:", got)

    def test_class(self):
        src = ('class Pet {\npub string name;\n'
               'fn init(string name) -> void {\nself.name = name;\n}\n}\n')
        got, _ = fmt(src)
        self.assertIn("    pub string name;", got)
        self.assertIn("        self.name = name;", got)

    def test_enum(self):
        src = 'enum Color {\nRed,\nGreen = 5,\nBlue\n}\n'
        got, _ = fmt(src)
        self.assertIn("    Red,", got)
        self.assertIn("    Green = 5,", got)
        self.assertIn("    Blue", got)

    def test_interface(self):
        src = 'interface Drawable {\nfn draw() -> void;\n}\n'
        got, _ = fmt(src)
        self.assertIn("    fn draw() -> void;", got)

    def test_ternary(self):
        src = 'fn main() -> i32 {\ni32 x = true ? 1 : 2;\nreturn 0;\n}\n'
        got, _ = fmt(src)
        self.assertIn("true ? 1 : 2", got)

    def test_map_literal(self):
        src = 'fn main() -> i32 {\nmap<string, i32> m = {"a": 1, "b": 2};\nreturn 0;\n}\n'
        got, _ = fmt(src)
        self.assertIn('{"a": 1, "b": 2}', got)

    def test_generic_types(self):
        src = 'fn main() -> i32 {\narray<i32> nums = [1, 2, 3];\nreturn 0;\n}\n'
        got, _ = fmt(src)
        self.assertIn("array<i32>", got)

    def test_comment_preserved(self):
        src = '// top comment\nfn main() -> i32 {\n// body\nreturn 0;\n}\n'
        got, _ = fmt(src)
        self.assertIn("// top comment", got)
        self.assertIn("    // body", got)

    def test_idempotent(self):
        src = ('import "fmt";\n\nfn main() -> i32 {\n'
               '    fmt.println("hello");\n    return 0;\n}\n')
        first, _ = fmt(src)
        second, _ = fmt(first)
        self.assertEqual(first, second)

    def test_check_unformatted(self):
        src = 'fn main() -> i32 {\nreturn 0;\n}\n'
        _, result = fmt(src, "--check")
        self.assertEqual(result.returncode, 1)

    def test_check_formatted(self):
        src = 'fn main() -> i32 {\n    return 0;\n}\n'
        _, result = fmt(src, "--check")
        self.assertEqual(result.returncode, 0)

    def test_while(self):
        src = ('fn main() -> i32 {\nwhile (x > 0) {\n'
               'x = x - 1;\n}\nreturn 0;\n}\n')
        got, _ = fmt(src)
        self.assertIn("    while x > 0 {", got)
        self.assertIn("        x = x - 1;", got)

    def test_blank_line_between_decls(self):
        src = 'const i32 MAX = 100;\nfn main() -> i32 {\nreturn 0;\n}\n'
        got, _ = fmt(src)
        self.assertIn("const i32 MAX = 100;\n\nfn main()", got)

    def test_format_alias(self):
        """vigil format is an alias for vigil fmt."""
        src = 'fn main() -> i32 {\nreturn 0;\n}\n'
        with tempfile.NamedTemporaryFile(suffix=".vigil", mode="wb", delete=False) as f:
            f.write(src.encode("utf-8"))
            path = f.name
        try:
            result = subprocess.run(
                [VIGIL_BIN, "format", path],
                capture_output=True, text=True, timeout=10
            )
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            with open(path, "rb") as f:
                got = f.read().decode("utf-8")
            self.assertIn("    return 0;", got)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
