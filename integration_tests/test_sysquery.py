"""Integration tests for the sysquery plugin."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


def resolve_vigil_command():
    env_bin = os.environ.get("VIGIL_BIN")
    if env_bin:
        return [env_bin]
    return [str(Path(__file__).resolve().parent.parent / "build" / "vigil")]


class TestSysqueryPlugin(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_sysquery_")
        self.vigil = resolve_vigil_command()
        r = subprocess.run(
            [*self.vigil, "doc", "sysquery"],
            capture_output=True, text=True, timeout=10,
        )
        if r.returncode != 0:
            self.skipTest("sysquery plugin not compiled in")

    def _write(self, name, content):
        path = Path(self.tmpdir) / name
        path.write_text(content, encoding="utf-8")
        return str(path)

    def _run(self, *args, **kwargs):
        return subprocess.run(
            [*self.vigil, *args],
            capture_output=True, text=True, timeout=10,
            **kwargs,
        )

    def test_check_sysquery_getuid(self):
        script = self._write(
            "sq_check.vigil",
            'import "sysquery"\n'
            "fn main() -> i32 {\n"
            "    string user = sysquery.getuid()\n"
            "    string time = sysquery.localtime()\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_check_sysquery_resolve(self):
        script = self._write(
            "sq_resolve.vigil",
            'import "sysquery"\n'
            "fn main() -> i32 {\n"
            "    string proxy = sysquery.getproxy()\n"
            "    string sid = sysquery.getsid()\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_doc_sysquery_lists_functions(self):
        r = self._run("doc", "sysquery")
        self.assertEqual(r.returncode, 0, r.stderr)
        for fn in ["sysinfo", "getuid", "ps", "ifconfig", "netstat", "resolve"]:
            self.assertIn(fn, r.stdout)

    def test_run_getuid(self):
        """getuid returns a non-empty string."""
        script = self._write(
            "sq_run.vigil",
            'import "fmt"\n'
            'import "sysquery"\n'
            "fn main() -> i32 {\n"
            "    string user = sysquery.getuid()\n"
            "    fmt.println(user)\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("run", script)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(len(r.stdout.strip()) > 0, "getuid returned empty")

    def test_run_localtime(self):
        """localtime returns a date string."""
        script = self._write(
            "sq_time.vigil",
            'import "fmt"\n'
            'import "sysquery"\n'
            "fn main() -> i32 {\n"
            "    string t = sysquery.localtime()\n"
            "    fmt.println(t)\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("run", script)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("202", r.stdout)  # year starts with 202x


if __name__ == "__main__":
    unittest.main()
