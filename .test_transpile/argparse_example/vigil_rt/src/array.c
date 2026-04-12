#include <stddef.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/array.h"

static vigil_status_t vigil_byte_buffer_validate(const vigil_byte_buffer_t *buffer, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (buffer == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "byte buffer must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (buffer->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "byte buffer runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (buffer->data == NULL && (buffer->length != 0U || buffer->capacity != 0U))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "byte buffer state is inconsistent");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (buffer->length > buffer->capacity)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "byte buffer length exceeds capacity");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_byte_buffer_grow(vigil_byte_buffer_t *buffer, size_t minimum_capacity, vigil_error_t *error)
{
    size_t capacity;
    size_t next_capacity;
    vigil_status_t status;
    void *memory;

    if (minimum_capacity <= buffer->capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    capacity = buffer->capacity == 0U ? 16U : buffer->capacity;
    next_capacity = capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (buffer->data == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(buffer->runtime, next_capacity, &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        buffer->data = (uint8_t *)memory;
        buffer->capacity = next_capacity;
        return VIGIL_STATUS_OK;
    }

    memory = buffer->data;
    status = vigil_runtime_realloc(buffer->runtime, &memory, next_capacity, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    buffer->data = (uint8_t *)memory;
    buffer->capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_byte_buffer_init(vigil_byte_buffer_t *buffer, vigil_runtime_t *runtime)
{
    if (buffer == NULL)
    {
        return;
    }

    memset(buffer, 0, sizeof(*buffer));
    buffer->runtime = runtime;
}

void vigil_byte_buffer_clear(vigil_byte_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return;
    }

    buffer->length = 0U;
}

void vigil_byte_buffer_free(vigil_byte_buffer_t *buffer)
{
    void *memory;

    if (buffer == NULL)
    {
        return;
    }

    if (buffer->runtime != NULL)
    {
        memory = buffer->data;
        vigil_runtime_free(buffer->runtime, &memory);
        buffer->data = NULL;
    }
    else
    {
        buffer->data = NULL;
    }

    buffer->length = 0U;
    buffer->capacity = 0U;
    buffer->runtime = NULL;
}

vigil_status_t vigil_byte_buffer_reserve(vigil_byte_buffer_t *buffer, size_t capacity, vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_byte_buffer_validate(buffer, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_byte_buffer_grow(buffer, capacity, error);
}

vigil_status_t vigil_byte_buffer_resize(vigil_byte_buffer_t *buffer, size_t length, vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_length;

    status = vigil_byte_buffer_validate(buffer, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    old_length = buffer->length;
    if (length > buffer->capacity)
    {
        status = vigil_byte_buffer_grow(buffer, length, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        vigil_error_clear(error);
    }

    if (length > old_length)
    {
        memset(buffer->data + old_length, 0, length - old_length);
    }

    buffer->length = length;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_byte_buffer_append(vigil_byte_buffer_t *buffer, const void *data, size_t length,
                                        vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_length;

    status = vigil_byte_buffer_validate(buffer, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (data == NULL && length != 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "append data must not be null when length is non-zero");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (length > SIZE_MAX - buffer->length)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "byte buffer append would overflow");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    old_length = buffer->length;
    status = vigil_byte_buffer_grow(buffer, old_length + length, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (length != 0U)
    {
        memcpy(buffer->data + old_length, data, length);
    }

    buffer->length = old_length + length;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_byte_buffer_append_byte(vigil_byte_buffer_t *buffer, uint8_t value, vigil_error_t *error)
{
    return vigil_byte_buffer_append(buffer, &value, 1U, error);
}
