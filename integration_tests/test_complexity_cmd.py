"""Integration tests for 'vigil complexity' command."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")


class ComplexityTest(unittest.TestCase):
    def _run(self, args):
        r = subprocess.run(
            [VIGIL_BIN, "complexity"] + args,
            capture_output=True, text=True, timeout=30,
        )
        return r.returncode, r.stdout, r.stderr

    def test_single_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "main.vigil"
            p.write_text("fn main() -> i32 {\n    if true { return 1 }\n    return 0\n}\n")
            rc, out, _ = self._run([str(p)])
            self.assertEqual(rc, 0)
            self.assertIn("main", out)
            self.assertIn("ccn=", out)
            self.assertIn("lines=", out)
            self.assertIn("1 function", out)

    def test_directory(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "a.vigil").write_text("fn foo() -> i32 { return 1 }\n")
            (Path(d) / "b.vigil").write_text("fn bar() -> i32 { return 2 }\n")
            rc, out, _ = self._run([d])
            self.assertEqual(rc, 0)
            self.assertIn("foo", out)
            self.assertIn("bar", out)
            self.assertIn("2 functions", out)

    def test_high_ccn_warning(self):
        with tempfile.TemporaryDirectory() as d:
            # Generate a function with ccn > 10
            body = "fn big() -> i32 {\n"
            for i in range(12):
                body += f"    if true {{ return {i} }}\n"
            body += "    return 0\n}\n"
            (Path(d) / "t.vigil").write_text(body)
            rc, out, _ = self._run([str(Path(d) / "t.vigil")])
            self.assertEqual(rc, 0)
            self.assertIn("high", out)

    def test_missing_file_fails(self):
        rc, _, _ = self._run(["/nonexistent/file.vigil"])
        self.assertNotEqual(rc, 0)

    def test_no_args_fails(self):
        r = subprocess.run(
            [VIGIL_BIN, "complexity"],
            capture_output=True, text=True, timeout=10,
        )
        self.assertNotEqual(r.returncode, 0)


if __name__ == "__main__":
    unittest.main()
