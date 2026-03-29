#include "vigil_test.h"

#include "vigil/vigil.h"

static vigil_source_id_t RegisterSource(int *vigil_test_failed_, vigil_source_registry_t *registry, const char *path,
                                        const char *text, vigil_error_t *error)
{
    vigil_source_id_t source_id = 0U;

    EXPECT_EQ(vigil_source_registry_register_cstr(registry, path, text, &source_id, error), VIGIL_STATUS_OK);
    return source_id;
}

static void FreeCheckerFixtures(vigil_runtime_t **runtime, vigil_source_registry_t *registry,
                                vigil_diagnostic_list_t *diagnostics)
{
    vigil_diagnostic_list_free(diagnostics);
    vigil_source_registry_free(registry);
    vigil_runtime_close(runtime);
}

static void InitCheckerFixtures(int *vigil_test_failed_, vigil_runtime_t **runtime, vigil_source_registry_t *registry,
                                vigil_diagnostic_list_t *diagnostics, vigil_error_t *error)
{
    (void)vigil_test_failed_;
    ASSERT_EQ(vigil_runtime_open(runtime, NULL, error), VIGIL_STATUS_OK);
    vigil_source_registry_init(registry, *runtime);
    vigil_diagnostic_list_init(diagnostics, *runtime);
}

static void ExpectSingleCheckerDiagnostic(int *vigil_test_failed_, vigil_source_registry_t *registry,
                                          vigil_source_id_t source_id, vigil_diagnostic_list_t *diagnostics,
                                          vigil_error_t *error, const char *message)
{
    const vigil_diagnostic_t *diagnostic;

    (void)vigil_test_failed_;
    EXPECT_EQ(vigil_check_source(registry, source_id, NULL, diagnostics, error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(diagnostics), 1U);
    diagnostic = vigil_diagnostic_list_get(diagnostics, 0U);
    ASSERT_NE(diagnostic, NULL);
    EXPECT_STREQ(vigil_string_c_str(&diagnostic->message), message);
}

TEST(VigilCheckerTest, ValidatesWellTypedProgramWithoutDiagnostics)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id;

    InitCheckerFixtures(vigil_test_failed_, &runtime, &registry, &diagnostics, &error);

    source_id = RegisterSource(vigil_test_failed_, &registry, "main.vigil",
                               "fn add(i32 left, i32 right) -> i32 {"
                               "    return left + right;"
                               "}"
                               "fn main() -> i32 {"
                               "    i32 total = add(3, 4);"
                               "    while (total > 4) {"
                               "        total = total - 1;"
                               "        if (total == 5) {"
                               "            break;"
                               "        }"
                               "    }"
                               "    return total;"
                               "}",
                               &error);

    EXPECT_EQ(vigil_check_source(&registry, source_id, NULL, &diagnostics, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_diagnostic_list_count(&diagnostics), 0U);

    FreeCheckerFixtures(&runtime, &registry, &diagnostics);
}

TEST(VigilCheckerTest, ReportsSemanticErrorsWithoutProducingEntrypoint)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id;

    InitCheckerFixtures(vigil_test_failed_, &runtime, &registry, &diagnostics, &error);

    source_id = RegisterSource(vigil_test_failed_, &registry, "bad.vigil",
                               "fn is_ready() -> bool {"
                               "    return true;"
                               "}"
                               "fn main() -> i32 {"
                               "    i32 value = 1;"
                               "    value = is_ready();"
                               "    return value;"
                               "}",
                               &error);

    ExpectSingleCheckerDiagnostic(vigil_test_failed_, &registry, source_id, &diagnostics, &error,
                                  "assigned expression type does not match local variable type");

    FreeCheckerFixtures(&runtime, &registry, &diagnostics);
}

TEST(VigilCheckerTest, ReportsMissingReturnOnSomePaths)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id;

    InitCheckerFixtures(vigil_test_failed_, &runtime, &registry, &diagnostics, &error);

    source_id = RegisterSource(vigil_test_failed_, &registry, "missing_return.vigil",
                               "fn choose(bool ready) -> i32 {"
                               "    if (ready) {"
                               "        return 1;"
                               "    }"
                               "}"
                               "fn main() -> i32 {"
                               "    return choose(false);"
                               "}",
                               &error);

    ExpectSingleCheckerDiagnostic(vigil_test_failed_, &registry, source_id, &diagnostics, &error,
                                  "function must return a value on all paths");

    FreeCheckerFixtures(&runtime, &registry, &diagnostics);
}

TEST(VigilCheckerTest, RequiresMainFunctionForCheckSource)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id;

    InitCheckerFixtures(vigil_test_failed_, &runtime, &registry, &diagnostics, &error);

    source_id = RegisterSource(vigil_test_failed_, &registry, "lib_only.vigil",
                               "pub fn twice(i32 value) -> i32 {"
                               "    return value * 2;"
                               "}",
                               &error);

    ExpectSingleCheckerDiagnostic(vigil_test_failed_, &registry, source_id, &diagnostics, &error,
                                  "expected top-level function 'main'");

    FreeCheckerFixtures(&runtime, &registry, &diagnostics);
}

TEST(VigilCheckerTest, ValidatesArguments)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id = 0U;

    ASSERT_EQ(vigil_check_source(NULL, source_id, NULL, &diagnostics, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_STREQ(vigil_error_message(&error), "source registry must not be null");

    InitCheckerFixtures(vigil_test_failed_, &runtime, &registry, &diagnostics, &error);
    source_id = RegisterSource(vigil_test_failed_, &registry, "main.vigil", "fn main() -> i32 { return 0; }", &error);

    EXPECT_EQ(vigil_check_source(&registry, source_id, NULL, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_STREQ(vigil_error_message(&error), "diagnostic list must not be null");

    FreeCheckerFixtures(&runtime, &registry, &diagnostics);
}

void register_checker_tests(void)
{
    REGISTER_TEST(VigilCheckerTest, ValidatesWellTypedProgramWithoutDiagnostics);
    REGISTER_TEST(VigilCheckerTest, ReportsSemanticErrorsWithoutProducingEntrypoint);
    REGISTER_TEST(VigilCheckerTest, ReportsMissingReturnOnSomePaths);
    REGISTER_TEST(VigilCheckerTest, RequiresMainFunctionForCheckSource);
    REGISTER_TEST(VigilCheckerTest, ValidatesArguments);
}
