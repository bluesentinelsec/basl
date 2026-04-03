#!/usr/bin/env python3

import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def resolve_vigil_command() -> list[str]:
    configured_bin = os.environ.get("VIGIL_BIN")
    native_bin = REPO_ROOT / "build" / ("vigil.exe" if os.name == "nt" else "vigil")
    wasm_bin = REPO_ROOT / "build" / "vigil.js"

    if configured_bin:
        return [configured_bin]
    if native_bin.exists():
        return [str(native_bin)]
    if wasm_bin.exists():
        return [os.environ.get("EMSDK_NODE", "node"), str(wasm_bin)]

    raise FileNotFoundError("could not locate VIGIL CLI executable in build output")


def write_sources(root: Path, sources: dict[str, str]) -> None:
    for relative_path, text in sources.items():
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(text).strip() + "\n", encoding="utf-8")


def run_vigil(root: Path, entrypoint: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [*resolve_vigil_command(), "run", str(root / entrypoint)],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        check=False,
    )


class SyntaxIntegrationTest(unittest.TestCase):
    """Existing tests from PR #88."""

    def test_import_alias_and_default_nested_alias(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(
                root,
                {
                    "lib/math.vigil": """
                        pub fn double(i32 x) -> i32 {
                            return x * 2
                        }
                    """,
                    "shared/utils.vigil": """
                        pub fn answer() -> i32 {
                            return 9
                        }
                    """,
                    "main.vigil": """
                        import "lib/math" as ops
                        import "shared/utils"

                        fn main() -> i32 {
                            return ops.double(utils.answer())
                        }
                    """,
                },
            )

            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 18, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_numeric_and_character_literals(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(
                root,
                {
                    "main.vigil": """
                        fn main() -> i32 {
                            i32 total = 0x10 + 0b11 + 0o7
                            string letter = 'A'
                            string newline = '\\n'

                            if letter == "A" && newline.len() == 1 {
                                return total
                            }
                            return 0
                        }
                    """,
                },
            )

            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 26, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_function_typed_globals_and_imported_collection_globals(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(
                root,
                {
                    "lib/state.vigil": """
                        pub array<i32> nums = []

                        pub fn seed() -> void {
                            nums.push(3)
                            nums.push(4)
                        }
                    """,
                    "main.vigil": """
                        import "lib/state" as st

                        fn twice(i32 value) -> i32 {
                            return value * 2
                        }

                        fn(i32) -> i32 op = twice
                        fn any = twice

                        fn main() -> i32 {
                            st.nums = []
                            st.seed()
                            st.nums[1] += 5
                            return op(st.nums[0]) + op(st.nums[1])
                        }
                    """,
                },
            )

            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 24, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_multiple_interface_dispatch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(
                root,
                {
                    "main.vigil": """
                        interface Named {
                            fn name() -> string
                        }

                        interface Sized {
                            fn size() -> i32
                        }

                        class Box implements Named, Sized {
                            pub string label
                            pub i32 count

                            fn init(string label, i32 count) -> void {
                                self.label = label
                                self.count = count
                            }

                            fn name() -> string {
                                return self.label
                            }

                            fn size() -> i32 {
                                return self.count
                            }
                        }

                        fn describe(Named named, Sized sized) -> i32 {
                            if named.name() == "ok" {
                                return sized.size()
                            }
                            return 0
                        }

                        fn main() -> i32 {
                            Box box = Box("ok", 12)
                            return describe(box, box)
                        }
                    """,
                },
            )

            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 12, msg=result.stderr)
            self.assertEqual(result.stderr, "")


class DocsConformanceTest(unittest.TestCase):
    """Tests derived from docs/syntax.md conformance sweep."""

    # -- closures and function values --

    def test_closure_captures_local(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 factor = 3
                    fn(i32) -> i32 scale = fn(i32 x) -> i32 {
                        return x * factor
                    }
                    return scale(7)
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 21, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_iife(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 result = fn() -> i32 {
                        return 42
                    }()
                    return result
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 42, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_lambda_shorthand_in_assignment_and_call_context(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn apply(fn(i32) -> i32 f, i32 x) -> i32 {
                    return f(x)
                }

                fn main() -> i32 {
                    fn(i32, i32) -> i32 combine = |a, b| {
                        i32 sum = a + b
                        return sum * 4
                    }
                    return apply(|x| x + 2, 5) + combine(1, 2)
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 19, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_local_named_function(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    fn helper(i32 x) -> i32 {
                        return x * 2
                    }
                    return helper(11)
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 22, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- error flow and guard --

    def test_error_flow(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn risky(bool fail) -> (i32, err) {
                    if fail {
                        return 0, err("bad", err.arg)
                    }
                    return 42, ok
                }

                fn main() -> i32 {
                    i32 v1, err e1 = risky(false)
                    if e1 != ok { return 1 }
                    if v1 != 42 { return 2 }

                    i32 v2, err e2 = risky(true)
                    if e2 == ok { return 3 }

                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_guard_statement(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn fallible(i32 x) -> (i32, err) {
                    if x == 0 {
                        return 0, err("zero", err.arg)
                    }
                    return x * 2, ok
                }

                fn main() -> i32 {
                    guard i32 val, err e = fallible(0) {
                        return 99
                    }
                    return val
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 99, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_fallible_class_init(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                class Conn {
                    pub string host

                    fn init(string host) -> err {
                        if host == "" {
                            return err("empty host", err.arg)
                        }
                        self.host = host
                        return ok
                    }
                }

                fn main() -> i32 {
                    Conn c1, err e1 = Conn("localhost")
                    if e1 != ok { return 1 }
                    if c1.host != "localhost" { return 2 }

                    Conn c2, err e2 = Conn("")
                    if e2 == ok { return 3 }

                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- loops, break, continue --

    def test_for_while_forin_break_continue(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 total = 0

                    for (i32 i = 0; i < 5; i++) {
                        if i == 3 { continue }
                        total += i
                    }

                    i32 w = 0
                    while w < 3 {
                        total += 10
                        w++
                    }

                    array<i32> nums = [10, 20, 30]
                    for val in nums {
                        total += val
                    }

                    map<string, i32> m = {"x": 1, "y": 2}
                    for key, val in m {
                        total += val
                    }

                    for (i32 j = 0; j < 100; j++) {
                        if j == 2 { break }
                        total += 50
                    }

                    return total
                }
            """})
            # 0+1+2+4=7, +30 while, +60 for-in array, +3 for-in map, +100 break loop = 200
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 200, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_for_in_string_iteration(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    // ASCII iteration
                    i32 count = 0
                    for ch in "hello" {
                        count += 1
                    }
                    if count != 5 { return 1 }

                    // Unicode codepoint iteration
                    i32 ucount = 0
                    for ch in "héllo" {
                        ucount += 1
                    }
                    if ucount != 5 { return 2 }

                    // Empty string
                    i32 ecount = 0
                    for ch in "" {
                        ecount += 1
                    }
                    if ecount != 0 { return 3 }

                    // Byte iteration via .bytes()
                    i32 bcount = 0
                    for b in "hi".bytes() {
                        bcount += 1
                    }
                    if bcount != 2 { return 4 }

                    // break in string iteration
                    i32 bk = 0
                    for ch in "abcdef" {
                        if ch == "d" { break }
                        bk += 1
                    }
                    if bk != 3 { return 5 }

                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- switch and enum --

    def test_switch_with_enum(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                enum Color {
                    Red,
                    Green,
                    Blue
                }

                fn main() -> i32 {
                    Color c = Color.Green
                    switch c {
                        case Color.Red:
                            return 1
                        case Color.Green:
                            return 2
                        case Color.Blue:
                            return 3
                        default:
                            return 0
                    }
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_switch_multi_case(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 x = 3
                    switch x {
                        case 1:
                            return 10
                        case 2, 3:
                            return 20
                        default:
                            return 30
                    }
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 20, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_enum_with_explicit_values(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                enum HttpStatus {
                    Ok = 200,
                    NotFound = 404
                }

                fn main() -> i32 {
                    HttpStatus s = HttpStatus.NotFound
                    switch s {
                        case HttpStatus.NotFound:
                            return 44
                        default:
                            return 0
                    }
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 44, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- defer --

    def test_defer_runs_after_return(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                i32 counter = 0

                fn bump() -> void {
                    counter += 1
                }

                fn work() -> i32 {
                    defer bump()
                    defer bump()
                    return counter
                }

                fn main() -> i32 {
                    i32 result = work()
                    return result + counter
                }
            """})
            # work() returns 0 (defers haven't run yet), then defers run: counter=2
            # main returns 0 + 2 = 2
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- string built-ins --

    def test_string_builtins(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    string s = "  Hello World  "
                    string trimmed = s.trim()
                    if trimmed != "Hello World" { return 1 }
                    if !trimmed.contains("World") { return 2 }
                    if !trimmed.starts_with("Hello") { return 3 }
                    if !trimmed.ends_with("World") { return 4 }
                    if trimmed.to_upper() != "HELLO WORLD" { return 5 }
                    if trimmed.to_lower() != "hello world" { return 6 }
                    if trimmed.replace("World", "VIGIL") != "Hello VIGIL" { return 7 }
                    array<string> parts = trimmed.split(" ")
                    if parts.len() != 2 { return 8 }
                    i32 idx, bool found = trimmed.index_of("World")
                    if !found { return 9 }
                    if idx != 6 { return 10 }
                    string sub, err e1 = trimmed.substr(0, 5)
                    if e1 != ok { return 11 }
                    if sub != "Hello" { return 12 }
                    array<u8> bytes = trimmed.bytes()
                    if bytes.len() != 11 { return 13 }
                    string ch, err e2 = trimmed.char_at(0)
                    if e2 != ok { return 14 }
                    if ch != "H" { return 15 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- array built-ins --

    def test_array_builtins(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    array<i32> a = [1, 2, 3]
                    a.push(4)
                    if a.len() != 4 { return 1 }
                    i32 popped, err e1 = a.pop()
                    if e1 != ok { return 2 }
                    if popped != 4 { return 3 }
                    i32 val, err e2 = a.get(1)
                    if e2 != ok { return 4 }
                    if val != 2 { return 5 }
                    err e3 = a.set(0, 99)
                    if e3 != ok { return 6 }
                    if a[0] != 99 { return 7 }
                    array<i32> sliced = a.slice(1, 3)
                    if sliced.len() != 2 { return 8 }
                    if !a.contains(99) { return 9 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- map built-ins --

    def test_map_builtins(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    map<string, i32> m = {"a": 1, "b": 2}
                    if m.len() != 2 { return 1 }
                    i32 val, bool found = m.get("a")
                    if !found { return 2 }
                    if val != 1 { return 3 }
                    if !m.has("b") { return 4 }
                    err e = m.set("c", 3)
                    if e != ok { return 5 }
                    if m.len() != 3 { return 6 }
                    i32 removed, bool had = m.remove("b")
                    if !had { return 7 }
                    if removed != 2 { return 8 }
                    array<string> keys = m.keys()
                    if keys.len() != 2 { return 9 }
                    array<i32> vals = m.values()
                    if vals.len() != 2 { return 10 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- additional string methods --

    def test_string_methods_extended(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                import "fmt"
                fn main() -> i32 {
                    string s = "  Hello World  "
                    if s.trim_left() != "Hello World  " { return 1 }
                    if s.trim_right() != "  Hello World" { return 2 }
                    if "abc".reverse() != "cba" { return 3 }
                    if "".none() != true { return 4 }
                    if "hi".none() != false { return 5 }
                    if "hello".char_count() != 5 { return 6 }
                    if "ab".repeat(3) != "ababab" { return 7 }
                    if "ababa".count("ab") != 2 { return 8 }
                    i32 idx, bool found = "hello world".last_index_of("o")
                    if !found { return 9 }
                    if idx != 7 { return 10 }
                    if "hello world".trim_prefix("hello ") != "world" { return 11 }
                    if "hello world".trim_suffix(" world") != "hello" { return 12 }
                    if !"Hello".equal_fold("hello") { return 13 }
                    if "abc".len() != 3 { return 14 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- array methods extended --

    def test_array_methods_extended(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    array<i32> a = [10, 20, 30, 40, 50]
                    if !a.contains(30) { return 1 }
                    if a.contains(99) { return 2 }
                    array<i32> sl = a.slice(1, 3)
                    if sl.len() != 2 { return 3 }
                    if sl[0] != 20 { return 4 }
                    if sl[1] != 30 { return 5 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- f-string format specifiers --

    def test_fstring_format_specifiers(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                import "fmt"
                fn main() -> i32 {
                    i32 n = 42
                    f64 pi = 3.14159
                    string name = "hi"

                    string s1 = f"{n:d}"
                    if s1 != "42" { return 1 }

                    string s2 = f"{n:x}"
                    if s2 != "2a" { return 2 }

                    string s3 = f"{n:X}"
                    if s3 != "2A" { return 3 }

                    string s4 = f"{n:b}"
                    if s4 != "101010" { return 4 }

                    string s5 = f"{n:o}"
                    if s5 != "52" { return 5 }

                    string s6 = f"{pi:.2f}"
                    if s6 != "3.14" { return 6 }

                    string s7 = f"{name:>10}"
                    if s7 != "        hi" { return 7 }

                    string s8 = f"{name:<10}"
                    if s8 != "hi        " { return 8 }

                    string s9 = f"{name:^10}"
                    if s9 != "    hi    " { return 9 }

                    string s10 = f"{name:*>10}"
                    if s10 != "********hi" { return 10 }

                    string s11 = f"{n:>10d}"
                    if s11 != "        42" { return 11 }

                    string s12 = f"{1000:,}"
                    if s12 != "1,000" { return 12 }

                    string s13 = f"plain text"
                    if s13 != "plain text" { return 13 }

                    string s14 = f"val={n}"
                    if s14 != "val=42" { return 14 }

                    string s15 = f"{true}"
                    if s15 != "true" { return 15 }

                    string s16 = f"{pi}"
                    if s16.len() < 3 { return 16 }

                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- f-string escape sequences --

    def test_fstring_escapes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": r"""
                import "fmt"
                fn main() -> i32 {
                    string s1 = f"{{braces}}"
                    if s1 != "{braces}" { return 1 }

                    string s2 = "\x48\x69"
                    if s2 != "Hi" { return 2 }

                    string s3 = "tab\there"
                    if s3.len() != 8 { return 3 }

                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)

    # -- type conversions --

    def test_type_conversions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 x = 42
                    i64 big = i64(x)
                    f64 fval = f64(x)
                    string s = string(x)
                    if s != "42" { return 1 }
                    if big != i64(42) { return 2 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- wider integer types --

    def test_wider_integer_types(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    u8 byte = u8(255)
                    u32 mid = u32(1000)
                    u64 big = u64(999)
                    i64 signed_big = i64(500)
                    i32 result = i32(byte) + i32(mid) + i32(big) + i32(signed_big)
                    if result != 2754 { return 1 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- constants --

    def test_constants(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                const i32 MAX = 100
                const string LABEL = "test"

                fn main() -> i32 {
                    const i32 LOCAL_MAX = 50
                    if LABEL != "test" { return 1 }
                    if MAX != 100 { return 2 }
                    if LOCAL_MAX != 50 { return 3 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- ternary --

    def test_ternary_expression(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 a = 10
                    i32 b = 20
                    i32 max = a > b ? a : b
                    return max
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 20, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- bitwise operations --

    def test_bitwise_operations(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 a = 0xFF
                    i32 b = 0x0F
                    i32 result = (a & b) | 0x10
                    return result
                }
            """})
            # 0xFF & 0x0F = 0x0F = 15, 15 | 0x10 = 31
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 31, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- f-strings --

    def test_fstring_interpolation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 x = 5
                    i32 y = 10
                    string msg = f"sum={x + y}"
                    if msg != "sum=15" { return 1 }
                    return 0
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- classes --

    def test_class_construction_and_methods(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                class Counter {
                    pub i32 value

                    fn init(i32 start) -> void {
                        self.value = start
                    }

                    pub fn add(i32 n) -> void {
                        self.value += n
                    }
                }

                fn main() -> i32 {
                    Counter c = Counter(10)
                    c.add(5)
                    return c.value
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 15, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- compound assignment --

    def test_compound_assignment_and_increment(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 x = 10
                    x += 5
                    x -= 1
                    x *= 2
                    x /= 7
                    x %= 3
                    x++
                    return x
                }
            """})
            # 10+5=15, -1=14, *2=28, /7=4, %3=1, ++=2
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- pub enforcement across modules --

    def test_pub_enforcement(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "lib.vigil": """
                    fn private_fn() -> i32 {
                        return 1
                    }

                    pub fn public_fn() -> i32 {
                        return 2
                    }
                """,
                "main.vigil": """
                    import "lib"

                    fn main() -> i32 {
                        return lib.private_fn()
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not public", result.stderr)


class RealProgramPatternsTest(unittest.TestCase):
    """Tests for patterns that arise in real programs."""

    # -- error kind routing --

    def test_error_kind_switch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn lookup(string key) -> (string, err) {
                    if key == "" {
                        return "", err("empty key", err.arg)
                    }
                    if key == "missing" {
                        return "", err("not found", err.not_found)
                    }
                    return "value_" + key, ok
                }

                fn main() -> i32 {
                    string v, err e = lookup("missing")
                    if e == ok { return 1 }
                    switch e.kind() {
                        case err.not_found:
                            return 10
                        case err.arg:
                            return 20
                        default:
                            return 30
                    }
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 10, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- cross-module classes, interfaces, enums --

    def test_cross_module_interface_dispatch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {
                "types.vigil": """
                    pub enum Status {
                        Active,
                        Inactive
                    }

                    pub interface Describable {
                        fn describe() -> string
                    }

                    pub class User implements Describable {
                        pub string name
                        pub Status status

                        fn init(string name, Status status) -> void {
                            self.name = name
                            self.status = status
                        }

                        fn describe() -> string {
                            return f"User({self.name})"
                        }
                    }
                """,
                "main.vigil": """
                    import "types"

                    fn print_desc(types.Describable d) -> string {
                        return d.describe()
                    }

                    fn main() -> i32 {
                        types.User u = types.User("alice", types.Status.Active)
                        string desc = print_desc(u)
                        if desc == "User(alice)" {
                            return 0
                        }
                        return 1
                    }
                """,
            })
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- nested generic types (>> token splitting) --

    def test_nested_array_type(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    array<array<i32>> grid = [[1, 2], [3, 4], [5, 6]]
                    i32 total = 0
                    for row in grid {
                        for val in row {
                            total += val
                        }
                    }
                    return total
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 21, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_map_with_nested_array_value(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    map<string, array<i32>> data = {"a": [1, 2], "b": [3, 4]}
                    array<i32> a_vals, bool found = data.get("a")
                    if !found { return 1 }
                    return a_vals[0] + a_vals[1]
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 3, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_triple_nested_array(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    array<array<array<i32>>> deep = [[[1, 2], [3]], [[4]]]
                    return deep.len()
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- nested control flow --

    def test_guard_in_loop_with_continue(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn maybe(i32 x) -> (i32, err) {
                    if x == 2 {
                        return 0, err("skip", err.arg)
                    }
                    return x * 10, ok
                }

                fn main() -> i32 {
                    i32 total = 0
                    for (i32 i = 0; i < 5; i++) {
                        guard i32 val, err e = maybe(i) {
                            continue
                        }
                        total += val
                    }
                    return total
                }
            """})
            # i=0->0, i=1->10, i=2->skip, i=3->30, i=4->40 = 80
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 80, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_defer_with_break(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                i32 counter = 0

                fn bump() -> void {
                    counter += 1
                }

                fn work() -> i32 {
                    defer bump()
                    for (i32 i = 0; i < 10; i++) {
                        if i == 3 { break }
                    }
                    return counter
                }

                fn main() -> i32 {
                    i32 result = work()
                    return result + counter
                }
            """})
            # work() returns 0, defer runs -> counter=1, main returns 0+1=1
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 1, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_defer_guard_forin_combined(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                i32 cleanup_count = 0

                fn do_cleanup() -> void {
                    cleanup_count += 1
                }

                fn maybe_val(i32 x) -> (i32, err) {
                    if x < 0 {
                        return 0, err("negative", err.arg)
                    }
                    return x, ok
                }

                fn process(array<i32> items) -> i32 {
                    defer do_cleanup()
                    i32 total = 0
                    for val in items {
                        guard i32 v, err e = maybe_val(val) {
                            continue
                        }
                        total += v
                    }
                    return total
                }

                fn main() -> i32 {
                    array<i32> data = [1, -2, 3, -4, 5]
                    i32 result = process(data)
                    if cleanup_count != 1 { return 99 }
                    return result
                }
            """})
            # 1+3+5=9, cleanup_count=1
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 9, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- closures capturing loop variables --

    def test_closure_captures_loop_variable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    array<fn() -> i32> fns = []
                    for (i32 i = 0; i < 3; i++) {
                        i32 captured = i
                        fn() -> i32 f = fn() -> i32 {
                            return captured
                        }
                        fns.push(f)
                    }
                    fn() -> i32 f0, err e0 = fns.get(0)
                    fn() -> i32 f2, err e2 = fns.get(2)
                    return f0() + f2()
                }
            """})
            # f0()=0, f2()=2, total=2
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 2, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    # -- compound expressions --

    def test_ternary_in_function_args(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn add(i32 a, i32 b) -> i32 {
                    return a + b
                }

                fn main() -> i32 {
                    bool flag = true
                    return add(flag ? 10 : 20, flag ? 1 : 2)
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 11, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_chained_string_methods(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    string result = "  Hello World  ".trim().to_lower().replace("world", "vigil")
                    if result == "hello vigil" {
                        return 0
                    }
                    return 1
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_map_compound_index_assignment(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    map<string, i32> m = {"a": 1, "b": 2}
                    m["a"] += 10
                    m["b"] *= 3
                    return m["a"] + m["b"]
                }
            """})
            # 1+10=11, 2*3=6, 11+6=17
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 17, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_nested_indexing(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    array<array<i32>> grid = []
                    array<i32> row1 = [1, 2, 3]
                    array<i32> row2 = [4, 5, 6]
                    grid.push(row1)
                    grid.push(row2)
                    return grid[0][1] + grid[1][2]
                }
            """})
            # grid[0][1]=2, grid[1][2]=6, 2+6=8
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 8, msg=result.stderr)
            self.assertEqual(result.stderr, "")

    def test_fstring_with_method_call(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_syntax_") as tmpdir:
            root = Path(tmpdir)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    string name = "  alice  "
                    string msg = f"hello {name.trim()}!"
                    if msg == "hello alice!" {
                        return 0
                    }
                    return 1
                }
            """})
            result = run_vigil(root, "main.vigil")
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
