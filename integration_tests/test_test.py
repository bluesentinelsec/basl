"""Integration tests for vigil test."""

import json
import os
import subprocess
import tempfile
import unittest

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")
# Resolve to absolute path so cwd changes don't break it.
if not os.path.isabs(VIGIL_BIN):
    VIGIL_BIN = os.path.abspath(VIGIL_BIN)


def run_test(*args, cwd=None):
    return subprocess.run(
        [VIGIL_BIN, "test"] + list(args),
        capture_output=True, text=True, timeout=30, cwd=cwd
    )


class TestVigilTest(unittest.TestCase):

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def _write(self, relpath, content):
        path = os.path.join(self.tmpdir, relpath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(content)
        return path

    def _write_coverage_project(self):
        self._write("vigil.toml", '[project]\nname = "coverage"\n')
        self._write("lib/helper.vigil",
            'pub fn classify(i32 value) -> i32 {\n'
            '    if value > 0 {\n'
            '        return 1\n'
            '    }\n'
            '    return 0\n'
            '}\n')
        test_path = self._write("test/helper_test.vigil",
            'import "test"\n'
            'import "helper" as helper\n'
            'fn test_classify(test.T t) -> void {\n'
            '    t.assert(helper.classify(1) == 1, "positive")\n'
            '}\n')
        return test_path

    def test_passing(self):
        self._write("pass_test.vigil",
            'import "test"\n'
            'fn test_ok(test.T t) -> void {\n'
            '    t.assert(true, "should pass")\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "pass_test.vigil"))
        self.assertEqual(r.returncode, 0)
        self.assertIn("PASS: 1 passed", r.stdout)

    def test_failing(self):
        self._write("fail_test.vigil",
            'import "test"\n'
            'fn test_bad(test.T t) -> void {\n'
            '    t.assert(false, "expected failure")\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "fail_test.vigil"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("FAIL: test_bad", r.stdout)
        self.assertIn("expected failure", r.stdout)

    def test_fail_method(self):
        self._write("tfail_test.vigil",
            'import "test"\n'
            'fn test_explicit(test.T t) -> void {\n'
            '    t.fail("boom")\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "tfail_test.vigil"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("boom", r.stdout)

    def test_verbose(self):
        self._write("v_test.vigil",
            'import "test"\n'
            'fn test_one(test.T t) -> void {\n'
            '    t.assert(true, "ok")\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "v_test.vigil"), "-v")
        self.assertEqual(r.returncode, 0)
        self.assertIn("=== RUN   test_one", r.stdout)
        self.assertIn("--- PASS: test_one", r.stdout)

    def test_run_filter(self):
        self._write("filter_test.vigil",
            'import "test"\n'
            'fn test_alpha(test.T t) -> void { t.assert(true, "ok") }\n'
            'fn test_beta(test.T t) -> void { t.fail("should not run") }\n')
        r = run_test(os.path.join(self.tmpdir, "filter_test.vigil"),
                     "-run", "alpha", "-v")
        self.assertEqual(r.returncode, 0)
        self.assertIn("test_alpha", r.stdout)
        self.assertNotIn("test_beta", r.stdout)

    def test_directory_discovery(self):
        self._write("sub/a_test.vigil",
            'import "test"\n'
            'fn test_a(test.T t) -> void { t.assert(true, "ok") }\n')
        self._write("sub/b_test.vigil",
            'import "test"\n'
            'fn test_b(test.T t) -> void { t.assert(true, "ok") }\n')
        r = run_test(os.path.join(self.tmpdir, "sub"), "-v")
        self.assertEqual(r.returncode, 0)
        self.assertIn("PASS: 2 passed", r.stdout)

    def test_no_test_files(self):
        self._write("regular.vigil", 'fn main() -> i32 { return 0 }\n')
        r = run_test(self.tmpdir)
        self.assertEqual(r.returncode, 0)
        self.assertIn("no test files found", r.stdout)

    def test_default_target_uses_current_directory_without_manifest(self):
        self._write("cwd_test.vigil",
            'import "test"\n'
            'fn test_cwd(test.T t) -> void {\n'
            '    t.assert(true, "should run from current directory")\n'
            '}\n')
        r = run_test(cwd=self.tmpdir)
        self.assertEqual(r.returncode, 0)
        self.assertIn("PASS: 1 passed", r.stdout)

    def test_project_default_target(self):
        self._write("vigil.toml", '[project]\nname = "myproj"\n')
        self._write("test/my_test.vigil",
            'import "test"\n'
            'fn test_proj(test.T t) -> void { t.assert(true, "ok") }\n')
        r = run_test(cwd=self.tmpdir)
        self.assertEqual(r.returncode, 0)
        self.assertIn("PASS: 1 passed", r.stdout)

    def test_imported_helper_module(self):
        self._write("vigil.toml", '[project]\nname = "myproj"\n')
        self._write("lib/helper.vigil",
            'pub fn message() -> string {\n'
            '    return "ok"\n'
            '}\n')
        test_path = self._write("test/import_test.vigil",
            'import "test"\n'
            'import "helper" as helper\n'
            'fn test_import(test.T t) -> void {\n'
            '    t.assert(helper.message() == "ok", "import should resolve")\n'
            '}\n')
        r = run_test(test_path)
        self.assertEqual(r.returncode, 0, msg=f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        self.assertIn("PASS: 1 passed", r.stdout)

    def test_explicit_non_test_file_argument_is_ignored(self):
        target = self._write("regular.vigil", 'fn main() -> i32 { return 0 }\n')
        r = run_test(target)
        self.assertEqual(r.returncode, 0)
        self.assertIn("no test files found", r.stdout)

    def test_missing_run_pattern(self):
        r = run_test("-run")
        self.assertEqual(r.returncode, 2)
        self.assertIn("-run requires a pattern", r.stderr)

    def test_compile_error_reports_diagnostic(self):
        bad_test = self._write("bad_test.vigil",
            'import "test"\n'
            'fn test_bad(test.T t) -> void {\n'
            '    broken(\n'
            '}\n')
        r = run_test(bad_test)
        self.assertEqual(r.returncode, 1)
        self.assertIn("FAIL\t", r.stdout)
        self.assertIn("bad_test.vigil", r.stdout)

    def test_mixed_pass_fail(self):
        self._write("mix_test.vigil",
            'import "test"\n'
            'fn test_good(test.T t) -> void { t.assert(true, "ok") }\n'
            'fn test_bad(test.T t) -> void { t.assert(false, "nope") }\n')
        r = run_test(os.path.join(self.tmpdir, "mix_test.vigil"), "-v")
        self.assertEqual(r.returncode, 1)
        self.assertIn("--- PASS: test_good", r.stdout)
        self.assertIn("--- FAIL: test_bad", r.stdout)
        self.assertIn("1 passed, 1 failed", r.stdout)

    def test_help(self):
        r = run_test("--help")
        self.assertEqual(r.returncode, 0)
        self.assertIn("Usage:", r.stdout)

    def test_coverage_summary(self):
        test_path = self._write_coverage_project()
        r = run_test("--coverage", test_path)
        self.assertEqual(r.returncode, 0, msg=f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        self.assertIn("coverage: 1 files, 1 test files", r.stdout)
        self.assertIn("helper.vigil", r.stdout)
        self.assertIn("66.7% lines (2/3)", r.stdout)
        self.assertIn("50.0% branches (1/2)", r.stdout)

    def test_coverage_verbose_lists_uncovered_details(self):
        test_path = self._write_coverage_project()
        r = run_test("--coverage", "--verbose", test_path)
        self.assertEqual(r.returncode, 0, msg=f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        self.assertIn("line 5: not covered", r.stdout)
        self.assertIn("line 2: true branch not taken", r.stdout)

    def test_coverage_json(self):
        test_path = self._write_coverage_project()
        r = run_test("--coverage", "--format", "json", test_path)
        self.assertEqual(r.returncode, 0, msg=f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        payload = json.loads(r.stdout)
        self.assertEqual(len(payload["files"]), 1)
        self.assertEqual(payload["files"][0]["lines"]["covered"], 2)
        self.assertEqual(payload["files"][0]["lines"]["total"], 3)
        self.assertEqual(payload["files"][0]["branches"]["covered"], 1)
        self.assertEqual(payload["files"][0]["branches"]["total"], 2)
        self.assertEqual(payload["files"][0]["uncovered_lines"], [5])
        self.assertEqual(payload["files"][0]["missed_branches"][0]["line"], 2)
        self.assertIn("PASS: 1 passed", r.stderr)

    def test_coverage_min_threshold_fails(self):
        test_path = self._write_coverage_project()
        r = run_test("--coverage", "--min-coverage", "80", test_path)
        self.assertEqual(r.returncode, 1, msg=f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        self.assertIn("coverage threshold not met: 66.7% < 80.0%", r.stdout)

    def test_coverage_include_deps(self):
        self._write("vigil.toml", '[project]\nname = "coverage"\n')
        external_root = tempfile.mkdtemp()
        shared_root = os.path.join(external_root, "shared")
        os.makedirs(shared_root, exist_ok=True)
        with open(os.path.join(shared_root, "helper.vigil"), "w") as f:
            f.write(
                'pub fn external() -> i32 {\n'
                '    if true {\n'
                '        return 7\n'
                '    }\n'
                '    return 0\n'
                '}\n'
            )
        import_path = os.path.relpath(os.path.join(shared_root, "helper"), os.path.join(self.tmpdir, "test"))
        import_path = import_path.replace(os.sep, "/")
        test_path = self._write("test/dep_test.vigil",
            'import "test"\n'
            f'import "{import_path}" as helper\n'
            'fn test_dep(test.T t) -> void {\n'
            '    t.assert(helper.external() == 7, "ok")\n'
            '}\n')
        without_deps = run_test("--coverage", test_path)
        self.assertEqual(without_deps.returncode, 0,
                         msg=f"stdout:\n{without_deps.stdout}\nstderr:\n{without_deps.stderr}")
        self.assertIn("coverage: 0 files, 1 test files", without_deps.stdout)
        with_deps = run_test("--coverage", "--include-deps", test_path)
        self.assertEqual(with_deps.returncode, 0, msg=f"stdout:\n{with_deps.stdout}\nstderr:\n{with_deps.stderr}")
        self.assertIn("coverage: 1 files, 1 test files", with_deps.stdout)
        self.assertIn("shared/helper.vigil", with_deps.stdout)

    def test_coverage_no_test_files(self):
        self._write("regular.vigil", 'fn main() -> i32 { return 0 }\n')
        r = run_test("--coverage", self.tmpdir)
        self.assertEqual(r.returncode, 0)
        self.assertIn("no test files found", r.stdout)
        self.assertNotIn("coverage:", r.stdout)

    def test_coverage_still_reports_when_tests_fail(self):
        self._write("vigil.toml", '[project]\nname = "coverage"\n')
        self._write("lib/helper.vigil",
            'pub fn classify(i32 value) -> i32 {\n'
            '    if value > 0 {\n'
            '        return 1\n'
            '    }\n'
            '    return 0\n'
            '}\n')
        test_path = self._write("test/helper_test.vigil",
            'import "test"\n'
            'import "helper" as helper\n'
            'fn test_classify(test.T t) -> void {\n'
            '    helper.classify(1)\n'
            '    t.fail("boom")\n'
            '}\n')
        r = run_test("--coverage", test_path)
        self.assertEqual(r.returncode, 1, msg=f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        self.assertIn("boom", r.stdout)
        self.assertIn("coverage: 1 files, 1 test files", r.stdout)
        self.assertIn("66.7% lines (2/3)", r.stdout)


if __name__ == "__main__":
    unittest.main()
