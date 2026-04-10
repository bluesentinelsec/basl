#include "vigil_test.h"

#include <string.h>

#include "vigil/stdlib.h"
#include "vigil/transpile.h"
#include "vigil/vigil.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static vigil_status_t CompileAndTranspile(vigil_runtime_t *runtime, const char *source_text, vigil_string_t *out,
                                          vigil_error_t *error)
{
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_native_registry_t natives;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id = 0U;
    vigil_status_t status;

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_native_registry_init(&natives);
    vigil_stdlib_register_all(&natives, error);

    status = vigil_source_registry_register_cstr(&registry, "main.vigil", source_text, &source_id, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    status = vigil_compile_source_with_natives(&registry, source_id, &natives, &function, &diagnostics, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    status = vigil_transpile_to_c(runtime, function, out, error);

cleanup:
    vigil_object_release(&function);
    vigil_native_registry_free(&natives);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    return status;
}

/* ── Null argument tests ─────────────────────────────────────────── */

TEST(TranspileTest, NullRuntimeReturnsError)
{
    vigil_string_t out;
    vigil_error_t error = {0};
    vigil_runtime_t *runtime = NULL;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    EXPECT_EQ(vigil_transpile_to_c(NULL, NULL, &out, &error), VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, NullFunctionReturnsError)
{
    vigil_string_t out;
    vigil_error_t error = {0};
    vigil_runtime_t *runtime = NULL;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    EXPECT_EQ(vigil_transpile_to_c(runtime, NULL, &out, &error), VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

/* ── Basic transpile tests ───────────────────────────────────────── */

TEST(TranspileTest, ReturnConstant)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime, "fn main() -> i32 { return 42 }\n", &out, &error), VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "vigil_generated.h") != NULL);
    EXPECT_TRUE(strstr(c, "42LL") != NULL);
    EXPECT_TRUE(strstr(c, "return r[") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, ArithmeticI32)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn main() -> i32 {\n"
                                  "    i32 a = 10\n"
                                  "    i32 b = 3\n"
                                  "    return a + b\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "10LL") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, ComparisonAndBranch)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn main() -> i32 {\n"
                                  "    i32 x = 5\n"
                                  "    if x < 10 { return 1 }\n"
                                  "    return 0\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "goto") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, FunctionCall)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn add(i32 a, i32 b) -> i32 { return a + b }\n"
                                  "fn main() -> i32 { return add(3, 4) - 7 }\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "vigil_fn_") != NULL);
    EXPECT_TRUE(strstr(c, "arg_0") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, RecursiveFibonacci)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn fib(i32 n) -> i32 {\n"
                                  "    if n < 2 { return n }\n"
                                  "    return fib(n - 1) + fib(n - 2)\n"
                                  "}\n"
                                  "fn main() -> i32 { return fib(10) - 55 }\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    /* Should have at least two functions */
    EXPECT_TRUE(strstr(c, "vigil_fn_0") != NULL);
    EXPECT_TRUE(strstr(c, "vigil_fn_1") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, BitwiseOps)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn main() -> i32 {\n"
                                  "    i32 a = 0xFF\n"
                                  "    i32 b = 0x0F\n"
                                  "    return (a & b) | (a ^ b)\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(c != NULL);
    EXPECT_TRUE(vigil_string_length(&out) > 0);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, MathIntrinsics)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "import \"math\"\n"
                                  "fn main() -> i32 {\n"
                                  "    f64 x = math.sqrt(4.0)\n"
                                  "    return i32(x) - 2\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "sqrt") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, TypeConversions)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn main() -> i32 {\n"
                                  "    i64 x = i64(42)\n"
                                  "    return i32(x)\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "int32_t") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, NestedLoop)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn main() -> i32 {\n"
                                  "    i32 sum = 0\n"
                                  "    for (i32 i = 0; i < 10; i = i + 1) {\n"
                                  "        for (i32 j = 0; j < 10; j = j + 1) {\n"
                                  "            sum = sum + 1\n"
                                  "        }\n"
                                  "    }\n"
                                  "    return sum - 100\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "goto") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, OutputContainsHeader)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime, "fn main() -> i32 { return 0 }\n", &out, &error), VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "#include \"vigil_generated.h\"") != NULL);
    EXPECT_TRUE(strstr(c, "#include <math.h>") != NULL);
    EXPECT_TRUE(strstr(c, "#include <string.h>") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

/* ── Phase 2 tests ───────────────────────────────────────────────── */

TEST(TranspileTest, StringConstantEmitsObjectNew)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn main() -> i32 {\n"
                                  "    string s = \"hello\"\n"
                                  "    return 0\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "vigil_string_object_new") != NULL);
    EXPECT_TRUE(strstr(c, "\"hello\"") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, NativeCallEmitsTcCallNative)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "import \"fmt\"\n"
                                  "fn main() -> i32 {\n"
                                  "    fmt.println(\"test\")\n"
                                  "    return 0\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "vigil_tc_call_native") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, GeneratedCodeIncludesTranspileRt)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime, "fn main() -> i32 { return 0 }\n", &out, &error), VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "vigil/transpile_rt.h") != NULL);
    EXPECT_TRUE(strstr(c, "vigil_tc_t *tc") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, ArrayNewEmitsVmOp)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "fn main() -> i32 {\n"
                                  "    i32 a = [1, 2, 3].len()\n"
                                  "    return a - 3\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "vigil_tc_vm_op") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, ClassFieldEmitsVmOp)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "class Point {\n"
                                  "    i32 x\n"
                                  "    i32 y\n"
                                  "}\n"
                                  "fn main() -> i32 {\n"
                                  "    Point p = Point(10, 20)\n"
                                  "    return p.x - 10\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    EXPECT_TRUE(strstr(c, "vigil_tc_new_instance") != NULL);
    EXPECT_TRUE(strstr(c, "vigil_tc_vm_op") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

TEST(TranspileTest, GlobalVarEmitsVmOp)
{
    vigil_runtime_t *runtime = NULL;
    vigil_string_t out;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&out, runtime);

    ASSERT_EQ(CompileAndTranspile(runtime,
                                  "i32 counter = 0\n"
                                  "fn get_counter() -> i32 { return counter }\n"
                                  "fn main() -> i32 {\n"
                                  "    counter = 5\n"
                                  "    return get_counter() - 5\n"
                                  "}\n",
                                  &out, &error),
              VIGIL_STATUS_OK);

    const char *c = vigil_string_c_str(&out);
    /* GET_GLOBAL = opcode 76, SET_GLOBAL = opcode 77 */
    EXPECT_TRUE(strstr(c, "vigil_tc_vm_op") != NULL);

    vigil_string_free(&out);
    vigil_runtime_close(&runtime);
}

/* ── Registration ────────────────────────────────────────────────── */

void register_transpile_tests(void)
{
    REGISTER_TEST(TranspileTest, NullRuntimeReturnsError);
    REGISTER_TEST(TranspileTest, NullFunctionReturnsError);
    REGISTER_TEST(TranspileTest, ReturnConstant);
    REGISTER_TEST(TranspileTest, ArithmeticI32);
    REGISTER_TEST(TranspileTest, ComparisonAndBranch);
    REGISTER_TEST(TranspileTest, FunctionCall);
    REGISTER_TEST(TranspileTest, RecursiveFibonacci);
    REGISTER_TEST(TranspileTest, BitwiseOps);
    REGISTER_TEST(TranspileTest, MathIntrinsics);
    REGISTER_TEST(TranspileTest, TypeConversions);
    REGISTER_TEST(TranspileTest, NestedLoop);
    REGISTER_TEST(TranspileTest, OutputContainsHeader);
    REGISTER_TEST(TranspileTest, StringConstantEmitsObjectNew);
    REGISTER_TEST(TranspileTest, NativeCallEmitsTcCallNative);
    REGISTER_TEST(TranspileTest, GeneratedCodeIncludesTranspileRt);
    REGISTER_TEST(TranspileTest, ArrayNewEmitsVmOp);
    REGISTER_TEST(TranspileTest, ClassFieldEmitsVmOp);
    REGISTER_TEST(TranspileTest, GlobalVarEmitsVmOp);
}
