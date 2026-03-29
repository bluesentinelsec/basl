"""Integration tests for 'vigil editor' CLI command."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")


def run_editor(args, env_override=None):
    env = dict(os.environ)
    if env_override:
        env.update(env_override)
        # Ensure both HOME and USERPROFILE are set for cross-platform compat
        if "HOME" in env_override and "USERPROFILE" not in env_override:
            env["USERPROFILE"] = env_override["HOME"]
        if "USERPROFILE" in env_override and "HOME" not in env_override:
            env["HOME"] = env_override["USERPROFILE"]
    r = subprocess.run(
        [VIGIL_BIN, "editor"] + args,
        capture_output=True, text=True, env=env, timeout=10,
    )
    return r.returncode, r.stdout, r.stderr


class EditorListTest(unittest.TestCase):
    def test_list_shows_supported_editors(self):
        rc, out, _ = run_editor(["list"])
        self.assertEqual(rc, 0)
        self.assertIn("vim", out)
        self.assertIn("nvim", out)
        self.assertIn("vscode", out)
        self.assertIn("emacs", out)
        self.assertIn("sublime", out)


class EditorInstallTest(unittest.TestCase):
    def test_install_nvim_creates_files(self):
        with tempfile.TemporaryDirectory() as d:
            rc, out, _ = run_editor(
                ["install", "nvim"],
                env_override={"HOME": d},
            )
            self.assertEqual(rc, 0, f"stderr: {out}")
            ft = Path(d) / ".config" / "nvim" / "after" / "ftdetect" / "vigil.vim"
            syn = Path(d) / ".config" / "nvim" / "after" / "syntax" / "vigil.vim"
            self.assertTrue(ft.exists())
            self.assertTrue(syn.exists())

    def test_install_vim_creates_files(self):
        with tempfile.TemporaryDirectory() as d:
            rc, out, _ = run_editor(
                ["install", "vim"],
                env_override={"HOME": d},
            )
            self.assertEqual(rc, 0, f"stderr: {out}")
            ft = Path(d) / ".vim" / "after" / "ftdetect" / "vigil.vim"
            syn = Path(d) / ".vim" / "after" / "syntax" / "vigil.vim"
            self.assertTrue(ft.exists())
            self.assertTrue(syn.exists())

    def test_install_emacs_creates_mode_and_init(self):
        with tempfile.TemporaryDirectory() as d:
            rc, out, _ = run_editor(
                ["install", "emacs"],
                env_override={"HOME": d},
            )
            self.assertEqual(rc, 0, f"stderr: {out}")
            mode_file = Path(d) / ".emacs.d" / "vigil-mode.el"
            init_file = Path(d) / ".emacs.d" / "init.el"
            self.assertTrue(mode_file.exists())
            self.assertTrue(init_file.exists())
            self.assertIn("vigil-mode", init_file.read_text())

    def test_install_sublime_creates_syntax(self):
        with tempfile.TemporaryDirectory() as d:
            rc, out, _ = run_editor(
                ["install", "sublime"],
                env_override={"HOME": d},
            )
            self.assertEqual(rc, 0, f"stderr: {out}")
            if os.name == "nt":
                syntax = Path(d) / "AppData" / "Roaming" / "Sublime Text" / "Packages" / "Vigil" / "Vigil.sublime-syntax"
            elif sys.platform == "darwin":
                syntax = Path(d) / "Library" / "Application Support" / "Sublime Text" / "Packages" / "Vigil" / "Vigil.sublime-syntax"
            else:
                syntax = Path(d) / ".config" / "sublime-text" / "Packages" / "Vigil" / "Vigil.sublime-syntax"
            self.assertTrue(syntax.exists())

    def test_install_unknown_editor_fails(self):
        rc, _, err = run_editor(["install", "notepad"])
        self.assertNotEqual(rc, 0)
        self.assertIn("unknown editor", err)

    def test_install_missing_editor_arg_fails(self):
        rc, _, err = run_editor(["install"])
        self.assertNotEqual(rc, 0)


class EditorUninstallTest(unittest.TestCase):
    def test_uninstall_emacs_removes_mode_file(self):
        with tempfile.TemporaryDirectory() as d:
            run_editor(["install", "emacs"], env_override={"HOME": d})
            mode_file = Path(d) / ".emacs.d" / "vigil-mode.el"
            self.assertTrue(mode_file.exists())
            rc, _, _ = run_editor(["uninstall", "emacs"], env_override={"HOME": d})
            self.assertEqual(rc, 0)
            self.assertFalse(mode_file.exists())

    def test_uninstall_removes_files(self):
        with tempfile.TemporaryDirectory() as d:
            run_editor(["install", "vim"], env_override={"HOME": d})
            ft = Path(d) / ".vim" / "after" / "ftdetect" / "vigil.vim"
            self.assertTrue(ft.exists())
            rc, _, _ = run_editor(["uninstall", "vim"], env_override={"HOME": d})
            self.assertEqual(rc, 0)
            self.assertFalse(ft.exists())

    def test_uninstall_when_not_installed_succeeds(self):
        with tempfile.TemporaryDirectory() as d:
            rc, _, _ = run_editor(["uninstall", "vim"], env_override={"HOME": d})
            self.assertEqual(rc, 0)


class EditorStatusTest(unittest.TestCase):
    def test_status_shows_installed(self):
        with tempfile.TemporaryDirectory() as d:
            run_editor(["install", "nvim"], env_override={"HOME": d})
            rc, out, _ = run_editor(["status"], env_override={"HOME": d})
            self.assertEqual(rc, 0)
            self.assertIn("nvim", out)
            self.assertIn("installed", out)

    def test_status_empty_when_nothing_installed(self):
        with tempfile.TemporaryDirectory() as d:
            rc, out, _ = run_editor(
                ["status"],
                env_override={"XDG_CONFIG_HOME": d, "HOME": d},
            )
            self.assertEqual(rc, 0)
            self.assertIn("No editor", out)


if __name__ == "__main__":
    unittest.main()
