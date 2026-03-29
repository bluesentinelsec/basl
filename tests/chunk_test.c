#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "vigil/vigil.h"

struct AllocatorStats
{
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

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

static vigil_source_span_t Span(vigil_source_id_t source_id, size_t start, size_t end)
{
    vigil_source_span_t span = {0};

    span.source_id = source_id;
    span.start_offset = start;
    span.end_offset = end;
    return span;
}

static vigil_status_t AppendOpcode(vigil_chunk_t *chunk, vigil_opcode_t opcode, vigil_error_t *error)
{
    return vigil_chunk_write_opcode(chunk, opcode, Span(1U, 0U, 0U), error);
}

static vigil_status_t AppendOpcodeU32(vigil_chunk_t *chunk, vigil_opcode_t opcode, uint32_t operand,
                                      vigil_error_t *error)
{
    vigil_status_t status;

    status = AppendOpcode(chunk, opcode, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_chunk_write_u32(chunk, operand, Span(1U, 0U, 0U), error);
}

static char *DuplicateString(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL)
    {
        return NULL;
    }

    length = strlen(text);
    copy = (char *)malloc(length + 1U);
    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, text, length + 1U);
    return copy;
}

struct DisassemblyInstruction
{
    vigil_opcode_t opcode;
    uint32_t operands[4];
    size_t operand_count;
};

static vigil_status_t AppendInstruction(vigil_chunk_t *chunk, const struct DisassemblyInstruction *instruction,
                                        vigil_error_t *error)
{
    vigil_status_t status;
    size_t operand_index;

    if (instruction->operand_count == 0U)
    {
        return AppendOpcode(chunk, instruction->opcode, error);
    }

    status = AppendOpcodeU32(chunk, instruction->opcode, instruction->operands[0], error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    for (operand_index = 1U; operand_index < instruction->operand_count; ++operand_index)
    {
        status = vigil_chunk_write_u32(chunk, instruction->operands[operand_index], Span(1U, 0U, 0U), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

static char *BuildOperandDisassemblyOutput(void)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_string_t output;
    char *result = NULL;
    static const struct DisassemblyInstruction instructions[] = {
        {VIGIL_OPCODE_CALL, {7U, 2U, 1U}, 3U},     {VIGIL_OPCODE_CALL_VALUE, {9U, 1U}, 2U},
        {VIGIL_OPCODE_NEW_CLOSURE, {5U, 3U}, 2U},  {VIGIL_OPCODE_CALL_INTERFACE, {1U, 2U, 4U, 5U}, 4U},
        {VIGIL_OPCODE_NEW_INSTANCE, {6U, 1U}, 2U}, {VIGIL_OPCODE_NEW_ARRAY, {8U, 2U}, 2U},
        {VIGIL_OPCODE_GET_LOCAL, {11U}, 1U},       {VIGIL_OPCODE_RETURN, {1U}, 1U},
    };
    size_t instruction_index;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        return NULL;
    }
    vigil_chunk_init(&chunk, runtime);
    vigil_string_init(&output, runtime);

    for (instruction_index = 0U; instruction_index < sizeof(instructions) / sizeof(instructions[0]);
         ++instruction_index)
    {
        if (AppendInstruction(&chunk, &instructions[instruction_index], &error) != VIGIL_STATUS_OK)
        {
            goto cleanup;
        }
    }

    if (vigil_chunk_disassemble(&chunk, &output, &error) != VIGIL_STATUS_OK)
    {
        result = NULL;
    }
    else
    {
        result = DuplicateString(vigil_string_c_str(&output));
    }

cleanup:
    vigil_string_free(&output);
    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
    return result;
}

static char *BuildBareReturnDisassemblyOutput(void)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_string_t output;
    char *result = NULL;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        return NULL;
    }
    vigil_chunk_init(&chunk, runtime);
    vigil_string_init(&output, runtime);

    if (AppendOpcode(&chunk, VIGIL_OPCODE_RETURN, &error) == VIGIL_STATUS_OK &&
        vigil_chunk_disassemble(&chunk, &output, &error) == VIGIL_STATUS_OK)
    {
        result = DuplicateString(vigil_string_c_str(&output));
    }

    vigil_string_free(&output);
    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
    return result;
}

static char *BuildLoopSpecializationDisassemblyOutput(void)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_string_t output;
    char *result = NULL;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        return NULL;
    }
    vigil_chunk_init(&chunk, runtime);
    vigil_string_init(&output, runtime);

    if (AppendOpcodeU32(&chunk, VIGIL_OPCODE_INCREMENT_LOCAL_I32, 2U, &error) == VIGIL_STATUS_OK &&
        vigil_chunk_write_byte(&chunk, 1U, Span(1U, 0U, 0U), &error) == VIGIL_STATUS_OK &&
        AppendOpcodeU32(&chunk, VIGIL_OPCODE_FORLOOP_I32, 2U, &error) == VIGIL_STATUS_OK &&
        vigil_chunk_write_byte(&chunk, 1U, Span(1U, 0U, 0U), &error) == VIGIL_STATUS_OK &&
        vigil_chunk_write_u32(&chunk, 3U, Span(1U, 0U, 0U), &error) == VIGIL_STATUS_OK &&
        vigil_chunk_write_byte(&chunk, 0U, Span(1U, 0U, 0U), &error) == VIGIL_STATUS_OK &&
        vigil_chunk_write_u32(&chunk, 12U, Span(1U, 0U, 0U), &error) == VIGIL_STATUS_OK &&
        vigil_chunk_disassemble(&chunk, &output, &error) == VIGIL_STATUS_OK)
    {
        result = DuplicateString(vigil_string_c_str(&output));
    }

    vigil_string_free(&output);
    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
    return result;
}

static char *BuildDisassembleFailureMessage(vigil_opcode_t opcode)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_string_t output;
    char *result = NULL;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        return NULL;
    }
    vigil_chunk_init(&chunk, runtime);
    vigil_string_init(&output, runtime);

    if (AppendOpcode(&chunk, opcode, &error) == VIGIL_STATUS_OK &&
        vigil_chunk_disassemble(&chunk, &output, &error) == VIGIL_STATUS_INTERNAL)
    {
        result = DuplicateString(error.value);
    }

    vigil_error_clear(&error);
    vigil_string_free(&output);
    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
    return result;
}

static const char *FindMissingSubstring(const char *text, const char *const *expected, size_t count)
{
    size_t index;

    if (text == NULL)
    {
        return "<null>";
    }

    for (index = 0U; index < count; ++index)
    {
        if (strstr(text, expected[index]) == NULL)
        {
            return expected[index];
        }
    }

    return NULL;
}

TEST(VigilChunkTest, InitStartsEmpty)
{
    vigil_chunk_t chunk;

    vigil_chunk_init(&chunk, NULL);

    EXPECT_EQ(chunk.runtime, NULL);
    EXPECT_EQ(vigil_chunk_code_size(&chunk), 0U);
    EXPECT_EQ(vigil_chunk_constant_count(&chunk), 0U);
    EXPECT_EQ(vigil_chunk_code(&chunk), NULL);
    EXPECT_EQ(vigil_chunk_constant(&chunk, 0U), NULL);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 0U).source_id, 0U);
}

TEST(VigilChunkTest, WriteOpcodeAndBytesTrackSourceSpans)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    const uint8_t *code;
    vigil_source_span_t opcode_span = Span(1U, 10U, 11U);
    vigil_source_span_t operand_span = Span(1U, 12U, 16U);

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);

    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, opcode_span, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0x78563412U, operand_span, &error), VIGIL_STATUS_OK);

    code = vigil_chunk_code(&chunk);
    ASSERT_NE(code, NULL);
    ASSERT_EQ(vigil_chunk_code_size(&chunk), 5U);
    EXPECT_EQ(code[0], (uint8_t)VIGIL_OPCODE_RETURN);
    EXPECT_EQ(code[1], 0x12U);
    EXPECT_EQ(code[2], 0x34U);
    EXPECT_EQ(code[3], 0x56U);
    EXPECT_EQ(code[4], 0x78U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 0U).start_offset, 10U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 1U).start_offset, 12U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 4U).end_offset, 16U);

    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, AddConstantCopiesOwnedValueAndReleasesOnFree)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_object_t *object = NULL;
    vigil_value_t value;
    const vigil_value_t *stored;
    size_t index = SIZE_MAX;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hello", &object, &error), VIGIL_STATUS_OK);

    vigil_value_init_object(&value, &object);
    ASSERT_EQ(object, NULL);

    ASSERT_EQ(vigil_chunk_add_constant(&chunk, &value, &index, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(index, 0U);
    EXPECT_EQ(vigil_chunk_constant_count(&chunk), 1U);
    stored = vigil_chunk_constant(&chunk, index);
    ASSERT_NE(stored, NULL);
    ASSERT_NE(vigil_value_as_object(stored), NULL);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(stored)), 2U);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(stored)), "hello");

    vigil_value_release(&value);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(stored)), 1U);

    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, WriteConstantEncodesInstructionAndConstantIndex)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_value_t value;
    size_t index = SIZE_MAX;
    const uint8_t *code;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&value, 42);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &value, Span(7U, 20U, 25U), &index, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(index, 0U);
    ASSERT_EQ(vigil_chunk_code_size(&chunk), 5U);
    code = vigil_chunk_code(&chunk);
    ASSERT_NE(code, NULL);
    EXPECT_EQ(code[0], (uint8_t)VIGIL_OPCODE_CONSTANT);
    EXPECT_EQ(code[1], 0U);
    EXPECT_EQ(code[2], 0U);
    EXPECT_EQ(code[3], 0U);
    EXPECT_EQ(code[4], 0U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 0U).source_id, 7U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 4U).end_offset, 25U);

    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, DisassembleFormatsOpcodesAndConstants)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_string_t output;
    vigil_value_t value;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_string_init(&output, runtime);
    vigil_value_init_int(&value, 123);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &value, Span(1U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(1U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_disassemble(&chunk, &output, &error), VIGIL_STATUS_OK);

    EXPECT_NE(strstr(vigil_string_c_str(&output), "0000 CONSTANT 0 123"), NULL);
    EXPECT_NE(strstr(vigil_string_c_str(&output), "0005 RETURN"), NULL);

    vigil_string_free(&output);
    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, DisassembleFormatsOperandInstructions)
{
    static const char *const expected[] = {
        "CALL 7 2",         "CALL_VALUE 9 1", "NEW_CLOSURE 5 3", "CALL_INTERFACE 1 2 4 5",
        "NEW_INSTANCE 6 1", "NEW_ARRAY 8 2",  "GET_LOCAL 11",    "RETURN 1",
    };
    const char *text;
    const char *missing;

    text = BuildOperandDisassemblyOutput();
    ASSERT_NE(text, NULL);
    missing = FindMissingSubstring(text, expected, sizeof(expected) / sizeof(expected[0]));
    EXPECT_EQ(missing, NULL);
    free((void *)text);
}

TEST(VigilChunkTest, DisassembleFormatsBareReturnWithoutOperand)
{
    char *text = BuildBareReturnDisassemblyOutput();

    ASSERT_NE(text, NULL);
    EXPECT_STREQ(text, "0000 RETURN\n");
    free(text);
}

TEST(VigilChunkTest, DisassembleFormatsLoopSpecializationInstructions)
{
    static const char *const expected[] = {"INCREMENT_LOCAL_I32 2 1", "FORLOOP_I32 2 1 3 0 12"};
    const char *missing;
    char *text = BuildLoopSpecializationDisassemblyOutput();

    ASSERT_NE(text, NULL);
    missing = FindMissingSubstring(text, expected, sizeof(expected) / sizeof(expected[0]));
    EXPECT_EQ(missing, NULL);
    free(text);
}

TEST(VigilChunkTest, DisassembleRejectsTruncatedCallInstructions)
{
    char *message = BuildDisassembleFailureMessage(VIGIL_OPCODE_CALL);
    ASSERT_NE(message, NULL);
    EXPECT_STREQ(message, "truncated call instruction");
    free(message);
}

TEST(VigilChunkTest, DisassembleRejectsTruncatedCallValueInstructions)
{
    char *message = BuildDisassembleFailureMessage(VIGIL_OPCODE_CALL_VALUE);
    ASSERT_NE(message, NULL);
    EXPECT_STREQ(message, "truncated indirect call instruction");
    free(message);
}

TEST(VigilChunkTest, DisassembleRejectsTruncatedClosureInstructions)
{
    char *message = BuildDisassembleFailureMessage(VIGIL_OPCODE_NEW_CLOSURE);
    ASSERT_NE(message, NULL);
    EXPECT_STREQ(message, "truncated closure instruction");
    free(message);
}

TEST(VigilChunkTest, DisassembleRejectsTruncatedInterfaceCallInstructions)
{
    char *message = BuildDisassembleFailureMessage(VIGIL_OPCODE_CALL_INTERFACE);
    ASSERT_NE(message, NULL);
    EXPECT_STREQ(message, "truncated interface call instruction");
    free(message);
}

TEST(VigilChunkTest, DisassembleRejectsTruncatedConstructorInstructions)
{
    char *message = BuildDisassembleFailureMessage(VIGIL_OPCODE_NEW_INSTANCE);
    ASSERT_NE(message, NULL);
    EXPECT_STREQ(message, "truncated constructor instruction");
    free(message);
}

TEST(VigilChunkTest, DisassembleRejectsTruncatedCollectionInstructions)
{
    char *message = BuildDisassembleFailureMessage(VIGIL_OPCODE_NEW_ARRAY);
    ASSERT_NE(message, NULL);
    EXPECT_STREQ(message, "truncated collection instruction");
    free(message);
}

TEST(VigilChunkTest, DisassembleRejectsTruncatedU32OperandInstructions)
{
    char *message = BuildDisassembleFailureMessage(VIGIL_OPCODE_GET_LOCAL);
    ASSERT_NE(message, NULL);
    EXPECT_STREQ(message, "truncated constant instruction");
    free(message);
}

TEST(VigilChunkTest, OpcodeNameReturnsUnknownForOutOfRangeOpcode)
{
    EXPECT_STREQ(vigil_opcode_name(VIGIL_OPCODE_RETURN), "RETURN");
    EXPECT_STREQ(vigil_opcode_name((vigil_opcode_t)(VIGIL_OPCODE_MULTIPLY_F64_STORE + 1)), "UNKNOWN");
}

TEST(VigilChunkTest, UsesRuntimeAllocatorHooks)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_value_t value;
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&value, 1);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &value, Span(1U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(1U, 2U, 3U), &error), VIGIL_STATUS_OK);

    EXPECT_GE(stats.allocate_calls, 3);

    vigil_chunk_free(&chunk);
    EXPECT_GE(stats.deallocate_calls, 2);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, RejectsMissingRuntimeForMutation)
{
    vigil_chunk_t chunk;
    vigil_error_t error = {0};
    vigil_value_t value;

    vigil_chunk_init(&chunk, NULL);
    vigil_value_init_nil(&value);

    EXPECT_EQ(vigil_chunk_add_constant(&chunk, &value, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "chunk runtime must not be null"), 0);

    EXPECT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(0U, 0U, 0U), &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
}

TEST(VigilChunkTest, RejectsMissingArguments)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_string_t output;
    vigil_value_t value;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_string_init(&output, runtime);
    vigil_value_init_nil(&value);

    EXPECT_EQ(vigil_chunk_add_constant(&chunk, NULL, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "chunk constant requires value and out_index"), 0);

    vigil_string_free(&output);
    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

void register_chunk_tests(void)
{
    REGISTER_TEST(VigilChunkTest, InitStartsEmpty);
    REGISTER_TEST(VigilChunkTest, WriteOpcodeAndBytesTrackSourceSpans);
    REGISTER_TEST(VigilChunkTest, AddConstantCopiesOwnedValueAndReleasesOnFree);
    REGISTER_TEST(VigilChunkTest, WriteConstantEncodesInstructionAndConstantIndex);
    REGISTER_TEST(VigilChunkTest, DisassembleFormatsOpcodesAndConstants);
    REGISTER_TEST(VigilChunkTest, DisassembleFormatsOperandInstructions);
    REGISTER_TEST(VigilChunkTest, DisassembleFormatsBareReturnWithoutOperand);
    REGISTER_TEST(VigilChunkTest, DisassembleFormatsLoopSpecializationInstructions);
    REGISTER_TEST(VigilChunkTest, DisassembleRejectsTruncatedCallInstructions);
    REGISTER_TEST(VigilChunkTest, DisassembleRejectsTruncatedCallValueInstructions);
    REGISTER_TEST(VigilChunkTest, DisassembleRejectsTruncatedClosureInstructions);
    REGISTER_TEST(VigilChunkTest, DisassembleRejectsTruncatedInterfaceCallInstructions);
    REGISTER_TEST(VigilChunkTest, DisassembleRejectsTruncatedConstructorInstructions);
    REGISTER_TEST(VigilChunkTest, DisassembleRejectsTruncatedCollectionInstructions);
    REGISTER_TEST(VigilChunkTest, DisassembleRejectsTruncatedU32OperandInstructions);
    REGISTER_TEST(VigilChunkTest, OpcodeNameReturnsUnknownForOutOfRangeOpcode);
    REGISTER_TEST(VigilChunkTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(VigilChunkTest, RejectsMissingRuntimeForMutation);
    REGISTER_TEST(VigilChunkTest, RejectsMissingArguments);
}
