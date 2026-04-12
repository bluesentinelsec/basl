#ifndef VIGIL_STRING_H
#define VIGIL_STRING_H

#include <stddef.h>

#include "vigil/array.h"
#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct vigil_string
    {
        vigil_byte_buffer_t bytes;
    } vigil_string_t;

    VIGIL_API void vigil_string_init(vigil_string_t *string, vigil_runtime_t *runtime);
    VIGIL_API void vigil_string_clear(vigil_string_t *string);
    VIGIL_API void vigil_string_free(vigil_string_t *string);
    VIGIL_API size_t vigil_string_length(const vigil_string_t *string);
    VIGIL_API const char *vigil_string_c_str(const vigil_string_t *string);
    VIGIL_API vigil_status_t vigil_string_reserve(vigil_string_t *string, size_t length, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_string_assign(vigil_string_t *string, const char *value, size_t length,
                                                 vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_string_assign_cstr(vigil_string_t *string, const char *value, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_string_append(vigil_string_t *string, const char *value, size_t length,
                                                 vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_string_append_cstr(vigil_string_t *string, const char *value, vigil_error_t *error);
    VIGIL_API int vigil_string_compare(const vigil_string_t *left, const vigil_string_t *right);
    VIGIL_API int vigil_string_equals_cstr(const vigil_string_t *string, const char *value);

#ifdef __cplusplus
}
#endif

#endif
