"""Integration tests for the audio plugin."""

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


class TestAudioPlugin(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_audio_")
        self.vigil = resolve_vigil_command()
        r = subprocess.run(
            [*self.vigil, "doc", "audio"],
            capture_output=True, text=True, timeout=10,
        )
        if r.returncode != 0:
            self.skipTest("audio plugin not compiled in")

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

    def test_check_audio_engine(self):
        script = self._write(
            "audio_check.vigil",
            'import "audio"\n'
            "fn main() -> i32 {\n"
            '    audio.Engine eng, err e = audio.Engine.new()\n'
            "    if e != ok { return 1 }\n"
            "    eng.destroy()\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_check_audio_sound(self):
        script = self._write(
            "audio_sound.vigil",
            'import "audio"\n'
            "fn main() -> i32 {\n"
            '    audio.Engine eng, err e = audio.Engine.new()\n'
            "    if e != ok { return 1 }\n"
            '    audio.Sound sfx, err e2 = audio.Sound.load(eng, "test.wav")\n'
            "    eng.destroy()\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_check_audio_music(self):
        script = self._write(
            "audio_music.vigil",
            'import "audio"\n'
            "fn main() -> i32 {\n"
            '    audio.Engine eng, err e = audio.Engine.new()\n'
            "    if e != ok { return 1 }\n"
            '    audio.Music bgm, err e2 = audio.Music.load(eng, "test.ogg")\n'
            "    eng.destroy()\n"
            "    return 0\n"
            "}\n",
        )
        r = self._run("check", script)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_doc_audio_lists_classes(self):
        r = self._run("doc", "audio")
        self.assertEqual(r.returncode, 0, r.stderr)
        for name in ["Engine", "Sound", "Music"]:
            self.assertIn(f"audio.{name}", r.stdout)

    def test_doc_audio_lists_listener(self):
        r = self._run("doc", "audio")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("set_listener_position", r.stdout)

    def test_run_engine_init_destroy(self):
        """Engine init + destroy — exits gracefully without audio files."""
        script = self._write(
            "audio_run.vigil",
            'import "fmt"\n'
            'import "audio"\n'
            "fn main() -> i32 {\n"
            '    audio.Engine eng, err e = audio.Engine.new()\n'
            "    if e != ok {\n"
            '        fmt.println("no audio")\n'
            "        return 0\n"
            "    }\n"
            "    eng.set_volume(0.5)\n"
            "    eng.destroy()\n"
            '    fmt.println("ok")\n'
            "    return 0\n"
            "}\n",
        )
        r = self._run("run", script)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("ok", r.stdout)


if __name__ == "__main__":
    unittest.main()
