#include "vigil_test.h"

#include <string.h>

#include "internal/vigil_binding.h"
#include "internal/vigil_compiler_backend.h"
#include "internal/vigil_compiler_internal.h"
#include "internal/vigil_compiler_semantics.h"
#include "vigil/vigil.h"

static vigil_source_id_t RegisterBackendSource(int *vigil_test_failed_, vigil_source_registry_t *registry,
                                               const char *path, const char *text, vigil_error_t *error)
{
    vigil_source_id_t source_id = 0U;

    EXPECT_EQ(vigil_source_registry_register_cstr(registry, path, text, &source_id, error), VIGIL_STATUS_OK);
    return source_id;
}

static void FreeBackendFixtures(vigil_runtime_t **runtime, vigil_vm_t **vm, vigil_program_state_t *program,
                                vigil_source_registry_t *registry, vigil_diagnostic_list_t *diagnostics)
{
    vigil_program_free(program);
    vigil_diagnostic_list_free(diagnostics);
    vigil_source_registry_free(registry);
    vigil_vm_close(vm);
    vigil_runtime_close(runtime);
}

static void InitBackendFixtures(int *vigil_test_failed_, vigil_runtime_t **runtime, vigil_vm_t **vm,
                                vigil_source_registry_t *registry, vigil_diagnostic_list_t *diagnostics,
                                vigil_program_state_t *program, const char *source_text, vigil_error_t *error)
{
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(runtime, NULL, error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(vm, *runtime, NULL, error), VIGIL_STATUS_OK);
    vigil_source_registry_init(registry, *runtime);
    vigil_diagnostic_list_init(diagnostics, *runtime);
    source_id = RegisterBackendSource(vigil_test_failed_, registry, "main.vigil", source_text, error);
    ASSERT_EQ(vigil_semantic_prepare_source_internal(registry, source_id, VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT, NULL,
                                                     diagnostics, program, error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_diagnostic_list_count(diagnostics), 0U);
}

TEST(CompilerBackendTest, MaterializesLoweredMainFunctionBody)
{
    static const char source[] = "fn main() -> i32 {"
                                 "    i32 total = 2;"
                                 "    return total + 5;"
                                 "}";
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_program_state_t program;
    vigil_lowered_function_body_t body;
    const vigil_function_decl_t *decl;
    vigil_object_t *function = NULL;
    vigil_value_t result;

    InitBackendFixtures(vigil_test_failed_, &runtime, &vm, &registry, &diagnostics, &program, source, &error);

    decl = vigil_binding_function_table_get(&program.functions, program.functions.main_index);
    ASSERT_NE(decl, NULL);

    vigil_lowered_function_body_init(&body);
    ASSERT_EQ(vigil_semantic_lower_function_body(&program, program.functions.main_index, NULL, &body), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_compile_materialize_lowered_function_body(&program, decl, &body, &function), VIGIL_STATUS_OK);
    ASSERT_NE(function, NULL);
    EXPECT_STREQ(vigil_function_object_name(function), "main");
    EXPECT_EQ(vigil_function_object_arity(function), 0U);
    EXPECT_EQ(vigil_function_object_return_count(function), 1U);
    EXPECT_NE(vigil_function_object_chunk(function), NULL);

    vigil_value_init_nil(&result);
    ASSERT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 7);

    vigil_value_release(&result);
    vigil_object_release(&function);
    FreeBackendFixtures(&runtime, &vm, &program, &registry, &diagnostics);
}

TEST(CompilerBackendTest, MaterializesSyntheticConstructorFunction)
{
    static const char source[] = "class Counter {"
                                 "    i32 value;"
                                 "    fn init(i32 value) -> void {"
                                 "        self.value = value + 1;"
                                 "    }"
                                 "}"
                                 "fn main() -> i32 {"
                                 "    Counter counter = Counter(6);"
                                 "    return counter.value;"
                                 "}";
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_program_state_t program;
    const vigil_class_decl_t *class_decl;
    vigil_function_decl_t *constructor_decl;
    int handled = 0;

    InitBackendFixtures(vigil_test_failed_, &runtime, &vm, &registry, &diagnostics, &program, source, &error);

    ASSERT_EQ(program.class_count, 1U);
    class_decl = &program.classes[0];
    ASSERT_NE(class_decl->constructor_function_index, SIZE_MAX);
    constructor_decl =
        vigil_binding_function_table_get_mutable(&program.functions, class_decl->constructor_function_index);
    ASSERT_NE(constructor_decl, NULL);
    EXPECT_EQ(constructor_decl->object, NULL);

    ASSERT_EQ(vigil_compile_backend_try_materialize_special_function(&program, class_decl->constructor_function_index,
                                                                     &handled),
              VIGIL_STATUS_OK);
    EXPECT_EQ(handled, 1);
    ASSERT_NE(constructor_decl->object, NULL);
    EXPECT_STREQ(vigil_function_object_name(constructor_decl->object), "Counter");
    EXPECT_EQ(vigil_function_object_arity(constructor_decl->object), 1U);
    EXPECT_EQ(vigil_function_object_return_count(constructor_decl->object), 1U);
    EXPECT_NE(vigil_function_object_chunk(constructor_decl->object), NULL);

    FreeBackendFixtures(&runtime, &vm, &program, &registry, &diagnostics);
}

void register_backend_tests(void)
{
    REGISTER_TEST(CompilerBackendTest, MaterializesLoweredMainFunctionBody);
    REGISTER_TEST(CompilerBackendTest, MaterializesSyntheticConstructorFunction);
}
