#!/usr/bin/env python3
"""Verify that converted source files use the custom allocator, not raw
malloc/calloc/realloc/free.

Run from the repository root:
    python3 scripts/check_allocator.py

Exit 0 if clean, 1 if any violation is found.
"""

import re
import sys
from pathlib import Path

# Regex matching raw allocation calls.  Negative lookbehind excludes
# identifiers that *contain* the word (e.g. pkg_lock_free, doc_free_ptr,
# vigil_toml_free).  We also skip macro wrappers like PKG_FREE.
RAW_ALLOC_RE = re.compile(
    r"(?<![_a-zA-Z0-9])"          # not preceded by identifier char
    r"(malloc|calloc|realloc|free)"
    r"\s*\("
)

# Patterns that indicate a legitimate use (wrapper helpers, comments, strings,
# or an explicit suppression marker).
FALSE_POSITIVE_RE = re.compile(
    r"(PKG_FREE|PKG_ALLOC|doc_alloc|doc_realloc|doc_free"
    r"|vigil_\w*free|vigil_\w*alloc|toml_free|curl_free"
    r"|pkg_lock_free|pkg_spec_free"
    r"|alloc-check:\s*exempt"
    r"|//|/\*|\*|\")"
)

# Files that are allowed to use raw allocation.  Paths are relative to the
# repository root.
EXEMPT_FILES = {
    # The default allocator implementation itself.
    "src/allocator.c",
    # CLI layer — owns the allocator, passes NULL for default.
    "src/cli/main.c",
    "src/cli_frontend.c",
    "src/cli_test.c",
    "src/coverage.c",
    # Platform layer — OS callbacks with fixed signatures.
    "src/platform/platform_posix.c",
    "src/platform/platform_win32.c",
    "src/platform/line_editor.c",
    # Stdlib modules with architectural constraints.
    "src/stdlib/thread.c",
    "src/stdlib/atomic.c",
    "src/stdlib/unsafe.c",
    "src/stdlib/http.c",
    "src/stdlib/fs.c",
    # Public API without runtime parameter.
    "src/value.c",
    # Embedding API — owns state lifecycle, uses calloc/free for vigil_state_t.
    "src/easy.c",
    # Editor integration — writes config files with stdio, no runtime context.
    "src/editor.c",
    # Register VM translator uses raw malloc for internal temporary
    # data structures (code arrays, jump tables, offset maps) that
    # are freed within the same translation call.
    "src/regvm.c",
    # Chunk clear/free releases cached reg_chunk via free.
    "src/chunk.c",
    # VM execute paths allocate/free reg_chunk cache.
    "src/vm.c",
    # Doc strings referencing unsafe.malloc etc.
    "src/doc_registry.c",
}


def check_file(path: Path, root: Path) -> list[str]:
    errors = []
    rel = str(path.relative_to(root))
    if rel in EXEMPT_FILES:
        return errors
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        stripped = line.lstrip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        if RAW_ALLOC_RE.search(line) and not FALSE_POSITIVE_RE.search(line):
            errors.append(f"{rel}:{lineno}: raw allocator call: {stripped.strip()}")
    return errors


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    scan_dirs = [root / "src", root / "include" / "vigil"]
    globs = ["*.c", "*.h"]

    errors: list[str] = []
    file_count = 0
    for d in scan_dirs:
        for g in globs:
            for path in sorted(d.rglob(g)):
                rel = str(path.relative_to(root))
                if rel in EXEMPT_FILES:
                    continue
                file_count += 1
                errors.extend(check_file(path, root))

    if errors:
        print(f"FAIL: {len(errors)} raw allocator call(s) in converted files:\n")
        for e in errors:
            print(f"  {e}")
        return 1

    print(f"OK: {file_count} files checked, no raw allocator violations.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
