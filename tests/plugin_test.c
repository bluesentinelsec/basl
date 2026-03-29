/* Tests for the build-time plugin system.
 *
 * Validates plugin discovery, registration, and end-to-end execution
 * of plugin functions from Vigil scripts.
 */
#include "vigil_test.h"

#include <string.h>

#include "vigil/native_module.h"
#include "vigil/stdlib.h"
#include "vigil/vigil.h"

#include "plugin_registry.h"

/* ── test harness ────────────────────────────────────────────────── */

#if VIGIL_PLUGIN_COUNT > 0

/*
 * Compile and run a Vigil program that imports both stdlib and plugin modules.
 * The program's main() must return i32.  Returns that value.
 */
static int64_t RunWithPlugins(int *vigil_test_failed_, const char *source_text)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_native_registry_t natives;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_source_id_t source_id = 0U;
    int64_t output = 0;

    EXPECT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_native_registry_init(&natives);
    EXPECT_EQ(vigil_stdlib_register_all(&natives, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_plugin_register_all(&natives, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_source_registry_register_cstr(&registry, "main.vigil", source_text, &source_id, &error),
              VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_compile_source_with_natives(&registry, source_id, &natives, &function, &diagnostics, &error),
              VIGIL_STATUS_OK);
    EXPECT_NE(function, NULL);
    EXPECT_EQ(vigil_diagnostic_list_count(&diagnostics), 0U);

    vigil_value_init_nil(&result);
    EXPECT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    output = vigil_value_as_int(&result);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_native_registry_free(&natives);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return output;
}

#endif /* VIGIL_PLUGIN_COUNT > 0 */

/* ── registry tests ──────────────────────────────────────────────── */

TEST(PluginRegistry, RegisterAndFind)
{
    vigil_native_registry_t natives;
    vigil_error_t error = {0};

    vigil_native_registry_init(&natives);
    EXPECT_EQ(vigil_plugin_register_all(&natives, &error), VIGIL_STATUS_OK);

#if VIGIL_PLUGIN_COUNT > 0
    /* test_plugin should be findable */
    const vigil_native_module_t *mod = vigil_native_registry_find(&natives, "test_plugin", 11U);
    EXPECT_NE(mod, NULL);
    EXPECT_EQ(mod->function_count, 3U);
#endif

    vigil_native_registry_free(&natives);
}

TEST(PluginRegistry, PluginCountMatchesTable)
{
    vigil_native_registry_t natives;
    vigil_error_t error = {0};
    size_t before;
    size_t after;

    vigil_native_registry_init(&natives);
    before = natives.module_count;
    EXPECT_EQ(vigil_plugin_register_all(&natives, &error), VIGIL_STATUS_OK);
    after = natives.module_count;
    EXPECT_EQ(after - before, (size_t)VIGIL_PLUGIN_COUNT);

    vigil_native_registry_free(&natives);
}

TEST(PluginRegistry, PluginIsKnownModule)
{
#if VIGIL_PLUGIN_COUNT > 0
    EXPECT_EQ(vigil_plugin_is_known_module("test_plugin", 11U), 1);
#endif
    EXPECT_EQ(vigil_plugin_is_known_module("nonexistent", 11U), 0);
    EXPECT_EQ(vigil_plugin_is_known_module("math", 4U), 0);
}

/* ── end-to-end VM tests ─────────────────────────────────────────── */

#if VIGIL_PLUGIN_COUNT > 0

TEST(PluginRegistry, PluginFunctionCallViaVM)
{
    int64_t result = RunWithPlugins(vigil_test_failed_, "import \"test_plugin\";\n"
                                                        "fn main() -> i32 {\n"
                                                        "    i32 x = test_plugin.add(2, 3);\n"
                                                        "    return x;\n"
                                                        "}\n");
    EXPECT_EQ(result, 5);
}

TEST(PluginRegistry, PluginNegate)
{
    int64_t result = RunWithPlugins(vigil_test_failed_, "import \"test_plugin\";\n"
                                                        "fn main() -> i32 {\n"
                                                        "    i32 x = test_plugin.negate(42);\n"
                                                        "    return x;\n"
                                                        "}\n");
    EXPECT_EQ(result, -42);
}

TEST(PluginRegistry, PluginStringReturn)
{
    int64_t result = RunWithPlugins(vigil_test_failed_, "import \"test_plugin\";\n"
                                                        "fn main() -> i32 {\n"
                                                        "    if (test_plugin.greet() == \"hello from plugin\") {\n"
                                                        "        return 1;\n"
                                                        "    }\n"
                                                        "    return 0;\n"
                                                        "}\n");
    EXPECT_EQ(result, 1);
}

TEST(PluginRegistry, PluginCoexistsWithStdlib)
{
    int64_t result = RunWithPlugins(vigil_test_failed_, "import \"test_plugin\";\n"
                                                        "import \"math\";\n"
                                                        "fn main() -> i32 {\n"
                                                        "    i32 sum = test_plugin.add(10, 20);\n"
                                                        "    f64 pi = math.pi();\n"
                                                        "    if (pi > 3.0 && sum == 30) {\n"
                                                        "        return 1;\n"
                                                        "    }\n"
                                                        "    return 0;\n"
                                                        "}\n");
    EXPECT_EQ(result, 1);
}

#endif /* VIGIL_PLUGIN_COUNT > 0 */

/* ── registration ────────────────────────────────────────────────── */

void register_plugin_tests(void)
{
    REGISTER_TEST(PluginRegistry, RegisterAndFind);
    REGISTER_TEST(PluginRegistry, PluginCountMatchesTable);
    REGISTER_TEST(PluginRegistry, PluginIsKnownModule);
#if VIGIL_PLUGIN_COUNT > 0
    REGISTER_TEST(PluginRegistry, PluginFunctionCallViaVM);
    REGISTER_TEST(PluginRegistry, PluginNegate);
    REGISTER_TEST(PluginRegistry, PluginStringReturn);
    REGISTER_TEST(PluginRegistry, PluginCoexistsWithStdlib);
#endif
}
