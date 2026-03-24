"""Integration tests for vigil check."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


def resolve_vigil_command():
    env_bin = os.environ.get("VIGIL_BIN")
    if env_bin:
        return [env_bin]
    return [str(Path(__file__).resolve().parent.parent / "build" / "RELEASE" / "vigil")]


class TestVigilCheck(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_check_")

    def _write(self, relpath, content):
        path = Path(self.tmpdir) / relpath
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def test_check_succeeds_for_project_imports(self):
        self._write("vigil.toml", '[project]\nname = "checkproj"\n')
        self._write("lib/helper.vigil", "pub fn value() -> i32 { return 7; }\n")
        script = self._write(
            "main.vigil",
            'import "helper";\n'
            "fn main() -> i32 {\n"
            "    return helper.value();\n"
            "}\n",
        )

        result = subprocess.run(
            [*resolve_vigil_command(), "check", str(script)],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
            timeout=10,
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout, "")

    def test_check_reports_compile_diagnostic(self):
        script = self._write(
            "bad.vigil",
            "fn main() -> i32 {\n"
            "    return broken(\n"
            "}\n",
        )

        result = subprocess.run(
            [*resolve_vigil_command(), "check", str(script)],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
            timeout=10,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("error:", result.stderr)
        self.assertIn("bad.vigil", result.stderr)

    def test_check_rejects_circular_import(self):
        self._write("vigil.toml", '[project]\nname = "circproj"\n')
        self._write(
            "lib/a.vigil",
            'import "b";\npub fn val() -> i32 { return b.get(); }\n',
        )
        self._write(
            "lib/b.vigil",
            'import "a";\npub fn get() -> i32 { return 1; }\n',
        )
        script = self._write(
            "main.vigil",
            'import "a";\nfn main() -> i32 { return a.val(); }\n',
        )

        result = subprocess.run(
            [*resolve_vigil_command(), "check", str(script)],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
            timeout=10,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("circular import detected", result.stderr)


if __name__ == "__main__":
    unittest.main()
