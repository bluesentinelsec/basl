#!/usr/bin/env python3
"""Embed vigil runtime, header, and plugin source files as compressed C arrays.

Reads source files, gzip-compresses each, and emits a C file with:
  - A byte array per file (compressed data)
  - A metadata table mapping output paths to arrays
  - A count of embedded files

Usage:
    python3 scripts/embed_plugin_sources.py --root . --output generated/embedded_sources.c
"""

import argparse
import zlib
import os
import sys
from pathlib import Path

# Files to embed, grouped by output subdirectory.
# Format: (output_path_in_vigil_rt, source_path_relative_to_root)
EMBED_FILES = []

def _add(out_dir, src_path):
    EMBED_FILES.append((out_dir, src_path))

# Public headers — embed all
import glob as _glob
_pub_headers = sorted([os.path.basename(p) for p in _glob.glob("include/vigil/*.h")])
# Fallback list if glob doesn't work at script time
if not _pub_headers:
    _pub_headers = [
        "allocator.h", "array.h", "builtins.h", "checker.h", "chunk.h",
        "cli_lib.h", "compiler.h", "dap.h", "debugger.h", "debug_info.h",
        "diagnostic.h", "doc.h", "doc_registry.h", "easy.h", "editor.h",
        "embed.h", "export.h", "fmt.h", "gc.h", "json.h", "jsonrpc.h",
        "lexer.h", "log.h", "lsp.h", "map.h", "native_module.h",
        "package.h", "pkg.h", "runtime.h", "semantic.h", "source.h",
        "status.h", "stdlib.h", "string.h", "symbol.h", "token.h",
        "toml.h", "transpile.h", "transpile_rt.h", "type.h",
        "unsafe_buffer.h", "url.h", "value.h", "vigil.h", "vm.h", "yaml.h",
    ]
for h in _pub_headers:
    _add("include/vigil", f"include/vigil/{h}")

# Internal headers — put inside src/internal/ to match original layout
for h in ["vigil_internal.h", "vigil_nanbox.h", "vigil_vm_internal.h", "vigil_xml.h",
           "vigil_regvm.h", "vigil_compiler_types.h", "vigil_cli_frontend.h",
           "vigil_aot.h", "vigil_binding.h", "vigil_compiler_backend.h",
           "vigil_compiler_internal.h", "vigil_compiler_semantics.h",
           "vigil_coverage.h", "vigil_transpile_c.h", "vigil_utf8.h",
           "ffi_callback.h"]:
    p = f"src/internal/{h}"
    _add("src/internal", p)

# Internal source-level headers
for h in ["vm_ops_collection.h", "vm_ops_string.h", "vm_ops_convert.h", "value_internal.h"]:
    _add("src", f"src/{h}")

# Generated headers for self-contained builds
_add("generated", "generated/transpile_plugin_registry.h")

# Runtime core sources
_RUNTIME_SOURCES = [
    "allocator.c", "array.c", "map.c", "string.c", "utf8.c", "xml.c",
    "binding.c", "gc.c", "native_module.c", "runtime.c", "source.c",
    "status.c", "symbol.c", "token.c", "type.c", "value.c",
    "checker.c", "compiler.c", "compiler_backend.c", "compiler_bindings.c",
    "compiler_builtins.c", "compiler_declarations.c", "compiler_program.c",
    "compiler_semantics.c", "compiler_strings.c", "compiler_typeparsing.c",
    "compiler_types.c", "lexer.c", "semantic.c",
    "chunk.c", "vm.c", "regvm.c", "aot.c",
    "vm_ops_collection.c", "vm_ops_convert.c", "vm_ops_string.c",
    "diagnostic.c", "fmt.c", "log.c", "json.c", "jsonrpc.c", "toml.c",
    "transpile_c.c", "transpile_rt.c",
    "builtins.c", "cli_lib.c", "coverage.c", "debug_info.c",
    "easy.c", "embed.c", "package.c", "pkg.c", "doc.c", "doc_registry.c",
    "cli_frontend.c", "cli_test.c", "ffi_callback.c",
    "url.c", "yaml.c",
]
for s in _RUNTIME_SOURCES:
    _add("src", f"src/{s}")

# Platform sources
for s in ["platform_posix.c", "platform_win32.c", "platform_stub.c", "line_editor.c"]:
    _add("src/platform", f"src/platform/{s}")
_add("src/platform", "src/platform/platform.h")

# Stdlib sources
_STDLIB_SOURCES = [
    "args.c", "atomic.c", "compress.c", "crypto.c", "csv.c", "encoding.c",
    "ffi.c", "fmt.c", "fs.c", "hash.c", "http.c", "json.c", "log.c",
    "math.c", "net.c", "os.c", "parse.c", "random.c", "readline.c",
    "regex.c", "regex_engine.c", "regex.h", "reflect.c", "strings.c", "test.c",
    "thread.c", "time.c", "unsafe.c", "url.c", "uuid.c", "xml.c", "yaml.c",
]
for s in _STDLIB_SOURCES:
    _add("src/stdlib", f"src/stdlib/{s}")

# Vendored deps (small enough to embed)
_MINIZ_FILES = [
    "CMakeLists.txt", "miniz.c", "miniz.h", "miniz_common.h", "miniz_export.h",
    "miniz_tdef.c", "miniz_tdef.h", "miniz_tinfl.c", "miniz_tinfl.h",
    "miniz_zip.c", "miniz_zip.h",
]
for f in _MINIZ_FILES:
    _add("deps/miniz", f"deps/miniz/{f}")

for f in ["CMakeLists.txt", "lz4.c", "lz4.h"]:
    _add("deps/lz4", f"deps/lz4/{f}")

for f in ["CMakeLists.txt", "vigil_crypto.c", "vigil_crypto.h"]:
    _add("deps/crypto", f"deps/crypto/{f}")

# libffi (vendored, needed by ffi stdlib module)
_LIBFFI_FILES = [
    "CMakeLists.txt", "preprocess_asm.cmake", "LICENSE",
    "include/ffi_cfi.h", "include/ffi_common.h", "include/ffi.h.in", "include/tramp.h",
    "src/closures.c", "src/debug.c", "src/dlmalloc.c", "src/java_raw_api.c",
    "src/prep_cif.c", "src/raw_api.c", "src/tramp.c", "src/types.c",
    "src/aarch64/ffi.c", "src/aarch64/ffitarget.h", "src/aarch64/internal.h", "src/aarch64/sysv.S",
    "src/aarch64/win64_armasm.S",
    "src/x86/ffi64.c", "src/x86/ffi.c", "src/x86/ffitarget.h", "src/x86/ffiw64.c",
    "src/x86/internal64.h", "src/x86/internal.h", "src/x86/asmnames.h",
    "src/x86/sysv_intel.S", "src/x86/sysv.S", "src/x86/unix64.S",
    "src/x86/win64_intel.S", "src/x86/win64.S",
]
for f in _LIBFFI_FILES:
    _add(f"deps/libffi/{os.path.dirname(f)}" if '/' in f else "deps/libffi", f"deps/libffi/{f}")

# stb headers (needed by SDL plugin for image/font loading, audio for vorbis)
for f in ["stb_image.h", "stb_truetype.h", "stb_vorbis.c"]:
    _add("deps/stb", f"deps/stb/{f}")

# Plugin sources
# Discover plugins from plugin.toml manifests
_PLUGINS = {}
_plugins_dir = os.path.join(os.path.dirname(__file__), "..", "plugins")
if os.path.isdir(_plugins_dir):
    for _pname in sorted(os.listdir(_plugins_dir)):
        _pdir = os.path.join(_plugins_dir, _pname)
        if not os.path.isdir(_pdir):
            continue
        # Collect all .c and .h files in the plugin directory (including subdirs)
        _files = []
        for _root, _dirs, _fnames in os.walk(_pdir):
            for _f in sorted(_fnames):
                if _f.endswith((".c", ".h")):
                    _rel = os.path.relpath(os.path.join(_root, _f), _pdir)
                    _files.append(_rel)
        if _files:
            _PLUGINS[_pname] = _files
for plugin, files in _PLUGINS.items():
    for f in files:
        _add(f"plugins/{plugin}", f"plugins/{plugin}/{f}")


def c_ident(path: str) -> str:
    """Convert a file path to a valid C identifier."""
    return "embed_" + path.replace("/", "_").replace("\\", "_").replace(".", "_").replace("-", "_")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".", help="Project root directory")
    parser.add_argument("--output", default="generated/embedded_sources.c")
    parser.add_argument("--check", action="store_true",
                        help="Check mode: verify existing output matches current sources")
    args = parser.parse_args()

    root = Path(args.root)
    entries = []
    missing = []

    for out_dir, src_rel in EMBED_FILES:
        src_path = root / src_rel
        if not src_path.exists():
            missing.append(src_rel)
            continue
        data = src_path.read_bytes()
        compressed = zlib.compress(data, 9)
        out_path = f"{out_dir}/{src_path.name}"
        ident = c_ident(src_rel)
        entries.append((ident, out_path, compressed, len(data)))

    if missing:
        print(f"Warning: {len(missing)} files not found (skipped):", file=sys.stderr)
        for m in missing[:10]:
            print(f"  {m}", file=sys.stderr)

    # Generate C source
    lines = []
    lines.append("/* Auto-generated by scripts/embed_plugin_sources.py — do not edit. */")
    lines.append("#include <stddef.h>")
    lines.append("#include <stdint.h>")
    lines.append("")

    for ident, out_path, compressed, orig_size in entries:
        lines.append(f"static const unsigned char {ident}_data[] = {{")
        for i in range(0, len(compressed), 16):
            chunk = compressed[i:i+16]
            hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
            lines.append(f"    {hex_vals},")
        lines.append("};")
        lines.append(f"static const size_t {ident}_compressed_size = {len(compressed)};")
        lines.append(f"static const size_t {ident}_original_size = {orig_size};")
        lines.append("")

    # Metadata table
    lines.append("typedef struct vigil_embedded_file {")
    lines.append("    const char *path;")
    lines.append("    const unsigned char *data;")
    lines.append("    size_t compressed_size;")
    lines.append("    size_t original_size;")
    lines.append("} vigil_embedded_file_t;")
    lines.append("")
    lines.append(f"static const size_t vigil_embedded_file_count = {len(entries)};")
    lines.append("static const vigil_embedded_file_t vigil_embedded_files[] = {")
    for ident, out_path, compressed, orig_size in entries:
        lines.append(f'    {{"{out_path}", {ident}_data, {len(compressed)}, {orig_size}}},')
    lines.append("};")
    lines.append("")

    output = "\n".join(lines) + "\n"

    if args.check:
        out_path = root / args.output
        if not out_path.exists():
            print(f"FAIL: {args.output} does not exist", file=sys.stderr)
            sys.exit(1)
        existing = out_path.read_text()
        if existing != output:
            print(f"FAIL: {args.output} is out of date. Run:", file=sys.stderr)
            print(f"  python3 scripts/embed_plugin_sources.py --root . --output {args.output}", file=sys.stderr)
            sys.exit(1)
        print(f"OK: {args.output} is up to date ({len(entries)} files)")
        sys.exit(0)

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(output)
    total_compressed = sum(len(e[2]) for e in entries)
    total_original = sum(e[3] for e in entries)
    print(f"Embedded {len(entries)} files: {total_original} bytes -> {total_compressed} bytes "
          f"({total_compressed * 100 // max(total_original, 1)}% of original)")


if __name__ == "__main__":
    main()
