#ifndef VIGIL_STATUS_H
#define VIGIL_STATUS_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum vigil_status
    {
        VIGIL_STATUS_OK = 0,
        VIGIL_STATUS_INVALID_ARGUMENT = 1,
        VIGIL_STATUS_OUT_OF_MEMORY = 2,
        VIGIL_STATUS_INTERNAL = 3,
        VIGIL_STATUS_UNSUPPORTED = 4,
        VIGIL_STATUS_SYNTAX_ERROR = 5
    } vigil_status_t;

    typedef struct vigil_source_location
    {
        uint32_t source_id;
        size_t offset;
        uint32_t line;
        uint32_t column;
    } vigil_source_location_t;

    typedef struct vigil_error
    {
        vigil_status_t type;
        const char *value;
        size_t length;
        vigil_source_location_t location;
    } vigil_error_t;

    VIGIL_API void vigil_error_clear(vigil_error_t *error);
    VIGIL_API void vigil_source_location_clear(vigil_source_location_t *location);
    VIGIL_API const char *vigil_error_message(const vigil_error_t *error);
    VIGIL_API const char *vigil_status_name(vigil_status_t status);

#ifdef __cplusplus
}
#endif

#endif
