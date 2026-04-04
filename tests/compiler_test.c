#include "vigil_test.h"

#include <stdio.h>
#include <string.h>

#include "vigil/vigil.h"

struct TestSource
{
    const char *path;
    const char *text;
};

static vigil_source_id_t RegisterSource(int *vigil_test_failed_, vigil_source_registry_t *registry, const char *path,
                                        const char *text, vigil_error_t *error)
{
    vigil_source_id_t source_id = 0U;

    EXPECT_EQ(vigil_source_registry_register_cstr(registry, path, text, &source_id, error), VIGIL_STATUS_OK);
    return source_id;
}

static int64_t CompileAndRun(int *vigil_test_failed_, const char *source_text)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_source_id_t source_id;
    int64_t output = 0;

    EXPECT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    source_id = RegisterSource(vigil_test_failed_, &registry, "main.vigil", source_text, &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_OK);
    EXPECT_NE(function, NULL);
    EXPECT_EQ(vigil_diagnostic_list_count(&diagnostics), 0U);

    vigil_value_init_nil(&result);
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

static int64_t CompileAndRunMulti(int *vigil_test_failed_, const struct TestSource *sources, size_t source_count,
                                  const char *entry_path)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_source_id_t source_id = 0U;
    int64_t output = 0;
    size_t index;

    EXPECT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    for (index = 0U; index < source_count; index += 1U)
    {
        RegisterSource(vigil_test_failed_, &registry, sources[index].path, sources[index].text, &error);
    }

    for (index = 1U; index <= vigil_source_registry_count(&registry); index += 1U)
    {
        const vigil_source_file_t *source = vigil_source_registry_get(&registry, (vigil_source_id_t)index);

        if (source != NULL && strcmp(vigil_string_c_str(&source->path), entry_path) == 0)
        {
            source_id = source->id;
            break;
        }
    }

    EXPECT_NE(source_id, 0U);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_OK);
    EXPECT_NE(function, NULL);
    EXPECT_EQ(vigil_diagnostic_list_count(&diagnostics), 0U);

    vigil_value_init_nil(&result);
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

static void CompileSingleDiagnosticMessage(const char *source_text, char *message, size_t message_size)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;
    size_t diagnostic_count;
    vigil_status_t status;

    message[0] = '\0';
    status = vigil_runtime_open(&runtime, NULL, &error);
    if (status != VIGIL_STATUS_OK)
    {
        snprintf(message, message_size, "<runtime-open:%d>", status);
        return;
    }
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    status = vigil_source_registry_register_cstr(&registry, "/project/main.vigil", source_text, &source_id, &error);
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_compile_source(&registry, source_id, &function, &diagnostics, &error);
    }

    diagnostic_count = vigil_diagnostic_list_count(&diagnostics);
    if (status == VIGIL_STATUS_SYNTAX_ERROR && diagnostic_count == 1U)
    {
        snprintf(message, message_size, "%s",
                 vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message));
    }
    else
    {
        snprintf(message, message_size, "<status:%d diagnostics:%zu>", status, diagnostic_count);
    }

    if (function != NULL)
    {
        vigil_object_release(&function);
    }
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

static void ExpectSingleCompilerDiagnostic(int *vigil_test_failed_, const char *source_text,
                                           const char *expected_message)
{
    char actual_message[256];

    (void)vigil_test_failed_;
    CompileSingleDiagnosticMessage(source_text, actual_message, sizeof(actual_message));
    EXPECT_STREQ(actual_message, expected_message);
}

static void ExpectSingleCompilerDiagnosticMulti(int *vigil_test_failed_, const struct TestSource *sources,
                                                size_t source_count, const char *entry_path,
                                                const char *expected_message)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id = 0U;
    const char *actual_message = NULL;
    size_t index;
    vigil_status_t status;

    (void)vigil_test_failed_;
    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    for (index = 0U; index < source_count; index += 1U)
    {
        RegisterSource(vigil_test_failed_, &registry, sources[index].path, sources[index].text, &error);
    }
    for (index = 1U; index <= vigil_source_registry_count(&registry); index += 1U)
    {
        const vigil_source_file_t *source = vigil_source_registry_get(&registry, (vigil_source_id_t)index);

        if (source != NULL && strcmp(vigil_string_c_str(&source->path), entry_path) == 0)
        {
            source_id = source->id;
            break;
        }
    }

    ASSERT_NE(source_id, 0U);
    status = vigil_compile_source(&registry, source_id, &function, &diagnostics, &error);
    ASSERT_EQ(status, VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    actual_message = vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message);
    EXPECT_STREQ(actual_message, expected_message);

    if (function != NULL)
        vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, CompilesAndExecutesArithmeticAndLocals)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    i32 x = 1 + 2 * 3;"
                                                "    x = (x + 4) / 2;"
                                                "    return x;"
                                                "}"),
              5);
}

TEST(VigilCompilerTest, CompilesAndExecutesFloatArithmeticAndComparison)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn scale(f64 v) -> f64 {"
                                                "    f64 value = v;"
                                                "    value *= 2.0;"
                                                "    value++;"
                                                "    return value;"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    f64 total = scale(2.5);"
                                                "    if total == 6.0 && total >= 5.5 {"
                                                "        return 6;"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              6);
}

TEST(VigilCompilerTest, CompilesAndExecutesConversionsConstLocalsAndBitwiseNot)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    const f64 scaled = f64(4) / 2.0;"
                                                "    const string label = string(5) + string(true);"
                                                "    i32 truncated = i32(scaled + 3.8);"
                                                "    if label == \"5true\" && ~1 == -2 {"
                                                "        return truncated;"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              5);
}

TEST(VigilCompilerTest, CompilesAndExecutesWiderIntegerTypesAndConversions)
{
    EXPECT_EQ(
        CompileAndRun(vigil_test_failed_,
                      "const u8 LIMIT = u8(12);"
                      "fn double_i64(i64 value) -> i64 {"
                      "    return value * i64(2);"
                      "}"
                      "fn bump_u8(u8 v) -> u8 {"
                      "    u8 value = v;"
                      "    value += u8(5);"
                      "    return value;"
                      "}"
                      "fn main() -> i32 {"
                      "    i64 total = double_i64(i64(20));"
                      "    u8 small = bump_u8(LIMIT);"
                      "    u32 count = u32(100000) + u32(25);"
                      "    u64 large = u64(2000000000) + u64(5);"
                      "    if total == i64(40) && small == u8(17) && count == u32(100025) && large == u64(2000000005) {"
                      "        return 9;"
                      "    }"
                      "    return 0;"
                      "}"),
        9);
}

TEST(VigilCompilerTest, CompilesAndExecutesI32ToI64ArithPromotion)
{
    /* Exercises the i32-arith → i64 peephole in vigil_parser_emit_integer_cast.
       Uses nested i32 expressions so the last opcode before the i64() cast
       is an i32 arith op (not already widened to i64). */
    EXPECT_EQ(CompileAndRun(vigil_test_failed_,
                            "fn main() -> i32 {"
                            "    i32 a = 10; i32 b = 3;"
                            /* Force i32 context by using i32 intermediate */
                            "    i32 s32 = a + b;"
                            "    i32 d32 = a - b;"
                            "    i32 m32 = a * b;"
                            "    i32 q32 = a / b;"
                            "    i32 r32 = a % b;"
                            "    i64 s = i64(s32 + 0);"
                            "    i64 d = i64(d32 - 0);"
                            "    i64 m = i64(m32 * 1);"
                            "    i64 q = i64(q32 / 1);"
                            "    i64 r = i64(r32 % 97);"
                            "    if s == i64(13) && d == i64(7) && m == i64(30) && q == i64(3) && r == i64(1) {"
                            "        return 7;"
                            "    }"
                            "    return 0;"
                            "}"),
              7);
}

TEST(VigilCompilerTest, CompilesAndExecutesFunctionValuesAndIndirectCalls)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "class Holder {"
                                                "    fn(i32, i32) -> i32 op;"
                                                "}"
                                                "fn add(i32 a, i32 b) -> i32 {"
                                                "    return a + b;"
                                                "}"
                                                "fn mul(i32 a, i32 b) -> i32 {"
                                                "    return a * b;"
                                                "}"
                                                "fn apply(fn(i32, i32) -> i32 op, i32 left, i32 right) -> i32 {"
                                                "    return op(left, right);"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    fn any_cb = add;"
                                                "    fn(i32, i32) -> i32 op = add;"
                                                "    Holder holder = Holder(mul);"
                                                "    if any_cb == op {"
                                                "        return apply(op, 2, 3) + holder.op(2, 4);"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              13);
}

TEST(VigilCompilerTest, CompilesAndExecutesAnonymousFunctionsClosuresAndLocalFunctions)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    i32 factor = 10;"
                                                "    fn(i32) -> i32 scale = fn(i32 x) -> i32 {"
                                                "        return x * factor;"
                                                "    };"
                                                "    factor = 20;"
                                                "    fn helper(i32 x) -> i32 {"
                                                "        return scale(x) + factor;"
                                                "    }"
                                                "    return helper(2) + fn() -> i32 {"
                                                "        return 1;"
                                                "    }();"
                                                "}"),
              41);
}

TEST(VigilCompilerTest, CompilesAndExecutesLambdaShorthandWithContextInference)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn apply(fn(i32) -> i32 f, i32 x) -> i32 {"
                                                "    return f(x);"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    fn(i32, i32) -> i32 op = |a, b| {"
                                                "        i32 sum = a + b;"
                                                "        return sum * 4;"
                                                "    };"
                                                "    return apply(|x| x * 3, 7) + op(2, 3);"
                                                "}"),
              41);
}

TEST(VigilCompilerTest, RejectsLambdaWithoutFunctionTypedContext)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn main() -> i32 {"
                                   "    i32 value = |x| x;"
                                   "    return value;"
                                   "}",
                                   "cannot infer lambda parameter types without context");
}

TEST(VigilCompilerTest, RejectsLambdaParameterCountMismatch)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn main() -> i32 {"
                                   "    fn(i32) -> i32 bad = |x, y| x + y;"
                                   "    return bad(1);"
                                   "}",
                                   "lambda parameter count does not match expected function type");
}

TEST(VigilCompilerTest, CompilesAndExecutesExplicitErrorValues)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn fail(bool bad) -> err {"
                                                "    if bad {"
                                                "        return err(\"boom\", err.arg);"
                                                "    }"
                                                "    return ok;"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    err first = fail(false);"
                                                "    err second = fail(true);"
                                                "    if first != ok {"
                                                "        return 0;"
                                                "    }"
                                                "    if second == ok {"
                                                "        return 0;"
                                                "    }"
                                                "    if second.kind() == err.arg && second.message() == \"boom\" {"
                                                "        switch second.kind() {"
                                                "            case err.arg:"
                                                "                return 7;"
                                                "            default:"
                                                "                return 0;"
                                                "        }"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              7);
}

TEST(VigilCompilerTest, CompilesAndExecutesTupleBindingsAndGuard)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_,
                            "fn divide(i32 a, i32 b) -> (i32, err) {"
                            "    if b == 0 {"
                            "        return 0, err(\"division by zero\", err.arg);"
                            "    }"
                            "    return a / b, ok;"
                            "}"
                            "fn main() -> i32 {"
                            "    i32 first, err first_err = divide(9, 3);"
                            "    i32 _, err _ = divide(8, 2);"
                            "    guard i32 second, err second_err = divide(5, 0) {"
                            "        if first_err == ok && first == 3 && second == 0 && second_err.kind() == err.arg {"
                            "            return 11;"
                            "        }"
                            "        return 0;"
                            "    }"
                            "    return 0;"
                            "}"),
              11);
}

TEST(VigilCompilerTest, CompilesAndExecutesIfElseAndWhile)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    i32 sum = 0;"
                                                "    i32 i = 0;"
                                                "    while i < 5 {"
                                                "        sum = sum + i;"
                                                "        i = i + 1;"
                                                "    }"
                                                "    if sum > 9 {"
                                                "        return sum;"
                                                "    } else {"
                                                "        return 0;"
                                                "    }"
                                                "}"),
              10);
}

TEST(VigilCompilerTest, CompilesAndExecutesTailCallInIfBranch)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn one() -> i32 {"
                                                "    return 1;"
                                                "}"
                                                "fn two() -> i32 {"
                                                "    return 2;"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    bool flag = false;"
                                                "    if flag {"
                                                "        return one();"
                                                "    }"
                                                "    return two();"
                                                "}"),
              2);
}

TEST(VigilCompilerTest, CompilesAndExecutesBoolLocalsAndEquality)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    bool ready = 1 + 1 == 2;"
                                                "    if ready != false {"
                                                "        return 7;"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              7);
}

TEST(VigilCompilerTest, CompilesAndExecutesDirectFunctionCalls)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn add(i32 left, i32 right) -> i32 {"
                                                "    return left + right;"
                                                "}"
                                                "fn is_ten(i32 value) -> bool {"
                                                "    return value == 10;"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    i32 result = add(4, 6);"
                                                "    if is_ten(result) {"
                                                "        return result;"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              10);
}

TEST(VigilCompilerTest, CompilesAndExecutesRecursiveFunctionCalls)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn sum_to(i32 value) -> i32 {"
                                                "    if value == 0 {"
                                                "        return 0;"
                                                "    }"
                                                "    return value + sum_to(value - 1);"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    return sum_to(5);"
                                                "}"),
              15);
}

TEST(VigilCompilerTest, CompilesAndExecutesShortCircuitLogicalOperators)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn panic_bool() -> bool {"
                                                "    i32 x = 1 / 0;"
                                                "    return x == 0;"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    if true || panic_bool() {"
                                                "        if !(false && panic_bool()) {"
                                                "            return 7;"
                                                "        }"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              7);
}

TEST(VigilCompilerTest, CompilesAndExecutesBitwiseShiftAndTernaryExpressions)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "const i32 MASK = (1 << 3) | 1;"
                                                "const i32 PICK = true ? MASK : 0;"
                                                "fn main() -> i32 {"
                                                "    i32 bits = PICK ^ 2;"
                                                "    return bits > 7 ? bits & 14 : 0;"
                                                "}"),
              10);
}

TEST(VigilCompilerTest, CompilesAndExecutesBreakAndContinue)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    i32 sum = 0;"
                                                "    i32 i = 0;"
                                                "    while i < 8 {"
                                                "        i = i + 1;"
                                                "        {"
                                                "            i32 current = i;"
                                                "            if current == 3 {"
                                                "                continue;"
                                                "            }"
                                                "            if current == 6 {"
                                                "                break;"
                                                "            }"
                                                "        }"
                                                "        sum = sum + i;"
                                                "    }"
                                                "    return sum;"
                                                "}"),
              12);
}

TEST(VigilCompilerTest, CompilesAndExecutesForLoopsAndIncrementClauses)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    i32 sum = 0;"
                                                "    for (i32 i = 0; i < 5; i++) {"
                                                "        sum += i;"
                                                "    }"
                                                "    return sum;"
                                                "}"),
              10);
}

TEST(VigilCompilerTest, CompilesAndExecutesForLoopContinueAndBreak)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    i32 sum = 0;"
                                                "    for (i32 i = 0; i < 6; i++) {"
                                                "        if i == 2 {"
                                                "            continue;"
                                                "        }"
                                                "        if i == 5 {"
                                                "            break;"
                                                "        }"
                                                "        sum += i;"
                                                "    }"
                                                "    return sum;"
                                                "}"),
              8);
}

TEST(VigilCompilerTest, CompilesAndExecutesImportedFunctionsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/math.vigil", "pub fn add(i32 left, i32 right) -> i32 {"
                                                                 "    return left + right;"
                                                                 "}"},
                                         {"/project/main.vigil", "import \"math\";"
                                                                 "fn main() -> i32 {"
                                                                 "    return math.add(2, 5);"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 7);
}

TEST(VigilCompilerTest, CompilesAndExecutesNestedImportsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/lib/math.vigil", "pub fn inc(i32 value) -> i32 {"
                                                                     "    return value + 1;"
                                                                     "}"},
                                         {"/project/lib/logic.vigil", "import \"math\";"
                                                                      "pub fn bump_twice(i32 value) -> i32 {"
                                                                      "    return math.inc(math.inc(value));"
                                                                      "}"},
                                         {"/project/main.vigil", "import \"lib/logic\";"
                                                                 "fn main() -> i32 {"
                                                                 "    return logic.bump_twice(5);"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 3U, "/project/main.vigil"), 7);
}

TEST(VigilCompilerTest, RejectsUnregisteredImportedSource)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "/project/main.vigil",
                               "import \"missing\";"
                               "fn main() -> i32 {"
                               "    return 0;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "imported source is not registered");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, CompilesAndExecutesModuleConstantsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/config.vigil", "pub const i32 LIMIT = 7;"},
                                         {"/project/main.vigil", "import \"config\";"
                                                                 "fn main() -> i32 {"
                                                                 "    return config.LIMIT;"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 7);
}

TEST(VigilCompilerTest, CompilesAndExecutesConstantExpressions)
{
    const struct TestSource sources[] = {{"/project/config.vigil", "pub const i32 BASE = 2 + 3 * 4;"
                                                                   "pub const bool READY = BASE == 14;"},
                                         {"/project/main.vigil", "import \"config\";"
                                                                 "fn main() -> i32 {"
                                                                 "    if config.READY {"
                                                                 "        return config.BASE;"
                                                                 "    }"
                                                                 "    return 0;"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 14);
}

TEST(VigilCompilerTest, CompilesAndExecutesClassesFieldAccessAndFieldAssignment)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "class Pair {"
                                                "    i32 left;"
                                                "    i32 right;"
                                                "}"
                                                "fn sum(Pair pair) -> i32 {"
                                                "    return pair.left + pair.right;"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    Pair pair = Pair(3, 4);"
                                                "    pair.left = pair.left + 1;"
                                                "    return sum(pair);"
                                                "}"),
              8);
}

TEST(VigilCompilerTest, CompilesAndExecutesClassesAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/model.vigil", "pub class Counter {"
                                                                  "    pub i32 value;"
                                                                  "}"
                                                                  "pub fn make_counter(i32 value) -> Counter {"
                                                                  "    return Counter(value);"
                                                                  "}"
                                                                  "pub fn read_counter(Counter counter) -> i32 {"
                                                                  "    return counter.value;"
                                                                  "}"},
                                         {"/project/main.vigil", "import \"model\";"
                                                                 "fn main() -> i32 {"
                                                                 "    model.Counter counter = model.make_counter(7);"
                                                                 "    return model.read_counter(counter);"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 7);
}

TEST(VigilCompilerTest, CompilesAndExecutesClassMethodsAndSelf)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "class Counter {"
                                                "    i32 value;"
                                                "    fn bump(i32 delta) -> i32 {"
                                                "        self.value = self.value + delta;"
                                                "        return self.value;"
                                                "    }"
                                                "    fn read() -> i32 {"
                                                "        return self.value;"
                                                "    }"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    Counter counter = Counter(5);"
                                                "    counter.bump(2);"
                                                "    return counter.read();"
                                                "}"),
              7);
}

TEST(VigilCompilerTest, CompilesAndExecutesFloatFieldsAndGlobals)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "f64 scale = 1.5;"
                                                "class Counter {"
                                                "    f64 value;"
                                                "    fn init(f64 value) -> void {"
                                                "        self.value = value * scale;"
                                                "    }"
                                                "    fn grow(f64 delta) -> void {"
                                                "        self.value += delta;"
                                                "    }"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    Counter counter = Counter(4.0);"
                                                "    counter.grow(1.0);"
                                                "    if counter.value > 6.5 {"
                                                "        return 7;"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              7);
}

TEST(VigilCompilerTest, CompilesAndExecutesInitBasedConstructors)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "class Counter {"
                                                "    i32 value;"
                                                "    i32 doubled;"
                                                "    fn init(i32 value) -> void {"
                                                "        self.value = value;"
                                                "        self.doubled = value * 2;"
                                                "    }"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    Counter counter = Counter(5);"
                                                "    return counter.value + counter.doubled;"
                                                "}"),
              15);
}

TEST(VigilCompilerTest, CompilesAndExecutesFallibleInitConstructorsWithGuard)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_,
                            "class Connection {"
                            "    pub i32 port;"
                            "    fn init(i32 port) -> err {"
                            "        if port < 0 {"
                            "            return err(\"bad port\", err.arg);"
                            "        }"
                            "        self.port = port;"
                            "        return ok;"
                            "    }"
                            "}"
                            "fn main() -> i32 {"
                            "    Connection good, err good_err = Connection(9);"
                            "    guard Connection bad, err bad_err = Connection(-1) {"
                            "        if good_err == ok && good.port == 9 && bad_err.kind() == err.arg {"
                            "            return 13;"
                            "        }"
                            "        return 0;"
                            "    }"
                            "    return 0;"
                            "}"),
              13);
}

TEST(VigilCompilerTest, CompilesAndExecutesFallibleInitConstructorsReturningOk)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "class SafeDiv {"
                                                "    i32 result;"
                                                "    fn init(i32 left, i32 right) -> err {"
                                                "        if right == 0 {"
                                                "            return err(\"division by zero\", err.arg);"
                                                "        }"
                                                "        self.result = left / right;"
                                                "        return ok;"
                                                "    }"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    SafeDiv value, err status = SafeDiv(10, 2);"
                                                "    if value.result == 5 && status == ok {"
                                                "        return 42;"
                                                "    }"
                                                "    return 0;"
                                                "}"),
              42);
}

TEST(VigilCompilerTest, CompilesAndExecutesMethodsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/model.vigil", "pub class Counter {"
                                                                  "    i32 value;"
                                                                  "    pub fn bump(i32 delta) -> i32 {"
                                                                  "        self.value = self.value + delta;"
                                                                  "        return self.value;"
                                                                  "    }"
                                                                  "}"
                                                                  "pub fn build(i32 value) -> Counter {"
                                                                  "    return Counter(value);"
                                                                  "}"},
                                         {"/project/main.vigil", "import \"model\";"
                                                                 "fn main() -> i32 {"
                                                                 "    model.Counter counter = model.build(8);"
                                                                 "    return counter.bump(3);"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 11);
}

TEST(VigilCompilerTest, CompilesAndExecutesPublicInitConstructorsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/models.vigil", "pub class Counter {"
                                                                   "    pub i32 value;"
                                                                   "    fn init(i32 value) -> void {"
                                                                   "        self.value = value + 1;"
                                                                   "    }"
                                                                   "}"},
                                         {"/project/main.vigil", "import \"models\";"
                                                                 "fn main() -> i32 {"
                                                                 "    models.Counter counter = models.Counter(6);"
                                                                 "    return counter.value;"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 7);
}

TEST(VigilCompilerTest, CompilesAndExecutesInterfacePolymorphismAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/model.vigil", "pub interface Reader {"
                                                                  "    fn read() -> i32;"
                                                                  "}"
                                                                  "class Counter implements Reader {"
                                                                  "    i32 value;"
                                                                  "    fn bump(i32 delta) -> i32 {"
                                                                  "        self.value = self.value + delta;"
                                                                  "        return self.value;"
                                                                  "    }"
                                                                  "    fn read() -> i32 {"
                                                                  "        return self.value;"
                                                                  "    }"
                                                                  "}"
                                                                  "pub fn make_reader(i32 value) -> Reader {"
                                                                  "    Counter counter = Counter(value);"
                                                                  "    counter.bump(1);"
                                                                  "    return counter;"
                                                                  "}"},
                                         {"/project/main.vigil", "import \"model\";"
                                                                 "fn use_reader(model.Reader reader) -> i32 {"
                                                                 "    return reader.read();"
                                                                 "}"
                                                                 "fn main() -> i32 {"
                                                                 "    model.Reader reader = model.make_reader(6);"
                                                                 "    return use_reader(reader);"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 7);
}

TEST(VigilCompilerTest, CompilesAndExecutesQualifiedModuleSymbolsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/model.vigil", "pub const i32 OFFSET = 2;"
                                                                  "pub class Counter {"
                                                                  "    pub i32 value;"
                                                                  "    pub fn bump(i32 delta) -> i32 {"
                                                                  "        self.value = self.value + delta;"
                                                                  "        return self.value;"
                                                                  "    }"
                                                                  "}"
                                                                  "pub fn make_counter(i32 value) -> Counter {"
                                                                  "    return Counter(value + OFFSET);"
                                                                  "}"},
                                         {"/project/main.vigil",
                                          "import \"model\" as models;"
                                          "fn read(models.Counter counter) -> i32 {"
                                          "    return counter.value;"
                                          "}"
                                          "fn main() -> i32 {"
                                          "    models.Counter counter = models.make_counter(models.OFFSET + 5);"
                                          "    return read(counter) + counter.bump(1);"
                                          "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 19);
}

TEST(VigilCompilerTest, RejectsUnqualifiedImportedSymbolsAcrossFiles)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;
    const struct TestSource sources[] = {{"/project/math.vigil", "pub fn add(i32 left, i32 right) -> i32 {"
                                                                 "    return left + right;"
                                                                 "}"},
                                         {"/project/main.vigil", "import \"math\";"
                                                                 "fn main() -> i32 {"
                                                                 "    return add(2, 5);"
                                                                 "}"}};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    for (size_t index = 0U; index < sizeof(sources) / sizeof(sources[0]); index += 1U)
    {
        source_id = RegisterSource(vigil_test_failed_, &registry, sources[index].path, sources[index].text, &error);
    }

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "unknown function");

    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, CompilesAndExecutesDuplicateTopLevelNamesAcrossModules)
{
    const struct TestSource sources[] = {{"/project/alpha.vigil", "pub const i32 VALUE = 4;"
                                                                  "pub fn read() -> i32 {"
                                                                  "    return VALUE;"
                                                                  "}"},
                                         {"/project/beta.vigil", "pub const i32 VALUE = 9;"
                                                                 "pub fn read() -> i32 {"
                                                                 "    return VALUE;"
                                                                 "}"},
                                         {"/project/main.vigil", "import \"alpha\";"
                                                                 "import \"beta\";"
                                                                 "fn main() -> i32 {"
                                                                 "    return alpha.read() + beta.read();"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 3U, "/project/main.vigil"), 13);
}

TEST(VigilCompilerTest, CompilesAndExecutesQualifiedImportedInterfacesAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/contracts.vigil", "pub interface Reader {"
                                                                      "    fn read() -> i32;"
                                                                      "}"},
                                         {"/project/model.vigil", "import \"contracts\";"
                                                                  "pub class Counter implements contracts.Reader {"
                                                                  "    i32 value;"
                                                                  "    fn read() -> i32 {"
                                                                  "        return self.value;"
                                                                  "    }"
                                                                  "}"
                                                                  "pub fn make_reader(i32 value) -> contracts.Reader {"
                                                                  "    return Counter(value);"
                                                                  "}"},
                                         {"/project/main.vigil", "import \"contracts\";"
                                                                 "import \"model\";"
                                                                 "fn use_reader(contracts.Reader reader) -> i32 {"
                                                                 "    return reader.read();"
                                                                 "}"
                                                                 "fn main() -> i32 {"
                                                                 "    return use_reader(model.make_reader(9));"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 3U, "/project/main.vigil"), 9);
}

TEST(VigilCompilerTest, CompilesAndExecutesQualifiedConstantsInConstantExpressions)
{
    const struct TestSource sources[] = {{"/project/config.vigil", "pub const i32 BASE = 7;"},
                                         {"/project/main.vigil", "import \"config\" as cfg;"
                                                                 "const i32 LIMIT = cfg.BASE + 1;"
                                                                 "fn main() -> i32 {"
                                                                 "    return LIMIT;"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 8);
}

TEST(VigilCompilerTest, CompilesAndExecutesEnumsAndSwitch)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "enum Color {"
                                                "    Red,"
                                                "    Green = 3,"
                                                "    Blue"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    Color color = Color.Blue;"
                                                "    switch color {"
                                                "        case Color.Red:"
                                                "            return 1;"
                                                "        case Color.Green, Color.Blue:"
                                                "            return 7;"
                                                "        default:"
                                                "            return 0;"
                                                "    }"
                                                "}"),
              7);
}

TEST(VigilCompilerTest, CompilesAndExecutesStringsAndStringConstants)
{
    const char *source = "\n"
                         "const string PREFIX = \"he\" + 'l';\n"
                         "\n"
                         "fn make_message() -> string {\n"
                         "    return PREFIX + `lo`;\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    string message = make_message();\n"
                         "    if message == \"hello\" && message != \"world\" {\n"
                         "        return 12;\n"
                         "    }\n"
                         "    return 0;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 12);
}

TEST(VigilCompilerTest, CompilesAndExecutesImportedStringConstantsAcrossFiles)
{
    const struct TestSource sources[] = {{"labels.vigil", "\n"
                                                          "pub const string GREETING = \"vi\" + \"gil\";\n"
                                                          "\n"
                                                          "pub fn render(string suffix) -> string {\n"
                                                          "    return GREETING + suffix;\n"
                                                          "}\n"
                                                          ""},
                                         {"main.vigil", "\n"
                                                        "import \"labels\";\n"
                                                        "\n"
                                                        "fn main() -> i32 {\n"
                                                        "    string value = labels.render(\" vm\");\n"
                                                        "    if value == \"vigil vm\" {\n"
                                                        "        return 15;\n"
                                                        "    }\n"
                                                        "    return 0;\n"
                                                        "}\n"
                                                        ""}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, sizeof(sources) / sizeof(sources[0]), "main.vigil"), 15);
}

TEST(VigilCompilerTest, CompilesAndExecutesStringBuiltInMethods)
{
    const char *source = "\n"
                         "fn main() -> i32 {\n"
                         "    string value = \"  vigil vm  \";\n"
                         "    string trimmed = value.trim();\n"
                         "    array<string> parts = \"a,b,c\".split(\",\");\n"
                         "    array<u8> bytes = \"AZ\".bytes();\n"
                         "    i32 split_hits = 0;\n"
                         "    i32 byte_sum = 0;\n"
                         "    i32 index, bool found = value.index_of(\"igil\");\n"
                         "    string sub, err sub_err = trimmed.substr(0, 5);\n"
                         "    string ch, err ch_err = value.char_at(2);\n"
                         "    string missing, err missing_err = trimmed.char_at(99);\n"
                         "\n"
                         "    for part in parts {\n"
                         "        if part == \"b\" || part == \"c\" {\n"
                         "            split_hits++;\n"
                         "        }\n"
                         "    }\n"
                         "\n"
                         "    for byte in bytes {\n"
                         "        byte_sum += i32(byte);\n"
                         "    }\n"
                         "\n"
                         "    if value.len() == 12 &&\n"
                         "        value.contains(\"gil\") &&\n"
                         "        value.starts_with(\"  v\") &&\n"
                         "        value.ends_with(\"  \") &&\n"
                         "        trimmed == \"vigil vm\" &&\n"
                         "        trimmed.to_upper() == \"VIGIL VM\" &&\n"
                         "        trimmed.to_lower() == \"vigil vm\" &&\n"
                         "        value.replace(\"vm\", \"bytecode\") == \"  vigil bytecode  \" &&\n"
                         "        index == 3 &&\n"
                         "        found == true &&\n"
                         "        sub == \"vigil\" &&\n"
                         "        sub_err == ok &&\n"
                         "        ch == \"v\" &&\n"
                         "        ch_err == ok &&\n"
                         "        missing == \"\" &&\n"
                         "        missing_err != ok &&\n"
                         "        split_hits == 2 &&\n"
                         "        byte_sum == 155 {\n"
                         "        return 19;\n"
                         "    }\n"
                         "\n"
                         "    return 0;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 19);
}

TEST(VigilCompilerTest, CompilesAndExecutesStringOrderingComparisons)
{
    const char *source = "\n"
                         "fn main() -> i32 {\n"
                         "    string low = \"a\";\n"
                         "    string high = \"b\";\n"
                         "    if low < high &&\n"
                         "        high > low &&\n"
                         "        low <= low &&\n"
                         "        high >= high {\n"
                         "        return 27;\n"
                         "    }\n"
                         "    return 0;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 27);
}

TEST(VigilCompilerTest, CompilesAndExecutesArrayBuiltInMethods)
{
    const char *source = "\n"
                         "fn main() -> i32 {\n"
                         "    array<i32> nums = [1, 2];\n"
                         "    nums.push(3);\n"
                         "    i32 hit, err hit_err = nums.get(1);\n"
                         "    err set_err = nums.set(0, 5);\n"
                         "    array<i32> prefix = nums.slice(0, 2);\n"
                         "    err mutate_err = nums.set(1, 9);\n"
                         "    i32 popped, err pop_err = nums.pop();\n"
                         "    i32 missing, err missing_err = nums.get(99);\n"
                         "\n"
                         "    if nums.len() == 2 &&\n"
                         "        hit == 2 &&\n"
                         "        hit_err == ok &&\n"
                         "        set_err == ok &&\n"
                         "        mutate_err == ok &&\n"
                         "        prefix.len() == 2 &&\n"
                         "        prefix.contains(5) &&\n"
                         "        prefix.contains(2) &&\n"
                         "        !prefix.contains(9) &&\n"
                         "        popped == 3 &&\n"
                         "        pop_err == ok &&\n"
                         "        missing == 0 &&\n"
                         "        missing_err != ok {\n"
                         "        return 23;\n"
                         "    }\n"
                         "\n"
                         "    return 0;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 23);
}

TEST(VigilCompilerTest, CompilesAndExecutesMapBuiltInMethods)
{
    const char *source = "\n"
                         "fn main() -> i32 {\n"
                         "    map<i32, string> labels = {1: \"a\", 2: \"b\"};\n"
                         "    string first, bool found_first = labels.get(1);\n"
                         "    err set_err = labels.set(3, \"c\");\n"
                         "    string removed, bool removed_ok = labels.remove(2);\n"
                         "    string missing, bool missing_ok = labels.get(99);\n"
                         "    i32 key_sum = 0;\n"
                         "    i32 value_hits = 0;\n"
                         "\n"
                         "    for key in labels.keys() {\n"
                         "        key_sum += key;\n"
                         "    }\n"
                         "\n"
                         "    for value in labels.values() {\n"
                         "        if value == \"a\" || value == \"c\" {\n"
                         "            value_hits++;\n"
                         "        }\n"
                         "    }\n"
                         "\n"
                         "    if labels.len() == 2 &&\n"
                         "        first == \"a\" &&\n"
                         "        found_first == true &&\n"
                         "        set_err == ok &&\n"
                         "        removed == \"b\" &&\n"
                         "        removed_ok == true &&\n"
                         "        missing == \"\" &&\n"
                         "        missing_ok == false &&\n"
                         "        labels.has(1) &&\n"
                         "        labels.has(3) &&\n"
                         "        !labels.has(2) &&\n"
                         "        key_sum == 4 &&\n"
                         "        value_hits == 2 {\n"
                         "        return 29;\n"
                         "    }\n"
                         "\n"
                         "    return 0;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 29);
}

TEST(VigilCompilerTest, CompilesAndExecutesTypedEmptyCollectionLiterals)
{
    const char *source = "\n"
                         "fn empty_nums() -> array<i32> {\n"
                         "    return [];\n"
                         "}\n"
                         "\n"
                         "fn empty_labels() -> map<i32, string> {\n"
                         "    return {};\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    array<i32> nums = [];\n"
                         "    map<i32, string> labels = {};\n"
                         "\n"
                         "    nums = empty_nums();\n"
                         "    labels = empty_labels();\n"
                         "    nums = [];\n"
                         "    labels = {};\n"
                         "\n"
                         "    if nums.len() == 0 && labels.len() == 0 {\n"
                         "        return 17;\n"
                         "    }\n"
                         "\n"
                         "    return 0;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 17);
}

TEST(VigilCompilerTest, CompilesAndExecutesVoidFunctionsAndMethods)
{
    const char *source = "\n"
                         "class Counter {\n"
                         "    i32 value;\n"
                         "\n"
                         "    fn bump() -> void {\n"
                         "        self.value++;\n"
                         "        return;\n"
                         "    }\n"
                         "}\n"
                         "\n"
                         "fn touch(Counter counter) -> void {\n"
                         "    counter.bump();\n"
                         "}\n"
                         "\n"
                         "fn reset(bool flag) -> void {\n"
                         "    if flag {\n"
                         "        return;\n"
                         "    }\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    Counter counter = Counter(4);\n"
                         "    touch(counter);\n"
                         "    reset(false);\n"
                         "    counter.bump();\n"
                         "    return counter.value;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 6);
}

TEST(VigilCompilerTest, CompilesAndExecutesDeferredCallsInLifoOrderWithEagerArguments)
{
    const char *source = "\n"
                         "i32 state = 0;\n"
                         "\n"
                         "fn push(i32 value) -> void {\n"
                         "    state = state * 10 + value;\n"
                         "}\n"
                         "\n"
                         "fn next() -> i32 {\n"
                         "    state = state + 1;\n"
                         "    return state;\n"
                         "}\n"
                         "\n"
                         "fn run() -> void {\n"
                         "    defer push(next());\n"
                         "    defer push(next());\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    run();\n"
                         "    return state;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 221);
}

TEST(VigilCompilerTest, CompilesAndExecutesDeferredMethodsAndReturnsAfterDrain)
{
    const char *source = "\n"
                         "class Counter {\n"
                         "    i32 value;\n"
                         "\n"
                         "    fn bump(i32 delta) -> void {\n"
                         "        self.value += delta;\n"
                         "    }\n"
                         "}\n"
                         "\n"
                         "fn run(Counter counter) -> i32 {\n"
                         "    defer counter.bump(2);\n"
                         "    defer counter.bump(3);\n"
                         "    return 7;\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    Counter counter = Counter(4);\n"
                         "    i32 result = run(counter);\n"
                         "    return counter.value * 10 + result;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 97);
}

TEST(VigilCompilerTest, CompilesAndExecutesDeferredInterfaceCalls)
{
    const char *source = "\n"
                         "interface Reader {\n"
                         "    fn read() -> i32;\n"
                         "}\n"
                         "\n"
                         "i32 state = 0;\n"
                         "\n"
                         "class Counter implements Reader {\n"
                         "    i32 value;\n"
                         "\n"
                         "    fn read() -> i32 {\n"
                         "        state = state * 10 + self.value;\n"
                         "        return self.value;\n"
                         "    }\n"
                         "}\n"
                         "\n"
                         "fn run(Reader reader) -> void {\n"
                         "    defer reader.read();\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    run(Counter(8));\n"
                         "    return state;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 8);
}

TEST(VigilCompilerTest, CompilesAndExecutesDeferredInitConstructors)
{
    const char *source = "\n"
                         "i32 state = 0;\n"
                         "\n"
                         "class Counter {\n"
                         "    i32 value;\n"
                         "\n"
                         "    fn init(i32 value) -> void {\n"
                         "        self.value = value;\n"
                         "        state = state * 10 + value;\n"
                         "    }\n"
                         "}\n"
                         "\n"
                         "fn run() -> void {\n"
                         "    defer Counter(2);\n"
                         "    defer Counter(3);\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    run();\n"
                         "    return state;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 32);
}

TEST(VigilCompilerTest, CompilesAndExecutesMultipleReturnValues)
{
    const char *source = "\n"
                         "fn pair(i32 value) -> (i32, i32) {\n"
                         "    return value, value + 1;\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    i32 left, i32 right = pair(4);\n"
                         "    return left * 10 + right;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 45);
}

TEST(VigilCompilerTest, CompilesAndExecutesDeferredMultipleReturnValues)
{
    const char *source = "\n"
                         "i32 state = 0;\n"
                         "\n"
                         "fn bump() -> void {\n"
                         "    state += 1;\n"
                         "}\n"
                         "\n"
                         "fn pair(i32 value) -> (i32, i32) {\n"
                         "    defer bump();\n"
                         "    return value, value + 1;\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    i32 left, i32 right = pair(4);\n"
                         "    return state * 100 + left * 10 + right;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 145);
}

TEST(VigilCompilerTest, CompilesAndExecutesDeferredFunctionValues)
{
    const char *source = "\n"
                         "i32 state = 0;\n"
                         "\n"
                         "fn bump() -> void {\n"
                         "    state = state * 10 + 1;\n"
                         "}\n"
                         "\n"
                         "fn run() -> void {\n"
                         "    fn() -> void action = bump;\n"
                         "    defer action();\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    run();\n"
                         "    return state;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 1);
}

TEST(VigilCompilerTest, CompilesAndExecutesDeferredConstructorsWithoutInit)
{
    const char *source = "\n"
                         "i32 state = 0;\n"
                         "\n"
                         "class Point {\n"
                         "    i32 x;\n"
                         "    i32 y;\n"
                         "}\n"
                         "\n"
                         "fn mark() -> void {\n"
                         "    state = state * 10 + 2;\n"
                         "}\n"
                         "\n"
                         "fn run() -> void {\n"
                         "    defer Point(3, 4);\n"
                         "    defer mark();\n"
                         "}\n"
                         "\n"
                         "fn main() -> i32 {\n"
                         "    run();\n"
                         "    return state;\n"
                         "}\n"
                         "";

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, source), 2);
}

TEST(VigilCompilerTest, CompilesAndExecutesPublicGlobalsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/lib.vigil", "pub i32 counter = 3;"
                                                                "pub fn bump() -> i32 {"
                                                                "    counter = counter + 1;"
                                                                "    return counter;"
                                                                "}"},
                                         {"/project/main.vigil", "import \"lib\";"
                                                                 "fn main() -> i32 {"
                                                                 "    return lib.bump() + lib.counter;"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 8);
}

TEST(VigilCompilerTest, CompilesAndExecutesQualifiedGlobalAssignmentAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/lib.vigil", "pub i32 counter = 3;"},
                                         {"/project/main.vigil", "import \"lib\";"
                                                                 "fn main() -> i32 {"
                                                                 "    lib.counter += 2;"
                                                                 "    lib.counter++;"
                                                                 "    return lib.counter;"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 6);
}

TEST(VigilCompilerTest, CompilesAndExecutesEmptyCollectionGlobalsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/lib.vigil", "pub array<i32> nums = [];"
                                                                "pub map<i32, string> labels = {};"},
                                         {"/project/main.vigil", "import \"lib\";"
                                                                 "fn main() -> i32 {"
                                                                 "    lib.nums = [];"
                                                                 "    lib.labels = {};"
                                                                 "    return lib.nums.len() + lib.labels.len() + 5;"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 5);
}

TEST(VigilCompilerTest, CompilesAndExecutesCompoundAssignmentsForGlobalsAndFields)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "class Counter {"
                                                "    i32 value;"
                                                "}"
                                                "i32 total = 2;"
                                                "fn main() -> i32 {"
                                                "    Counter counter = Counter(3);"
                                                "    total += 4;"
                                                "    counter.value *= 2;"
                                                "    counter.value--;"
                                                "    return total + counter.value;"
                                                "}"),
              11);
}

TEST(VigilCompilerTest, CompilesAndExecutesFStringsWithInterpolationAndFormatting)
{
    EXPECT_EQ(
        CompileAndRun(vigil_test_failed_,
                      "fn main() -> i32 {"
                      "    string name = \"Alice\";"
                      "    i32 age = 30;"
                      "    f64 pi = 3.14159;"
                      "    string msg = f\"Name: {name}, Age: {age}, Next: {age + 1}, pi={pi:.2f}, braces={{ok}}\";"
                      "    if msg == \"Name: Alice, Age: 30, Next: 31, pi=3.14, braces={ok}\" {"
                      "        return 1;"
                      "    }"
                      "    return 0;"
                      "}"),
        1);
}

TEST(VigilCompilerTest, CompilesAndExecutesFStringsWithAlignmentAndRadixFormatting)
{
    EXPECT_EQ(
        CompileAndRun(vigil_test_failed_,
                      "fn main() -> i32 {"
                      "    string name = \"Alice\";"
                      "    i32 age = 30;"
                      "    i32 negative = -30;"
                      "    string empty = \"\";"
                      "    f64 pi = 3.14159;"
                      "    bool ready = true;"
                      "    string left = f\"[{name:<10}]\";"
                      "    string right = f\"[{name:>10}]\";"
                      "    string center = f\"[{name:^10}]\";"
                      "    string zero_pad = f\"[{age:0>8d}]\";"
                      "    string decimal = f\"[{age:d}]\";"
                      "    string hex_lower = f\"[{age:x}]\";"
                      "    string hex_upper = f\"[{age:X}]\";"
                      "    string binary = f\"[{age:b}]\";"
                      "    string octal = f\"[{age:o}]\";"
                      "    string grouped = f\"[{1234567:,}]\";"
                      "    string negative_grouped = f\"[{-1234567:,}]\";"
                      "    string float_text = f\"[{pi:.4f}]\";"
                      "    string neg_hex = f\"[{negative:x}]\";"
                      "    string neg_binary = f\"[{negative:b}]\";"
                      "    string neg_octal = f\"[{negative:o}]\";"
                      "    string zero_binary = f\"[{0:b}]\";"
                      "    string bool_text = f\"[{ready}]\";"
                      "    string empty_pad = f\"[{empty:>4}]\";"
                      "    if left == \"[Alice     ]\" && right == \"[     Alice]\" && center == \"[  Alice   ]\" &&"
                      "        zero_pad == \"[00000030]\" && decimal == \"[30]\" && hex_lower == \"[1e]\" &&"
                      "        hex_upper == \"[1E]\" && binary == \"[11110]\" && octal == \"[36]\" &&"
                      "        grouped == \"[1,234,567]\" && negative_grouped == \"[-1,234,567]\" &&"
                      "        float_text == \"[3.1416]\" && neg_hex == \"[-1e]\" && neg_binary == \"[-11110]\" &&"
                      "        neg_octal == \"[-36]\" && zero_binary == \"[0]\" && bool_text == \"[true]\" &&"
                      "        empty_pad == \"[    ]\" {"
                      "        return 2;"
                      "    }"
                      "    return 0;"
                      "}"),
        2);
}

TEST(VigilCompilerTest, CompilesAndExecutesPublicClassesInterfacesAndGlobals)
{
    const struct TestSource sources[] = {{"/project/contracts.vigil", "pub interface Reader {"
                                                                      "    fn read() -> i32;"
                                                                      "}"},
                                         {"/project/model.vigil", "import \"contracts\";"
                                                                  "pub i32 seed = 4;"
                                                                  "pub class Counter implements contracts.Reader {"
                                                                  "    pub i32 value;"
                                                                  "    pub fn read() -> i32 {"
                                                                  "        return self.value;"
                                                                  "    }"
                                                                  "}"
                                                                  "pub fn make_reader(i32 delta) -> contracts.Reader {"
                                                                  "    return Counter(seed + delta);"
                                                                  "}"},
                                         {"/project/main.vigil",
                                          "import \"contracts\";"
                                          "import \"model\";"
                                          "fn main() -> i32 {"
                                          "    contracts.Reader reader = model.make_reader(5);"
                                          "    model.Counter counter = model.Counter(model.seed);"
                                          "    return reader.read() + counter.value;"
                                          "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 3U, "/project/main.vigil"), 13);
}

TEST(VigilCompilerTest, RejectsAccessToNonPublicClassMembersAcrossFiles)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    RegisterSource(vigil_test_failed_, &registry, "/project/model.vigil",
                   "pub class Counter {"
                   "    i32 value;"
                   "    fn init(i32 value) -> void {"
                   "        self.value = value;"
                   "    }"
                   "    fn read() -> i32 {"
                   "        return self.value;"
                   "    }"
                   "}",
                   &error);
    source_id = RegisterSource(vigil_test_failed_, &registry, "/project/main.vigil",
                               "import \"model\";"
                               "fn main() -> i32 {"
                               "    model.Counter counter = model.Counter(7);"
                               "    return counter.value;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "class field is not public");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsAccessToNonPublicClassMethodsAcrossFiles)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    RegisterSource(vigil_test_failed_, &registry, "/project/model.vigil",
                   "pub class Counter {"
                   "    pub i32 value;"
                   "    fn init(i32 value) -> void {"
                   "        self.value = value;"
                   "    }"
                   "    fn read() -> i32 {"
                   "        return self.value;"
                   "    }"
                   "}",
                   &error);
    source_id = RegisterSource(vigil_test_failed_, &registry, "/project/main.vigil",
                               "import \"model\";"
                               "fn main() -> i32 {"
                               "    model.Counter counter = model.Counter(7);"
                               "    return counter.read();"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "class method is not public");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, CompilesAndExecutesQualifiedImportedEnumsAcrossFiles)
{
    const struct TestSource sources[] = {{"/project/colors.vigil", "pub enum Color {"
                                                                   "    Red,"
                                                                   "    Green,"
                                                                   "    Blue"
                                                                   "}"
                                                                   "pub fn pick() -> Color {"
                                                                   "    return Color.Green;"
                                                                   "}"},
                                         {"/project/main.vigil", "import \"colors\";"
                                                                 "fn main() -> i32 {"
                                                                 "    colors.Color color = colors.pick();"
                                                                 "    switch color {"
                                                                 "        case colors.Color.Red:"
                                                                 "            return 1;"
                                                                 "        case colors.Color.Green:"
                                                                 "            return 2;"
                                                                 "        default:"
                                                                 "            return 0;"
                                                                 "    }"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 2);
}

TEST(VigilCompilerTest, CompilesAndExecutesQualifiedConstantsWithBitwiseExpressions)
{
    const struct TestSource sources[] = {{"/project/config.vigil", "pub const i32 BASE = 1 << 4;"
                                                                   "pub const i32 FLAG = BASE | 3;"},
                                         {"/project/main.vigil", "import \"config\" as cfg;"
                                                                 "const i32 LIMIT = true ? cfg.FLAG : 0;"
                                                                 "fn main() -> i32 {"
                                                                 "    return LIMIT ^ 2;"
                                                                 "}"}};

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil"), 17);
}

TEST(VigilCompilerTest, RejectsDuplicateGlobalConstantNames)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "/project/main.vigil",
                               "const i32 LIMIT = 1;"
                               "const i32 LIMIT = 2;"
                               "fn main() -> i32 {"
                               "    return LIMIT;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "global constant is already declared");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsDuplicateGlobalVariableNames)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "i32 LIMIT = 1;"
                                   "i32 LIMIT = 2;"
                                   "fn main() -> i32 {"
                                   "    return LIMIT;"
                                   "}",
                                   "global variable is already declared");
}

TEST(VigilCompilerTest, RejectsGlobalVariableNameConflictsWithGlobalConstant)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "const i32 LIMIT = 1;"
                                   "i32 LIMIT = 2;"
                                   "fn main() -> i32 {"
                                   "    return LIMIT;"
                                   "}",
                                   "global variable name conflicts with global constant");
}

TEST(VigilCompilerTest, RejectsGlobalConstantNameConflictsWithGlobalVariable)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "i32 LIMIT = 1;"
                                   "const i32 LIMIT = 2;"
                                   "fn main() -> i32 {"
                                   "    return LIMIT;"
                                   "}",
                                   "global constant name conflicts with global variable");
}

TEST(VigilCompilerTest, RejectsGlobalVariableMissingInitializer)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "i32 LIMIT = ;"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected initializer expression for global variable");
}

TEST(VigilCompilerTest, RejectsGlobalVariableNameConflictsWithFunction)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn LIMIT() -> i32 {"
                                   "    return 1;"
                                   "}"
                                   "i32 LIMIT = 2;"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "global variable name conflicts with function");
}

TEST(VigilCompilerTest, RejectsGlobalConstantInitializerTypeMismatch)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "const i32 LIMIT = true;"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "initializer type does not match global constant type");
}

TEST(VigilCompilerTest, RejectsGlobalConstantNameConflictsWithFunction)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn LIMIT() -> i32 {"
                                   "    return 1;"
                                   "}"
                                   "const i32 LIMIT = 2;"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "global constant name conflicts with function");
}

TEST(VigilCompilerTest, RejectsAssigningRawI32ToEnumVariable)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "/project/main.vigil",
                               "enum Color { Red, Blue }"
                               "fn main() -> i32 {"
                               "    Color color = 1;"
                               "    return 0;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "initializer type does not match local variable type");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsDuplicateEnumNames)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "enum Color { Red }"
                                   "enum Color { Blue }"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "enum is already declared");
}

TEST(VigilCompilerTest, RejectsEnumNameConflictsWithGlobalConstant)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "const i32 Color = 1;"
                                   "enum Color { Red }"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "enum name conflicts with global constant");
}

TEST(VigilCompilerTest, RejectsEnumNameConflictsWithFunction)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn Color() -> i32 {"
                                   "    return 1;"
                                   "}"
                                   "enum Color { Red }"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "enum name conflicts with function");
}

TEST(VigilCompilerTest, RejectsMissingEnumBodyStart)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "enum Color Red"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected '{' after enum name");
}

TEST(VigilCompilerTest, RejectsDuplicateEnumMembers)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "enum Color { Red, Red }"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "enum member is already declared");
}

TEST(VigilCompilerTest, RejectsEnumMembersWithNonI32Values)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "enum Color { Red = true }"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "enum member value must be i32");
}

TEST(VigilCompilerTest, RejectsMissingEnumMemberSeparator)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "enum Color { Red Blue }"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected ',' or '}' after enum member");
}

TEST(VigilCompilerTest, RejectsQualifiedAccessToNonPublicModuleMembers)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    RegisterSource(vigil_test_failed_, &registry, "/project/lib.vigil",
                   "fn hidden() -> i32 {"
                   "    return 7;"
                   "}"
                   "i32 value = 3;",
                   &error);
    source_id = RegisterSource(vigil_test_failed_, &registry, "/project/main.vigil",
                               "import \"lib\";"
                               "fn main() -> i32 {"
                               "    return lib.hidden() + lib.value;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "module member is not public");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsAssignmentToImportedConstants)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    RegisterSource(vigil_test_failed_, &registry, "/project/lib.vigil", "pub const i32 LIMIT = 3;", &error);
    source_id = RegisterSource(vigil_test_failed_, &registry, "/project/main.vigil",
                               "import \"lib\";"
                               "fn main() -> i32 {"
                               "    lib.LIMIT = 4;"
                               "    return 0;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "cannot assign to module constant");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsAssignmentToImportedFunctions)
{
    const struct TestSource sources[] = {{"/project/lib.vigil", "pub fn value() -> i32 { return 3; }"},
                                         {"/project/main.vigil", "import \"lib\";"
                                                                 "fn main() -> i32 {"
                                                                 "    lib.value = 4;"
                                                                 "    return 0;"
                                                                 "}"}};

    ExpectSingleCompilerDiagnosticMulti(vigil_test_failed_, sources, 2U, "/project/main.vigil",
                                        "module member is not assignable");
}

TEST(VigilCompilerTest, RejectsClassesMissingInterfaceMethods)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "missing_interface_method.vigil",
                               "interface Reader { fn read() -> i32; }"
                               "class Counter implements Reader {"
                               "    i32 value;"
                               "}"
                               "fn main() -> i32 {"
                               "    return 0;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "class does not implement required interface method");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsNonVoidInitMethods)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_init.vigil",
                               "class Counter {"
                               "    fn init(i32 value) -> i32 {"
                               "        return value;"
                               "    }"
                               "}"
                               "fn main() -> i32 {"
                               "    Counter(1);"
                               "    return 0;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "init methods must return void or err");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsInterfaceMethodsWithWrongSignature)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "wrong_interface_signature.vigil",
                               "interface Reader { fn read() -> i32; }"
                               "class Counter implements Reader {"
                               "    i32 value;"
                               "    fn read(i32 delta) -> i32 {"
                               "        return self.value + delta;"
                               "    }"
                               "}"
                               "fn main() -> i32 {"
                               "    return 0;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "class method signature does not match interface");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsDuplicateInterfaceNames)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader { fn read() -> i32; }"
                                   "interface Reader { fn write() -> i32; }"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "interface is already declared");
}

TEST(VigilCompilerTest, RejectsInterfaceNameConflictsWithFunction)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn Reader() -> i32 {"
                                   "    return 1;"
                                   "}"
                                   "interface Reader { fn read() -> i32; }"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "interface name conflicts with function");
}

TEST(VigilCompilerTest, RejectsMissingInterfaceBodyStart)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader fn read() -> i32;"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected '{' after interface name");
}

TEST(VigilCompilerTest, RejectsDuplicateInterfaceMethods)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader {"
                                   "    fn read() -> i32;"
                                   "    fn read() -> i32;"
                                   "}"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "interface method is already declared");
}

TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingParameterNames)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader {"
                                   "    fn read(i32) -> i32;"
                                   "}"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected parameter name");
}

TEST(VigilCompilerTest, AcceptsInterfaceMethodsWithoutSemicolons)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "interface Reader {"
                                                "    fn read() -> i32"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    return 0;"
                                                "}"),
              0);
}

TEST(VigilCompilerTest, RejectsInterfaceNameConflictsWithGlobalConstant)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "const i32 Reader = 1;"
                                   "interface Reader {"
                                   "    fn read() -> i32;"
                                   "}"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "interface name conflicts with global constant");
}

TEST(VigilCompilerTest, RejectsInvalidInterfaceBodyMembers)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader {"
                                   "    i32 value;"
                                   "}"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected interface method declaration");
}

TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingNames)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader {"
                                   "    fn () -> i32;"
                                   "}"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected method name");
}

TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingOpeningParens)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader {"
                                   "    fn read -> i32;"
                                   "}"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected '(' after method name");
}

TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingClosingParens)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader {"
                                   "    fn read(i32 value -> i32;"
                                   "}"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected ')' after parameter list");
}

TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingReturnArrows)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "interface Reader {"
                                   "    fn read() i32;"
                                   "}"
                                   "fn main() -> i32 {"
                                   "    return 0;"
                                   "}",
                                   "expected '->' after method signature");
}

TEST(VigilCompilerTest, RejectsUnknownClassFields)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "missing_field.vigil",
                               "class Pair { i32 left; }"
                               "fn main() -> i32 {"
                               "    Pair pair = Pair(1);"
                               "    return pair.right;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "unknown class field");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsUnknownClassMethods)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "missing_method.vigil",
                               "class Pair { i32 left; }"
                               "fn main() -> i32 {"
                               "    Pair pair = Pair(1);"
                               "    return pair.right();"
                               "}",
                               &error);

    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "unknown class method");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsNonI32MainReturnTypesAndUnsupportedReturnExpressions)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;
    const vigil_diagnostic_t *diagnostic;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id =
        RegisterSource(vigil_test_failed_, &registry, "string_type.vigil", "fn main() -> string { return 1; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    diagnostic = vigil_diagnostic_list_get(&diagnostics, 0U);
    ASSERT_NE(diagnostic, NULL);
    EXPECT_STREQ(vigil_string_c_str(&diagnostic->message), "main entrypoint must declare return type i32");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "void_return_value.vigil",
                               "fn helper() -> void { return 1; }"
                               "fn main() -> i32 { helper(); return 0; }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "void functions cannot return a value");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id =
        RegisterSource(vigil_test_failed_, &registry, "bool_return.vigil", "fn main() -> i32 { return true; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "main entrypoint must return an i32 expression");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id =
        RegisterSource(vigil_test_failed_, &registry, "nil_return.vigil", "fn main() -> i32 { return nil; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "main entrypoint must return an i32 expression");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "float_return.vigil", "fn main() -> i32 { return 3.14; }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "main entrypoint must return an i32 expression");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsMixedI32AndF64Arithmetic)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "mixed_numeric.vigil",
                               "fn main() -> i32 { f64 x = 1.0 + 2; return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "'+' requires matching integer, f64, or string operands");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, CompilesAndExecutesLargeIntegerLiteralInference)
{
    EXPECT_EQ(
        CompileAndRun(vigil_test_failed_,
                      "fn main() -> i32 {"
                      "    i64 signed_large = 3000000000;"
                      "    u64 huge = 9223372036854775808;"
                      "    u64 max = 18446744073709551615;"
                      "    u64 previous = max - u64(1);"
                      "    if signed_large == i64(3000000000) && huge > u64(9223372036854775807) && previous < max {"
                      "        return 11;"
                      "    }"
                      "    return 0;"
                      "}"),
        11);
}

TEST(VigilCompilerTest, RejectsInvalidLocalsAndConditions)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id =
        RegisterSource(vigil_test_failed_, &registry, "uninit.vigil", "fn main() -> i32 { i32 x; return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "variables must be initialized at declaration");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id =
        RegisterSource(vigil_test_failed_, &registry, "unknown.vigil", "fn main() -> i32 { x = 1; return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "unknown local variable");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "condition.vigil",
                               "fn main() -> i32 { if 1 { return 1; } return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "if condition must be bool");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "type.vigil",
                               "fn main() -> i32 { bool ready = 1; return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "initializer type does not match local variable type");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsInvalidFunctionSignaturesAndCalls)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "main_params.vigil",
                               "fn main(i32 value) -> i32 { return value; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    EXPECT_EQ(error.location.source_id, source_id);
    EXPECT_EQ(error.location.line, 1U);
    EXPECT_EQ(error.location.column, 4U);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "main entrypoint must not declare parameters");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "arg_count.vigil",
                               "fn add(i32 left, i32 right) -> i32 { return left + right; }"
                               "fn main() -> i32 { return add(1); }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "call argument count does not match function signature");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "arg_type.vigil",
                               "fn truthy(bool ready) -> i32 {"
                               "    if ready { return 1; }"
                               "    return 0;"
                               "}"
                               "fn main() -> i32 { return truthy(1); }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "call argument type does not match parameter type");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "unknown_call.vigil",
                               "fn main() -> i32 { return missing(1); }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "unknown function");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsWrappingConditionParentheses)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "wrapped_condition.vigil",
                               "fn main() -> i32 { if (true) { return 1; } return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "unnecessary parentheses around condition (remove them)");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsInvalidLogicalOperandsAndLoopControlOutsideLoops)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "logical_type.vigil",
                               "fn main() -> i32 { if 1 && true { return 1; } return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "logical '&&' requires bool operands");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "break_outside.vigil",
                               "fn main() -> i32 { break; return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "'break' is only valid inside a loop");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "continue_outside.vigil",
                               "fn main() -> i32 { continue; return 0; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "'continue' is only valid inside a loop");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RequiresGuaranteedReturnAndPreservesNestedScopeShadowing)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "missing_return.vigil",
                               "fn choose(bool ready) -> i32 {"
                               "    if ready {"
                               "        return 1;"
                               "    }"
                               "}"
                               "fn main() -> i32 {"
                               "    return choose(true);"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "function must return a value on all paths");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "missing_main_return.vigil",
                               "fn main() -> i32 {"
                               "    if true {"
                               "        return 1;"
                               "    }"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "main entrypoint must return an i32 value on all paths");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "void_missing_value.vigil",
                               "fn helper(bool ready) -> void {"
                               "    if ready {"
                               "        return;"
                               "    }"
                               "}"
                               "fn main() -> i32 { helper(false); return 0; }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_diagnostic_list_count(&diagnostics), 0U);
    vigil_object_release(&function);
    function = NULL;

    vigil_diagnostic_list_clear(&diagnostics);
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn choose(bool ready) -> i32 {"
                                                "    if ready {"
                                                "        return 1;"
                                                "    } else {"
                                                "        return 2;"
                                                "    }"
                                                "}"
                                                "fn main() -> i32 {"
                                                "    return choose(false);"
                                                "}"),
              2);

    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    i32 value = 7;"
                                                "    {"
                                                "        i32 value = 2;"
                                                "    }"
                                                "    return value;"
                                                "}"),
              7);

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsVoidInNonReturnTypePositions)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "void_local.vigil",
                               "fn helper() -> void {}"
                               "fn main() -> i32 { void x = helper(); return 0; }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "local variables cannot use type void");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "void_param.vigil",
                               "fn bad(void value) -> i32 { return 0; }"
                               "fn main() -> i32 { return 0; }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "function parameters cannot use type void");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "void_global.vigil",
                               "void state = nil;"
                               "fn main() -> i32 { return 0; }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "global variables cannot use type void");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "void_field.vigil",
                               "class Bad { void value; }"
                               "fn main() -> i32 { return 0; }",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "class fields cannot use type void");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsDeferWithoutCallExpression)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_defer.vigil",
                               "fn main() -> i32 {"
                               "    i32 value = 1;"
                               "    defer value;"
                               "    return 0;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "defer requires a call expression");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsAssignmentToConstLocal)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "const_assign.vigil",
                               "fn main() -> i32 {"
                               "    const i32 value = 1;"
                               "    value = 2;"
                               "    return value;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "cannot assign to const local variable");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, CompilesAndExecutesArrayAndMapLiteralsIndexingAndAssignment)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    array<i32> nums = [1, 2, 3];"
                                                "    map<i32, i32> scores = {1: 4, 2: 5};"
                                                "    map<bool, i32> flags = {true: 3, false: 1};"
                                                "    nums[1] = nums[0] + scores[2];"
                                                "    nums[2] += scores[1];"
                                                "    nums[2]--;"
                                                "    scores[1] = nums[1] + flags[true];"
                                                "    scores[2] *= nums[0];"
                                                "    return scores[1];"
                                                "}"),
              9);
}

TEST(VigilCompilerTest, CompilesAndExecutesForInOverArraysAndMaps)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {"
                                                "    array<i32> nums = [2, 4, 6, 8];"
                                                "    map<string, i32> scores = {\"a\": 1, \"b\": 3, \"c\": 5};"
                                                "    i32 sum = 0;"
                                                "    for value in nums {"
                                                "        if value == 6 {"
                                                "            continue;"
                                                "        }"
                                                "        if value == 8 {"
                                                "            break;"
                                                "        }"
                                                "        sum += value;"
                                                "    }"
                                                "    for key, value in scores {"
                                                "        if key == \"b\" {"
                                                "            sum += value;"
                                                "        }"
                                                "    }"
                                                "    return sum;"
                                                "}"),
              9);
}

TEST(VigilCompilerTest, RejectsInvalidForInBindingsAndIterables)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_array_for_in.vigil",
                               "fn main() -> i32 {"
                               "    array<i32> nums = [1, 2];"
                               "    for left, right in nums {"
                               "        return left + right;"
                               "    }"
                               "    return 0;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "for-in over arrays requires a single loop binding");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_non_iterable_for_in.vigil",
                               "fn main() -> i32 {"
                               "    i32 value = 4;"
                               "    for item in value {"
                               "        return item;"
                               "    }"
                               "    return 0;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "for-in requires an array, map, or string iterable");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsInvalidCollectionIndexingAndCompoundIndexedAssignmentTypes)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_array_index.vigil",
                               "fn main() -> i32 {"
                               "    array<i32> nums = [1, 2];"
                               "    return nums[\"zero\"];"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "array index must be i32");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_map_index.vigil",
                               "fn main() -> i32 {"
                               "    map<string, i32> scores = {\"a\": 1};"
                               "    return scores[0];"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "map index must match map key type");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_map_key_type.vigil",
                               "fn main() -> i32 {"
                               "    map<f64, i32> scores = {1.5: 1};"
                               "    return 0;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "map keys must use an integer, bool, string, or enum type");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_index_compound_type.vigil",
                               "fn main() -> i32 {"
                               "    array<i32> nums = [1, 2];"
                               "    nums[0] += \"x\";"
                               "    return nums[0];"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "compound assignment requires matching integer, f64, or string operands");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsInvalidBuiltinConversions)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_conv.vigil",
                               "fn main() -> i32 {"
                               "    bool ready = bool(1);"
                               "    return ready ? 1 : 0;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "bool(...) requires a bool argument");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsCallingBareFunctionTypeAndMismatchedFunctionSignatures)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_any_call.vigil",
                               "fn add(i32 a, i32 b) -> i32 {"
                               "    return a + b;"
                               "}"
                               "fn main() -> i32 {"
                               "    fn op = add;"
                               "    return op(1, 2);"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "indirect calls require a concrete function signature");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_function_assign.vigil",
                               "fn log_name(string name) -> void {}"
                               "fn main() -> i32 {"
                               "    fn(i32, i32) -> i32 op = log_name;"
                               "    return 0;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "initializer type does not match local variable type");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsInvalidErrorConstructionAndMethods)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_err_ctor.vigil",
                               "fn main() -> i32 {"
                               "    err e = err(1, err.arg);"
                               "    return 0;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "err(...) message must be a string");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_err_method.vigil",
                               "fn main() -> i32 {"
                               "    err e = ok;"
                               "    return e.code();"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "unknown error method");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsInvalidGuardBindings)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad_guard.vigil",
                               "fn divide(i32 a, i32 b) -> (i32, err) {"
                               "    if b == 0 {"
                               "        return 0, err(\"division by zero\", err.arg);"
                               "    }"
                               "    return a / b, ok;"
                               "}"
                               "fn main() -> i32 {"
                               "    guard i32 value, err _ = divide(1, 0) {"
                               "        return 1;"
                               "    }"
                               "    return 0;"
                               "}",
                               &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    EXPECT_EQ(function, NULL);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "guard error binding must be named");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, ReportsSyntaxErrorsForUnsupportedShape)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;
    const vigil_diagnostic_t *diagnostic;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad.vigil", "fn helper() -> i32 { return 1; }", &error);
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    diagnostic = vigil_diagnostic_list_get(&diagnostics, 0U);
    ASSERT_NE(diagnostic, NULL);
    EXPECT_STREQ(vigil_string_c_str(&diagnostic->message), "expected top-level function 'main'");

    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, CompilesExternFnDeclaration)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;
    vigil_status_t status;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "main.vigil",
                               "extern fn add(i32 a, i32 b) -> i32 from \"libm\";\n"
                               "fn main() -> i32 { return 0; }",
                               &error);
    status = vigil_compile_source(&registry, source_id, &function, &diagnostics, &error);
    EXPECT_EQ(status, VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_diagnostic_list_count(&diagnostics), 0U);

    if (function != NULL)
        vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsDirectCircularImport)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    RegisterSource(vigil_test_failed_, &registry, "/project/a.vigil",
                   "import \"b\";\n"
                   "pub fn value() -> i32 { return b.get(); }\n"
                   "fn main() -> i32 { return value(); }",
                   &error);
    source_id = RegisterSource(vigil_test_failed_, &registry, "/project/b.vigil",
                               "import \"a\";\n"
                               "pub fn get() -> i32 { return 1; }",
                               &error);
    /* Compile from a.vigil; it imports b which imports a — cycle. */
    source_id = (vigil_source_id_t)1U;
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_TRUE(vigil_diagnostic_list_count(&diagnostics) >= 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "circular import detected");

    if (function != NULL)
        vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, RejectsIndirectCircularImport)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    RegisterSource(vigil_test_failed_, &registry, "/project/a.vigil",
                   "import \"b\";\n"
                   "fn main() -> i32 { return b.value(); }",
                   &error);
    RegisterSource(vigil_test_failed_, &registry, "/project/b.vigil",
                   "import \"c\";\n"
                   "pub fn value() -> i32 { return c.get(); }",
                   &error);
    RegisterSource(vigil_test_failed_, &registry, "/project/c.vigil",
                   "import \"a\";\n"
                   "pub fn get() -> i32 { return 1; }",
                   &error);

    source_id = (vigil_source_id_t)1U;
    EXPECT_EQ(vigil_compile_source(&registry, source_id, &function, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_TRUE(vigil_diagnostic_list_count(&diagnostics) >= 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message), "circular import detected");

    if (function != NULL)
        vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilCompilerTest, AllowsDiamondImportsWithoutCycle)
{
    /* A imports B and C; both B and C import D — no cycle. */
    const struct TestSource sources[] = {
        {"/project/d.vigil", "pub fn base() -> i32 { return 3; }"},
        {"/project/b.vigil", "import \"d\";\npub fn get_b() -> i32 { return d.base(); }"},
        {"/project/c.vigil", "import \"d\";\npub fn get_c() -> i32 { return d.base(); }"},
        {"/project/main.vigil", "import \"b\";\nimport \"c\";\n"
                                "fn main() -> i32 { return b.get_b() + c.get_c(); }"},
    };

    EXPECT_EQ(CompileAndRunMulti(vigil_test_failed_, sources, 4U, "/project/main.vigil"), 6);
}

static void register_compiler_import_tests(void)
{
    REGISTER_TEST(VigilCompilerTest, RejectsDirectCircularImport);
    REGISTER_TEST(VigilCompilerTest, RejectsIndirectCircularImport);
    REGISTER_TEST(VigilCompilerTest, AllowsDiamondImportsWithoutCycle);
}

TEST(VigilCompilerTest, AcceptsTrailingCommasInCallsArraysAndMaps)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn add(i32 a, i32 b) -> i32 { return a + b; }\n"
                                                "fn main() -> i32 {\n"
                                                "    i32 x = add(3, 4,);\n"
                                                "    array<i32> a = [10, 20,];\n"
                                                "    map<string, i32> m = {\"k\": 1,};\n"
                                                "    return x + a.len() + m.len();\n"
                                                "}\n"),
              10);
}

TEST(VigilCompilerTest, ImplicitSemiBeforeClosingBrace)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 { if true { return 7 } return 0 }"), 7);
}

TEST(VigilCompilerTest, AnyNoneMethodsOnAllCollectionTypes)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {\n"
                                                "    string s = \"hi\";\n"
                                                "    string e = \"\";\n"
                                                "    if !s.any() { return 1; }\n"
                                                "    if !e.none() { return 2; }\n"
                                                "    if s.none() { return 3; }\n"
                                                "    if e.any() { return 4; }\n"
                                                "    array<i32> a = [1];\n"
                                                "    array<i32> ea = [];\n"
                                                "    if !a.any() { return 5; }\n"
                                                "    if !ea.none() { return 6; }\n"
                                                "    map<string, i32> m = {\"k\": 1};\n"
                                                "    map<string, i32> em = {};\n"
                                                "    if !m.any() { return 7; }\n"
                                                "    if !em.none() { return 8; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilCompilerTest, WalrusInfersSingleValueTypes)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {\n"
                                                "    x := 42;\n"
                                                "    name := \"hello\";\n"
                                                "    pi := 3.14;\n"
                                                "    flag := true;\n"
                                                "    if x != 42 { return 1; }\n"
                                                "    if name != \"hello\" { return 2; }\n"
                                                "    if pi < 3.0 { return 3; }\n"
                                                "    if !flag { return 4; }\n"
                                                "    x = 99;\n"
                                                "    if x != 99 { return 5; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilCompilerTest, WalrusInfersFromFunctionReturn)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn double(i32 n) -> i32 { return n * 2; }\n"
                                                "fn main() -> i32 {\n"
                                                "    result := double(21);\n"
                                                "    return result;\n"
                                                "}\n"),
              42);
}

TEST(VigilCompilerTest, WalrusMultiReturnInference)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn divide(i32 a, i32 b) -> (i32, err) {\n"
                                                "    if b == 0 { return 0, err(\"zero\", err.arg); }\n"
                                                "    return a / b, ok;\n"
                                                "}\n"
                                                "fn main() -> i32 {\n"
                                                "    val, e := divide(10, 2);\n"
                                                "    if e != ok { return 1; }\n"
                                                "    return val;\n"
                                                "}\n"),
              5);
}

TEST(VigilCompilerTest, ConstWalrusInfersAndEnforcesImmutability)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {\n"
                                                "    const x := 42;\n"
                                                "    return x;\n"
                                                "}\n"),
              42);
}

TEST(VigilCompilerTest, RejectsConstWalrusReassignment)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn main() -> i32 {\n"
                                   "    const x := 42;\n"
                                   "    x = 99;\n"
                                   "    return x;\n"
                                   "}\n",
                                   "cannot assign to const local variable");
}

TEST(VigilCompilerTest, RejectsParameterReassignment)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn mutate(i32 x) -> i32 {\n"
                                   "    x = 10;\n"
                                   "    return x;\n"
                                   "}\n"
                                   "fn main() -> i32 { return mutate(5); }\n",
                                   "cannot assign to function parameter");
}

TEST(VigilCompilerTest, RejectsWalrusWithVoidExpression)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "fn noop() -> void {}\n"
                                   "fn main() -> i32 {\n"
                                   "    x := noop();\n"
                                   "    return 0;\n"
                                   "}\n",
                                   "cannot infer type from void expression");
}

TEST(VigilCompilerTest, StructDeclarationAndConstruction)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "struct Point { f64 x; f64 y; }\n"
                                                "fn main() -> i32 {\n"
                                                "    Point p = Point(3.0, 4.0);\n"
                                                "    if p.x != 3.0 { return 1; }\n"
                                                "    if p.y != 4.0 { return 2; }\n"
                                                "    p.x = 10.0;\n"
                                                "    if p.x != 10.0 { return 3; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilCompilerTest, StructAsParameter)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "struct Pair { i32 a; i32 b; }\n"
                                                "fn sum(Pair p) -> i32 { return p.a + p.b; }\n"
                                                "fn main() -> i32 { return sum(Pair(17, 25)); }\n"),
              42);
}

TEST(VigilCompilerTest, RejectsStructWithMethods)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "struct Bad { i32 x; fn foo() -> void {} }\n"
                                   "fn main() -> i32 { return 0; }\n",
                                   "structs cannot contain methods");
}

TEST(VigilCompilerTest, RejectsEmptyStruct)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "struct Empty {}\n"
                                   "fn main() -> i32 { return 0; }\n",
                                   "structs must have at least one field");
}

TEST(VigilCompilerTest, RejectsStructWithPub)
{
    ExpectSingleCompilerDiagnostic(vigil_test_failed_,
                                   "struct Bad { pub i32 x; }\n"
                                   "fn main() -> i32 { return 0; }\n",
                                   "struct fields are public by default; remove 'pub'");
}

TEST(VigilCompilerTest, ArraySortReverseIndexOf)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {\n"
                                                "    array<i32> a = [3, 1, 2];\n"
                                                "    a.sort();\n"
                                                "    if a[0] != 1 { return 1; }\n"
                                                "    if a[2] != 3 { return 2; }\n"
                                                "    a.sort_desc();\n"
                                                "    if a[0] != 3 { return 3; }\n"
                                                "    a.reverse();\n"
                                                "    if a[0] != 1 { return 4; }\n"
                                                "    if a.index_of(2) != 1 { return 5; }\n"
                                                "    if a.index_of(99) != -1 { return 6; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilCompilerTest, ArrayRemoveInsertClear)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {\n"
                                                "    array<i32> a = [10, 20, 30];\n"
                                                "    i32 removed, err e = a.remove(1);\n"
                                                "    if e != ok { return 1; }\n"
                                                "    if removed != 20 { return 2; }\n"
                                                "    if a.len() != 2 { return 3; }\n"
                                                "    err e2 = a.insert(0, 99);\n"
                                                "    if e2 != ok { return 4; }\n"
                                                "    if a[0] != 99 { return 5; }\n"
                                                "    if a.len() != 3 { return 6; }\n"
                                                "    a.clear();\n"
                                                "    if a.len() != 0 { return 7; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}

TEST(VigilCompilerTest, MapClear)
{
    EXPECT_EQ(CompileAndRun(vigil_test_failed_, "fn main() -> i32 {\n"
                                                "    map<string, i32> m = {\"a\": 1, \"b\": 2};\n"
                                                "    if m.len() != 2 { return 1; }\n"
                                                "    m.clear();\n"
                                                "    if m.len() != 0 { return 2; }\n"
                                                "    return 0;\n"
                                                "}\n"),
              0);
}
static void register_compiler_defer_tests(void)
{
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDeferredFunctionValues);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDeferredConstructorsWithoutInit);
    REGISTER_TEST(VigilCompilerTest, CompilesExternFnDeclaration);
}

void register_compiler_tests(void)
{
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesArithmeticAndLocals);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesFloatArithmeticAndComparison);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesConversionsConstLocalsAndBitwiseNot);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesWiderIntegerTypesAndConversions);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesI32ToI64ArithPromotion);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesFunctionValuesAndIndirectCalls);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesAnonymousFunctionsClosuresAndLocalFunctions);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesLambdaShorthandWithContextInference);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesExplicitErrorValues);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesTupleBindingsAndGuard);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesIfElseAndWhile);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesTailCallInIfBranch);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesBoolLocalsAndEquality);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDirectFunctionCalls);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesRecursiveFunctionCalls);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesShortCircuitLogicalOperators);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesBitwiseShiftAndTernaryExpressions);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesBreakAndContinue);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesForLoopsAndIncrementClauses);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesForLoopContinueAndBreak);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesImportedFunctionsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesNestedImportsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, RejectsUnregisteredImportedSource);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesModuleConstantsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesConstantExpressions);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesClassesFieldAccessAndFieldAssignment);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesClassesAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesClassMethodsAndSelf);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesFloatFieldsAndGlobals);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesInitBasedConstructors);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesFallibleInitConstructorsWithGuard);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesFallibleInitConstructorsReturningOk);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesMethodsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesPublicInitConstructorsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesInterfacePolymorphismAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesQualifiedModuleSymbolsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, RejectsUnqualifiedImportedSymbolsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDuplicateTopLevelNamesAcrossModules);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesQualifiedImportedInterfacesAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesQualifiedConstantsInConstantExpressions);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesEnumsAndSwitch);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesStringsAndStringConstants);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesImportedStringConstantsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesStringBuiltInMethods);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesStringOrderingComparisons);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesArrayBuiltInMethods);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesMapBuiltInMethods);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesTypedEmptyCollectionLiterals);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesVoidFunctionsAndMethods);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDeferredCallsInLifoOrderWithEagerArguments);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDeferredMethodsAndReturnsAfterDrain);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDeferredInterfaceCalls);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDeferredInitConstructors);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesMultipleReturnValues);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesDeferredMultipleReturnValues);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesPublicGlobalsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesQualifiedGlobalAssignmentAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesEmptyCollectionGlobalsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesCompoundAssignmentsForGlobalsAndFields);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesFStringsWithInterpolationAndFormatting);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesFStringsWithAlignmentAndRadixFormatting);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesPublicClassesInterfacesAndGlobals);
    REGISTER_TEST(VigilCompilerTest, RejectsAccessToNonPublicClassMembersAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, RejectsAccessToNonPublicClassMethodsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesQualifiedImportedEnumsAcrossFiles);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesQualifiedConstantsWithBitwiseExpressions);
    REGISTER_TEST(VigilCompilerTest, RejectsDuplicateGlobalConstantNames);
    REGISTER_TEST(VigilCompilerTest, RejectsDuplicateGlobalVariableNames);
    REGISTER_TEST(VigilCompilerTest, RejectsGlobalVariableNameConflictsWithGlobalConstant);
    REGISTER_TEST(VigilCompilerTest, RejectsGlobalConstantNameConflictsWithGlobalVariable);
    REGISTER_TEST(VigilCompilerTest, RejectsGlobalVariableMissingInitializer);
    REGISTER_TEST(VigilCompilerTest, RejectsGlobalVariableNameConflictsWithFunction);
    REGISTER_TEST(VigilCompilerTest, RejectsGlobalConstantInitializerTypeMismatch);
    REGISTER_TEST(VigilCompilerTest, RejectsGlobalConstantNameConflictsWithFunction);
    REGISTER_TEST(VigilCompilerTest, RejectsAssigningRawI32ToEnumVariable);
    REGISTER_TEST(VigilCompilerTest, RejectsDuplicateEnumNames);
    REGISTER_TEST(VigilCompilerTest, RejectsEnumNameConflictsWithGlobalConstant);
    REGISTER_TEST(VigilCompilerTest, RejectsEnumNameConflictsWithFunction);
    REGISTER_TEST(VigilCompilerTest, RejectsMissingEnumBodyStart);
    REGISTER_TEST(VigilCompilerTest, RejectsDuplicateEnumMembers);
    REGISTER_TEST(VigilCompilerTest, RejectsEnumMembersWithNonI32Values);
    REGISTER_TEST(VigilCompilerTest, RejectsMissingEnumMemberSeparator);
    REGISTER_TEST(VigilCompilerTest, RejectsQualifiedAccessToNonPublicModuleMembers);
    REGISTER_TEST(VigilCompilerTest, RejectsAssignmentToImportedConstants);
    REGISTER_TEST(VigilCompilerTest, RejectsAssignmentToImportedFunctions);
    REGISTER_TEST(VigilCompilerTest, RejectsClassesMissingInterfaceMethods);
    REGISTER_TEST(VigilCompilerTest, RejectsNonVoidInitMethods);
    REGISTER_TEST(VigilCompilerTest, RejectsInterfaceMethodsWithWrongSignature);
    REGISTER_TEST(VigilCompilerTest, RejectsDuplicateInterfaceNames);
    REGISTER_TEST(VigilCompilerTest, RejectsInterfaceNameConflictsWithFunction);
    REGISTER_TEST(VigilCompilerTest, RejectsMissingInterfaceBodyStart);
    REGISTER_TEST(VigilCompilerTest, RejectsDuplicateInterfaceMethods);
    REGISTER_TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingParameterNames);
    REGISTER_TEST(VigilCompilerTest, AcceptsInterfaceMethodsWithoutSemicolons);
    REGISTER_TEST(VigilCompilerTest, RejectsInterfaceNameConflictsWithGlobalConstant);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidInterfaceBodyMembers);
    REGISTER_TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingNames);
    REGISTER_TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingOpeningParens);
    REGISTER_TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingClosingParens);
    REGISTER_TEST(VigilCompilerTest, RejectsInterfaceMethodsMissingReturnArrows);
    REGISTER_TEST(VigilCompilerTest, RejectsUnknownClassFields);
    REGISTER_TEST(VigilCompilerTest, RejectsUnknownClassMethods);
    REGISTER_TEST(VigilCompilerTest, RejectsNonI32MainReturnTypesAndUnsupportedReturnExpressions);
    REGISTER_TEST(VigilCompilerTest, RejectsMixedI32AndF64Arithmetic);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesLargeIntegerLiteralInference);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidLocalsAndConditions);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidFunctionSignaturesAndCalls);
    REGISTER_TEST(VigilCompilerTest, RejectsLambdaWithoutFunctionTypedContext);
    REGISTER_TEST(VigilCompilerTest, RejectsLambdaParameterCountMismatch);
    REGISTER_TEST(VigilCompilerTest, RejectsWrappingConditionParentheses);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidLogicalOperandsAndLoopControlOutsideLoops);
    REGISTER_TEST(VigilCompilerTest, RequiresGuaranteedReturnAndPreservesNestedScopeShadowing);
    REGISTER_TEST(VigilCompilerTest, RejectsVoidInNonReturnTypePositions);
    REGISTER_TEST(VigilCompilerTest, RejectsDeferWithoutCallExpression);
    REGISTER_TEST(VigilCompilerTest, RejectsAssignmentToConstLocal);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesArrayAndMapLiteralsIndexingAndAssignment);
    REGISTER_TEST(VigilCompilerTest, CompilesAndExecutesForInOverArraysAndMaps);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidForInBindingsAndIterables);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidCollectionIndexingAndCompoundIndexedAssignmentTypes);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidBuiltinConversions);
    REGISTER_TEST(VigilCompilerTest, RejectsCallingBareFunctionTypeAndMismatchedFunctionSignatures);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidErrorConstructionAndMethods);
    REGISTER_TEST(VigilCompilerTest, RejectsInvalidGuardBindings);
    REGISTER_TEST(VigilCompilerTest, ReportsSyntaxErrorsForUnsupportedShape);
    register_compiler_import_tests();
    register_compiler_defer_tests();
    REGISTER_TEST(VigilCompilerTest, AcceptsTrailingCommasInCallsArraysAndMaps);
    REGISTER_TEST(VigilCompilerTest, ImplicitSemiBeforeClosingBrace);
    REGISTER_TEST(VigilCompilerTest, AnyNoneMethodsOnAllCollectionTypes);
    REGISTER_TEST(VigilCompilerTest, WalrusInfersSingleValueTypes);
    REGISTER_TEST(VigilCompilerTest, WalrusInfersFromFunctionReturn);
    REGISTER_TEST(VigilCompilerTest, WalrusMultiReturnInference);
    REGISTER_TEST(VigilCompilerTest, ConstWalrusInfersAndEnforcesImmutability);
    REGISTER_TEST(VigilCompilerTest, RejectsConstWalrusReassignment);
    REGISTER_TEST(VigilCompilerTest, RejectsParameterReassignment);
    REGISTER_TEST(VigilCompilerTest, RejectsWalrusWithVoidExpression);
    REGISTER_TEST(VigilCompilerTest, StructDeclarationAndConstruction);
    REGISTER_TEST(VigilCompilerTest, StructAsParameter);
    REGISTER_TEST(VigilCompilerTest, RejectsStructWithMethods);
    REGISTER_TEST(VigilCompilerTest, RejectsEmptyStruct);
    REGISTER_TEST(VigilCompilerTest, RejectsStructWithPub);
    REGISTER_TEST(VigilCompilerTest, ArraySortReverseIndexOf);
    REGISTER_TEST(VigilCompilerTest, ArrayRemoveInsertClear);
    REGISTER_TEST(VigilCompilerTest, MapClear);
}
