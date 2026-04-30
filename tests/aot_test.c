#include "vigil_test.h"

#include <string.h>

#if !defined(_WIN32)
#include "internal/vigil_regvm.h"
#endif
#include "vigil/vigil.h"

static vigil_status_t CompileMainFunction(vigil_runtime_t *runtime, const char *source_text,
                                          vigil_source_registry_t *registry, vigil_diagnostic_list_t *diagnostics,
                                          vigil_object_t **out_function, vigil_error_t *error)
{
    vigil_source_id_t source_id = 0U;

    if (runtime == NULL || source_text == NULL || registry == NULL || diagnostics == NULL || out_function == NULL)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "compile helper arguments must not be null";
            error->length = 41;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_function = NULL;
    vigil_source_registry_init(registry, runtime);
    vigil_diagnostic_list_init(diagnostics, runtime);

    if (vigil_source_registry_register_cstr(registry, "main.vigil", source_text, &source_id, error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }

    return vigil_compile_source(registry, source_id, out_function, diagnostics, error);
}

#if defined(VIGIL_ENABLE_AOT) && !defined(_WIN32)

/* Helper: compile source, execute with AOT enabled, return the i32 result. */
static int64_t AotRun(int *vigil_test_failed_, const char *source_text)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_error_t error = {0};
    int64_t output = 0;

    EXPECT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(CompileMainFunction(runtime, source_text, &registry, &diagnostics, &function, &error), VIGIL_STATUS_OK);

    vigil_value_init_nil(&result);
    vigil_vm_set_aot_enabled(vm, 1);
    EXPECT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    output = vigil_value_as_int(&result);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return output;
}
#endif /* VIGIL_ENABLE_AOT && !_WIN32 */

/* ── Existing tests ──────────────────────────────────────────────── */

TEST(VigilAotTest, VmAotSetterRoundTrips)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);

    vigil_vm_set_aot_enabled(vm, 0);
    EXPECT_FALSE(vigil_vm_aot_enabled(vm));
    vigil_vm_set_aot_enabled(vm, 1);
    EXPECT_TRUE(vigil_vm_aot_enabled(vm));

    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilAotTest, ExecuteFunctionWithAotDisabledStillWorks)
{
    static const char *source_text = "fn main() -> i32 { return 34 }\n";
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(CompileMainFunction(runtime, source_text, &registry, &diagnostics, &function, &error), VIGIL_STATUS_OK);

    vigil_value_init_nil(&result);
    vigil_vm_set_aot_enabled(vm, 0);
    ASSERT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 34);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

#if defined(VIGIL_ENABLE_AOT) && !defined(_WIN32)
TEST(VigilAotTest, ExecuteFunctionBuildsAotCache)
{
    static const char *source_text = "fn fib(i32 n) -> i32 { if n < 2 { return n } return fib(n - 1) + fib(n - 2) }\n"
                                     "fn main() -> i32 { return fib(8) }\n";
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_error_t error = {0};
    const vigil_chunk_t *chunk;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(CompileMainFunction(runtime, source_text, &registry, &diagnostics, &function, &error), VIGIL_STATUS_OK);

    chunk = vigil_function_object_chunk(function);
    ASSERT_TRUE(chunk != NULL);
    EXPECT_TRUE(chunk->reg_cache == NULL || chunk->reg_cache->aot_cache == NULL);

    vigil_value_init_nil(&result);
    vigil_vm_set_aot_enabled(vm, 1);
    ASSERT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 21);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

/* ── AOT execution correctness tests ────────────────────────────── */

TEST(VigilAotTest, ReturnsConstant)
{
    EXPECT_EQ(AotRun(vigil_test_failed_, "fn main() -> i32 { return 42 }\n"), 42);
}

TEST(VigilAotTest, ExecutesI32Arithmetic)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 a = 10\n"
                     "    i32 b = 3\n"
                     "    return (a + b) * (a - b) / b\n"
                     "}\n"),
              30);
}

TEST(VigilAotTest, ExecutesI64Arithmetic)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i64 a = i64(1000000);\n"
                     "    i64 b = i64(2000000);\n"
                     "    i64 c = a + b;\n"
                     "    return i32(c / i64(1000000));\n"
                     "}\n"),
              3);
}

TEST(VigilAotTest, ExecutesBitwiseOperations)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 a = 0xFF\n"
                     "    i32 b = 0x0F\n"
                     "    i32 band = a & b\n"
                     "    i32 bor = a | 0x100\n"
                     "    i32 bxor = band ^ 5\n"
                     "    i32 shl = 1 << 4\n"
                     "    i32 shr = 32 >> 2\n"
                     "    return band + shl + shr\n"
                     "}\n"),
              15 + 16 + 8);
}

TEST(VigilAotTest, ExecutesIfElseBranching)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 x = 10\n"
                     "    if x > 5 {\n"
                     "        return 1\n"
                     "    } else {\n"
                     "        return 0\n"
                     "    }\n"
                     "}\n"),
              1);
}

TEST(VigilAotTest, ExecutesWhileLoop)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 sum = 0\n"
                     "    i32 i = 0\n"
                     "    while i < 10 {\n"
                     "        sum = sum + i\n"
                     "        i = i + 1\n"
                     "    }\n"
                     "    return sum\n"
                     "}\n"),
              45);
}

TEST(VigilAotTest, ExecutesForLoop)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 sum = 0;\n"
                     "    for (i32 i = 0; i < 5; i++) {\n"
                     "        sum += i;\n"
                     "    }\n"
                     "    return sum;\n"
                     "}\n"),
              10);
}

TEST(VigilAotTest, ExecutesNestedLoops)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 count = 0;\n"
                     "    for (i32 i = 0; i < 3; i++) {\n"
                     "        for (i32 j = 0; j < 4; j++) {\n"
                     "            count += 1;\n"
                     "        }\n"
                     "    }\n"
                     "    return count;\n"
                     "}\n"),
              12);
}

TEST(VigilAotTest, ExecutesRecursiveFibonacci)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn fib(i32 n) -> i32 {\n"
                     "    if n < 2 { return n }\n"
                     "    return fib(n - 1) + fib(n - 2)\n"
                     "}\n"
                     "fn main() -> i32 { return fib(10) }\n"),
              55);
}

TEST(VigilAotTest, ExecutesI32Comparisons)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn check(i32 a, i32 b) -> i32 {\n"
                     "    i32 r = 0\n"
                     "    if a < b  { r = r + 1 }\n"
                     "    if a <= b { r = r + 2 }\n"
                     "    if a > b  { r = r + 4 }\n"
                     "    if a >= b { r = r + 8 }\n"
                     "    if a == b { r = r + 16 }\n"
                     "    if a != b { r = r + 32 }\n"
                     "    return r\n"
                     "}\n"
                     "fn main() -> i32 {\n"
                     "    return check(3, 5) * 100 + check(5, 5)\n"
                     "}\n"),
              /* check(3,5): <,<=,!=  = 1+2+32 = 35 */
              /* check(5,5): <=,>=,== = 2+8+16 = 26 */
              35 * 100 + 26);
}

TEST(VigilAotTest, ExecutesNegation)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 a = 7\n"
                     "    return -a\n"
                     "}\n"),
              -7);
}

TEST(VigilAotTest, ExecutesBitwiseNot)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 a = 0\n"
                     "    return ~a\n"
                     "}\n"),
              -1);
}

TEST(VigilAotTest, ExecutesI32DivisionAndModulo)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i32 a = 17\n"
                     "    i32 b = 5\n"
                     "    return a / b * 10 + a % b\n"
                     "}\n"),
              32);
}

TEST(VigilAotTest, AotAndInterpreterAgree)
{
    /* Run the same program with AOT enabled and disabled, verify same result. */
    static const char *source_text = "fn fib(i32 n) -> i32 {\n"
                                     "    if n < 2 { return n }\n"
                                     "    return fib(n - 1) + fib(n - 2)\n"
                                     "}\n"
                                     "fn main() -> i32 { return fib(12) }\n";
    vigil_runtime_t *rt1 = NULL, *rt2 = NULL;
    vigil_vm_t *vm1 = NULL, *vm2 = NULL;
    vigil_source_registry_t reg1, reg2;
    vigil_diagnostic_list_t diag1, diag2;
    vigil_object_t *fn1 = NULL, *fn2 = NULL;
    vigil_value_t res1, res2;
    vigil_error_t err1 = {0}, err2 = {0};

    ASSERT_EQ(vigil_runtime_open(&rt1, NULL, &err1), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm1, rt1, NULL, &err1), VIGIL_STATUS_OK);
    ASSERT_EQ(CompileMainFunction(rt1, source_text, &reg1, &diag1, &fn1, &err1), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_runtime_open(&rt2, NULL, &err2), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm2, rt2, NULL, &err2), VIGIL_STATUS_OK);
    ASSERT_EQ(CompileMainFunction(rt2, source_text, &reg2, &diag2, &fn2, &err2), VIGIL_STATUS_OK);

    vigil_value_init_nil(&res1);
    vigil_value_init_nil(&res2);
    vigil_vm_set_aot_enabled(vm1, 1);
    vigil_vm_set_aot_enabled(vm2, 0);

    ASSERT_EQ(vigil_vm_execute_function(vm1, fn1, &res1, &err1), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_execute_function(vm2, fn2, &res2, &err2), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&res1), vigil_value_as_int(&res2));
    EXPECT_EQ(vigil_value_as_int(&res1), 144);

    vigil_value_release(&res1);
    vigil_value_release(&res2);
    vigil_object_release(&fn1);
    vigil_object_release(&fn2);
    vigil_diagnostic_list_free(&diag1);
    vigil_diagnostic_list_free(&diag2);
    vigil_source_registry_free(&reg1);
    vigil_source_registry_free(&reg2);
    vigil_vm_close(&vm1);
    vigil_vm_close(&vm2);
    vigil_runtime_close(&rt1);
    vigil_runtime_close(&rt2);
}

TEST(VigilAotTest, ExecutesI64ForLoop)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn main() -> i32 {\n"
                     "    i64 sum = i64(0);\n"
                     "    for (i64 i = i64(0); i < i64(100); i++) {\n"
                     "        sum += i;\n"
                     "    }\n"
                     "    return i32(sum / i64(100));\n"
                     "}\n"),
              49);
}

TEST(VigilAotTest, ExecutesDeepRecursion)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn sum_to(i32 n) -> i32 {\n"
                     "    if n <= 0 { return 0 }\n"
                     "    return n + sum_to(n - 1)\n"
                     "}\n"
                     "fn main() -> i32 { return sum_to(20) }\n"),
              210);
}

TEST(VigilAotTest, ExecutesMultipleReturnPaths)
{
    EXPECT_EQ(AotRun(vigil_test_failed_,
                     "fn classify(i32 n) -> i32 {\n"
                     "    if n < 0 { return -1 }\n"
                     "    if n == 0 { return 0 }\n"
                     "    if n < 10 { return 1 }\n"
                     "    if n < 100 { return 2 }\n"
                     "    return 3\n"
                     "}\n"
                     "fn main() -> i32 {\n"
                     "    return classify(-5) + classify(0) * 10 + classify(7) * 100 + classify(50) * 1000 + classify(200) * 10000\n"
                     "}\n"),
              -1 + 0 + 100 + 2000 + 30000);
}

TEST(VigilAotTest, NonNumericFunctionFallsBackToInterpreter)
{
    /* A function that uses strings should NOT be AOT-compiled but should
       still execute correctly via the interpreter fallback. */
    static const char *source_text = "fn main() -> i32 {\n"
                                     "    string s = \"hello\";\n"
                                     "    if s == \"hello\" { return 5 }\n"
                                     "    return 0;\n"
                                     "}\n";
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(CompileMainFunction(runtime, source_text, &registry, &diagnostics, &function, &error), VIGIL_STATUS_OK);

    vigil_value_init_nil(&result);
    vigil_vm_set_aot_enabled(vm, 1);
    /* This non-numeric function falls back to the interpreter. */
    ASSERT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 5);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

#endif /* VIGIL_ENABLE_AOT && !_WIN32 */

void register_aot_tests(void)
{
    REGISTER_TEST(VigilAotTest, VmAotSetterRoundTrips);
    REGISTER_TEST(VigilAotTest, ExecuteFunctionWithAotDisabledStillWorks);
#if defined(VIGIL_ENABLE_AOT) && !defined(_WIN32)
    REGISTER_TEST(VigilAotTest, ExecuteFunctionBuildsAotCache);
    REGISTER_TEST(VigilAotTest, ReturnsConstant);
    REGISTER_TEST(VigilAotTest, ExecutesI32Arithmetic);
    REGISTER_TEST(VigilAotTest, ExecutesI64Arithmetic);
    REGISTER_TEST(VigilAotTest, ExecutesBitwiseOperations);
    REGISTER_TEST(VigilAotTest, ExecutesIfElseBranching);
    REGISTER_TEST(VigilAotTest, ExecutesWhileLoop);
    REGISTER_TEST(VigilAotTest, ExecutesForLoop);
    REGISTER_TEST(VigilAotTest, ExecutesNestedLoops);
    REGISTER_TEST(VigilAotTest, ExecutesRecursiveFibonacci);
    REGISTER_TEST(VigilAotTest, ExecutesI32Comparisons);
    REGISTER_TEST(VigilAotTest, ExecutesNegation);
    REGISTER_TEST(VigilAotTest, ExecutesBitwiseNot);
    REGISTER_TEST(VigilAotTest, ExecutesI32DivisionAndModulo);
    REGISTER_TEST(VigilAotTest, AotAndInterpreterAgree);
    REGISTER_TEST(VigilAotTest, ExecutesI64ForLoop);
    REGISTER_TEST(VigilAotTest, ExecutesDeepRecursion);
    REGISTER_TEST(VigilAotTest, ExecutesMultipleReturnPaths);
    REGISTER_TEST(VigilAotTest, NonNumericFunctionFallsBackToInterpreter);
#endif
}
