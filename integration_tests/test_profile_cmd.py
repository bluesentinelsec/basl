"""Integration tests for 'vigil profile' command."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")


def run_profile(script_content):
    with tempfile.TemporaryDirectory(prefix="vigil_prof_") as d:
        p = Path(d) / "main.vigil"
        p.write_text(script_content)
        r = subprocess.run(
            [VIGIL_BIN, "profile", str(p)],
            capture_output=True, text=True, timeout=30,
        )
        return r.returncode, r.stdout, r.stderr


class ProfileOutputTest(unittest.TestCase):
    def test_profile_shows_timing(self):
        rc, out, _ = run_profile("fn main() -> i32 { return 0 }")
        self.assertEqual(rc, 0)
        self.assertIn("Timing", out)
        self.assertIn("Total:", out)
        self.assertIn("Compile:", out)
        self.assertIn("Execute:", out)

    def test_profile_shows_memory(self):
        rc, out, _ = run_profile("fn main() -> i32 { return 0 }")
        self.assertEqual(rc, 0)
        self.assertIn("Memory", out)
        self.assertIn("Peak RSS:", out)
        self.assertIn("Allocations:", out)
        self.assertIn("Bytes alloc:", out)

    def test_profile_invalid_source_fails(self):
        rc, _, _ = run_profile("this is not valid")
        self.assertNotEqual(rc, 0)

    def test_profile_missing_file_fails(self):
        r = subprocess.run(
            [VIGIL_BIN, "profile", "/nonexistent/file.vigil"],
            capture_output=True, text=True, timeout=10,
        )
        self.assertNotEqual(r.returncode, 0)


if __name__ == "__main__":
    unittest.main()
