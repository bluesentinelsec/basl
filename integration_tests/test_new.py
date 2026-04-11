"""Integration tests for `vigil new` command.

Uses pexpect for interactive prompt testing and subprocess for non-interactive cases.
Requires VIGIL_BIN environment variable pointing to the vigil binary.
"""

import os
import shutil
import subprocess
import tempfile
import unittest

try:
    import pexpect
except ImportError:
    pexpect = None

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")


def run_new(*args, cwd=None):
    """Run vigil new with given args, return (returncode, stdout, stderr)."""
    result = subprocess.run(
        [VIGIL_BIN, "new"] + list(args),
        capture_output=True, text=True, cwd=cwd, timeout=10,
        stdin=subprocess.DEVNULL
    )
    return result.returncode, result.stdout, result.stderr


class TestNewCLI(unittest.TestCase):
    """Tests for --type cli (default)."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_basic_cli_project(self):
        rc, out, err = run_new("myapp", cwd=self.tmpdir)
        self.assertEqual(rc, 0, err)
        self.assertIn("created myapp", out)
        d = os.path.join(self.tmpdir, "myapp")
        self.assertTrue(os.path.isfile(os.path.join(d, "vigil.toml")))
        self.assertTrue(os.path.isfile(os.path.join(d, "main.vigil")))
        self.assertTrue(os.path.isfile(os.path.join(d, "lib", "myapp.vigil")))
        self.assertTrue(os.path.isfile(os.path.join(d, "test", "myapp_test.vigil")))
        self.assertTrue(os.path.isfile(os.path.join(d, ".gitignore")))

    def test_toml_has_all_fields(self):
        run_new("myapp", cwd=self.tmpdir)
        toml = open(os.path.join(self.tmpdir, "myapp", "vigil.toml")).read()
        for field in ["name", "description", "version", "type", "org", "author",
                       "license", "homepage", "repository", "readme", "keywords",
                       "platforms", "[icon]", "[platform.macos]", "[platform.ios]",
                       "[platform.android]", "[platform.windows]", "[platform.linux]",
                       "[dependencies]"]:
            self.assertIn(field, toml, f"Missing field: {field}")

    def test_toml_values(self):
        run_new("myapp", "-d", "My App", "--org", "com.test", "--version", "2.0.0", cwd=self.tmpdir)
        toml = open(os.path.join(self.tmpdir, "myapp", "vigil.toml")).read()
        self.assertIn('name = "myapp"', toml)
        self.assertIn('description = "My App"', toml)
        self.assertIn('version = "2.0.0"', toml)
        self.assertIn('org = "com.test"', toml)
        self.assertIn('type = "cli"', toml)

    def test_default_platforms_cli(self):
        run_new("myapp", cwd=self.tmpdir)
        toml = open(os.path.join(self.tmpdir, "myapp", "vigil.toml")).read()
        self.assertIn('"windows"', toml)
        self.assertIn('"linux"', toml)
        self.assertIn('"macos"', toml)
        self.assertNotIn('"ios"', toml.split("platforms")[1].split("\n")[0])

    def test_custom_platforms(self):
        run_new("myapp", "-p", "linux,web", cwd=self.tmpdir)
        toml = open(os.path.join(self.tmpdir, "myapp", "vigil.toml")).read()
        line = [l for l in toml.split("\n") if l.startswith("platforms")][0]
        self.assertIn('"linux"', line)
        self.assertIn('"web"', line)

    def test_project_name_flag(self):
        rc, out, _ = run_new("--project-name", "myapp", cwd=self.tmpdir)
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.isdir(os.path.join(self.tmpdir, "myapp")))

    def test_scaffold_compiles(self):
        """Generated CLI scaffold should pass vigil check."""
        run_new("myapp", cwd=self.tmpdir)
        main = os.path.join(self.tmpdir, "myapp", "main.vigil")
        result = subprocess.run([VIGIL_BIN, "check", main], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)


class TestNewLibrary(unittest.TestCase):
    """Tests for --type library."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_library_no_main(self):
        run_new("mylib", "-t", "library", cwd=self.tmpdir)
        d = os.path.join(self.tmpdir, "mylib")
        self.assertFalse(os.path.exists(os.path.join(d, "main.vigil")))
        self.assertTrue(os.path.isfile(os.path.join(d, "lib", "mylib.vigil")))
        self.assertTrue(os.path.isfile(os.path.join(d, "test", "mylib_test.vigil")))

    def test_library_toml_type(self):
        run_new("mylib", "-t", "library", cwd=self.tmpdir)
        toml = open(os.path.join(self.tmpdir, "mylib", "vigil.toml")).read()
        self.assertIn('type = "library"', toml)


class TestNewGUI(unittest.TestCase):
    """Tests for --type gui."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_gui_structure(self):
        run_new("mygui", "-t", "gui", cwd=self.tmpdir)
        d = os.path.join(self.tmpdir, "mygui")
        self.assertTrue(os.path.isfile(os.path.join(d, "main.vigil")))
        self.assertTrue(os.path.isfile(os.path.join(d, "lib", "mygui.vigil")))
        lib = open(os.path.join(d, "lib", "mygui.vigil")).read()
        self.assertIn('import "sdl"', lib)
        self.assertIn("SDL3", lib)

    def test_gui_default_platforms(self):
        run_new("mygui", "-t", "gui", cwd=self.tmpdir)
        toml = open(os.path.join(self.tmpdir, "mygui", "vigil.toml")).read()
        for p in ["windows", "linux", "macos", "ios", "android", "web"]:
            self.assertIn(f'"{p}"', toml)


class TestNewWorkspace(unittest.TestCase):
    """Tests for --type workspace."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_workspace_structure(self):
        run_new("myws", "-t", "workspace", cwd=self.tmpdir)
        d = os.path.join(self.tmpdir, "myws")
        self.assertTrue(os.path.isfile(os.path.join(d, "vigil.toml")))
        self.assertTrue(os.path.isfile(os.path.join(d, ".gitignore")))
        self.assertFalse(os.path.exists(os.path.join(d, "main.vigil")))
        self.assertFalse(os.path.exists(os.path.join(d, "lib")))
        self.assertFalse(os.path.exists(os.path.join(d, "test")))

    def test_workspace_toml(self):
        run_new("myws", "-t", "workspace", cwd=self.tmpdir)
        toml = open(os.path.join(self.tmpdir, "myws", "vigil.toml")).read()
        self.assertIn("[workspace]", toml)
        self.assertIn('members = ["*"]', toml)
        self.assertNotIn("[icon]", toml)
        self.assertNotIn("[platform.", toml)

    def test_workspace_rejects_platforms(self):
        rc, _, err = run_new("myws", "-t", "workspace", "-p", "linux", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("not allowed for workspace", err)

    def test_workspace_member_creation(self):
        """Creating a project inside a workspace creates relative to workspace root."""
        ws = os.path.join(self.tmpdir, "myws")
        run_new("myws", "-t", "workspace", cwd=self.tmpdir)
        run_new("cat", "-t", "cli", cwd=ws)
        self.assertTrue(os.path.isdir(os.path.join(ws, "cat")))
        self.assertTrue(os.path.isfile(os.path.join(ws, "cat", "vigil.toml")))
        self.assertTrue(os.path.isfile(os.path.join(ws, "cat", "main.vigil")))

    def test_workspace_glob_aware_creation(self):
        """members = ['crates/*'] should create under crates/<name>/."""
        ws = os.path.join(self.tmpdir, "myws")
        run_new("myws", "-t", "workspace", cwd=self.tmpdir)
        # Modify members to use crates/* glob
        toml_path = os.path.join(ws, "vigil.toml")
        content = open(toml_path).read()
        content = content.replace('members = ["*"]', 'members = ["crates/*"]')
        open(toml_path, "w").write(content)
        os.makedirs(os.path.join(ws, "crates"), exist_ok=True)
        run_new("mylib", "-t", "library", cwd=ws)
        self.assertTrue(os.path.isdir(os.path.join(ws, "crates", "mylib")))

    def test_workspace_explicit_list_append(self):
        """Explicit member list gets new member appended."""
        ws = os.path.join(self.tmpdir, "myws")
        run_new("myws", "-t", "workspace", cwd=self.tmpdir)
        toml_path = os.path.join(ws, "vigil.toml")
        content = open(toml_path).read()
        content = content.replace('members = ["*"]', 'members = ["cat"]')
        open(toml_path, "w").write(content)
        run_new("grep", "-t", "cli", cwd=ws)
        toml = open(toml_path).read()
        self.assertIn('"grep"', toml)

    def test_workspace_metadata_inheritance(self):
        """Members inherit org and version from workspace root."""
        ws = os.path.join(self.tmpdir, "myws")
        run_new("myws", "-t", "workspace", "--org", "com.myorg", "--version", "2.0.0", cwd=self.tmpdir)
        run_new("svc", "-t", "cli", cwd=ws)
        toml = open(os.path.join(ws, "svc", "vigil.toml")).read()
        self.assertIn('org = "com.myorg"', toml)
        self.assertIn('version = "2.0.0"', toml)

    def test_workspace_member_from_subdir(self):
        """Creating from a workspace subdirectory still creates relative to root."""
        ws = os.path.join(self.tmpdir, "myws")
        run_new("myws", "-t", "workspace", cwd=self.tmpdir)
        subdir = os.path.join(ws, "subdir")
        os.makedirs(subdir)
        run_new("app", "-t", "cli", cwd=subdir)
        self.assertTrue(os.path.isdir(os.path.join(ws, "app")))


class TestNewValidation(unittest.TestCase):
    """Tests for input validation."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_reject_absolute_path(self):
        rc, _, err = run_new("/abs/path", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("path separator", err)

    def test_reject_relative_path(self):
        rc, _, err = run_new("../escape", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("path separator", err)

    def test_reject_windows_reserved(self):
        for name in ["con", "CON", "prn", "aux", "nul", "com1", "lpt1"]:
            rc, _, err = run_new(name, cwd=self.tmpdir)
            self.assertNotEqual(rc, 0, f"{name} should be rejected")
            self.assertIn("Windows reserved", err)

    def test_reject_leading_digit(self):
        rc, _, err = run_new("123app", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("invalid identifier", err)

    def test_reject_unknown_type(self):
        rc, _, err = run_new("foo", "-t", "bogus", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("unknown project type", err)

    def test_reject_invalid_org(self):
        rc, _, err = run_new("foo", "--org", "not valid", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("invalid --org", err)

    def test_reject_invalid_platforms(self):
        rc, _, err = run_new("foo", "-p", "windows,bogus", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("invalid --platforms", err)

    def test_reject_duplicate_directory(self):
        run_new("dup", cwd=self.tmpdir)
        rc, _, err = run_new("dup", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("already exists", err)

    def test_reject_empty_name_noninteractive(self):
        rc, _, err = run_new(cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("non-interactive", err)

    def test_name_normalization(self):
        run_new("My Cool App", cwd=self.tmpdir)
        d = os.path.join(self.tmpdir, "my_cool_app")
        self.assertTrue(os.path.isdir(d))
        toml = open(os.path.join(d, "vigil.toml")).read()
        self.assertIn('name = "My Cool App"', toml)

    def test_normalization_collision_message(self):
        run_new("foo", cwd=self.tmpdir)
        rc, _, err = run_new("FOO", cwd=self.tmpdir)
        self.assertNotEqual(rc, 0)
        self.assertIn("normalized from", err)


@unittest.skipIf(pexpect is None, "pexpect not installed")
class TestNewInteractive(unittest.TestCase):
    """Tests for interactive prompt using pexpect."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_prompt_for_name(self):
        child = pexpect.spawn(VIGIL_BIN, ["new"], cwd=self.tmpdir, timeout=10)
        child.expect("Project name:")
        child.sendline("prompted_app")
        child.expect(pexpect.EOF)
        child.close()
        self.assertEqual(child.exitstatus, 0)
        self.assertTrue(os.path.isdir(os.path.join(self.tmpdir, "prompted_app")))


if __name__ == "__main__":
    unittest.main()
