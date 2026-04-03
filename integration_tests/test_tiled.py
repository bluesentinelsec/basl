"""Integration tests for the tiled plugin."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")


def run_vigil(root: Path, script: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [VIGIL_BIN, "run", str(root / script)],
        capture_output=True, text=True, timeout=10,
    )


def write_sources(root: Path, sources: dict[str, str]) -> None:
    for relpath, text in sources.items():
        path = root / relpath
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


SAMPLE_MAP = {
    "width": 4,
    "height": 3,
    "tilewidth": 16,
    "tileheight": 16,
    "orientation": "orthogonal",
    "renderorder": "right-down",
    "infinite": False,
    "layers": [
        {
            "name": "Ground",
            "type": "tilelayer",
            "id": 1,
            "width": 4,
            "height": 3,
            "data": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12],
            "opacity": 1.0,
            "visible": True,
        },
        {
            "name": "Entities",
            "type": "objectgroup",
            "id": 2,
            "objects": [
                {
                    "id": 1,
                    "name": "player_spawn",
                    "type": "spawn",
                    "x": 32.0,
                    "y": 48.0,
                    "width": 16.0,
                    "height": 16.0,
                },
                {
                    "id": 2,
                    "name": "exit",
                    "type": "trigger",
                    "x": 0.0,
                    "y": 0.0,
                    "width": 0.0,
                    "height": 0.0,
                    "point": True,
                },
            ],
        },
    ],
    "tilesets": [
        {
            "firstgid": 1,
            "name": "tiles",
            "image": "tiles.png",
            "imagewidth": 64,
            "imageheight": 48,
            "tilewidth": 16,
            "tileheight": 16,
            "tilecount": 12,
            "columns": 4,
        }
    ],
    "properties": [
        {"name": "author", "type": "string", "value": "test"},
    ],
}


class TiledPluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_tiled_")

    def _write_map(self, name: str = "level.tmj") -> str:
        path = os.path.join(self.tmpdir, name)
        with open(path, "w") as f:
            json.dump(SAMPLE_MAP, f)
        return path

    def _run(self, code: str, expected: int = 0) -> None:
        write_sources(Path(self.tmpdir), {"main.vigil": code})
        result = run_vigil(Path(self.tmpdir), "main.vigil")
        self.assertEqual(
            result.returncode, expected,
            msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )

    def test_load_and_map_dimensions(self) -> None:
        map_path = self._write_map()
        self._run(f"""
            import "tiled";
            fn main() -> i32 {{
                i32 h, err e = tiled.load("{map_path}");
                if e != ok {{ return 1; }}
                if tiled.map_width(h) != 4 {{ return 2; }}
                if tiled.map_height(h) != 3 {{ return 3; }}
                if tiled.map_tile_width(h) != 16 {{ return 4; }}
                if tiled.map_tile_height(h) != 16 {{ return 5; }}
                tiled.close(h);
                return 0;
            }}
        """)

    def test_map_orientation_and_infinite(self) -> None:
        map_path = self._write_map()
        self._run(f"""
            import "tiled";
            fn main() -> i32 {{
                i32 h, err e = tiled.load("{map_path}");
                if e != ok {{ return 1; }}
                if tiled.map_orientation(h) != "orthogonal" {{ return 2; }}
                if tiled.map_infinite(h) {{ return 3; }}
                tiled.close(h);
                return 0;
            }}
        """)

    def test_layer_access(self) -> None:
        map_path = self._write_map()
        self._run(f"""
            import "tiled";
            fn main() -> i32 {{
                i32 h, err e = tiled.load("{map_path}");
                if e != ok {{ return 1; }}
                if tiled.map_layer_count(h) != 2 {{ return 2; }}
                if tiled.layer_name(h, 0) != "Ground" {{ return 3; }}
                if tiled.layer_type(h, 0) != "tilelayer" {{ return 4; }}
                if tiled.layer_width(h, 0) != 4 {{ return 5; }}
                if tiled.layer_height(h, 0) != 3 {{ return 6; }}
                if tiled.layer_name(h, 1) != "Entities" {{ return 7; }}
                if tiled.layer_type(h, 1) != "objectgroup" {{ return 8; }}
                if tiled.layer_object_count(h, 1) != 2 {{ return 9; }}
                tiled.close(h);
                return 0;
            }}
        """)

    def test_tileset_access(self) -> None:
        map_path = self._write_map()
        self._run(f"""
            import "tiled";
            fn main() -> i32 {{
                i32 h, err e = tiled.load("{map_path}");
                if e != ok {{ return 1; }}
                if tiled.map_tileset_count(h) != 1 {{ return 2; }}
                if tiled.tileset_name(h, 0) != "tiles" {{ return 3; }}
                if tiled.tileset_first_gid(h, 0) != 1 {{ return 4; }}
                if tiled.tileset_image(h, 0) != "tiles.png" {{ return 5; }}
                if tiled.tileset_tile_width(h, 0) != 16 {{ return 6; }}
                if tiled.tileset_tile_count(h, 0) != 12 {{ return 7; }}
                if tiled.tileset_columns(h, 0) != 4 {{ return 8; }}
                tiled.close(h);
                return 0;
            }}
        """)

    def test_load_nonexistent_file_returns_error(self) -> None:
        self._run("""
            import "tiled";
            fn main() -> i32 {
                i32 h, err e = tiled.load("/nonexistent/map.tmj");
                if e == ok { return 1; }
                return 0;
            }
        """)

    def test_load_invalid_json_returns_error(self) -> None:
        bad_path = os.path.join(self.tmpdir, "bad.tmj")
        with open(bad_path, "w") as f:
            f.write("{invalid json")
        self._run(f"""
            import "tiled";
            fn main() -> i32 {{
                i32 h, err e = tiled.load("{bad_path}");
                if e == ok {{ return 1; }}
                return 0;
            }}
        """)


if __name__ == "__main__":
    unittest.main()
