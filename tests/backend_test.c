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

typedef struct
{
    vigil_runtime_t *runtime;
    vigil_vm_t *vm;
    vigil_error_t error;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_program_state_t program;
} backend_fixture_t;

static void FreeBackendFixtures(backend_fixture_t *fixture)
{
    vigil_program_free(&fixture->program);
    vigil_diagnostic_list_free(&fixture->diagnostics);
    vigil_source_registry_free(&fixture->registry);
    vigil_vm_close(&fixture->vm);
    vigil_runtime_close(&fixture->runtime);
}

static void InitBackendFixtures(int *vigil_test_failed_, backend_fixture_t *fixture, const char *source_text)
{
    vigil_source_id_t source_id;

    memset(fixture, 0, sizeof(*fixture));
    ASSERT_EQ(vigil_runtime_open(&fixture->runtime, NULL, &fixture->error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&fixture->vm, fixture->runtime, NULL, &fixture->error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&fixture->registry, fixture->runtime);
    vigil_diagnostic_list_init(&fixture->diagnostics, fixture->runtime);
    source_id =
        RegisterBackendSource(vigil_test_failed_, &fixture->registry, "main.vigil", source_text, &fixture->error);
    ASSERT_EQ(vigil_semantic_prepare_source_internal(&fixture->registry, source_id, VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT,
                                                     NULL, &fixture->diagnostics, &fixture->program, &fixture->error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_diagnostic_list_count(&fixture->diagnostics), 0U);
}

static void MaterializeMainFunction(int *vigil_test_failed_, backend_fixture_t *fixture,
                                    vigil_lowered_function_body_t *body, vigil_object_t **out_function)
{
    const vigil_function_decl_t *decl;

    decl = vigil_binding_function_table_get(&fixture->program.functions, fixture->program.functions.main_index);
    ASSERT_NE(decl, NULL);

    vigil_lowered_function_body_init(body);
    ASSERT_EQ(vigil_semantic_lower_function_body(&fixture->program, fixture->program.functions.main_index, NULL, body),
              VIGIL_STATUS_OK);
    *out_function = NULL;
    ASSERT_EQ(vigil_compile_materialize_lowered_function_body(&fixture->program, decl, body, out_function),
              VIGIL_STATUS_OK);
    ASSERT_NE(*out_function, NULL);
}

static void ExpectChunkContains(int *vigil_test_failed_, vigil_runtime_t *runtime, const vigil_chunk_t *chunk,
                                vigil_error_t *error, const char *needle)
{
    vigil_string_t output;

    vigil_string_init(&output, runtime);
    ASSERT_EQ(vigil_chunk_disassemble(chunk, &output, error), VIGIL_STATUS_OK);
    EXPECT_TRUE(strstr(vigil_string_c_str(&output), needle) != NULL);
    vigil_string_free(&output);
}

TEST(CompilerBackendTest, MaterializesLoweredMainFunctionBody)
{
    static const char source[] = "fn main() -> i32 {"
                                 "    i32 total = 2;"
                                 "    return total + 5;"
                                 "}";
    backend_fixture_t fixture;
    vigil_lowered_function_body_t body;
    vigil_object_t *function = NULL;
    vigil_value_t result;

    InitBackendFixtures(vigil_test_failed_, &fixture, source);
    MaterializeMainFunction(vigil_test_failed_, &fixture, &body, &function);
    ASSERT_NE(function, NULL);
    EXPECT_STREQ(vigil_function_object_name(function), "main");
    EXPECT_EQ(vigil_function_object_arity(function), 0U);
    EXPECT_EQ(vigil_function_object_return_count(function), 1U);
    EXPECT_NE(vigil_function_object_chunk(function), NULL);

    vigil_value_init_nil(&result);
    ASSERT_EQ(vigil_vm_execute_function(fixture.vm, function, &result, &fixture.error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 7);

    vigil_value_release(&result);
    vigil_object_release(&function);
    FreeBackendFixtures(&fixture);
}

TEST(CompilerBackendTest, MaterializesLoweredConditionalBytecodeShape)
{
    static const char source[] = "fn main() -> i32 {"
                                 "    if (1 < 2) {"
                                 "        return 7;"
                                 "    }"
                                 "    return 4;"
                                 "}";
    backend_fixture_t fixture;
    vigil_lowered_function_body_t body;
    vigil_object_t *function;
    vigil_value_t result;

    InitBackendFixtures(vigil_test_failed_, &fixture, source);
    MaterializeMainFunction(vigil_test_failed_, &fixture, &body, &function);

    ExpectChunkContains(vigil_test_failed_, fixture.runtime, vigil_function_object_chunk(function), &fixture.error,
                        "JUMP_IF_FALSE");
    ExpectChunkContains(vigil_test_failed_, fixture.runtime, vigil_function_object_chunk(function), &fixture.error,
                        "JUMP ");

    vigil_value_init_nil(&result);
    ASSERT_EQ(vigil_vm_execute_function(fixture.vm, function, &result, &fixture.error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 7);

    vigil_value_release(&result);
    vigil_object_release(&function);
    FreeBackendFixtures(&fixture);
}

TEST(CompilerBackendTest, MaterializesLoweredForLoopProgram)
{
    static const char source[] = "fn main() -> i32 {"
                                 "    i32 sum = 0;"
                                 "    for (i32 i = 0; i < 5; i++) {"
                                 "        sum += i;"
                                 "    }"
                                 "    return sum;"
                                 "}";
    backend_fixture_t fixture;
    vigil_lowered_function_body_t body;
    vigil_object_t *function;
    vigil_value_t result;

    InitBackendFixtures(vigil_test_failed_, &fixture, source);
    MaterializeMainFunction(vigil_test_failed_, &fixture, &body, &function);

    vigil_value_init_nil(&result);
    ASSERT_EQ(vigil_vm_execute_function(fixture.vm, function, &result, &fixture.error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 10);

    vigil_value_release(&result);
    vigil_object_release(&function);
    FreeBackendFixtures(&fixture);
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
    backend_fixture_t fixture;
    const vigil_class_decl_t *class_decl;
    vigil_function_decl_t *constructor_decl;
    int handled = 0;

    InitBackendFixtures(vigil_test_failed_, &fixture, source);

    ASSERT_EQ(fixture.program.class_count, 1U);
    class_decl = &fixture.program.classes[0];
    ASSERT_NE(class_decl->constructor_function_index, SIZE_MAX);
    constructor_decl =
        vigil_binding_function_table_get_mutable(&fixture.program.functions, class_decl->constructor_function_index);
    ASSERT_NE(constructor_decl, NULL);
    EXPECT_EQ(constructor_decl->object, NULL);

    ASSERT_EQ(vigil_compile_backend_try_materialize_special_function(&fixture.program,
                                                                     class_decl->constructor_function_index, &handled),
              VIGIL_STATUS_OK);
    EXPECT_EQ(handled, 1);
    ASSERT_NE(constructor_decl->object, NULL);
    EXPECT_STREQ(vigil_function_object_name(constructor_decl->object), "Counter");
    EXPECT_EQ(vigil_function_object_arity(constructor_decl->object), 1U);
    EXPECT_EQ(vigil_function_object_return_count(constructor_decl->object), 1U);
    EXPECT_NE(vigil_function_object_chunk(constructor_decl->object), NULL);

    FreeBackendFixtures(&fixture);
}

TEST(CompilerBackendTest, MaterializesExternWrapperFunction)
{
    static const char source[] = "extern fn add(i32 a, i32 b) -> i32 from \"libm\";"
                                 "fn main() -> i32 {"
                                 "    return 0;"
                                 "}";
    backend_fixture_t fixture;
    const vigil_extern_fn_decl_t *extern_decl;
    vigil_function_decl_t *wrapper_decl;
    int handled = 0;

    InitBackendFixtures(vigil_test_failed_, &fixture, source);

    ASSERT_EQ(fixture.program.extern_fn_count, 1U);
    extern_decl = &fixture.program.extern_fns[0];
    wrapper_decl = vigil_binding_function_table_get_mutable(&fixture.program.functions, extern_decl->function_index);
    ASSERT_NE(wrapper_decl, NULL);
    EXPECT_EQ(wrapper_decl->object, NULL);

    ASSERT_EQ(
        vigil_compile_backend_try_materialize_special_function(&fixture.program, extern_decl->function_index, &handled),
        VIGIL_STATUS_OK);
    EXPECT_EQ(handled, 1);
    ASSERT_NE(wrapper_decl->object, NULL);
    ExpectChunkContains(vigil_test_failed_, fixture.runtime, vigil_function_object_chunk(wrapper_decl->object),
                        &fixture.error, "CALL_EXTERN");

    FreeBackendFixtures(&fixture);
}

void register_backend_tests(void)
{
    REGISTER_TEST(CompilerBackendTest, MaterializesLoweredMainFunctionBody);
    REGISTER_TEST(CompilerBackendTest, MaterializesLoweredConditionalBytecodeShape);
    REGISTER_TEST(CompilerBackendTest, MaterializesLoweredForLoopProgram);
    REGISTER_TEST(CompilerBackendTest, MaterializesSyntheticConstructorFunction);
    REGISTER_TEST(CompilerBackendTest, MaterializesExternWrapperFunction);
}
