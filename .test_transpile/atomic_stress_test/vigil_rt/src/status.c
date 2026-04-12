#include <string.h>

#include "internal/vigil_internal.h"

void vigil_source_location_clear(vigil_source_location_t *location)
{
    if (location == NULL)
    {
        return;
    }

    location->source_id = 0U;
    location->offset = 0U;
    location->line = 0U;
    location->column = 0U;
}

void vigil_error_set_literal(vigil_error_t *error, vigil_status_t type, const char *value)
{
    if (error == NULL)
    {
        return;
    }

    error->type = type;
    error->value = value;
    error->length = value == NULL ? 0U : strlen(value);
    vigil_source_location_clear(&error->location);
}

void vigil_error_clear(vigil_error_t *error)
{
    if (error == NULL)
    {
        return;
    }

    error->type = VIGIL_STATUS_OK;
    error->value = NULL;
    error->length = 0U;
    vigil_source_location_clear(&error->location);
}

const char *vigil_error_message(const vigil_error_t *error)
{
    if (error == NULL || error->value == NULL)
    {
        return "unknown error";
    }

    return error->value;
}

const char *vigil_status_name(vigil_status_t status)
{
    switch (status)
    {
    case VIGIL_STATUS_OK:
        return "ok";
    case VIGIL_STATUS_INVALID_ARGUMENT:
        return "invalid_argument";
    case VIGIL_STATUS_OUT_OF_MEMORY:
        return "out_of_memory";
    case VIGIL_STATUS_INTERNAL:
        return "internal";
    case VIGIL_STATUS_UNSUPPORTED:
        return "unsupported";
    case VIGIL_STATUS_SYNTAX_ERROR:
        return "syntax_error";
    default:
        return "unknown";
    }
}
