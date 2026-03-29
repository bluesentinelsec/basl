"""Integration tests for 'vigil editor' CLI command."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")


def run_editor(args, env_override=None):
    env = dict(os.environ)
    if env_override:
        env.update(env_override)
    r = subprocess.run(
        [VIGIL_BIN, "editor"] + args,
        capture_output=True, text=True, env=env, timeout=10,
    )
    return r.returncode, r.stdout, r.stderr


class EditorListTest(unittest.TestCase):
    def test_list_shows_supported_editors(self):
        rc, out, _ = run_editor(["list"])
        self.assertEqual(rc, 0)
        self.assertIn("nvim", out)
        self.assertIn("vim", out)
        self.assertIn("vscode", out)


class EditorInstallTest(unittest.TestCase):
    def test_install_nvim_creates_files(self):
        with tempfile.TemporaryDirectory() as d:
            rc, out, _ = run_editor(
                ["install", "nvim"],
                env_override={"XDG_CONFIG_HOME": d},
            )
            self.assertEqual(rc, 0, f"stderr: {out}")
            lsp = Path(d) / "nvim" / "after" / "plugin" / "vigil.lua"
            ft = Path(d) / "nvim" / "after" / "ftdetect" / "vigil.lua"
            self.assertTrue(lsp.exists(), f"missing {lsp}")
            self.assertTrue(ft.exists(), f"missing {ft}")
            # Verify content references vigil lsp
            self.assertIn("lsp", lsp.read_text())

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

    def test_install_unknown_editor_fails(self):
        rc, _, err = run_editor(["install", "notepad"])
        self.assertNotEqual(rc, 0)
        self.assertIn("unknown editor", err)

    def test_install_missing_editor_arg_fails(self):
        rc, _, err = run_editor(["install"])
        self.assertNotEqual(rc, 0)


class EditorUninstallTest(unittest.TestCase):
    def test_uninstall_removes_files(self):
        with tempfile.TemporaryDirectory() as d:
            run_editor(["install", "nvim"], env_override={"XDG_CONFIG_HOME": d})
            lsp = Path(d) / "nvim" / "after" / "plugin" / "vigil.lua"
            self.assertTrue(lsp.exists())

            rc, _, _ = run_editor(
                ["uninstall", "nvim"],
                env_override={"XDG_CONFIG_HOME": d},
            )
            self.assertEqual(rc, 0)
            self.assertFalse(lsp.exists())

    def test_uninstall_when_not_installed_succeeds(self):
        with tempfile.TemporaryDirectory() as d:
            rc, _, _ = run_editor(
                ["uninstall", "nvim"],
                env_override={"XDG_CONFIG_HOME": d},
            )
            self.assertEqual(rc, 0)


class EditorStatusTest(unittest.TestCase):
    def test_status_shows_installed(self):
        with tempfile.TemporaryDirectory() as d:
            run_editor(["install", "nvim"], env_override={"XDG_CONFIG_HOME": d})
            rc, out, _ = run_editor(
                ["status"],
                env_override={"XDG_CONFIG_HOME": d},
            )
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
