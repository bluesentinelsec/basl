"""Integration tests for the gui plugin."""

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


class TestGuiPlugin(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_gui_")
        self.vigil = resolve_vigil_command()
        # Skip all tests if the gui plugin is not compiled in.
        r = subprocess.run(
            [*self.vigil, "doc", "gui"],
            capture_output=True, text=True, timeout=10,
        )
        if r.returncode != 0:
            self.skipTest("gui plugin not compiled in")

    def _write(self, name, content):
        path = Path(self.tmpdir) / name
        path.write_text(content, encoding="utf-8")
        return str(path)

    def _run(self, *args, **kwargs):
        return subprocess.run(
            [*self.vigil, *args],
            capture_output=True,
            text=True,
            timeout=10,
            **kwargs,
        )

    # ── vigil check ──────────────────────────────────────────────

    def test_check_gui_program_succeeds(self):
        script = self._write(
            "gui_check.vigil",
            'import "gui"\n'
            "fn main() -> i32 {\n"
            '    gui.App app, err e = gui.App.new("test")\n'
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_check_gui_window_label_button(self):
        script = self._write(
            "gui_full.vigil",
            'import "gui"\n'
            "fn main() -> i32 {\n"
            '    gui.App app, err e = gui.App.new("t")\n'
            '    gui.Window win, err e2 = gui.Window.new(app, "w", 640, 480)\n'
            '    gui.Label lbl, err e3 = gui.Label.new(win, "hi")\n'
            "    lbl.grid(0, 0)\n"
            '    gui.Button btn, err e4 = gui.Button.new(win, "ok")\n'
            "    btn.grid(0, 1)\n"
            "    btn.on_click(fn() -> void {})\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_check_gui_type_error(self):
        script = self._write(
            "gui_typerr.vigil",
            'import "gui"\n'
            "fn main() -> i32 {\n"
            '    gui.App app, err e = gui.App.new(42)\n'
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("type", r.stderr.lower())

    # ── vigil doc ────────────────────────────────────────────────

    def test_doc_gui_lists_classes(self):
        r = self._run("doc", "gui")
        self.assertEqual(r.returncode, 0, r.stderr)
        for name in ["App", "Window", "Label", "Button"]:
            self.assertIn(f"gui.{name}", r.stdout)

    def test_doc_gui_lists_message_box(self):
        r = self._run("doc", "gui")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("gui.message_box", r.stdout)

    def test_doc_gui_lists_methods(self):
        r = self._run("doc", "gui")
        self.assertEqual(r.returncode, 0, r.stderr)
        for method in ["on_click", "grid", "set_text", "main_loop"]:
            self.assertIn(method, r.stdout)


if __name__ == "__main__":
    unittest.main()

    # ── Phase 2 widgets ──────────────────────────────────────────

    def test_check_entry_checkbox(self):
        script = self._write(
            "gui_entry.vigil",
            'import "gui"\n'
            "fn main() -> i32 {\n"
            '    gui.App app, err e = gui.App.new("t")\n'
            '    gui.Window win, err e2 = gui.Window.new(app, "w", 400, 300)\n'
            "    gui.Entry ent, err e3 = gui.Entry.new(win)\n"
            '    gui.Checkbox cb, err e4 = gui.Checkbox.new(win, "opt")\n'
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_check_slider_select(self):
        script = self._write(
            "gui_slider.vigil",
            'import "gui"\n'
            "fn main() -> i32 {\n"
            '    gui.App app, err e = gui.App.new("t")\n'
            '    gui.Window win, err e2 = gui.Window.new(app, "w", 400, 300)\n'
            "    gui.Slider sl, err e3 = gui.Slider.new(win, 0.0, 100.0)\n"
            "    gui.Select sel, err e4 = gui.Select.new(win)\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_check_frame_listbox_menu(self):
        script = self._write(
            "gui_frame.vigil",
            'import "gui"\n'
            "fn main() -> i32 {\n"
            '    gui.App app, err e = gui.App.new("t")\n'
            '    gui.Window win, err e2 = gui.Window.new(app, "w", 400, 300)\n'
            '    gui.Frame frm, err e3 = gui.Frame.new(win, "opts")\n'
            "    gui.Listbox lb, err e4 = gui.Listbox.new(win)\n"
            "    gui.Menu menu, err e5 = gui.Menu.new(win)\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_doc_gui_lists_new_classes(self):
        r = self._run("doc", "gui")
        self.assertEqual(r.returncode, 0, r.stderr)
        for name in ["Entry", "Checkbox", "Slider", "Select", "Frame",
                      "Listbox", "Menu", "Canvas", "Toplevel"]:
            self.assertIn(f"gui.{name}", r.stdout)
