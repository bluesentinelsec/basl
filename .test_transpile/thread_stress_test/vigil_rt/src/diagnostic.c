#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/diagnostic.h"

static vigil_status_t vigil_diagnostic_list_validate_mutable(const vigil_diagnostic_list_t *list, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (list == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic list must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (list->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic list runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (list->items == NULL && (list->count != 0U || list->capacity != 0U))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic list state is inconsistent");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (list->count > list->capacity)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic list count exceeds capacity");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_diagnostic_list_grow(vigil_diagnostic_list_t *list, size_t minimum_capacity,
                                                 vigil_error_t *error)
{
    size_t old_capacity;
    size_t capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= list->capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = list->capacity;
    capacity = old_capacity == 0U ? 4U : old_capacity;
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

    if (next_capacity > (SIZE_MAX / sizeof(*list->items)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "diagnostic list allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (list->items == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(list->runtime, next_capacity * sizeof(*list->items), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        list->items = (vigil_diagnostic_t *)memory;
        list->capacity = next_capacity;
        return VIGIL_STATUS_OK;
    }

    memory = list->items;
    status = vigil_runtime_realloc(list->runtime, &memory, next_capacity * sizeof(*list->items), error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    list->items = (vigil_diagnostic_t *)memory;
    memset(list->items + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*list->items));
    list->capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_diagnostic_list_init(vigil_diagnostic_list_t *list, vigil_runtime_t *runtime)
{
    if (list == NULL)
    {
        return;
    }

    memset(list, 0, sizeof(*list));
    list->runtime = runtime;
}

void vigil_diagnostic_list_clear(vigil_diagnostic_list_t *list)
{
    size_t i;

    if (list == NULL)
    {
        return;
    }

    for (i = 0U; i < list->count; ++i)
    {
        vigil_string_free(&list->items[i].message);
        memset(&list->items[i], 0, sizeof(list->items[i]));
    }

    list->count = 0U;
}

void vigil_diagnostic_list_free(vigil_diagnostic_list_t *list)
{
    void *memory;

    if (list == NULL)
    {
        return;
    }

    vigil_diagnostic_list_clear(list);

    memory = list->items;
    if (list->runtime != NULL)
    {
        vigil_runtime_free(list->runtime, &memory);
    }

    memset(list, 0, sizeof(*list));
}

size_t vigil_diagnostic_list_count(const vigil_diagnostic_list_t *list)
{
    if (list == NULL)
    {
        return 0U;
    }

    return list->count;
}

const char *vigil_diagnostic_severity_name(vigil_diagnostic_severity_t severity)
{
    switch (severity)
    {
    case VIGIL_DIAGNOSTIC_ERROR:
        return "error";
    case VIGIL_DIAGNOSTIC_WARNING:
        return "warning";
    case VIGIL_DIAGNOSTIC_NOTE:
        return "note";
    default:
        return "unknown";
    }
}

const vigil_diagnostic_t *vigil_diagnostic_list_get(const vigil_diagnostic_list_t *list, size_t index)
{
    if (list == NULL || index >= list->count)
    {
        return NULL;
    }

    return &list->items[index];
}

vigil_status_t vigil_diagnostic_list_append(vigil_diagnostic_list_t *list, vigil_diagnostic_severity_t severity,
                                            vigil_source_span_t span, const char *message, size_t message_length,
                                            vigil_error_t *error)
{
    vigil_status_t status;
    vigil_diagnostic_t *diagnostic;

    status = vigil_diagnostic_list_validate_mutable(list, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (message == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic message must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (list->count == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "diagnostic list capacity overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_diagnostic_list_grow(list, list->count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    diagnostic = &list->items[list->count];
    vigil_string_init(&diagnostic->message, list->runtime);
    status = vigil_string_assign(&diagnostic->message, message, message_length, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_string_free(&diagnostic->message);
        return status;
    }

    diagnostic->severity = severity;
    diagnostic->span = span;
    list->count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_diagnostic_list_append_cstr(vigil_diagnostic_list_t *list, vigil_diagnostic_severity_t severity,
                                                 vigil_source_span_t span, const char *message, vigil_error_t *error)
{
    if (message == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic message must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_diagnostic_list_append(list, severity, span, message, strlen(message), error);
}

vigil_status_t vigil_diagnostic_format(const vigil_source_registry_t *registry, const vigil_diagnostic_t *diagnostic,
                                       vigil_string_t *output, vigil_error_t *error)
{
    vigil_source_location_t location;
    const vigil_source_file_t *source;
    char number[32];
    int written;
    vigil_status_t status;
    const char *path;

    vigil_error_clear(error);

    if (registry == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source registry must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (diagnostic == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (output == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "output string must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (output->bytes.runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "output string runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_string_clear(output);

    path = "<unknown>";
    vigil_source_location_clear(&location);
    location.source_id = diagnostic->span.source_id;
    location.offset = diagnostic->span.start_offset;
    source = vigil_source_registry_get(registry, diagnostic->span.source_id);
    if (source != NULL)
    {
        path = vigil_string_c_str(&source->path);
        if (vigil_source_registry_resolve_location(registry, &location, NULL) != VIGIL_STATUS_OK)
        {
            vigil_source_location_clear(&location);
            location.source_id = diagnostic->span.source_id;
            location.offset = diagnostic->span.start_offset;
        }
    }

    status = vigil_string_append_cstr(output, path, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_string_append_cstr(output, ":", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    written = snprintf(number, sizeof(number), "%u", location.line);
    if (written < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format diagnostic line");
        return VIGIL_STATUS_INTERNAL;
    }
    status = vigil_string_append(output, number, (size_t)written, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_string_append_cstr(output, ":", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    written = snprintf(number, sizeof(number), "%u", location.column);
    if (written < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format diagnostic column");
        return VIGIL_STATUS_INTERNAL;
    }
    status = vigil_string_append(output, number, (size_t)written, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_string_append_cstr(output, ": ", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_string_append_cstr(output, vigil_diagnostic_severity_name(diagnostic->severity), error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_string_append_cstr(output, ": ", error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_string_append_cstr(output, vigil_string_c_str(&diagnostic->message), error);
}
