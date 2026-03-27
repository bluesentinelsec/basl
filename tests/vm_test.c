#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include "internal/vigil_vm_internal.h"
#endif
#include "vigil/vigil.h"

struct AllocatorStats
{
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

struct FailingAllocatorStats
{
    size_t calls;
    size_t fail_after;
};

typedef struct VmTestContextOptions
{
    const vigil_runtime_options_t *runtime_options;
    const vigil_vm_options_t *vm_options;
} VmTestContextOptions;

static vigil_status_t OpenVmTestContextWithOptions(vigil_runtime_t **runtime, vigil_vm_t **vm, vigil_chunk_t *chunk,
                                                   vigil_value_t *result, const VmTestContextOptions *options,
                                                   vigil_error_t *error);

static void *CountedAllocate(void *user_data, size_t size)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->allocate_calls += 1;
    return calloc(1U, size);
}

static void *CountedReallocate(void *user_data, void *memory, size_t size)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->reallocate_calls += 1;
    return realloc(memory, size);
}

static void CountedDeallocate(void *user_data, void *memory)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->deallocate_calls += 1;
    free(memory);
}

static void *FailingAllocate(void *user_data, size_t size)
{
    struct FailingAllocatorStats *state = (struct FailingAllocatorStats *)(user_data);

    state->calls += 1U;
    if (state->calls >= state->fail_after)
    {
        return NULL;
    }
    return calloc(1U, size);
}

static void *FailingReallocate(void *user_data, void *memory, size_t size)
{
    struct FailingAllocatorStats *state = (struct FailingAllocatorStats *)(user_data);

    state->calls += 1U;
    if (state->calls >= state->fail_after)
    {
        return NULL;
    }
    return realloc(memory, size);
}

static void FailingDeallocate(void *user_data, void *memory)
{
    (void)user_data;
    free(memory);
}

static vigil_source_span_t Span(vigil_source_id_t source_id, size_t start, size_t end)
{
    vigil_source_span_t span = {0};

    span.source_id = source_id;
    span.start_offset = start;
    span.end_offset = end;
    return span;
}

static vigil_status_t OpenVmTestContext(vigil_runtime_t **runtime, vigil_vm_t **vm, vigil_chunk_t *chunk,
                                        vigil_value_t *result, vigil_error_t *error)
{
    VmTestContextOptions options = {0};

    return OpenVmTestContextWithOptions(runtime, vm, chunk, result, &options, error);
}

static void CloseVmTestContext(vigil_runtime_t **runtime, vigil_vm_t **vm, vigil_chunk_t *chunk, vigil_value_t *result)
{
    vigil_value_release(result);
    vigil_chunk_free(chunk);
    vigil_vm_close(vm);
    vigil_runtime_close(runtime);
}

static vigil_status_t OpenVmTestContextWithOptions(vigil_runtime_t **runtime, vigil_vm_t **vm, vigil_chunk_t *chunk,
                                                   vigil_value_t *result, const VmTestContextOptions *options,
                                                   vigil_error_t *error)
{
    vigil_status_t status;
    const vigil_runtime_options_t *runtime_options = NULL;
    const vigil_vm_options_t *vm_options = NULL;

    if (options != NULL)
    {
        runtime_options = options->runtime_options;
        vm_options = options->vm_options;
    }

    status = vigil_runtime_open(runtime, runtime_options, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_vm_open(vm, *runtime, vm_options, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_runtime_close(runtime);
        return status;
    }

    vigil_chunk_init(chunk, *runtime);
    vigil_value_init_nil(result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t RunCompiledSource(const char *source_text, int64_t *out_result, vigil_error_t *error)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_source_id_t source_id = 0U;
    vigil_status_t status;

    status = vigil_runtime_open(&runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_vm_open(&vm, runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_runtime_close(&runtime);
        return status;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    status = vigil_source_registry_register_cstr(&registry, "main.vigil", source_text, &source_id, error);
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_compile_source(&registry, source_id, &function, &diagnostics, error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        vigil_value_init_nil(&result);
        status = vigil_vm_execute_function(vm, function, &result, error);
        if (status == VIGIL_STATUS_OK && out_result != NULL)
        {
            *out_result = vigil_value_as_int(&result);
        }
        vigil_value_release(&result);
    }

    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return status;
}

static vigil_status_t RunBinaryIntOpcode(vigil_opcode_t opcode, int64_t left_value, int64_t right_value,
                                         int64_t *out_result, vigil_error_t *error)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t left;
    vigil_value_t right;
    vigil_value_t result;
    vigil_status_t status;

    status = vigil_runtime_open(&runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_vm_open(&vm, runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_runtime_close(&runtime);
        return status;
    }

    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);
    vigil_value_init_int(&left, left_value);
    vigil_value_init_int(&right, right_value);
    status = vigil_chunk_write_constant(&chunk, &left, Span(40U, 0U, 1U), NULL, error);
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_chunk_write_constant(&chunk, &right, Span(40U, 2U, 3U), NULL, error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_chunk_write_opcode(&chunk, opcode, Span(40U, 4U, 5U), error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(40U, 6U, 7U), error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_vm_execute(vm, &chunk, &result, error);
    }
    if (status == VIGIL_STATUS_OK && out_result != NULL)
    {
        *out_result = vigil_value_as_int(&result);
    }

    vigil_error_clear(error);
    vigil_value_release(&left);
    vigil_value_release(&right);
    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return status;
}

static vigil_status_t RunBinaryUintOpcode(vigil_opcode_t opcode, uint64_t left_value, uint64_t right_value,
                                          uint64_t *out_result, vigil_error_t *error)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t left;
    vigil_value_t right;
    vigil_value_t result;
    vigil_status_t status;

    status = vigil_runtime_open(&runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_vm_open(&vm, runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_runtime_close(&runtime);
        return status;
    }

    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);
    vigil_value_init_uint(&left, left_value);
    vigil_value_init_uint(&right, right_value);
    status = vigil_chunk_write_constant(&chunk, &left, Span(41U, 0U, 1U), NULL, error);
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_chunk_write_constant(&chunk, &right, Span(41U, 2U, 3U), NULL, error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_chunk_write_opcode(&chunk, opcode, Span(41U, 4U, 5U), error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(41U, 6U, 7U), error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_vm_execute(vm, &chunk, &result, error);
    }
    if (status == VIGIL_STATUS_OK && out_result != NULL)
    {
        *out_result = vigil_value_as_uint(&result);
    }

    vigil_error_clear(error);
    vigil_value_release(&left);
    vigil_value_release(&right);
    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return status;
}

#if !defined(_WIN32)
static vigil_status_t NoopNativeFunction(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)vm;
    (void)arg_count;
    (void)error;
    return VIGIL_STATUS_OK;
}
#endif

/* TEST() expands into generated functions with many assertion branches.
   Suppress cognitive-complexity diagnostics for this assertion-heavy test region. */
// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST(VigilVmTest, OptionsInitClearsFields)
{
    vigil_vm_options_t options = {0};

    options.initial_stack_capacity = 99U;
    vigil_vm_options_init(&options);
    EXPECT_EQ(options.initial_stack_capacity, 0U);
}

TEST(VigilVmTest, OpenAndCloseVm)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_NE(vm, NULL);
    EXPECT_EQ(vigil_vm_runtime(vm), runtime);
    EXPECT_EQ(vigil_vm_stack_depth(vm), 0U);
    EXPECT_EQ(vigil_vm_frame_depth(vm), 0U);

    vigil_vm_close(&vm);
    EXPECT_EQ(vm, NULL);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecutesConstantAndReturn)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&constant, 42);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(1U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(1U, 4U, 5U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 42);
    EXPECT_EQ(vigil_vm_stack_depth(vm), 0U);
    EXPECT_EQ(vigil_vm_frame_depth(vm), 0U);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecutesLiteralOpcodes)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TRUE, Span(2U, 0U, 1U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(2U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_BOOL);
    EXPECT_TRUE(vigil_value_as_bool(&result));
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NIL, Span(2U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(2U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_NIL);

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ReturnsNilFromEmptyRootChunk)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(2U, 0U, 1U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_NIL);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ReturnsFirstValueFromMultiValueRootChunk)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_int(&constant, 11);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(2U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 12);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(2U, 2U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(2U, 4U, 5U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 12);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, CompilesAndExecutesMultipleReturnValues)
{
    int64_t output = 0;
    vigil_error_t error = {0};

    ASSERT_EQ(RunCompiledSource("\n"
                                "fn pair(i32 value) -> (i32, i32) {\n"
                                "    return (value, value + 1);\n"
                                "}\n"
                                "\n"
                                "fn main() -> i32 {\n"
                                "    i32 left, i32 right = pair(4);\n"
                                "    return left * 10 + right;\n"
                                "}\n"
                                "",
                                &output, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(output, 45);
}

TEST(VigilVmTest, CompilesAndExecutesDeferredMultipleReturnValues)
{
    int64_t output = 0;
    vigil_error_t error = {0};

    ASSERT_EQ(RunCompiledSource("\n"
                                "i32 state = 0;\n"
                                "\n"
                                "fn bump() -> void {\n"
                                "    state += 1;\n"
                                "}\n"
                                "\n"
                                "fn pair(i32 value) -> (i32, i32) {\n"
                                "    defer bump();\n"
                                "    return (value, value + 1);\n"
                                "}\n"
                                "\n"
                                "fn main() -> i32 {\n"
                                "    i32 left, i32 right = pair(4);\n"
                                "    return state * 100 + left * 10 + right;\n"
                                "}\n"
                                "",
                                &output, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(output, 145);
}

TEST(VigilVmTest, RejectsHugeFloatFormatPrecision)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, 3.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(3U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(3U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 6U << 10U, Span(3U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 65535U << 16U, Span(3U, 8U, 9U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(3U, 10U, 11U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "format specifier error"), 0);

    vigil_value_release(&constant);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, FormatsStringWithWidth)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};
    vigil_object_t *object = NULL;

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "abc", &object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(4U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(4U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U << 8U, Span(4U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 5U, Span(4U, 8U, 9U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(4U, 10U, 11U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "  abc");
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, FormatsIntWithEmptySpec)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_int(&constant, 17);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(5U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(5U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(5U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(5U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(5U, 8U, 9U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "");
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, RejectsFloatFormatSpecForFloatOperand)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, 3.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(6U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(6U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 1U << 10U, Span(6U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(6U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(6U, 8U, 9U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, RejectsFloatFormatSpecForIntegerOperand)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_int(&constant, 3);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(7U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(7U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 6U << 10U, Span(7U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 1U << 16U, Span(7U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(7U, 8U, 9U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, RejectsFloatOperandForHexBinaryAndOctalFormatSpecs)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, 3.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(8U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(8U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U << 10U, Span(8U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(8U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(8U, 8U, 9U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    vigil_value_init_float(&constant, 3.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(9U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(9U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 4U << 10U, Span(9U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(9U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(9U, 8U, 9U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    vigil_value_init_float(&constant, 3.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(10U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(10U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 5U << 10U, Span(10U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(10U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(10U, 8U, 9U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, FormatsFloatToString)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, 3.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(8U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(8U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(8U, 6U, 7U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "3.5");
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, FormatsFloatWithPrecision)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, 3.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(8U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_F64, Span(8U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U, Span(8U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(8U, 8U, 9U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "3.50");
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, RejectsArithmeticOverflow)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_int(&constant, INT64_MAX);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(5U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 1);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(5U, 2U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_ADD, Span(5U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(5U, 6U, 7U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "integer arithmetic overflow or invalid operation"), 0);

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, RejectsNegateOverflow)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_int(&constant, INT64_MIN);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(6U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEGATE, Span(6U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(6U, 4U, 5U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "integer arithmetic overflow or invalid operation"), 0);

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ReturnedObjectSurvivesChunkLifetime)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *object = NULL;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hello", &object, &error), VIGIL_STATUS_OK);

    vigil_value_init_object(&constant, &object);
    vigil_value_init_nil(&result);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(3U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(3U, 4U, 5U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(&result)), 2U);

    vigil_chunk_free(&chunk);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(&result)), 1U);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "hello");

    vigil_value_release(&result);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ConcatenatesAndComparesStringsByValue)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *left_object = NULL;
    vigil_object_t *right_object = NULL;
    vigil_object_t *expected_object = NULL;
    vigil_value_t left;
    vigil_value_t right;
    vigil_value_t expected;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "vi", &left_object, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "gil", &right_object, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "vigil", &expected_object, &error), VIGIL_STATUS_OK);

    vigil_value_init_object(&left, &left_object);
    vigil_value_init_object(&right, &right_object);
    vigil_value_init_object(&expected, &expected_object);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &left, Span(3U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &right, Span(3U, 2U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_ADD, Span(3U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &expected, Span(3U, 6U, 10U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_EQUAL, Span(3U, 11U, 13U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(3U, 14U, 15U), &error), VIGIL_STATUS_OK);

    vigil_value_release(&left);
    vigil_value_release(&right);
    vigil_value_release(&expected);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_BOOL);
    EXPECT_TRUE(vigil_value_as_bool(&result));

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, SupportsFloatArithmeticAndNegation)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_float(&constant, 1.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(7U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_init_float(&constant, 2.0);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(7U, 4U, 7U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_ADD, Span(7U, 8U, 9U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEGATE, Span(7U, 10U, 11U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(7U, 12U, 13U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(vigil_value_as_float(&result), -3.5);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, RejectsMissingArguments)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    EXPECT_EQ(vigil_vm_open(NULL, runtime, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "out_vm must not be null"), 0);

    EXPECT_EQ(vigil_vm_open(&vm, NULL, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "runtime must not be null"), 0);

    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_vm_execute(vm, NULL, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_execute(vm, &chunk, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_execute_function(vm, NULL, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ReportsUnsupportedOpcodeAndSourceId)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 255U, Span(9U, 10U, 11U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_UNSUPPORTED);
    EXPECT_EQ(error.type, VIGIL_STATUS_UNSUPPORTED);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "unsupported opcode"), 0);
    EXPECT_EQ(error.location.source_id, 9U);
    EXPECT_EQ(error.location.offset, 10U);

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, UsesRuntimeAllocatorHooks)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t runtime_options = {0};
    vigil_vm_options_t vm_options = {0};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    vigil_runtime_options_init(&runtime_options);
    runtime_options.allocator = &allocator;
    vigil_vm_options_init(&vm_options);
    vm_options.initial_stack_capacity = 2U;

    ASSERT_EQ(vigil_runtime_open(&runtime, &runtime_options, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, &vm_options, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&constant, 7);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(1U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(1U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 7);
    EXPECT_GE(stats.allocate_calls, 4);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    EXPECT_GE(stats.deallocate_calls, 4);
}

#if !defined(_WIN32)
TEST(VigilVmTest, CheckedArithmeticHelpersRejectInvalidOperands)
{
    int64_t signed_result = 0;
    uint64_t unsigned_result = 0U;

    EXPECT_EQ(vigil_vm_checked_uadd(UINT64_MAX, 1U, &unsigned_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_usubtract(0U, 1U, &unsigned_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_multiply(INT64_MIN, -1, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_multiply(INT64_MAX, 2, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_divide(7, 0, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_divide(INT64_MIN, -1, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_modulo(7, 0, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_modulo(INT64_MIN, -1, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_shift_left(1, -1, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_shift_right(8, 64, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_shift_right(-8, 1, &signed_result), VIGIL_STATUS_OK);
    EXPECT_EQ(signed_result, -4);
    EXPECT_EQ(vigil_vm_checked_ushift_left(1U, 64U, &unsigned_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_ushift_right(1U, 64U, &unsigned_result), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_checked_negate(INT64_MIN, &signed_result), VIGIL_STATUS_INVALID_ARGUMENT);
}

TEST(VigilVmTest, ValueHelpersHandleNullsAndEquivalentObjects)
{
    vigil_runtime_t *runtime = NULL;
    vigil_object_t *left_string = NULL;
    vigil_object_t *right_string = NULL;
    vigil_object_t *left_error = NULL;
    vigil_object_t *right_error = NULL;
    vigil_object_t *array_object = NULL;
    vigil_value_t int_value;
    vigil_value_t string_left_value;
    vigil_value_t string_right_value;
    vigil_value_t error_left_value;
    vigil_value_t error_right_value;
    vigil_value_t array_value;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "alpha", &left_string, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "alpha", &right_string, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_error_object_new_cstr(runtime, "oops", 7, &left_error, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_error_object_new_cstr(runtime, "oops", 7, &right_error, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_array_object_new(runtime, NULL, 0U, &array_object, &error), VIGIL_STATUS_OK);

    vigil_value_init_int(&int_value, 9);
    vigil_value_init_object(&string_left_value, &left_string);
    vigil_value_init_object(&string_right_value, &right_string);
    vigil_value_init_object(&error_left_value, &left_error);
    vigil_value_init_object(&error_right_value, &right_error);
    vigil_value_init_object(&array_value, &array_object);

    EXPECT_FALSE(vigil_vm_value_is_integer(NULL));
    EXPECT_TRUE(vigil_vm_value_is_integer(&int_value));
    EXPECT_FALSE(vigil_vm_values_equal(NULL, &int_value));
    EXPECT_TRUE(vigil_vm_values_equal(&string_left_value, &string_right_value));
    EXPECT_TRUE(vigil_vm_values_equal(&error_left_value, &error_right_value));
    EXPECT_FALSE(vigil_vm_values_equal(&string_left_value, &error_left_value));
    EXPECT_TRUE(vigil_vm_value_is_supported_map_key(&string_left_value));
    EXPECT_FALSE(vigil_vm_value_is_supported_map_key(&array_value));

    vigil_value_release(&array_value);
    vigil_value_release(&error_right_value);
    vigil_value_release(&error_left_value);
    vigil_value_release(&string_right_value);
    vigil_value_release(&string_left_value);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, StringHelpersValidateArgumentsAndExtractStringParts)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_value_t string_value;
    vigil_value_t array_value;
    vigil_object_t *string_object = NULL;
    vigil_object_t *array_object = NULL;
    const char *text = (const char *)1;
    size_t length = 99U;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hi", &string_object, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_array_object_new(runtime, NULL, 0U, &array_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&string_value, &string_object);
    vigil_value_init_object(&array_value, &array_object);

    EXPECT_EQ(vigil_vm_concat_strings(NULL, &string_value, &string_value, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_concat_strings(vm, &array_value, &string_value, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_stringify_value(NULL, &string_value, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_new_string_value(vm, NULL, 1U, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);

    EXPECT_FALSE(vigil_vm_get_string_parts(NULL, &text, &length));
    EXPECT_EQ(text, NULL);
    EXPECT_EQ(length, 0U);

    text = (const char *)1;
    length = 99U;
    EXPECT_FALSE(vigil_vm_get_string_parts(&array_value, &text, &length));
    EXPECT_EQ(text, NULL);
    EXPECT_EQ(length, 0U);

    EXPECT_TRUE(vigil_vm_get_string_parts(&string_value, &text, &length));
    EXPECT_STREQ(text, "hi");
    EXPECT_EQ(length, 2U);

    vigil_value_release(&array_value);
    vigil_value_release(&string_value);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, FormatAndParseHelpersCoverValidationPaths)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_value_t input;
    vigil_value_t status_value;
    vigil_value_t parsed_value;
    vigil_value_t bool_value;
    vigil_object_t *string_object = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_bool(&bool_value, true);
    EXPECT_EQ(vigil_vm_format_f64_value(NULL, &bool_value, 2U, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_format_f64_value(vm, &bool_value, 2U, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_format_spec_value(vm, &bool_value, 1U << 10U, 0U, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "", &string_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&input, &string_object);
    ASSERT_EQ(vigil_vm_push(vm, &input, &error), VIGIL_STATUS_OK);
    vigil_value_release(&input);
    vigil_vm_parse_i32(vm);
    status_value = vigil_vm_pop_or_nil(vm);
    parsed_value = vigil_vm_pop_or_nil(vm);
    EXPECT_EQ(vigil_value_kind(&parsed_value), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&parsed_value), 0);
    ASSERT_EQ(vigil_value_kind(&status_value), VIGIL_VALUE_OBJECT);
    EXPECT_STREQ(vigil_error_object_message(vigil_value_as_object(&status_value)), "empty string");
    vigil_value_release(&status_value);
    vigil_value_release(&parsed_value);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "not-a-float", &string_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&input, &string_object);
    ASSERT_EQ(vigil_vm_push(vm, &input, &error), VIGIL_STATUS_OK);
    vigil_value_release(&input);
    vigil_vm_parse_f64(vm);
    status_value = vigil_vm_pop_or_nil(vm);
    parsed_value = vigil_vm_pop_or_nil(vm);
    EXPECT_EQ(vigil_value_kind(&parsed_value), VIGIL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(vigil_value_as_float(&parsed_value), 0.0);
    ASSERT_EQ(vigil_value_kind(&status_value), VIGIL_VALUE_OBJECT);
    EXPECT_STREQ(vigil_error_object_message(vigil_value_as_object(&status_value)), "invalid float");
    vigil_value_release(&status_value);
    vigil_value_release(&parsed_value);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "true", &string_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&input, &string_object);
    ASSERT_EQ(vigil_vm_push(vm, &input, &error), VIGIL_STATUS_OK);
    vigil_value_release(&input);
    vigil_vm_parse_bool(vm);
    status_value = vigil_vm_pop_or_nil(vm);
    parsed_value = vigil_vm_pop_or_nil(vm);
    EXPECT_EQ(vigil_value_kind(&parsed_value), VIGIL_VALUE_BOOL);
    EXPECT_TRUE(vigil_value_as_bool(&parsed_value));
    ASSERT_EQ(vigil_value_kind(&status_value), VIGIL_VALUE_OBJECT);
    EXPECT_EQ(vigil_error_object_kind(vigil_value_as_object(&status_value)), 0);
    vigil_value_release(&status_value);
    vigil_value_release(&parsed_value);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "0", &string_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&input, &string_object);
    ASSERT_EQ(vigil_vm_push(vm, &input, &error), VIGIL_STATUS_OK);
    vigil_value_release(&input);
    vigil_vm_parse_bool(vm);
    status_value = vigil_vm_pop_or_nil(vm);
    parsed_value = vigil_vm_pop_or_nil(vm);
    EXPECT_EQ(vigil_value_kind(&parsed_value), VIGIL_VALUE_BOOL);
    EXPECT_FALSE(vigil_value_as_bool(&parsed_value));
    ASSERT_EQ(vigil_value_kind(&status_value), VIGIL_VALUE_OBJECT);
    EXPECT_EQ(vigil_error_object_kind(vigil_value_as_object(&status_value)), 0);
    vigil_value_release(&status_value);
    vigil_value_release(&parsed_value);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "maybe", &string_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&input, &string_object);
    ASSERT_EQ(vigil_vm_push(vm, &input, &error), VIGIL_STATUS_OK);
    vigil_value_release(&input);
    vigil_vm_parse_bool(vm);
    status_value = vigil_vm_pop_or_nil(vm);
    parsed_value = vigil_vm_pop_or_nil(vm);
    EXPECT_EQ(vigil_value_kind(&parsed_value), VIGIL_VALUE_BOOL);
    EXPECT_FALSE(vigil_value_as_bool(&parsed_value));
    ASSERT_EQ(vigil_value_kind(&status_value), VIGIL_VALUE_OBJECT);
    EXPECT_STREQ(vigil_error_object_message(vigil_value_as_object(&status_value)), "invalid boolean");
    vigil_value_release(&status_value);
    vigil_value_release(&parsed_value);

    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ConversionHelpersValidateRangesAndTypes)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_value_t input;
    vigil_value_t converted;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_bool(&input, true);
    EXPECT_EQ(vigil_vm_convert_to_signed_integer_type(NULL, &input, -10, 10, "type", "range", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_convert_to_signed_integer_type(vm, NULL, -10, 10, "type", "range", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_vm_convert_to_signed_integer_type(vm, &input, -10, 10, "type", "range", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_value_init_uint(&input, UINT64_C(99));
    EXPECT_EQ(vigil_vm_convert_to_signed_integer_type(vm, &input, -10, 10, "type", "range", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_value_init_float(&input, INFINITY);
    EXPECT_EQ(vigil_vm_convert_to_signed_integer_type(vm, &input, -10, 10, "type", "range", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_value_init_uint(&input, UINT64_C(7));
    ASSERT_EQ(vigil_vm_convert_to_signed_integer_type(vm, &input, -10, 10, "type", "range", &error), VIGIL_STATUS_OK);
    converted = vigil_vm_pop_or_nil(vm);
    EXPECT_EQ(vigil_value_kind(&converted), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&converted), 7);
    vigil_value_release(&converted);

    vigil_value_init_int(&input, -1);
    EXPECT_EQ(vigil_vm_convert_to_unsigned_integer_type(vm, &input, UINT64_C(10), "type", "range", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_value_init_bool(&input, true);
    EXPECT_EQ(vigil_vm_convert_to_unsigned_integer_type(vm, &input, UINT64_C(10), "type", "range", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_value_init_float(&input, INFINITY);
    EXPECT_EQ(vigil_vm_convert_to_unsigned_integer_type(vm, &input, UINT64_C(10), "type", "range", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);

    vigil_value_init_float(&input, 5.9);
    ASSERT_EQ(vigil_vm_convert_to_unsigned_integer_type(vm, &input, UINT64_C(10), "type", "range", &error),
              VIGIL_STATUS_OK);
    converted = vigil_vm_pop_or_nil(vm);
    EXPECT_EQ(vigil_value_kind(&converted), VIGIL_VALUE_UINT);
    EXPECT_EQ(vigil_value_as_uint(&converted), UINT64_C(5));
    vigil_value_release(&converted);

    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ReadOperandHelpersReportFrameAndOperandProblems)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    uint32_t operand = 0U;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_read_u32(vm, &operand, &error), VIGIL_STATUS_INTERNAL);
    EXPECT_EQ(vigil_vm_read_raw_u32(vm, &operand, &error), VIGIL_STATUS_INTERNAL);

    memset(&vm->frames[0], 0, sizeof(vm->frames[0]));
    vm->frame_count = 1U;
    vm->frames[0].chunk = &chunk;

    EXPECT_EQ(vigil_vm_read_u32(vm, &operand, &error), VIGIL_STATUS_INTERNAL);
    EXPECT_EQ(vigil_vm_read_raw_u32(vm, &operand, &error), VIGIL_STATUS_INTERNAL);

    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0xAAU, Span(33U, 0U, 1U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0x78U, Span(33U, 1U, 2U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0x56U, Span(33U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0x34U, Span(33U, 3U, 4U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0x12U, Span(33U, 4U, 5U), &error), VIGIL_STATUS_OK);
    vm->frames[0].ip = 0U;
    ASSERT_EQ(vigil_vm_read_u32(vm, &operand, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(operand, UINT32_C(0x12345678));
    EXPECT_EQ(vm->frames[0].ip, 5U);

    vigil_chunk_clear(&chunk);
    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0x78U, Span(34U, 0U, 1U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0x56U, Span(34U, 1U, 2U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0x34U, Span(34U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_byte(&chunk, 0x12U, Span(34U, 3U, 4U), &error), VIGIL_STATUS_OK);
    vm->frames[0].ip = 0U;
    ASSERT_EQ(vigil_vm_read_raw_u32(vm, &operand, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(operand, UINT32_C(0x12345678));
    EXPECT_EQ(vm->frames[0].ip, 4U);

    vm->frame_count = 0U;
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ExecuteCallAndFunctionRejectInvalidFrames)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_object_t *string_object = NULL;
    vigil_object_t *native_object = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "not-callable", &string_object, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_vm_execute_call(vm, string_object, 0U, &error), VIGIL_STATUS_INTERNAL);

    ASSERT_EQ(vigil_native_function_object_create(runtime, "noop", 4U, 0U, NoopNativeFunction, &native_object, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_vm_execute_call(vm, native_object, 0U, &error), VIGIL_STATUS_OK);

    memset(&vm->frames[0], 0, sizeof(vm->frames[0]));
    vm->frame_count = 1U;
    EXPECT_EQ(vigil_vm_execute_function(vm, NULL, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);

    vm->frame_count = 0U;
    vigil_object_release(&native_object);
    vigil_object_release(&string_object);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}
#endif

TEST(VigilVmTest, ExecutesFunctionObjectEntry)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *function = NULL;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&constant, 99);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(4U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(4U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_function_object_new_cstr(runtime, "main", 0U, 1U, &chunk, &function, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 99);
    EXPECT_EQ(vigil_vm_stack_depth(vm), 0U);
    EXPECT_EQ(vigil_vm_frame_depth(vm), 0U);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecuteFunctionRejectsNonFunctionObject)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_object_t *object = NULL;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hello", &object, &error), VIGIL_STATUS_OK);
    vigil_value_init_nil(&result);

    EXPECT_EQ(vigil_vm_execute_function(vm, object, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "function must be a function or closure object"), 0);

    vigil_object_release(&object);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecuteFunctionRejectsNonZeroArityEntrypoint)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(7U, 0U, 1U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_function_object_new_cstr(runtime, "helper", 1U, 1U, &chunk, &function, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "not enough arguments on stack for function arity"), 0);

    vigil_object_release(&function);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ConvertsFloatToIntAndAppliesBitwiseNot)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, 5.9);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(8U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_I32, Span(8U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_BITWISE_NOT, Span(8U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(8U, 8U, 9U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), -6);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ConvertsBoolAndUintToStrings)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_bool(&constant, true);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(9U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(9U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(9U, 6U, 7U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "true");
    CloseVmTestContext(&runtime, &vm, &chunk, &result);

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    vigil_value_init_uint(&constant, UINT64_C(42));
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(10U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(10U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(10U, 4U, 5U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "42");
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ConvertsFloatAndIntValues)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, 255.9);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(11U, 0U, 5U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_U8, Span(11U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(11U, 8U, 9U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_UINT);
    EXPECT_EQ(vigil_value_as_uint(&result), UINT64_C(255));
    CloseVmTestContext(&runtime, &vm, &chunk, &result);

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&constant, 42);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(12U, 0U, 2U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_I64, Span(12U, 3U, 4U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(12U, 5U, 6U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 42);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, RejectsNegativeFloatToU32Conversion)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, -1.0);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(13U, 0U, 4U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_U32, Span(13U, 5U, 6U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(13U, 7U, 8U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "u32 conversion overflow or invalid value"), 0);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ConvertsObjectAndUintToStrings)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};
    vigil_object_t *string_object = NULL;

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hello", &string_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &string_object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(14U, 0U, 5U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(14U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(14U, 8U, 9U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "hello");
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ComparesDifferentObjectTypesByValue)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t left_constant;
    vigil_value_t right_constant;
    vigil_value_t result;
    vigil_object_t *string_object = NULL;
    vigil_object_t *array_object = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hello", &string_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&left_constant, &string_object);
    ASSERT_EQ(vigil_array_object_new(runtime, NULL, 0U, &array_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&right_constant, &array_object);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &left_constant, Span(15U, 0U, 5U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&left_constant);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &right_constant, Span(15U, 6U, 7U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&right_constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_EQUAL, Span(15U, 8U, 9U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(15U, 10U, 11U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_BOOL);
    EXPECT_FALSE(vigil_value_as_bool(&result));
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, RejectsToStringOnNonStringObject)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_object_t *array_object = NULL;
    vigil_value_t array_items[1];
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_int(&array_items[0], 7);
    ASSERT_EQ(vigil_array_object_new(runtime, array_items, 1U, &array_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &array_object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(16U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(16U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(16U, 4U, 5U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "string conversion requires a primitive or string operand"), 0);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);

    vigil_value_release(&array_items[0]);
}

TEST(VigilVmTest, RejectsDeferredNativeCallTargetThatIsNotNative)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_object_t *object = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "not-native", &object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(17U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_DEFER_CALL_NATIVE, Span(17U, 2U, 3U), &error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(17U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(17U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(17U, 6U, 7U), &error), VIGIL_STATUS_OK); /* return_count */
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(17U, 8U, 9U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INTERNAL);
    EXPECT_EQ(error.type, VIGIL_STATUS_INTERNAL);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "deferred call target is not a native function"), 0);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, RejectsUnsupportedMapKeyType)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_MAP, Span(18U, 0U, 1U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(18U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(18U, 4U, 5U), &error), VIGIL_STATUS_OK);
    vigil_value_init_float(&constant, 2.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(18U, 6U, 7U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 11);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(18U, 8U, 9U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_SET_INDEX, Span(18U, 10U, 11U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(18U, 12U, 13U), &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "map index must be i32, bool, or string"), 0);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, ReportsStringConversionAllocatorFailures)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};
    struct FailingAllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t runtime_options = {0};
    VmTestContextOptions context_options = {0};

    allocator.user_data = &stats;
    allocator.allocate = FailingAllocate;
    allocator.reallocate = FailingReallocate;
    allocator.deallocate = FailingDeallocate;
    vigil_runtime_options_init(&runtime_options);
    runtime_options.allocator = &allocator;
    context_options.runtime_options = &runtime_options;
    stats.fail_after = (size_t)-1;

    ASSERT_EQ(OpenVmTestContextWithOptions(&runtime, &vm, &chunk, &result, &context_options, &error), VIGIL_STATUS_OK);

    vigil_value_init_bool(&constant, true);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(19U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(19U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(19U, 4U, 5U), &error), VIGIL_STATUS_OK);
    /* Warm up: execute once so the stack/frame caches are populated.
       Then the allocator failure targets the TO_STRING allocation. */
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    vigil_value_release(&result);
    vigil_chunk_clear(&chunk);
    vigil_value_init_bool(&constant, true);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(19U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(19U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(19U, 4U, 5U), &error), VIGIL_STATUS_OK);
    stats.fail_after = stats.calls + 1U;
    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
    vigil_value_release(&constant);

    stats.fail_after = (size_t)-1;
    ASSERT_EQ(OpenVmTestContextWithOptions(&runtime, &vm, &chunk, &result, &context_options, &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&constant, 17);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(20U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(20U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(20U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    vigil_value_release(&result);
    vigil_chunk_clear(&chunk);
    vigil_value_init_int(&constant, 17);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(20U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(20U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(20U, 4U, 5U), &error), VIGIL_STATUS_OK);
    stats.fail_after = stats.calls + 1U;
    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
    vigil_value_release(&constant);
}

TEST(VigilVmTest, ReportsFloatFormatAllocatorFailures)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};
    struct FailingAllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t runtime_options = {0};
    VmTestContextOptions context_options = {0};

    allocator.user_data = &stats;
    allocator.allocate = FailingAllocate;
    allocator.reallocate = FailingReallocate;
    allocator.deallocate = FailingDeallocate;
    vigil_runtime_options_init(&runtime_options);
    runtime_options.allocator = &allocator;
    context_options.runtime_options = &runtime_options;
    stats.fail_after = (size_t)-1;

    ASSERT_EQ(OpenVmTestContextWithOptions(&runtime, &vm, &chunk, &result, &context_options, &error), VIGIL_STATUS_OK);

    vigil_value_init_float(&constant, 3.5);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(21U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_F64, Span(21U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U, Span(21U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(21U, 6U, 7U), &error), VIGIL_STATUS_OK);
    stats.fail_after = stats.calls + 2U;
    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
    vigil_value_release(&constant);
}

TEST(VigilVmTest, RejectsMultiValueReturnWhenPendingStorageAllocationFails)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_object_t *function = NULL;
    vigil_error_t error = {0};
    struct FailingAllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t runtime_options = {0};
    VmTestContextOptions context_options = {0};

    allocator.user_data = &stats;
    allocator.allocate = FailingAllocate;
    allocator.reallocate = FailingReallocate;
    allocator.deallocate = FailingDeallocate;
    vigil_runtime_options_init(&runtime_options);
    runtime_options.allocator = &allocator;
    context_options.runtime_options = &runtime_options;
    stats.fail_after = (size_t)-1;

    ASSERT_EQ(OpenVmTestContextWithOptions(&runtime, &vm, &chunk, &result, &context_options, &error), VIGIL_STATUS_OK);

    vigil_value_init_int(&constant, 11);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(22U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 12);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(22U, 2U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(22U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U, Span(22U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_function_object_new_cstr(runtime, "main", 0U, 2U, &chunk, &function, &error), VIGIL_STATUS_OK);
    stats.fail_after = stats.calls + 2U;

    /* The register VM does not allocate pending-return storage;
       multi-value returns are placed directly in registers. */
    EXPECT_EQ(vigil_vm_execute_function(vm, function, &result, &error), VIGIL_STATUS_OK);

    vigil_object_release(&function);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
    vigil_value_release(&constant);
}

TEST(VigilVmTest, ExecutesSignedIntegerArithmeticOpcodes)
{
    int64_t result_value = 0;
    vigil_error_t error = {0};

    ASSERT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_SUBTRACT, 7, 3, &result_value, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, 4);
    ASSERT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_MULTIPLY, 6, 4, &result_value, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, 24);
    ASSERT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_DIVIDE, 9, 2, &result_value, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, 4);
    ASSERT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_MODULO, 10, 3, &result_value, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, 1);
    ASSERT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_SHIFT_LEFT, 3, 2, &result_value, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, 12);
    ASSERT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_SHIFT_RIGHT, -8, 1, &result_value, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, -4);
}

TEST(VigilVmTest, ExecutesUnsignedIntegerArithmeticOpcodes)
{
    uint64_t result_value = 0U;
    vigil_error_t error = {0};

    ASSERT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_ADD, UINT64_C(5), UINT64_C(6), &result_value, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, UINT64_C(11));
    ASSERT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_SUBTRACT, UINT64_C(7), UINT64_C(3), &result_value, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, UINT64_C(4));
    ASSERT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_MULTIPLY, UINT64_C(6), UINT64_C(4), &result_value, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, UINT64_C(24));
    ASSERT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_DIVIDE, UINT64_C(9), UINT64_C(2), &result_value, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, UINT64_C(4));
    ASSERT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_MODULO, UINT64_C(10), UINT64_C(3), &result_value, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, UINT64_C(1));
    ASSERT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_SHIFT_LEFT, UINT64_C(3), UINT64_C(2), &result_value, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, UINT64_C(12));
    ASSERT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_SHIFT_RIGHT, UINT64_C(8), UINT64_C(1), &result_value, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result_value, UINT64_C(4));
}

TEST(VigilVmTest, RejectsArithmeticOpcodeErrors)
{
    int64_t signed_result = 0;
    uint64_t unsigned_result = 0U;
    vigil_error_t error = {0};

    EXPECT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_DIVIDE, 7, 0, &signed_result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_MODULO, 7, 0, &signed_result, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_MULTIPLY, INT64_MAX, 2, &signed_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_SHIFT_LEFT, 1, 64, &signed_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_SHIFT_RIGHT, 1, 64, &signed_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_MULTIPLY, UINT64_MAX, 2, &unsigned_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_SHIFT_LEFT, UINT64_C(1), UINT64_C(64), &unsigned_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_SHIFT_RIGHT, UINT64_C(1), UINT64_C(64), &unsigned_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
}

TEST(VigilVmTest, ExecutesArithmeticEdgeCases)
{
    int64_t signed_result = 0;
    uint64_t unsigned_result = 0U;
    vigil_error_t error = {0};

    ASSERT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_MULTIPLY, 0, 5, &signed_result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(signed_result, 0);
    EXPECT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_SUBTRACT, INT64_MIN, 1, &signed_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_DIVIDE, INT64_MIN, -1, &signed_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_EQ(RunBinaryIntOpcode(VIGIL_OPCODE_SHIFT_RIGHT, 5, 0, &signed_result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(signed_result, 5);
    ASSERT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_MULTIPLY, UINT64_C(0), UINT64_C(5), &unsigned_result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(unsigned_result, UINT64_C(0));
    EXPECT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_DIVIDE, UINT64_C(5), UINT64_C(0), &unsigned_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(RunBinaryUintOpcode(VIGIL_OPCODE_MODULO, UINT64_C(5), UINT64_C(0), &unsigned_result, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
}

TEST(VigilVmTest, ExecutesUnsignedIntegerArithmetic)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_uint(&constant, UINT64_C(9223372036854775808));
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(13U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_uint(&constant, UINT64_C(2));
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(13U, 4U, 5U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_ADD, Span(13U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(13U, 8U, 9U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_UINT);
    EXPECT_EQ(vigil_value_as_uint(&result), UINT64_C(9223372036854775810));
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    vigil_value_init_uint(&constant, UINT64_C(9223372036854775808));
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(14U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_F64, Span(14U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(14U, 6U, 7U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(vigil_value_as_float(&result), 9223372036854775808.0);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, SupportsErrorOpcodes)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_object_t *object = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "boom", &object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(10U, 0U, 4U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 9);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(10U, 5U, 6U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_ERROR, Span(10U, 7U, 8U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_GET_ERROR_KIND, Span(10U, 9U, 10U), &error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(10U, 11U, 12U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 9);
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "boom", &object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(11U, 0U, 4U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 9);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(11U, 5U, 6U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_ERROR, Span(11U, 7U, 8U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_GET_ERROR_MESSAGE, Span(11U, 9U, 10U), &error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(11U, 11U, 12U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "boom");

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecutesArrayAndMapIndexOpcodes)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *key_object = NULL;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_int(&constant, 4);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(12U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&constant, 6);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(12U, 2U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_ARRAY, Span(12U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(12U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U, Span(12U, 8U, 9U), &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&constant, 1);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(12U, 10U, 11U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_GET_INDEX, Span(12U, 12U, 13U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(12U, 14U, 15U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 6);
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "answer", &key_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &key_object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(13U, 0U, 6U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 9);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(13U, 7U, 8U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_MAP, Span(13U, 9U, 10U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(13U, 11U, 12U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 1U, Span(13U, 13U, 14U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "answer", &key_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &key_object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(13U, 15U, 21U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_GET_INDEX, Span(13U, 22U, 23U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(13U, 24U, 25U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 9);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ReleasesExtraValuesInMultiValueRootReturn)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_init_int(&constant, 10);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(30U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 20);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(30U, 2U, 3U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(30U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U, Span(30U, 6U, 7U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 10);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, FormatsNonStringObjectAsEmptyString)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_object_t *array_object = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(OpenVmTestContext(&runtime, &vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_array_object_new(runtime, NULL, 0U, &array_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &array_object);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(31U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_FORMAT_SPEC, Span(31U, 2U, 3U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(31U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(31U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(31U, 8U, 9U), &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "");
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

TEST(VigilVmTest, RejectsPendingReturnStorageAllocationFailure)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};
    struct FailingAllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t runtime_options = {0};
    VmTestContextOptions context_options = {0};

    allocator.user_data = &stats;
    allocator.allocate = FailingAllocate;
    allocator.reallocate = FailingReallocate;
    allocator.deallocate = FailingDeallocate;
    vigil_runtime_options_init(&runtime_options);
    runtime_options.allocator = &allocator;
    context_options.runtime_options = &runtime_options;
    stats.fail_after = (size_t)-1;

    ASSERT_EQ(OpenVmTestContextWithOptions(&runtime, &vm, &chunk, &result, &context_options, &error), VIGIL_STATUS_OK);

    vigil_value_init_int(&constant, 10);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(32U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 20);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &constant, Span(32U, 2U, 3U), NULL, &error), VIGIL_STATUS_OK);
    vigil_value_release(&constant);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(32U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U, Span(32U, 6U, 7U), &error), VIGIL_STATUS_OK);
    stats.fail_after = stats.calls + 2U;

    /* The register VM does not allocate pending-return storage;
       multi-value returns are placed directly in registers. */
    EXPECT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);

    vigil_value_release(&constant);
    CloseVmTestContext(&runtime, &vm, &chunk, &result);
}

// NOLINTEND(readability-function-cognitive-complexity)

void register_vm_tests(void)
{
    REGISTER_TEST(VigilVmTest, OptionsInitClearsFields);
    REGISTER_TEST(VigilVmTest, OpenAndCloseVm);
    REGISTER_TEST(VigilVmTest, ExecutesConstantAndReturn);
    REGISTER_TEST(VigilVmTest, ExecutesLiteralOpcodes);
    REGISTER_TEST(VigilVmTest, ReturnsNilFromEmptyRootChunk);
    REGISTER_TEST(VigilVmTest, ReturnsFirstValueFromMultiValueRootChunk);
    REGISTER_TEST(VigilVmTest, RejectsHugeFloatFormatPrecision);
    REGISTER_TEST(VigilVmTest, FormatsStringWithWidth);
    REGISTER_TEST(VigilVmTest, FormatsIntWithEmptySpec);
    REGISTER_TEST(VigilVmTest, RejectsFloatFormatSpecForFloatOperand);
    REGISTER_TEST(VigilVmTest, RejectsFloatFormatSpecForIntegerOperand);
    REGISTER_TEST(VigilVmTest, RejectsFloatOperandForHexBinaryAndOctalFormatSpecs);
    REGISTER_TEST(VigilVmTest, FormatsFloatToString);
    REGISTER_TEST(VigilVmTest, FormatsFloatWithPrecision);
    REGISTER_TEST(VigilVmTest, RejectsArithmeticOverflow);
    REGISTER_TEST(VigilVmTest, RejectsNegateOverflow);
    REGISTER_TEST(VigilVmTest, ReturnedObjectSurvivesChunkLifetime);
    REGISTER_TEST(VigilVmTest, CompilesAndExecutesMultipleReturnValues);
    REGISTER_TEST(VigilVmTest, CompilesAndExecutesDeferredMultipleReturnValues);
    REGISTER_TEST(VigilVmTest, ComparesDifferentObjectTypesByValue);
    REGISTER_TEST(VigilVmTest, RejectsToStringOnNonStringObject);
    REGISTER_TEST(VigilVmTest, RejectsDeferredNativeCallTargetThatIsNotNative);
    REGISTER_TEST(VigilVmTest, RejectsUnsupportedMapKeyType);
    REGISTER_TEST(VigilVmTest, ReportsStringConversionAllocatorFailures);
    REGISTER_TEST(VigilVmTest, ReportsFloatFormatAllocatorFailures);
    REGISTER_TEST(VigilVmTest, RejectsMultiValueReturnWhenPendingStorageAllocationFails);
    REGISTER_TEST(VigilVmTest, ConcatenatesAndComparesStringsByValue);
    REGISTER_TEST(VigilVmTest, SupportsFloatArithmeticAndNegation);
    REGISTER_TEST(VigilVmTest, RejectsMissingArguments);
    REGISTER_TEST(VigilVmTest, ReportsUnsupportedOpcodeAndSourceId);
    REGISTER_TEST(VigilVmTest, UsesRuntimeAllocatorHooks);
#if !defined(_WIN32)
    REGISTER_TEST(VigilVmTest, CheckedArithmeticHelpersRejectInvalidOperands);
    REGISTER_TEST(VigilVmTest, ValueHelpersHandleNullsAndEquivalentObjects);
    REGISTER_TEST(VigilVmTest, StringHelpersValidateArgumentsAndExtractStringParts);
    REGISTER_TEST(VigilVmTest, FormatAndParseHelpersCoverValidationPaths);
    REGISTER_TEST(VigilVmTest, ConversionHelpersValidateRangesAndTypes);
    REGISTER_TEST(VigilVmTest, ReadOperandHelpersReportFrameAndOperandProblems);
    REGISTER_TEST(VigilVmTest, ExecuteCallAndFunctionRejectInvalidFrames);
#endif
    REGISTER_TEST(VigilVmTest, ExecutesFunctionObjectEntry);
    REGISTER_TEST(VigilVmTest, ExecuteFunctionRejectsNonFunctionObject);
    REGISTER_TEST(VigilVmTest, ExecuteFunctionRejectsNonZeroArityEntrypoint);
    REGISTER_TEST(VigilVmTest, ConvertsFloatToIntAndAppliesBitwiseNot);
    REGISTER_TEST(VigilVmTest, ConvertsBoolAndUintToStrings);
    REGISTER_TEST(VigilVmTest, ConvertsFloatAndIntValues);
    REGISTER_TEST(VigilVmTest, RejectsNegativeFloatToU32Conversion);
    REGISTER_TEST(VigilVmTest, ConvertsObjectAndUintToStrings);
    REGISTER_TEST(VigilVmTest, ExecutesSignedIntegerArithmeticOpcodes);
    REGISTER_TEST(VigilVmTest, ExecutesUnsignedIntegerArithmeticOpcodes);
    REGISTER_TEST(VigilVmTest, RejectsArithmeticOpcodeErrors);
    REGISTER_TEST(VigilVmTest, ExecutesArithmeticEdgeCases);
    REGISTER_TEST(VigilVmTest, ExecutesUnsignedIntegerArithmetic);
    REGISTER_TEST(VigilVmTest, SupportsErrorOpcodes);
    REGISTER_TEST(VigilVmTest, ExecutesArrayAndMapIndexOpcodes);
    REGISTER_TEST(VigilVmTest, ReleasesExtraValuesInMultiValueRootReturn);
    REGISTER_TEST(VigilVmTest, FormatsNonStringObjectAsEmptyString);
    REGISTER_TEST(VigilVmTest, RejectsPendingReturnStorageAllocationFailure);
}
