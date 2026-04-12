#ifndef VIGIL_ARRAY_H
#define VIGIL_ARRAY_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct vigil_byte_buffer
    {
        vigil_runtime_t *runtime;
        uint8_t *data;
        size_t length;
        size_t capacity;
    } vigil_byte_buffer_t;

    VIGIL_API void vigil_byte_buffer_init(vigil_byte_buffer_t *buffer, vigil_runtime_t *runtime);
    VIGIL_API void vigil_byte_buffer_clear(vigil_byte_buffer_t *buffer);
    VIGIL_API void vigil_byte_buffer_free(vigil_byte_buffer_t *buffer);
    VIGIL_API vigil_status_t vigil_byte_buffer_reserve(vigil_byte_buffer_t *buffer, size_t capacity,
                                                       vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_byte_buffer_resize(vigil_byte_buffer_t *buffer, size_t length, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_byte_buffer_append(vigil_byte_buffer_t *buffer, const void *data, size_t length,
                                                      vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_byte_buffer_append_byte(vigil_byte_buffer_t *buffer, uint8_t value,
                                                           vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
