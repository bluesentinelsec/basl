#!/usr/bin/env python3
"""Enforce intended internal include boundaries between core layers."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from fnmatch import fnmatch
from pathlib import Path


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


@dataclass(frozen=True)
class Layer:
    name: str
    paths: tuple[str, ...]
    allow: tuple[str, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        default="architecture/layers.json",
        help="Path to architecture layer manifest",
    )
    return parser.parse_args()


def load_layers(repo_root: Path, manifest_path: Path) -> list[Layer]:
    with manifest_path.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    layers: list[Layer] = []
    for raw_layer in manifest["layers"]:
        layers.append(
            Layer(
                name=raw_layer["name"],
                paths=tuple(raw_layer["paths"]),
                allow=tuple(raw_layer.get("allow", ())),
            )
        )
    return layers


def expand_layer_files(repo_root: Path, layer: Layer) -> list[Path]:
    files: list[Path] = []

    for pattern in layer.paths:
        matches = sorted(repo_root.glob(pattern))
        files.extend(path for path in matches if path.is_file())
    return files


def build_file_layer_map(repo_root: Path, layers: list[Layer]) -> tuple[dict[Path, str], list[str]]:
    file_to_layer: dict[Path, str] = {}
    failures: list[str] = []

    for layer in layers:
        for path in expand_layer_files(repo_root, layer):
            existing = file_to_layer.get(path)
            if existing is not None:
                failures.append(
                    f"manifest overlap: {path.relative_to(repo_root)} belongs to both {existing} and {layer.name}"
                )
                continue
            file_to_layer[path] = layer.name
    return file_to_layer, failures


def resolve_repo_include(repo_root: Path, including_file: Path, include_name: str) -> Path | None:
    if include_name.startswith("vigil/"):
        candidate = repo_root / "include" / include_name
    elif include_name.startswith("internal/"):
        candidate = repo_root / "src" / include_name
    else:
        candidate = including_file.parent / include_name

    try:
        candidate = candidate.resolve()
        candidate.relative_to(repo_root)
    except (FileNotFoundError, ValueError):
        return None

    if not candidate.exists() or not candidate.is_file():
        return None
    return candidate


def layer_by_name(layers: list[Layer]) -> dict[str, Layer]:
    return {layer.name: layer for layer in layers}


def check_includes(repo_root: Path, layers: list[Layer], file_to_layer: dict[Path, str]) -> list[str]:
    failures: list[str] = []
    layers_by_name = layer_by_name(layers)

    for path, source_layer_name in sorted(file_to_layer.items()):
        source_layer = layers_by_name[source_layer_name]
        if not source_layer.allow:
            continue

        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = INCLUDE_RE.match(line)
            if match is None:
                continue

            target = resolve_repo_include(repo_root, path, match.group(1))
            if target is None:
                continue

            target_layer_name = file_to_layer.get(target)
            if target_layer_name is None:
                continue
            if target_layer_name in source_layer.allow:
                continue

            failures.append(
                f"{path.relative_to(repo_root)}:{lineno}: {source_layer_name} must not include "
                f"{target.relative_to(repo_root)} ({target_layer_name})"
            )

    return failures


def validate_manifest_globs(repo_root: Path, layers: list[Layer]) -> list[str]:
    failures: list[str] = []

    for layer in layers:
        for pattern in layer.paths:
            if not any(path.is_file() for path in repo_root.glob(pattern)):
                failures.append(f"manifest path matches no files: {pattern} ({layer.name})")
    return failures


def main() -> int:
    args = parse_args()
    repo_root = Path.cwd()
    manifest_path = repo_root / args.manifest

    layers = load_layers(repo_root, manifest_path)
    failures = validate_manifest_globs(repo_root, layers)
    file_to_layer, overlap_failures = build_file_layer_map(repo_root, layers)
    failures.extend(overlap_failures)
    failures.extend(check_includes(repo_root, layers, file_to_layer))

    if failures:
        print("architecture boundary check failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print(f"architecture boundary check passed ({len(file_to_layer)} files checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
