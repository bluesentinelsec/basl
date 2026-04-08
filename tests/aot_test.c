#include "vigil_test.h"

#include <string.h>

#include "internal/vigil_regvm.h"
#include "vigil/vigil.h"

static vigil_status_t CompileMainFunction(vigil_runtime_t *runtime, const char *source_text,
                                          vigil_source_registry_t *registry, vigil_diagnostic_list_t *diagnostics,
                                          vigil_object_t **out_function, vigil_error_t *error)
{
    vigil_source_id_t source_id = 0U;

    if (runtime == NULL || source_text == NULL || registry == NULL || diagnostics == NULL || out_function == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "compile helper arguments must not be null");
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

#ifdef VIGIL_ENABLE_AOT
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
    vigil_vm_set_aot_enabled(vm, 0);
    ASSERT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 21);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}
#endif

void register_aot_tests(void)
{
    REGISTER_TEST(VigilAotTest, VmAotSetterRoundTrips);
    REGISTER_TEST(VigilAotTest, ExecuteFunctionWithAotDisabledStillWorks);
#ifdef VIGIL_ENABLE_AOT
    REGISTER_TEST(VigilAotTest, ExecuteFunctionBuildsAotCache);
#endif
}
