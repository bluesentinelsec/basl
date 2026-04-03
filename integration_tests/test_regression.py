#!/usr/bin/env python3
"""Regression tests for refactoring safety.

These tests cover boundary conditions, error paths, complex interactions,
and edge cases that are critical to preserve during refactoring.
"""

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
    raise FileNotFoundError("could not locate VIGIL CLI executable")


def write_sources(root: Path, sources: dict[str, str]) -> None:
    for rel, text in sources.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(textwrap.dedent(text).strip() + "\n", encoding="utf-8")


def run_vigil(root: Path, entry: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [*resolve_vigil_command(), "run", str(root / entry)],
        capture_output=True, text=True, cwd=REPO_ROOT, check=False,
    )


def run_ok(tc: unittest.TestCase, root: Path, entry: str, expected: int) -> None:
    """Helper: run and assert exit code with no stderr."""
    r = run_vigil(root, entry)
    tc.assertEqual(r.returncode, expected, msg=r.stderr)
    tc.assertEqual(r.stderr, "")


def run_err(tc: unittest.TestCase, root: Path, entry: str, fragment: str) -> None:
    """Helper: run and assert non-zero exit with stderr containing fragment."""
    r = run_vigil(root, entry)
    tc.assertNotEqual(r.returncode, 0)
    tc.assertIn(fragment, r.stderr)


class BoundaryTest(unittest.TestCase):
    """Boundary conditions and edge cases."""

    def _run(self, code: str, expected: int) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_ok(self, root, "main.vigil", expected)

    def test_empty_array_pop_returns_error(self) -> None:
        self._run("""
            fn main() -> i32 {
                array<i32> a = []
                i32 v, err e = a.pop()
                if e != ok { return 1 }
                return 0
            }
        """, 1)

    def test_array_get_out_of_bounds(self) -> None:
        self._run("""
            fn main() -> i32 {
                array<i32> a = [1]
                i32 v, err e = a.get(99)
                if e != ok { return 1 }
                return 0
            }
        """, 1)

    def test_array_set_out_of_bounds(self) -> None:
        self._run("""
            fn main() -> i32 {
                array<i32> a = [1]
                err e = a.set(99, 5)
                if e != ok { return 1 }
                return 0
            }
        """, 1)

    def test_empty_string_length(self) -> None:
        self._run("""
            fn main() -> i32 {
                string s = ""
                return s.len()
            }
        """, 0)

    def test_string_char_at_out_of_bounds(self) -> None:
        self._run("""
            fn main() -> i32 {
                string s = "hi"
                string ch, err e = s.char_at(99)
                if e != ok { return 1 }
                return 0
            }
        """, 1)

    def test_string_substr_past_end(self) -> None:
        self._run("""
            fn main() -> i32 {
                string s = "hello"
                string sub, err e = s.substr(3, 99)
                if e != ok { return 1 }
                return 0
            }
        """, 1)

    def test_empty_map_get_returns_not_found(self) -> None:
        self._run("""
            fn main() -> i32 {
                map<string, i32> m = {}
                i32 v, bool found = m.get("x")
                if !found { return 1 }
                return 0
            }
        """, 1)

    def test_empty_map_length(self) -> None:
        self._run("""
            fn main() -> i32 {
                map<string, i32> m = {}
                return m.len()
            }
        """, 0)

    def test_map_remove_missing_key(self) -> None:
        self._run("""
            fn main() -> i32 {
                map<string, i32> m = {"a": 1}
                i32 v, bool had = m.remove("z")
                if !had { return 1 }
                return 0
            }
        """, 1)

    def test_string_split_with_empty_segments(self) -> None:
        self._run("""
            fn main() -> i32 {
                array<string> parts = "a,,b".split(",")
                return parts.len()
            }
        """, 3)

    def test_string_index_of_not_found(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 idx, bool found = "hello".index_of("xyz")
                if !found { return 1 }
                return 0
            }
        """, 1)

    def test_array_slice_empty_range(self) -> None:
        self._run("""
            fn main() -> i32 {
                array<i32> a = [1, 2, 3]
                array<i32> s = a.slice(1, 1)
                return s.len()
            }
        """, 0)

    def test_array_contains_missing(self) -> None:
        self._run("""
            fn main() -> i32 {
                array<i32> a = [1, 2, 3]
                if !a.contains(99) { return 1 }
                return 0
            }
        """, 1)


class RecursionAndControlFlowTest(unittest.TestCase):
    """Recursion, nested loops, and complex control flow."""

    def _run(self, code: str, expected: int) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_ok(self, root, "main.vigil", expected)

    def test_recursive_fibonacci(self) -> None:
        self._run("""
            fn fib(i32 n) -> i32 {
                if n <= 1 { return n }
                return fib(n - 1) + fib(n - 2)
            }
            fn main() -> i32 { return fib(10) }
        """, 55)

    def test_mutual_recursion(self) -> None:
        self._run("""
            fn is_even(i32 n) -> bool {
                if n == 0 { return true }
                return is_odd(n - 1)
            }
            fn is_odd(i32 n) -> bool {
                if n == 0 { return false }
                return is_even(n - 1)
            }
            fn main() -> i32 {
                if is_even(10) && is_odd(7) { return 0 }
                return 1
            }
        """, 0)

    def test_nested_loop_continue(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 total = 0
                for (i32 i = 0; i < 3; i++) {
                    for (i32 j = 0; j < 3; j++) {
                        if j == 1 { continue }
                        total += 1
                    }
                }
                return total
            }
        """, 6)

    def test_nested_loop_break(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 total = 0
                for (i32 i = 0; i < 5; i++) {
                    for (i32 j = 0; j < 5; j++) {
                        if j == 2 { break }
                        total += 1
                    }
                }
                return total
            }
        """, 10)

    def test_while_true_with_break(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 x = 0
                while true {
                    x += 1
                    if x == 10 { break }
                }
                return x
            }
        """, 10)

    def test_nested_for_in_with_guard(self) -> None:
        self._run("""
            fn safe_div(i32 a, i32 b) -> (i32, err) {
                if b == 0 { return 0, err("zero", err.arg) }
                return a / b, ok
            }
            fn main() -> i32 {
                array<i32> divisors = [2, 0, 3, 0, 5]
                i32 total = 0
                for d in divisors {
                    guard i32 val, err e = safe_div(30, d) {
                        continue
                    }
                    total += val
                }
                return total
            }
        """, 31)

    def test_self_recursive_tail_call(self) -> None:
        self._run("""
            fn count_down(i32 n, i32 acc) -> i32 {
                if n <= 0 { return acc }
                return count_down(n - 1, acc + n)
            }
            fn main() -> i32 { return count_down(10, 0) }
        """, 55)

    def test_switch_no_match_hits_default(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 x = 99
                switch x {
                    case 1: return 10
                    case 2: return 20
                    default: return 30
                }
            }
        """, 30)

    def test_switch_first_case_match(self) -> None:
        self._run("""
            fn main() -> i32 {
                switch 0 {
                    case 0: return 10
                    default: return 20
                }
            }
        """, 10)


class ClosureAndFunctionValueTest(unittest.TestCase):
    """Closures, function values, and higher-order patterns."""

    def _run(self, code: str, expected: int) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_ok(self, root, "main.vigil", expected)

    def test_closure_factory(self) -> None:
        self._run("""
            fn make_adder(i32 base) -> fn(i32) -> i32 {
                fn(i32) -> i32 adder = fn(i32 x) -> i32 {
                    return base + x
                }
                return adder
            }
            fn main() -> i32 {
                fn(i32) -> i32 add5 = make_adder(5)
                fn(i32) -> i32 add10 = make_adder(10)
                return add5(3) + add10(3)
            }
        """, 21)

    def test_function_value_as_argument(self) -> None:
        self._run("""
            fn apply(fn(i32) -> i32 f, i32 x) -> i32 { return f(x) }
            fn triple(i32 x) -> i32 { return x * 3 }
            fn main() -> i32 { return apply(triple, 7) }
        """, 21)

    def test_nested_local_functions(self) -> None:
        self._run("""
            fn outer() -> i32 {
                fn inner(i32 x) -> i32 {
                    fn innermost(i32 y) -> i32 { return y + 1 }
                    return innermost(x) * 2
                }
                return inner(5)
            }
            fn main() -> i32 { return outer() }
        """, 12)

    def test_closure_over_multiple_variables(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 a = 10
                i32 b = 20
                fn() -> i32 f = fn() -> i32 { return a + b }
                return f()
            }
        """, 30)

    def test_closure_returned_from_function(self) -> None:
        self._run("""
            fn make_counter(i32 start) -> fn() -> i32 {
                fn() -> i32 f = fn() -> i32 { return start }
                return f
            }
            fn main() -> i32 {
                fn() -> i32 c = make_counter(42)
                return c()
            }
        """, 42)


class DeferTest(unittest.TestCase):
    """Defer ordering and interaction with control flow."""

    def _run(self, code: str, expected: int) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_ok(self, root, "main.vigil", expected)

    def test_defer_lifo_three(self) -> None:
        self._run("""
            i32 seq = 0
            fn a() -> void { seq = seq * 10 + 1 }
            fn b() -> void { seq = seq * 10 + 2 }
            fn c() -> void { seq = seq * 10 + 3 }
            fn work() -> void {
                defer a()
                defer b()
                defer c()
            }
            fn main() -> i32 {
                work()
                if seq == 321 { return 0 }
                return 1
            }
        """, 0)

    def test_defer_runs_on_early_return(self) -> None:
        self._run("""
            i32 ran = 0
            fn cleanup() -> void { ran = 1 }
            fn work(bool early) -> i32 {
                defer cleanup()
                if early { return 1 }
                return 2
            }
            fn main() -> i32 {
                work(true)
                return ran
            }
        """, 1)

    def test_multiple_defers_in_loop_all_run(self) -> None:
        self._run("""
            i32 count = 0
            fn bump() -> void { count += 1 }
            fn work() -> void {
                for (i32 i = 0; i < 5; i++) {
                    defer bump()
                }
            }
            fn main() -> i32 {
                work()
                return count
            }
        """, 5)


class ClassAndInterfaceTest(unittest.TestCase):
    """Classes, interfaces, and object-oriented patterns."""

    def _run(self, code: str, expected: int) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_ok(self, root, "main.vigil", expected)

    def test_class_field_compound_assignment(self) -> None:
        self._run("""
            class Counter {
                pub i32 value
                fn init(i32 v) -> void { self.value = v }
                pub fn add(i32 n) -> void { self.value += n }
            }
            fn main() -> i32 {
                Counter c = Counter(10)
                c.add(5)
                c.value *= 2
                return c.value
            }
        """, 30)

    def test_multiple_interface_implementation(self) -> None:
        self._run("""
            interface A { fn a() -> i32 }
            interface B { fn b() -> i32 }
            class Both implements A, B {
                fn init() -> void {}
                fn a() -> i32 { return 1 }
                fn b() -> i32 { return 2 }
            }
            fn use_a(A x) -> i32 { return x.a() }
            fn use_b(B x) -> i32 { return x.b() }
            fn main() -> i32 {
                Both obj = Both()
                return use_a(obj) + use_b(obj)
            }
        """, 3)

    def test_fallible_init_success_and_failure(self) -> None:
        self._run("""
            class Validated {
                pub i32 value
                fn init(i32 v) -> err {
                    if v < 0 { return err("negative", err.arg) }
                    self.value = v
                    return ok
                }
            }
            fn main() -> i32 {
                Validated good, err e1 = Validated(10)
                if e1 != ok { return 1 }
                Validated bad, err e2 = Validated(-1)
                if e2 == ok { return 2 }
                return good.value
            }
        """, 10)

    def test_class_method_calls_other_method(self) -> None:
        self._run("""
            class Calc {
                pub i32 base
                fn init(i32 b) -> void { self.base = b }
                pub fn double() -> i32 { return self.base * 2 }
                pub fn quad() -> i32 { return self.double() * 2 }
            }
            fn main() -> i32 {
                Calc c = Calc(5)
                return c.quad()
            }
        """, 20)


class ErrorFlowTest(unittest.TestCase):
    """Error propagation, guard, and error inspection."""

    def _run(self, code: str, expected: int) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_ok(self, root, "main.vigil", expected)

    def test_error_message_inspection(self) -> None:
        self._run("""
            fn fail() -> (i32, err) {
                return 0, err("test error", err.not_found)
            }
            fn main() -> i32 {
                i32 v, err e = fail()
                if e != ok {
                    string msg = e.message()
                    if msg == "test error" { return 0 }
                }
                return 1
            }
        """, 0)

    def test_error_kind_routing(self) -> None:
        self._run("""
            fn fail(i32 mode) -> (i32, err) {
                if mode == 1 { return 0, err("a", err.not_found) }
                if mode == 2 { return 0, err("b", err.permission) }
                return 42, ok
            }
            fn main() -> i32 {
                i32 v1, err e1 = fail(1)
                i32 kind1 = 0
                switch e1.kind() {
                    case err.not_found: kind1 = 1
                    default: kind1 = 9
                }
                if kind1 != 1 { return 1 }
                i32 v2, err e2 = fail(2)
                i32 kind2 = 0
                switch e2.kind() {
                    case err.permission: kind2 = 2
                    default: kind2 = 9
                }
                if kind2 != 2 { return 2 }
                i32 v3, err e3 = fail(0)
                if e3 != ok { return 3 }
                if v3 != 42 { return 4 }
                return 0
            }
        """, 0)

    def test_guard_binds_value_on_success(self) -> None:
        self._run("""
            fn maybe(i32 x) -> (i32, err) {
                return x * 10, ok
            }
            fn main() -> i32 {
                guard i32 val, err e = maybe(5) {
                    return 1
                }
                return val
            }
        """, 50)

    def test_discard_error_with_underscore(self) -> None:
        self._run("""
            fn pair() -> (i32, err) { return 42, ok }
            fn main() -> i32 {
                i32 val, err _ = pair()
                return val
            }
        """, 42)

    def test_discard_multi_return_bare_call(self) -> None:
        self._run("""
            fn two() -> (i32, i32) { return 1, 2 }
            fn three() -> (i32, i32, i32) { return 10, 20, 30 }
            fn main() -> i32 {
                two()
                three()
                i32 a, i32 b = two()
                if a != 1 { return 1 }
                if b != 2 { return 2 }
                return 0
            }
        """, 0)

    def test_standalone_err_variable(self) -> None:
        self._run("""
            fn main() -> i32 {
                err e = err("test", err.arg)
                if e != ok { return 1 }
                return 0
            }
        """, 1)


class MultiModuleTest(unittest.TestCase):
    """Cross-module interactions critical for refactoring safety."""

    def test_imported_class_with_interface(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {
                "lib.vigil": """
                    pub interface Printable {
                        fn label() -> string
                    }
                    pub class Item implements Printable {
                        pub string name
                        fn init(string n) -> void { self.name = n }
                        fn label() -> string { return self.name }
                    }
                """,
                "main.vigil": """
                    import "lib"
                    fn get_label(lib.Printable p) -> string { return p.label() }
                    fn main() -> i32 {
                        lib.Item item = lib.Item("test")
                        if get_label(item) == "test" { return 0 }
                        return 1
                    }
                """,
            })
            run_ok(self, root, "main.vigil", 0)

    def test_imported_enum_in_switch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {
                "defs.vigil": """
                    pub enum Level { Low, Medium, High }
                """,
                "main.vigil": """
                    import "defs"
                    fn main() -> i32 {
                        defs.Level lv = defs.Level.Medium
                        switch lv {
                            case defs.Level.Low: return 1
                            case defs.Level.Medium: return 2
                            case defs.Level.High: return 3
                            default: return 0
                        }
                    }
                """,
            })
            run_ok(self, root, "main.vigil", 2)

    def test_imported_global_mutation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {
                "state.vigil": """
                    pub i32 counter = 0
                    pub fn increment() -> void { counter += 1 }
                """,
                "main.vigil": """
                    import "state"
                    fn main() -> i32 {
                        state.increment()
                        state.increment()
                        state.increment()
                        return state.counter
                    }
                """,
            })
            run_ok(self, root, "main.vigil", 3)

    def test_private_access_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {
                "lib.vigil": """
                    fn secret() -> i32 { return 42 }
                    pub fn public_fn() -> i32 { return 1 }
                """,
                "main.vigil": """
                    import "lib"
                    fn main() -> i32 { return lib.secret() }
                """,
            })
            run_err(self, root, "main.vigil", "not public")

    def test_three_module_chain(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {
                "a.vigil": """
                    pub fn base() -> i32 { return 10 }
                """,
                "b.vigil": """
                    import "a"
                    pub fn doubled() -> i32 { return a.base() * 2 }
                """,
                "main.vigil": """
                    import "b"
                    fn main() -> i32 { return b.doubled() }
                """,
            })
            run_ok(self, root, "main.vigil", 20)


class TypeConversionAndArithmeticTest(unittest.TestCase):
    """Type conversions, arithmetic edge cases, and numeric boundaries."""

    def _run(self, code: str, expected: int) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_ok(self, root, "main.vigil", expected)

    def test_all_integer_conversions(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 a = 42
                i64 b = i64(a)
                u8 c = u8(a)
                u32 d = u32(a)
                u64 e = u64(a)
                f64 f = f64(a)
                i32 back = i32(b) + i32(c) + i32(d) + i32(e)
                if back != 168 { return 1 }
                return 0
            }
        """, 0)

    def test_string_conversion(self) -> None:
        self._run("""
            fn main() -> i32 {
                if string(42) != "42" { return 1 }
                if string(true) != "true" { return 2 }
                if string(false) != "false" { return 3 }
                return 0
            }
        """, 0)

    def test_integer_modulo(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 a = 17 % 5
                if a != 2 { return 1 }
                return 0
            }
        """, 0)

    def test_logical_short_circuit(self) -> None:
        self._run("""
            i32 counter = 0
            fn side() -> bool { counter += 1; return true }
            fn main() -> i32 {
                bool r1 = false && side()
                bool r2 = true || side()
                return counter
            }
        """, 0)

    def test_bitwise_shift_and_complement(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 a = 1 << 4
                i32 b = a >> 2
                if a != 16 { return 1 }
                if b != 4 { return 2 }
                return 0
            }
        """, 0)

    def test_nested_ternary(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 x = 50
                i32 size = x < 10 ? 1 : x < 100 ? 2 : 3
                return size
            }
        """, 2)

    def test_string_replace_all_occurrences(self) -> None:
        self._run("""
            fn main() -> i32 {
                string s = "hello world".replace("o", "0")
                if s == "hell0 w0rld" { return 0 }
                return 1
            }
        """, 0)


class CompileErrorTest(unittest.TestCase):
    """Compiler error detection — must survive refactoring."""

    def _err(self, code: str, fragment: str) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_err(self, root, "main.vigil", fragment)

    def test_rejects_type_mismatch(self) -> None:
        self._err("""
            fn main() -> i32 {
                i32 x = "hello"
                return 0
            }
        """, "does not match")

    def test_rejects_unknown_variable(self) -> None:
        self._err("""
            fn main() -> i32 { return xyz }
        """, "unknown")

    def test_rejects_break_outside_loop(self) -> None:
        self._err("""
            fn main() -> i32 { break; return 0 }
        """, "break")

    def test_rejects_continue_outside_loop(self) -> None:
        self._err("""
            fn main() -> i32 { continue; return 0 }
        """, "continue")

    def test_rejects_assignment_to_const(self) -> None:
        self._err("""
            fn main() -> i32 {
                const i32 X = 10
                X = 20
                return 0
            }
        """, "const")

    def test_rejects_missing_return(self) -> None:
        self._err("""
            fn add(i32 a, i32 b) -> i32 {
                i32 c = a + b
            }
            fn main() -> i32 { return add(1, 2) }
        """, "return")

    def test_rejects_wrong_arg_count(self) -> None:
        self._err("""
            fn add(i32 a, i32 b) -> i32 { return a + b }
            fn main() -> i32 { return add(1) }
        """, "argument")

    def test_rejects_mixed_numeric_types(self) -> None:
        self._err("""
            fn main() -> i32 {
                i32 a = 1
                f64 b = 2.0
                f64 c = a + b
                return 0
            }
        """, "")  # any error about type mismatch

    def test_rejects_parenthesized_multi_return(self) -> None:
        self._err("""
            fn pair() -> (i32, err) { return (42, ok) }
            fn main() -> i32 { return 0 }
        """, "does not use parentheses")


class FStringAndStringTest(unittest.TestCase):
    """F-string edge cases and string operations."""

    def _run(self, code: str, expected: int) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": code})
            run_ok(self, root, "main.vigil", expected)

    def test_fstring_format_specifier(self) -> None:
        self._run("""
            fn main() -> i32 {
                f64 pi = 3.14159
                string s = f"pi={pi:.2f}"
                if s == "pi=3.14" { return 0 }
                return 1
            }
        """, 0)

    def test_fstring_format_variants(self) -> None:
        self._run("""
            fn main() -> i32 {
                string name = "Alice"
                i32 age = 30
                i32 zero = 0
                i32 neg = -42
                f64 pi = 3.1415926535
                bool ready = true
                string a = f"{name}"
                string b = f"{age}"
                string c = f"{pi}"
                string d = f"{ready}"
                string e = f"{name:<10}"
                string f = f"{name:>10}"
                string g = f"{name:^11}"
                string h = f"{age:0>8d}"
                string i = f"{age:x}"
                string j = f"{age:X}"
                string k = f"{age:b}"
                string l = f"{age:o}"
                string m = f"{1234567:,}"
                string n = f"{-1234567:,}"
                string o = f"{zero:b}"
                string p = f"{neg:x}"
                string q = f"{neg:b}"
                string r = f"{neg:o}"
                string s = f"{pi:.4f}"
                if a != "Alice" { return 1 }
                if b != "30" { return 2 }
                if c != "3.1415926535000001" { return 3 }
                if d != "true" { return 4 }
                if e != "Alice     " { return 5 }
                if f != "     Alice" { return 6 }
                if g != "   Alice   " { return 7 }
                if h != "00000030" { return 8 }
                if i != "1e" { return 9 }
                if j != "1E" { return 10 }
                if k != "11110" { return 11 }
                if l != "36" { return 12 }
                if m != "1,234,567" { return 13 }
                if n != "-1,234,567" { return 14 }
                if o != "0" { return 15 }
                if p != "-2a" { return 16 }
                if q != "-101010" { return 17 }
                if r != "-52" { return 18 }
                if s != "3.1416" { return 19 }
                return 0
            }
        """, 0)

    def test_fstring_escaped_braces(self) -> None:
        self._run("""
            fn main() -> i32 {
                string s = f"literal {{braces}}"
                if s == "literal {braces}" { return 0 }
                return 1
            }
        """, 0)

    def test_fstring_multiple_expressions(self) -> None:
        self._run("""
            fn main() -> i32 {
                i32 a = 1
                i32 b = 2
                string s = f"{a}+{b}={a+b}"
                if s == "1+2=3" { return 0 }
                return 1
            }
        """, 0)

    def test_fstring_rejects_integer_format_on_float(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    f64 pi = 3.14
                    string s = f"{pi:d}"
                    return 0
                }
            """})
            run_err(self, root, "main.vigil", "integer format specifier requires an integer value")

    def test_fstring_rejects_float_format_on_int(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vigil_reg_") as d:
            root = Path(d)
            write_sources(root, {"main.vigil": """
                fn main() -> i32 {
                    i32 n = 3
                    string s = f"{n:f}"
                    return 0
                }
            """})
            run_err(self, root, "main.vigil", "float format specifier 'f' requires an f64 value")

    def test_raw_string_no_escapes(self) -> None:
        self._run("""
            fn main() -> i32 {
                string raw = `hello\\nworld`
                if raw.len() == 12 { return 0 }
                return 1
            }
        """, 0)

    def test_string_bytes_roundtrip(self) -> None:
        self._run("""
            fn main() -> i32 {
                string s = "ABC"
                array<u8> b = s.bytes()
                if b.len() != 3 { return 1 }
                u8 v, err e = b.get(0)
                if i32(v) != 65 { return 2 }
                return 0
            }
        """, 0)


if __name__ == "__main__":
    unittest.main()
