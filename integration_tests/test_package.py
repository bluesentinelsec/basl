"""Integration tests for 'vigil package' command and args module."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


def resolve_vigil_command():
    env_bin = os.environ.get("VIGIL_BIN")
    if env_bin:
        return [env_bin]
    return [str(Path(__file__).resolve().parent.parent / "build" / "RELEASE" / "vigil")]


class TestVigilArgs(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_args_")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_args_count_and_at(self):
        """args.count() and args.at() return script arguments."""
        script = Path(self.tmpdir) / "test.vigil"
        script.write_text(
            'import "args"\n'
            'import "fmt"\n'
            'fn main() -> i32 {\n'
            '    fmt.println(f"{args.count()}")\n'
            '    return args.count()\n'
            '}\n'
        )
        result = subprocess.run(
            [*resolve_vigil_command(), "run", str(script), "a", "b", "c"],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 3)
        self.assertIn("3", result.stdout)

    def test_args_no_args(self):
        """args.count() returns 0 when no script args."""
        script = Path(self.tmpdir) / "test.vigil"
        script.write_text(
            'import "args"\n'
            'fn main() -> i32 {\n'
            '    return args.count()\n'
            '}\n'
        )
        result = subprocess.run(
            [*resolve_vigil_command(), "run", str(script)],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 0)


class TestVigilPackage(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_pkg_")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_package_and_run(self):
        if sys.platform == "win32":
            self.skipTest("Packaged binary execution unreliable on Windows CI")
        """Package a script and run the standalone binary."""
        script = Path(self.tmpdir) / "hello.vigil"
        script.write_text(
            'import "fmt"\n'
            'fn main() -> i32 {\n'
            '    fmt.println("packaged!")\n'
            '    return 42\n'
            '}\n'
        )
        out_bin = Path(self.tmpdir) / "hello_app"
        result = subprocess.run(
            [*resolve_vigil_command(), "package", str(script), "-o", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue(out_bin.exists())

        # Run the packaged binary.
        result = subprocess.run(
            [str(out_bin)], capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 42)
        self.assertIn("packaged!", result.stdout)

    def test_package_with_args(self):
        if sys.platform == "win32":
            self.skipTest("Packaged binary execution unreliable on Windows CI")
        """Packaged binary passes CLI args to script."""
        script = Path(self.tmpdir) / "argtest.vigil"
        script.write_text(
            'import "args"\n'
            'fn main() -> i32 {\n'
            '    return args.count()\n'
            '}\n'
        )
        out_bin = Path(self.tmpdir) / "argtest_app"
        subprocess.run(
            [*resolve_vigil_command(), "package", str(script), "-o", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        result = subprocess.run(
            [str(out_bin), "x", "y"],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 2)

    def test_package_inspect(self):
        """vigil package --inspect shows bundled files."""
        script = Path(self.tmpdir) / "inspect.vigil"
        script.write_text('fn main() -> i32 { return 0 }\n')
        out_bin = Path(self.tmpdir) / "inspect_app"
        subprocess.run(
            [*resolve_vigil_command(), "package", str(script), "-o", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        result = subprocess.run(
            [*resolve_vigil_command(), "package", "--inspect", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("entry.vigil", result.stdout)

    def test_package_inspect_requires_path(self):
        """vigil package --inspect without a path should fail with usage error."""
        result = subprocess.run(
            [*resolve_vigil_command(), "package", "--inspect"],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("--inspect requires a binary path", result.stderr)

    def test_package_default_output_path(self):
        if sys.platform == "win32":
            self.skipTest("Packaged binary execution unreliable on Windows CI")
        """vigil package without -o should derive output name from the script path."""
        script = Path(self.tmpdir) / "default_name.vigil"
        script.write_text('fn main() -> i32 { return 0 }\n')
        result = subprocess.run(
            [*resolve_vigil_command(), "package", str(script)],
            capture_output=True, text=True, timeout=10, cwd=self.tmpdir
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue((Path(self.tmpdir) / "default_name").exists())

    def test_package_collects_imported_sources(self):
        """Packaged binary should include imported non-entry source files."""
        script = Path(self.tmpdir) / "main.vigil"
        helper = Path(self.tmpdir) / "helper.vigil"
        out_bin = Path(self.tmpdir) / "with_imports"

        helper.write_text('pub fn value() -> i32 { return 7 }\n')
        script.write_text(
            'import "helper"\n'
            'fn main() -> i32 {\n'
            '    return helper.value()\n'
            '}\n'
        )

        result = subprocess.run(
            [*resolve_vigil_command(), "package", str(script), "-o", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)

        result = subprocess.run(
            [*resolve_vigil_command(), "package", "--inspect", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("helper.vigil", result.stdout)

    def test_package_encrypted(self):
        if sys.platform == "win32":
            self.skipTest("Packaged binary execution unreliable on Windows CI")
        """Encrypted package runs correctly."""
        script = Path(self.tmpdir) / "secret.vigil"
        script.write_text(
            'import "fmt"\n'
            'fn main() -> i32 {\n'
            '    fmt.println("secret!")\n'
            '    return 7\n'
            '}\n'
        )
        out_bin = Path(self.tmpdir) / "secret_app"
        subprocess.run(
            [*resolve_vigil_command(), "package", str(script),
             "-o", str(out_bin), "--key", "mypassword"],
            capture_output=True, text=True, timeout=10
        )
        result = subprocess.run(
            [str(out_bin)], capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 7)
        self.assertIn("secret!", result.stdout)

        # Verify source is not visible in binary.
        with open(out_bin, "rb") as f:
            binary_data = f.read()
        self.assertNotIn(b"secret!", binary_data[-1000:])


if __name__ == "__main__":
    unittest.main()
