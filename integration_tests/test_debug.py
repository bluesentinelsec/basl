"""Integration tests for vigil debug."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


def resolve_vigil_command():
    env_bin = os.environ.get("VIGIL_BIN")
    if env_bin:
        return [env_bin]

    repo_root = Path(__file__).resolve().parent.parent
    release_bin = repo_root / "build" / "RELEASE" / "vigil"
    if release_bin.exists():
        return [str(release_bin)]
    return [str(repo_root / "build" / "vigil")]


class TestVigilDebug(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_debug_")

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _write(self, relpath, content):
        path = Path(self.tmpdir) / relpath
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def _run_debug(self, command_input, source=None):
        if source is None:
            source = (
                "fn main() -> i32 {\n"
                "    i32 value = 1\n"
                "    return value\n"
                "}\n"
            )

        script = self._write(
            "main.vigil",
            source,
        )

        return subprocess.run(
            [*resolve_vigil_command(), "debug", "--interactive", str(script)],
            cwd=self.tmpdir,
            input=command_input,
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_debug_interactive_help_and_quit(self):
        result = self._run_debug("  help\nq\n")

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("VIGIL Interactive Debugger", result.stdout)
        self.assertIn("Stepped:", result.stdout)
        self.assertIn("Debugger commands:", result.stdout)

    def test_debug_interactive_command_routing(self):
        source = (
            "fn helper(i32 x) -> i32 {\n"
            "    i32 inner = x + 1\n"
            "    return inner\n"
            "}\n"
            "\n"
            "fn main() -> i32 {\n"
            "    i32 value = helper(1)\n"
            "    return value\n"
            "}\n"
        )
        result = self._run_debug(
            "bt\n"
            "l\n"
            "list 2\n"
            "w\n"
            "b 8\n"
            "c\n"
            "p value\n"
            "d 0\n"
            "?\n"
            "q\n",
            source,
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("#0 main", result.stdout)
        self.assertIn("(no locals)", result.stdout)
        self.assertIn("Breakpoint 0 set at line 8", result.stdout)
        self.assertIn("Breakpoint hit:", result.stdout)
        self.assertIn("2", result.stdout)
        self.assertIn("Breakpoint 0 deleted", result.stdout)
        self.assertIn("Unknown command: ?", result.stdout)

    def test_debug_interactive_invalid_commands(self):
        source = (
            "fn helper(i32 x) -> i32 {\n"
            "    i32 inner = x + 1\n"
            "    return inner\n"
            "}\n"
            "\n"
            "fn main() -> i32 {\n"
            "    i32 value = helper(1)\n"
            "    return value\n"
            "}\n"
        )
        result = self._run_debug("b missing\nb \np \nd 99\nq\n", source)

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Function 'missing' not found", result.stdout)
        self.assertIn("Usage: b <line> or b <function>", result.stdout)
        self.assertIn("Usage: p <variable>", result.stdout)
        self.assertIn("No such breakpoint", result.stdout)

    def test_debug_interactive_resume_commands_finish_program(self):
        source = (
            "fn helper(i32 x) -> i32 {\n"
            "    i32 inner = x + 1\n"
            "    return inner\n"
            "}\n"
            "\n"
            "fn main() -> i32 {\n"
            "    i32 value = helper(1)\n"
            "    return value\n"
            "}\n"
        )

        for command in ("continue\n", "step\n", "n\n", "o\n"):
            with self.subTest(command=command.strip()):
                result = self._run_debug(command, source)
                self.assertEqual(result.returncode, 0, msg=result.stderr)
                self.assertIn("Program finished normally.", result.stdout)


if __name__ == "__main__":
    unittest.main()
