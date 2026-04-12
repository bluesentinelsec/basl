#include <stddef.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/string.h"

static size_t vigil_string_storage_length(const vigil_string_t *string)
{
    if (string == NULL)
    {
        return 0U;
    }

    return string->bytes.length;
}

static vigil_status_t vigil_string_validate_mutable(const vigil_string_t *string, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (string == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (string->bytes.runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static int vigil_string_overlaps_storage(const vigil_string_t *string, const char *value, size_t length)
{
    const uint8_t *start;
    const uint8_t *end;
    const uint8_t *value_start;
    const uint8_t *value_end;

    if (string == NULL || value == NULL || length == 0U || string->bytes.data == NULL)
    {
        return 0;
    }

    start = string->bytes.data;
    end = start + vigil_string_length(string);
    value_start = (const uint8_t *)value;
    value_end = value_start + length;

    return value_start < end && value_end > start;
}

static vigil_status_t vigil_string_copy_input(vigil_string_t *string, const char *value, size_t length,
                                              const char **out_value, void **out_temp, vigil_error_t *error)
{
    vigil_status_t status;

    *out_value = value;
    *out_temp = NULL;

    if (!vigil_string_overlaps_storage(string, value, length))
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    status = vigil_runtime_alloc(string->bytes.runtime, length, out_temp, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    memcpy(*out_temp, value, length);
    *out_value = (const char *)*out_temp;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_string_prepare_storage(vigil_string_t *string, size_t length, vigil_error_t *error)
{
    vigil_status_t status;

    if (length == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string length would overflow");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_byte_buffer_reserve(&string->bytes, length + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    string->bytes.length = length + 1U;
    string->bytes.data[length] = '\0';
    return VIGIL_STATUS_OK;
}

void vigil_string_init(vigil_string_t *string, vigil_runtime_t *runtime)
{
    if (string == NULL)
    {
        return;
    }

    vigil_byte_buffer_init(&string->bytes, runtime);
}

void vigil_string_clear(vigil_string_t *string)
{
    if (string == NULL)
    {
        return;
    }

    if (string->bytes.data != NULL)
    {
        string->bytes.length = 1U;
        string->bytes.data[0] = '\0';
    }
}

void vigil_string_free(vigil_string_t *string)
{
    if (string == NULL)
    {
        return;
    }

    vigil_byte_buffer_free(&string->bytes);
}

size_t vigil_string_length(const vigil_string_t *string)
{
    size_t storage_length;

    storage_length = vigil_string_storage_length(string);
    if (storage_length == 0U)
    {
        return 0U;
    }

    return storage_length - 1U;
}

const char *vigil_string_c_str(const vigil_string_t *string)
{
    static const char empty[] = "";

    if (string == NULL || string->bytes.data == NULL || string->bytes.length == 0U)
    {
        return empty;
    }

    return (const char *)string->bytes.data;
}

vigil_status_t vigil_string_reserve(vigil_string_t *string, size_t length, vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_string_validate_mutable(string, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (length == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string reserve would overflow");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_byte_buffer_reserve(&string->bytes, length + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (string->bytes.length == 0U)
    {
        string->bytes.length = 1U;
        string->bytes.data[0] = '\0';
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_string_assign(vigil_string_t *string, const char *value, size_t length, vigil_error_t *error)
{
    vigil_status_t status;
    const char *copied_value;
    void *temp;

    status = vigil_string_validate_mutable(string, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_string_copy_input(string, value, length, &copied_value, &temp, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_string_prepare_storage(string, length, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_runtime_free(string->bytes.runtime, &temp);
        return status;
    }

    if (length != 0U)
    {
        memcpy(string->bytes.data, copied_value, length);
        string->bytes.data[length] = '\0';
    }

    vigil_runtime_free(string->bytes.runtime, &temp);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_string_assign_cstr(vigil_string_t *string, const char *value, vigil_error_t *error)
{
    size_t length;

    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    length = strlen(value);
    return vigil_string_assign(string, value, length, error);
}

vigil_status_t vigil_string_append(vigil_string_t *string, const char *value, size_t length, vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_length;
    const char *copied_value;
    void *temp;

    status = vigil_string_validate_mutable(string, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    old_length = vigil_string_length(string);
    if (length > SIZE_MAX - old_length - 1U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string append would overflow");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_string_copy_input(string, value, length, &copied_value, &temp, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_string_prepare_storage(string, old_length + length, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_runtime_free(string->bytes.runtime, &temp);
        return status;
    }

    if (length != 0U)
    {
        memcpy(string->bytes.data + old_length, copied_value, length);
        string->bytes.data[old_length + length] = '\0';
    }

    vigil_runtime_free(string->bytes.runtime, &temp);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_string_append_cstr(vigil_string_t *string, const char *value, vigil_error_t *error)
{
    size_t length;

    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    length = strlen(value);
    return vigil_string_append(string, value, length, error);
}

int vigil_string_compare(const vigil_string_t *left, const vigil_string_t *right)
{
    size_t left_length;
    size_t right_length;
    size_t common_length;
    int compared;

    left_length = vigil_string_length(left);
    right_length = vigil_string_length(right);
    common_length = left_length < right_length ? left_length : right_length;

    compared = memcmp(vigil_string_c_str(left), vigil_string_c_str(right), common_length);
    if (compared != 0)
    {
        return compared;
    }

    if (left_length < right_length)
    {
        return -1;
    }

    if (left_length > right_length)
    {
        return 1;
    }

    return 0;
}

int vigil_string_equals_cstr(const vigil_string_t *string, const char *value)
{
    size_t value_length;

    value_length = value == NULL ? 0U : strlen(value);
    if (vigil_string_length(string) != value_length)
    {
        return 0;
    }

    return memcmp(vigil_string_c_str(string), value == NULL ? "" : value, value_length) == 0;
}
