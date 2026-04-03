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

static const vigil_native_module_function_t *FindModuleFunction(const vigil_native_module_t *module, const char *name)
{
    size_t i;
    if (module == NULL || name == NULL)
        return NULL;
    for (i = 0; i < module->function_count; i++)
    {
        const vigil_native_module_function_t *function = &module->functions[i];
        if (strcmp(function->name, name) == 0)
            return function;
    }
    return NULL;
}

static const vigil_native_class_t *FindModuleClass(const vigil_native_module_t *module, const char *name)
{
    size_t i;

    if (module == NULL || name == NULL)
        return NULL;
    for (i = 0; i < module->class_count; i++)
    {
        const vigil_native_class_t *cls = &module->classes[i];
        if (strcmp(cls->name, name) == 0)
            return cls;
    }
    return NULL;
}

static const vigil_native_class_method_t *FindClassMethod(const vigil_native_class_t *cls, const char *name)
{
    size_t i;

    if (cls == NULL || name == NULL)
        return NULL;
    for (i = 0; i < cls->method_count; i++)
    {
        const vigil_native_class_method_t *method = &cls->methods[i];
        if (strcmp(method->name, name) == 0)
            return method;
    }
    return NULL;
}

static void FailNotFound(int *vigil_test_failed_, const char *kind, const char *name)
{
    fprintf(stderr, "  %s:%d: Failure\n    Missing %s: %s\n", __FILE__, __LINE__, kind, name);
    *vigil_test_failed_ = 1;
}

static int InitSdlRegistry(int *vigil_test_failed_, vigil_native_registry_t *natives, vigil_error_t *error,
                           const vigil_native_module_t **mod)
{
    if (!vigil_plugin_is_known_module("sdl", 3U))
        return 0;

    vigil_native_registry_init(natives);
    EXPECT_EQ(vigil_plugin_register_all(natives, error), VIGIL_STATUS_OK);

    *mod = vigil_native_registry_find(natives, "sdl", 3U);
    EXPECT_NE(*mod, NULL);
    return 1;
}

static void ExpectModuleFunctionsPresent(int *vigil_test_failed_, const vigil_native_module_t *module,
                                         const char *const *names, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (FindModuleFunction(module, names[i]) == NULL)
            FailNotFound(vigil_test_failed_, "module function", names[i]);
    }
}

static void ExpectClassMethodsPresent(int *vigil_test_failed_, const vigil_native_module_t *module,
                                      const char *class_name, const char *const *names, size_t count)
{
    const vigil_native_class_t *cls = FindModuleClass(module, class_name);
    size_t i;

    if (cls == NULL)
    {
        FailNotFound(vigil_test_failed_, "class", class_name);
        return;
    }

    for (i = 0; i < count; i++)
    {
        if (FindClassMethod(cls, names[i]) == NULL)
            FailNotFound(vigil_test_failed_, "class method", names[i]);
    }
}

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
                                                        "    if test_plugin.greet() == \"hello from plugin\" {\n"
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
                                                        "    if pi > 3.0 && sum == 30 {\n"
                                                        "        return 1;\n"
                                                        "    }\n"
                                                        "    return 0;\n"
                                                        "}\n");
    EXPECT_EQ(result, 1);
}

TEST(PluginRegistry, PluginConstantResolvesWithoutCall)
{
    int64_t result = RunWithPlugins(vigil_test_failed_, "import \"test_plugin\";\n"
                                                        "fn main() -> i32 {\n"
                                                        "    i32 val = test_plugin.ANSWER;\n"
                                                        "    return val;\n"
                                                        "}\n");
    EXPECT_EQ(result, 42);
}

TEST(PluginRegistry, SdlExportsParityBatchOneFunctions)
{
    vigil_native_registry_t natives;
    vigil_error_t error = {0};
    const vigil_native_module_t *mod;
    static const char *const module_functions[] = {"create_window_with_properties", "create_renderer_with_properties",
                                                   "get_gamepad_mapping_for_guid", "get_gamepad_mappings",
                                                   "get_joystick_guid_info"};

    if (!InitSdlRegistry(vigil_test_failed_, &natives, &error, &mod))
        return;
    ExpectModuleFunctionsPresent(vigil_test_failed_, mod, module_functions,
                                 sizeof(module_functions) / sizeof(module_functions[0]));

    vigil_native_registry_free(&natives);
}

TEST(PluginRegistry, SdlExportsEnvironmentBatchFunctions)
{
    vigil_native_registry_t natives;
    vigil_error_t error = {0};
    const vigil_native_module_t *mod;
    static const char *const module_functions[] = {"create_environment",          "destroy_environment",
                                                   "get_environment_variables",   "get_environment_variable_from",
                                                   "set_environment_variable_in", "unset_environment_variable_in"};

    if (!InitSdlRegistry(vigil_test_failed_, &natives, &error, &mod))
        return;
    ExpectModuleFunctionsPresent(vigil_test_failed_, mod, module_functions,
                                 sizeof(module_functions) / sizeof(module_functions[0]));

    vigil_native_registry_free(&natives);
}

TEST(PluginRegistry, SdlExportsWindowAndMessageBoxBatchFunctions)
{
    vigil_native_registry_t natives;
    vigil_error_t error = {0};
    const vigil_native_module_t *mod;
    static const char *const module_functions[] = {"show_message_box"};
    static const char *const window_methods[] = {"get_fullscreen_mode", "update_surface_rect", "show_message_box"};

    if (!InitSdlRegistry(vigil_test_failed_, &natives, &error, &mod))
        return;
    ExpectModuleFunctionsPresent(vigil_test_failed_, mod, module_functions,
                                 sizeof(module_functions) / sizeof(module_functions[0]));
    ExpectClassMethodsPresent(vigil_test_failed_, mod, "Window", window_methods,
                              sizeof(window_methods) / sizeof(window_methods[0]));

    vigil_native_registry_free(&natives);
}

TEST(PluginRegistry, SdlExportsCameraClipboardAndBindingsBatchFunctions)
{
    vigil_native_registry_t natives;
    vigil_error_t error = {0};
    const vigil_native_module_t *mod;
    static const char *const module_functions[] = {"get_camera_supported_formats", "get_cameras", "get_gamepads",
                                                   "get_clipboard_data", "set_clipboard_data"};
    static const char *const window_methods[] = {"update_surface_rects"};
    static const char *const gamepad_methods[] = {"get_bindings", "get_guid", "get_power_info"};
    static const char *const camera_methods[] = {"get_spec"};

    if (!InitSdlRegistry(vigil_test_failed_, &natives, &error, &mod))
        return;
    ExpectModuleFunctionsPresent(vigil_test_failed_, mod, module_functions,
                                 sizeof(module_functions) / sizeof(module_functions[0]));
    ExpectClassMethodsPresent(vigil_test_failed_, mod, "Window", window_methods,
                              sizeof(window_methods) / sizeof(window_methods[0]));
    ExpectClassMethodsPresent(vigil_test_failed_, mod, "Gamepad", gamepad_methods,
                              sizeof(gamepad_methods) / sizeof(gamepad_methods[0]));
    ExpectClassMethodsPresent(vigil_test_failed_, mod, "Camera", camera_methods,
                              sizeof(camera_methods) / sizeof(camera_methods[0]));

    vigil_native_registry_free(&natives);
}

TEST(PluginRegistry, SdlGuidHelpersViaVM)
{
    int64_t result;

    if (!vigil_plugin_is_known_module("sdl", 3U))
        return;

    result = RunWithPlugins(vigil_test_failed_,
                            "import \"sdl\";\n"
                            "fn main() -> i32 {\n"
                            "    string guid = sdl.string_to_guid(\"00000000000000000000000000000000\");\n"
                            "    string mapping = sdl.get_gamepad_mapping_for_guid(guid);\n"
                            "    string mappings = sdl.get_gamepad_mappings();\n"
                            "    i32 vendor, i32 product, i32 version, i32 crc = sdl.get_joystick_guid_info(guid);\n"
                            "    if mapping != \"\" {\n"
                            "        return 1;\n"
                            "    }\n"
                            "    if vendor != 0 || product != 0 || version != 0 || crc != 0 {\n"
                            "        return 2;\n"
                            "    }\n"
                            "    if mappings.len() < 0 {\n"
                            "        return 3;\n"
                            "    }\n"
                            "    return 0;\n"
                            "}\n");
    EXPECT_EQ(result, 0);
}

TEST(PluginRegistry, SdlEnvironmentHelpersViaVM)
{
    int64_t result;

    if (!vigil_plugin_is_known_module("sdl", 3U))
        return;

    result =
        RunWithPlugins(vigil_test_failed_,
                       "import \"sdl\";\n"
                       "fn main() -> i32 {\n"
                       "    i64 env, err e = sdl.create_environment(0);\n"
                       "    if e != ok {\n"
                       "        return 1;\n"
                       "    }\n"
                       "    bool success, err set_err = sdl.set_environment_variable_in(env, \"ALPHA\", \"beta\", 1);\n"
                       "    if success == false || set_err != ok {\n"
                       "        return 2;\n"
                       "    }\n"
                       "    string value = sdl.get_environment_variable_from(env, \"ALPHA\");\n"
                       "    if value != \"beta\" {\n"
                       "        return 3;\n"
                       "    }\n"
                       "    string vars = sdl.get_environment_variables(env);\n"
                       "    if vars.contains(\"ALPHA=beta\") == false {\n"
                       "        return 4;\n"
                       "    }\n"
                       "    bool unset_success, err unset_err = sdl.unset_environment_variable_in(env, \"ALPHA\");\n"
                       "    if unset_success == false || unset_err != ok {\n"
                       "        return 5;\n"
                       "    }\n"
                       "    if sdl.get_environment_variable_from(env, \"ALPHA\") != \"\" {\n"
                       "        return 6;\n"
                       "    }\n"
                       "    sdl.destroy_environment(env);\n"
                       "    return 0;\n"
                       "}\n");
    EXPECT_EQ(result, 0);
}

TEST(PluginRegistry, SdlCameraFormatHelpersViaVM)
{
    int64_t result;

    if (!vigil_plugin_is_known_module("sdl", 3U))
        return;

    result = RunWithPlugins(vigil_test_failed_, "import \"sdl\";\n"
                                                "fn main() -> i32 {\n"
                                                "    string specs = sdl.get_camera_supported_formats(0);\n"
                                                "    if specs.len() < 0 {\n"
                                                "        return 1;\n"
                                                "    }\n"
                                                "    return 0;\n"
                                                "}\n");
    EXPECT_EQ(result, 0);
}

TEST(PluginRegistry, SdlDeviceListingHelpersViaVM)
{
    int64_t result;

    if (!vigil_plugin_is_known_module("sdl", 3U))
        return;

    result = RunWithPlugins(vigil_test_failed_, "import \"sdl\";\n"
                                                "fn main() -> i32 {\n"
                                                "    string cameras = sdl.get_cameras();\n"
                                                "    string pads = sdl.get_gamepads();\n"
                                                "    string mimes = sdl.get_clipboard_mime_types();\n"
                                                "    if cameras.len() < 0 {\n"
                                                "        return 1;\n"
                                                "    }\n"
                                                "    if pads.len() < 0 {\n"
                                                "        return 2;\n"
                                                "    }\n"
                                                "    if mimes.len() < 0 {\n"
                                                "        return 3;\n"
                                                "    }\n"
                                                "    return 0;\n"
                                                "}\n");
    EXPECT_EQ(result, 0);
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
    REGISTER_TEST(PluginRegistry, PluginConstantResolvesWithoutCall);
    REGISTER_TEST(PluginRegistry, SdlExportsParityBatchOneFunctions);
    REGISTER_TEST(PluginRegistry, SdlExportsEnvironmentBatchFunctions);
    REGISTER_TEST(PluginRegistry, SdlExportsWindowAndMessageBoxBatchFunctions);
    REGISTER_TEST(PluginRegistry, SdlExportsCameraClipboardAndBindingsBatchFunctions);
    REGISTER_TEST(PluginRegistry, SdlGuidHelpersViaVM);
    REGISTER_TEST(PluginRegistry, SdlEnvironmentHelpersViaVM);
    REGISTER_TEST(PluginRegistry, SdlCameraFormatHelpersViaVM);
    REGISTER_TEST(PluginRegistry, SdlDeviceListingHelpersViaVM);
#endif
}
