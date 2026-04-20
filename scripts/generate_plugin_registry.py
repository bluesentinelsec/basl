#!/usr/bin/env python3
"""Generate transpile_plugin_registry.h from plugin.toml manifests.

Reads all plugins/*/plugin.toml files and generates a C header that
registers plugins for transpiled projects.

Usage:
    python3 scripts/generate_plugin_registry.py --root . --output generated/transpile_plugin_registry.h
"""

import argparse
import os
import sys


def parse_toml_simple(path):
    """Parse a simple TOML file into a nested dict."""
    result = {}
    current = result
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("[[") and line.endswith("]]"):
                key = line[2:-2].strip()
                parts = key.split(".")
                d = result
                for p in parts[:-1]:
                    d = d.setdefault(p, {})
                d.setdefault(parts[-1], []).append({})
                current = d[parts[-1]][-1]
                continue
            if line.startswith("[") and line.endswith("]"):
                key = line[1:-1].strip()
                parts = key.split(".")
                d = result
                for p in parts:
                    d = d.setdefault(p, {})
                current = d
                continue
            if "=" in line:
                key, _, val = line.partition("=")
                key, val = key.strip(), val.strip()
                if val.startswith('"') and val.endswith('"'):
                    val = val[1:-1]
                elif val.startswith("[") and val.endswith("]"):
                    inner = val[1:-1].strip()
                    val = [v.strip().strip('"') for v in inner.split(",")] if inner else []
                elif val in ("true", "false"):
                    val = val == "true"
                elif val.startswith("{") and val.endswith("}"):
                    inner = val[1:-1].strip()
                    val = dict(p.partition("=")[::2] for p in inner.split(",")) if inner else {}
                    val = {k.strip(): v.strip().strip('"') for k, v in val.items()}
                current[key] = val
    return result


def discover_plugins(root):
    """Find all plugin.toml files under root/plugins/."""
    plugins = []
    plugins_dir = os.path.join(root, "plugins")
    if not os.path.isdir(plugins_dir):
        return plugins
    for name in sorted(os.listdir(plugins_dir)):
        toml_path = os.path.join(plugins_dir, name, "plugin.toml")
        if os.path.isfile(toml_path):
            plugins.append(parse_toml_simple(toml_path))
    return plugins


def generate(plugins):
    """Generate the header content."""
    conditional = [p for p in plugins if p.get("transpile", {}).get("compile_definition")]
    unconditional = [p for p in plugins if not p.get("transpile", {}).get("compile_definition")]

    o = []
    o.append("/* Auto-generated from plugin.toml manifests — do not edit. */")
    o.append("#ifndef VIGIL_TRANSPILE_PLUGIN_REGISTRY_H")
    o.append("#define VIGIL_TRANSPILE_PLUGIN_REGISTRY_H")
    o.append("")
    o.append("#include <string.h>")
    o.append("#include \"vigil/export.h\"")
    o.append("#include \"vigil/native_module.h\"")
    o.append("#include \"vigil/status.h\"")
    o.append("")
    o.append("#ifdef __cplusplus")
    o.append("extern \"C\" {")
    o.append("#endif")
    o.append("")

    for p in plugins:
        o.append(f"extern VIGIL_API const vigil_native_module_t vigil_plugin_{p['name']};")
    o.append("")

    # VIGIL_PLUGIN_COUNT as a simple constant — unconditional count + conditional via #ifdef
    # Use nested #if to compute the count at preprocessor time
    base = len(unconditional)
    if not conditional:
        o.append(f"#define VIGIL_PLUGIN_COUNT ({base}U)")
    else:
        # Emit a chain: base + 1 per enabled conditional
        # E.g., #ifdef A \n #ifdef B \n #define COUNT (base+2) \n #else \n #define COUNT (base+1) ...
        # Simpler: just enumerate all 2^N combinations... or use a different approach.
        # Simplest: define count as the max, fill table conditionally, track actual count in fill.
        o.append(f"#define VIGIL_PLUGIN_COUNT_MAX ({len(plugins)}U)")
    o.append("")

    o.append("typedef struct vigil_plugin_entry {")
    o.append("    const char *name;")
    o.append("    size_t name_length;")
    o.append("    const vigil_native_module_t *module;")
    o.append("} vigil_plugin_entry_t;")
    o.append("")

    # Fill function returns actual count
    o.append("static inline size_t vigil_plugin_fill_table_(vigil_plugin_entry_t *table) {")
    o.append("    size_t i = 0;")
    for p in unconditional:
        n = p["name"]
        o.append(f"    table[i].name = \"{n}\"; table[i].name_length = {len(n)}U; table[i].module = &vigil_plugin_{n}; i++;")
    for p in conditional:
        n = p["name"]
        defn = p["transpile"]["compile_definition"]
        o.append(f"#ifdef {defn}")
        o.append(f"    table[i].name = \"{n}\"; table[i].name_length = {len(n)}U; table[i].module = &vigil_plugin_{n}; i++;")
        o.append("#endif")
    o.append("    return i;")
    o.append("}")
    o.append("")

    max_count = f"VIGIL_PLUGIN_COUNT_MAX" if conditional else f"VIGIL_PLUGIN_COUNT"

    o.append("static inline int vigil_plugin_is_known_module(const char *name, size_t name_length) {")
    o.append(f"    vigil_plugin_entry_t table[{max_count} + 1U];")
    o.append(f"    size_t count = vigil_plugin_fill_table_(table);")
    o.append("    for (size_t i = 0U; i < count; i++) {")
    o.append("        if (table[i].name_length == name_length && memcmp(table[i].name, name, name_length) == 0)")
    o.append("            return 1;")
    o.append("    }")
    o.append("    return 0;")
    o.append("}")
    o.append("")

    o.append("static inline vigil_status_t vigil_plugin_register_all(vigil_native_registry_t *registry, vigil_error_t *error) {")
    o.append(f"    vigil_plugin_entry_t table[{max_count} + 1U];")
    o.append(f"    size_t count = vigil_plugin_fill_table_(table);")
    o.append("    for (size_t i = 0U; i < count; i++) {")
    o.append("        vigil_status_t s = vigil_native_registry_add(registry, table[i].module, error);")
    o.append("        if (s != VIGIL_STATUS_OK) return s;")
    o.append("    }")
    o.append("    return VIGIL_STATUS_OK;")
    o.append("}")
    o.append("")

    # Compat: define VIGIL_PLUGIN_COUNT for code that uses it
    if conditional:
        o.append("/* For compatibility — use vigil_plugin_fill_table_ for actual count. */")
        o.append(f"#define VIGIL_PLUGIN_COUNT VIGIL_PLUGIN_COUNT_MAX")
        o.append("")

    o.append("#ifdef __cplusplus")
    o.append("}")
    o.append("#endif")
    o.append("")
    o.append("#endif /* VIGIL_TRANSPILE_PLUGIN_REGISTRY_H */")
    o.append("")
    return "\n".join(o)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    plugins = discover_plugins(args.root)
    header = generate(plugins)
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(header)
    print(f"Generated {args.output} with {len(plugins)} plugins")


if __name__ == "__main__":
    main()
