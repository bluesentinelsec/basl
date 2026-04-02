#include "vigil_test.h"

#if !defined(_WIN32)

#include <string.h>

#include "internal/vigil_vm_internal.h"
#include "vigil/vigil.h"
#include "vm_ops_collection.h"

typedef struct VmCollectionTestContext
{
    vigil_runtime_t *runtime;
    vigil_vm_t *vm;
    vigil_chunk_t chunk;
    vigil_vm_frame_t *frame;
} VmCollectionTestContext;

static vigil_source_span_t CollectionSpan(void)
{
    vigil_source_span_t span = {0};

    span.source_id = 1U;
    span.start_offset = 0U;
    span.end_offset = 1U;
    return span;
}

static vigil_status_t OpenCollectionTestContext(VmCollectionTestContext *context, vigil_opcode_t opcode,
                                                vigil_error_t *error)
{
    vigil_status_t status;

    memset(context, 0, sizeof(*context));
    status = vigil_runtime_open(&context->runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_vm_open(&context->vm, context->runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_runtime_close(&context->runtime);
        return status;
    }

    vigil_chunk_init(&context->chunk, context->runtime);
    status = vigil_chunk_write_opcode(&context->chunk, opcode, CollectionSpan(), error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_chunk_free(&context->chunk);
        vigil_vm_close(&context->vm);
        vigil_runtime_close(&context->runtime);
        memset(context, 0, sizeof(*context));
        return status;
    }

    context->vm->frame_count = 1U;
    context->frame = &context->vm->frames[0];
    memset(context->frame, 0, sizeof(*context->frame));
    context->frame->chunk = &context->chunk;
    return VIGIL_STATUS_OK;
}

static void CloseCollectionTestContext(VmCollectionTestContext *context)
{
    vigil_chunk_free(&context->chunk);
    vigil_vm_close(&context->vm);
    vigil_runtime_close(&context->runtime);
    memset(context, 0, sizeof(*context));
}

static vigil_status_t PushValue(VmCollectionTestContext *context, const vigil_value_t *value, vigil_error_t *error)
{
    return vigil_vm_push(context->vm, value, error);
}

static vigil_status_t PushInt(VmCollectionTestContext *context, int64_t value, vigil_error_t *error)
{
    vigil_value_t pushed;

    vigil_value_init_int(&pushed, value);
    return PushValue(context, &pushed, error);
}

static vigil_status_t PushNil(VmCollectionTestContext *context, vigil_error_t *error)
{
    vigil_value_t pushed;

    vigil_value_init_nil(&pushed);
    return PushValue(context, &pushed, error);
}

static vigil_status_t PushFloat(VmCollectionTestContext *context, double value, vigil_error_t *error)
{
    vigil_value_t pushed;

    vigil_value_init_float(&pushed, value);
    return PushValue(context, &pushed, error);
}

static vigil_status_t PushString(VmCollectionTestContext *context, const char *text, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object = NULL;
    vigil_value_t pushed;

    status = vigil_string_object_new_cstr(context->runtime, text, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_value_init_object(&pushed, &object);
    vigil_object_release(&object);
    status = PushValue(context, &pushed, error);
    vigil_value_release(&pushed);
    return status;
}

static vigil_status_t PushArrayInts(VmCollectionTestContext *context, const int64_t *items, size_t item_count,
                                    vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object = NULL;
    vigil_value_t *values = NULL;
    vigil_value_t pushed;
    size_t index;

    if (item_count > 0U)
    {
        values = (vigil_value_t *)calloc(item_count, sizeof(*values));
        if (values == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "failed to allocate test array values");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
    }

    for (index = 0U; index < item_count; index += 1U)
    {
        vigil_value_init_int(&values[index], items[index]);
    }

    status = vigil_array_object_new(context->runtime, values, item_count, &object, error);
    for (index = 0U; index < item_count; index += 1U)
    {
        vigil_value_release(&values[index]);
    }
    free(values);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_value_init_object(&pushed, &object);
    vigil_object_release(&object);
    status = PushValue(context, &pushed, error);
    vigil_value_release(&pushed);
    return status;
}

static vigil_status_t PushEmptyMap(VmCollectionTestContext *context, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object = NULL;
    vigil_value_t pushed;

    status = vigil_map_object_new(context->runtime, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_value_init_object(&pushed, &object);
    vigil_object_release(&object);
    status = PushValue(context, &pushed, error);
    vigil_value_release(&pushed);
    return status;
}

static vigil_status_t PushMapEntry(VmCollectionTestContext *context, const vigil_value_t *key,
                                   const vigil_value_t *value, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object = NULL;
    vigil_value_t pushed;

    status = vigil_map_object_new(context->runtime, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_map_object_set(object, key, value, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_object_release(&object);
        return status;
    }

    vigil_value_init_object(&pushed, &object);
    vigil_object_release(&object);
    status = PushValue(context, &pushed, error);
    vigil_value_release(&pushed);
    return status;
}

static void ExpectErrorMessage(int *vigil_test_failed_, const vigil_error_t *error, vigil_status_t expected_type,
                               const char *expected_value)
{
    ASSERT_EQ(error->type, expected_type);
    ASSERT_NE(error->value, NULL);
    EXPECT_STREQ(error->value, expected_value);
}

static void ExpectStackIntAt(int *vigil_test_failed_, const VmCollectionTestContext *context, size_t index,
                             int64_t expected)
{
    ASSERT_TRUE(index < context->vm->stack_count);
    EXPECT_EQ(vigil_value_kind(&context->vm->stack[index]), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&context->vm->stack[index]), expected);
}

static void ExpectStackBoolAt(int *vigil_test_failed_, const VmCollectionTestContext *context, size_t index,
                              bool expected)
{
    ASSERT_TRUE(index < context->vm->stack_count);
    EXPECT_EQ(vigil_value_kind(&context->vm->stack[index]), VIGIL_VALUE_BOOL);
    EXPECT_EQ(vigil_value_as_bool(&context->vm->stack[index]), expected ? 1 : 0);
}

static void ExpectStackErrorAt(int *vigil_test_failed_, const VmCollectionTestContext *context, size_t index,
                               int64_t expected_kind, const char *expected_message)
{
    vigil_object_t *object;

    ASSERT_TRUE(index < context->vm->stack_count);
    ASSERT_EQ(vigil_value_kind(&context->vm->stack[index]), VIGIL_VALUE_OBJECT);
    object = vigil_value_as_object(&context->vm->stack[index]);
    ASSERT_NE(object, NULL);
    ASSERT_EQ(vigil_object_type(object), VIGIL_OBJECT_ERROR);
    EXPECT_EQ(vigil_error_object_kind(object), expected_kind);
    EXPECT_STREQ(vigil_error_object_message(object), expected_message);
}

TEST(VmOpsCollectionTest, GetIndexRejectsInvalidReceiverAndIndexes)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    vigil_status_t status;
    const int64_t items[] = {11, 22};
    vigil_value_t key;
    vigil_value_t value;

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_INDEX, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 7, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 0, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_index(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "index access requires an array or map");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_INDEX, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 2U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, -1, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_index(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "array index must be a non-negative i32");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_INDEX, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 2U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 8, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_index(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT, "array index is out of range");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    vigil_value_init_int(&value, 91);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_INDEX, &error), VIGIL_STATUS_OK);
    vigil_value_init_float(&key, 1.25);
    ASSERT_EQ(PushMapEntry(&context, &value, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushFloat(&context, 1.25, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_index(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "map index must be i32, bool, or string");
    CloseCollectionTestContext(&context);
    vigil_value_release(&key);
    vigil_value_release(&value);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_INDEX, &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&key, 1);
    vigil_value_init_int(&value, 91);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 2, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_index(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT, "map key is not present");
    CloseCollectionTestContext(&context);
    vigil_value_release(&key);
    vigil_value_release(&value);
}

TEST(VmOpsCollectionTest, GetCollectionSizeReturnsCountsAndValidatesReceiver)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    vigil_status_t status;
    const int64_t items[] = {3, 5, 8};
    vigil_value_t key;
    vigil_value_t value;

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_COLLECTION_SIZE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 3U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_get_collection_size(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 3);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_COLLECTION_SIZE, &error), VIGIL_STATUS_OK);
    vigil_value_init_bool(&key, true);
    vigil_value_init_int(&value, 17);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_get_collection_size(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 1);
    CloseCollectionTestContext(&context);
    vigil_value_release(&key);
    vigil_value_release(&value);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_COLLECTION_SIZE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 9, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_collection_size(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "collection size requires an array or map");
    CloseCollectionTestContext(&context);
}

TEST(VmOpsCollectionTest, ArraySafeOperationsReturnExpectedValues)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    const int64_t items[] = {7, 9};

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_POP, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 2U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushNil(&context, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_array_pop(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 2U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 9);
    ExpectStackErrorAt(vigil_test_failed_, &context, 1U, 0, "");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_POP, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, NULL, 0U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushNil(&context, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_array_pop(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 2U);
    ExpectStackErrorAt(vigil_test_failed_, &context, 0U, 7, "array is empty");
    EXPECT_EQ(vigil_value_kind(&context.vm->stack[1]), VIGIL_VALUE_NIL);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_GET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 2U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 111, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_array_get_safe(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 2U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 9);
    ExpectStackErrorAt(vigil_test_failed_, &context, 1U, 0, "");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_GET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 2U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 9, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 111, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_array_get_safe(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 2U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 111);
    ExpectStackErrorAt(vigil_test_failed_, &context, 1U, 7, "array index is out of range");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_SET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 2U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 4, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 123, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_array_set_safe(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ExpectStackErrorAt(vigil_test_failed_, &context, 0U, 7, "array index is out of range");
    CloseCollectionTestContext(&context);
}

TEST(VmOpsCollectionTest, MapSafeOperationsReturnExpectedValues)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    vigil_value_t key;
    vigil_value_t value;

    vigil_value_init_int(&key, 1);
    vigil_value_init_int(&value, 44);

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_GET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 200, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_map_get_safe(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 2U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 44);
    ExpectStackBoolAt(vigil_test_failed_, &context, 1U, true);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_GET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 2, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 200, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_map_get_safe(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 2U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 200);
    ExpectStackBoolAt(vigil_test_failed_, &context, 1U, false);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_REMOVE_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 0, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_map_remove_safe(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 2U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 44);
    ExpectStackBoolAt(vigil_test_failed_, &context, 1U, true);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_HAS, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 2, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_map_has(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ExpectStackBoolAt(vigil_test_failed_, &context, 0U, false);
    CloseCollectionTestContext(&context);

    vigil_value_release(&key);
    vigil_value_release(&value);
}

TEST(VmOpsCollectionTest, MapIterationOperationsValidateInputsAndReturnEntries)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    vigil_status_t status;
    uint8_t code[] = {(uint8_t)VIGIL_OPCODE_MAP_KEYS};
    vigil_value_t key;
    vigil_value_t value;

    vigil_value_init_int(&key, 5);
    vigil_value_init_int(&value, 12);

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_KEYS, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_map_keys_values(context.vm, context.frame, code, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ASSERT_EQ(vigil_value_kind(&context.vm->stack[0]), VIGIL_VALUE_OBJECT);
    ASSERT_EQ(vigil_object_type(vigil_value_as_object(&context.vm->stack[0])), VIGIL_OBJECT_ARRAY);
    EXPECT_EQ(vigil_array_object_length(vigil_value_as_object(&context.vm->stack[0])), 1U);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_VALUES, &error), VIGIL_STATUS_OK);
    code[0] = (uint8_t)VIGIL_OPCODE_MAP_VALUES;
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_map_keys_values(context.vm, context.frame, code, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ASSERT_EQ(vigil_value_kind(&context.vm->stack[0]), VIGIL_VALUE_OBJECT);
    ASSERT_EQ(vigil_object_type(vigil_value_as_object(&context.vm->stack[0])), VIGIL_OBJECT_ARRAY);
    EXPECT_EQ(vigil_array_object_length(vigil_value_as_object(&context.vm->stack[0])), 1U);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_MAP_KEY_AT, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 4, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 0, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_map_key_at(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "map iteration requires a map object");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_MAP_KEY_AT, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 2, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_map_key_at(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "map iteration index is out of range");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_MAP_VALUE_AT, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 0, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_get_map_value_at(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 12);
    CloseCollectionTestContext(&context);

    vigil_value_release(&key);
    vigil_value_release(&value);
}

TEST(VmOpsCollectionTest, CollectionMethodsRejectNonCollectionObjects)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    vigil_status_t status;

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_PUSH, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_array_push(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "array push() requires an array receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_GET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 0, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 7, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_array_get_safe(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "array get() requires an array receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_SET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 0, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 7, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_array_set_safe(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "array set() requires an array receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_SLICE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 0, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_array_slice(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "array slice() requires an array receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_CONTAINS, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_array_contains(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "array contains() requires an array receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_GET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 2, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_map_get_safe(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT, "map get() requires a map receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_SET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 2, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_map_set_safe(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT, "map set() requires a map receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_REMOVE_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 2, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_map_remove_safe(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "map remove() requires a map receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_HAS, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_map_has(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT, "map has() requires a map receiver");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_KEYS, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushString(&context, "nope", &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_map_keys_values(context.vm, context.frame, (const uint8_t[]){VIGIL_OPCODE_MAP_KEYS}, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT, "map method requires a map receiver");
    CloseCollectionTestContext(&context);
}

TEST(VmOpsCollectionTest, ArrayMutationSliceAndContainsCoverSuccessAndValidation)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    vigil_status_t status;
    const int64_t items[] = {4, 8, 15};

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_PUSH, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 3U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 16, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_array_push(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 0U);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_SLICE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 3U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 3, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_array_slice(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ASSERT_EQ(vigil_object_type(vigil_value_as_object(&context.vm->stack[0])), VIGIL_OBJECT_ARRAY);
    EXPECT_EQ(vigil_array_object_length(vigil_value_as_object(&context.vm->stack[0])), 2U);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_SLICE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 3U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, -1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_array_slice(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "array slice() start and end must be non-negative i32 values");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_SLICE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 3U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 9, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_array_slice(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT, "array slice range is out of bounds");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_ARRAY_CONTAINS, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 3U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 8, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_array_contains(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ExpectStackBoolAt(vigil_test_failed_, &context, 0U, true);
    CloseCollectionTestContext(&context);
}

TEST(VmOpsCollectionTest, MapMutationAndIterationCoverRemainingSafePaths)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    vigil_status_t status;

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_SET_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushEmptyMap(&context, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 77, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_map_set_safe(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 1U);
    ExpectStackErrorAt(vigil_test_failed_, &context, 0U, 0, "");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_MAP_REMOVE_SAFE, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushEmptyMap(&context, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 77, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_map_remove_safe(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 2U);
    ExpectStackIntAt(vigil_test_failed_, &context, 0U, 77);
    ExpectStackBoolAt(vigil_test_failed_, &context, 1U, false);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_MAP_KEY_AT, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushEmptyMap(&context, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, -1, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_map_key_at(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "map iteration index must be a non-negative i32");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_GET_MAP_VALUE_AT, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushEmptyMap(&context, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, -1, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_get_map_value_at(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "map iteration index must be a non-negative i32");
    CloseCollectionTestContext(&context);
}

TEST(VmOpsCollectionTest, SetIndexUpdatesCollectionsAndRejectsInvalidInputs)
{
    VmCollectionTestContext context;
    vigil_error_t error = {0};
    vigil_status_t status;
    const int64_t items[] = {10, 20};
    vigil_value_t key;
    vigil_value_t value;

    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_SET_INDEX, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 2U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 99, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_op_set_index(context.vm, context.frame, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(context.vm->stack_count, 0U);
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_SET_INDEX, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 0, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 99, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_set_index(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "indexed assignment requires an array or map");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_SET_INDEX, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushArrayInts(&context, items, 2U, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, -1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 99, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_set_index(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "array index must be a non-negative i32");
    CloseCollectionTestContext(&context);

    vigil_error_clear(&error);
    vigil_value_init_bool(&key, true);
    vigil_value_init_int(&value, 1);
    ASSERT_EQ(OpenCollectionTestContext(&context, VIGIL_OPCODE_SET_INDEX, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushMapEntry(&context, &key, &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushFloat(&context, 9.5, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(PushInt(&context, 11, &error), VIGIL_STATUS_OK);
    status = vigil_vm_op_set_index(context.vm, context.frame, &error);
    ASSERT_EQ(status, VIGIL_STATUS_INVALID_ARGUMENT);
    ExpectErrorMessage(vigil_test_failed_, &error, VIGIL_STATUS_INVALID_ARGUMENT,
                       "map index must be i32, bool, or string");
    CloseCollectionTestContext(&context);
    vigil_value_release(&key);
    vigil_value_release(&value);
}

void register_vm_ops_collection_tests(void)
{
    REGISTER_TEST(VmOpsCollectionTest, GetIndexRejectsInvalidReceiverAndIndexes);
    REGISTER_TEST(VmOpsCollectionTest, GetCollectionSizeReturnsCountsAndValidatesReceiver);
    REGISTER_TEST(VmOpsCollectionTest, ArraySafeOperationsReturnExpectedValues);
    REGISTER_TEST(VmOpsCollectionTest, MapSafeOperationsReturnExpectedValues);
    REGISTER_TEST(VmOpsCollectionTest, MapIterationOperationsValidateInputsAndReturnEntries);
    REGISTER_TEST(VmOpsCollectionTest, CollectionMethodsRejectNonCollectionObjects);
    REGISTER_TEST(VmOpsCollectionTest, ArrayMutationSliceAndContainsCoverSuccessAndValidation);
    REGISTER_TEST(VmOpsCollectionTest, MapMutationAndIterationCoverRemainingSafePaths);
    REGISTER_TEST(VmOpsCollectionTest, SetIndexUpdatesCollectionsAndRejectsInvalidInputs);
}

#else

void register_vm_ops_collection_tests(void)
{
}

#endif
