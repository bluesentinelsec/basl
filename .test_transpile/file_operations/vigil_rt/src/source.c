#include <stddef.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/source.h"

static vigil_status_t vigil_source_registry_validate(const vigil_source_registry_t *registry, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (registry == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source registry must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (registry->files == NULL && (registry->count != 0U || registry->capacity != 0U))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source registry state is inconsistent");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (registry->count > registry->capacity)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source registry count exceeds capacity");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_source_registry_validate_mutable(const vigil_source_registry_t *registry,
                                                             vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_source_registry_validate(registry, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (registry->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source registry runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_source_registry_locate(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                                   size_t offset, vigil_source_location_t *out_location,
                                                   vigil_error_t *error)
{
    const vigil_source_file_t *file;
    const char *text;
    size_t length;
    size_t index;
    vigil_status_t status;
    vigil_source_location_t location;

    status = vigil_source_registry_validate(registry, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (out_location == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source location output must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    file = vigil_source_registry_get(registry, source_id);
    if (file == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "source_id must reference a registered source file");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    text = vigil_string_c_str(&file->text);
    length = vigil_string_length(&file->text);
    if (offset > length)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source offset exceeds source text length");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_source_location_clear(&location);
    location.source_id = source_id;
    location.offset = offset;
    location.line = 1U;
    location.column = 1U;

    for (index = 0U; index < offset; index += 1U)
    {
        if (text[index] == '\n')
        {
            location.line += 1U;
            location.column = 1U;
        }
        else
        {
            location.column += 1U;
        }
    }

    *out_location = location;
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_source_registry_grow(vigil_source_registry_t *registry, size_t minimum_capacity,
                                                 vigil_error_t *error)
{
    size_t old_capacity;
    size_t capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= registry->capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = registry->capacity;
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

    if (next_capacity > (SIZE_MAX / sizeof(*registry->files)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "source registry allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (registry->files == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(registry->runtime, next_capacity * sizeof(*registry->files), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        registry->files = (vigil_source_file_t *)memory;
        registry->capacity = next_capacity;
        return VIGIL_STATUS_OK;
    }

    memory = registry->files;
    status = vigil_runtime_realloc(registry->runtime, &memory, next_capacity * sizeof(*registry->files), error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    registry->files = (vigil_source_file_t *)memory;
    memset(registry->files + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*registry->files));
    registry->capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_source_span_clear(vigil_source_span_t *span)
{
    if (span == NULL)
    {
        return;
    }

    memset(span, 0, sizeof(*span));
}

void vigil_source_registry_init(vigil_source_registry_t *registry, vigil_runtime_t *runtime)
{
    if (registry == NULL)
    {
        return;
    }

    memset(registry, 0, sizeof(*registry));
    registry->runtime = runtime;
}

void vigil_source_registry_free(vigil_source_registry_t *registry)
{
    size_t i;
    void *memory;

    if (registry == NULL)
    {
        return;
    }

    for (i = 0U; i < registry->count; ++i)
    {
        vigil_string_free(&registry->files[i].path);
        vigil_string_free(&registry->files[i].text);
    }

    memory = registry->files;
    if (registry->runtime != NULL)
    {
        vigil_runtime_free(registry->runtime, &memory);
    }

    memset(registry, 0, sizeof(*registry));
}

size_t vigil_source_registry_count(const vigil_source_registry_t *registry)
{
    if (registry == NULL)
    {
        return 0U;
    }

    return registry->count;
}

const vigil_source_file_t *vigil_source_registry_get(const vigil_source_registry_t *registry,
                                                     vigil_source_id_t source_id)
{
    if (registry == NULL || source_id == 0U)
    {
        return NULL;
    }

    if ((size_t)source_id > registry->count)
    {
        return NULL;
    }

    return &registry->files[source_id - 1U];
}

vigil_status_t vigil_source_registry_resolve_location(const vigil_source_registry_t *registry,
                                                      vigil_source_location_t *location, vigil_error_t *error)
{
    if (location == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source location must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_source_registry_locate(registry, (vigil_source_id_t)location->source_id, location->offset, location,
                                        error);
}

vigil_status_t vigil_source_registry_resolve_span_start(const vigil_source_registry_t *registry,
                                                        vigil_source_span_t span, vigil_source_location_t *out_location,
                                                        vigil_error_t *error)
{
    return vigil_source_registry_locate(registry, span.source_id, span.start_offset, out_location, error);
}

vigil_status_t vigil_source_registry_register(vigil_source_registry_t *registry, const char *path, size_t path_length,
                                              const char *text, size_t text_length, vigil_source_id_t *out_source_id,
                                              vigil_error_t *error)
{
    vigil_status_t status;
    vigil_source_file_t *file;

    status = vigil_source_registry_validate_mutable(registry, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (path == NULL || text == NULL || out_source_id == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "source register requires path, text, and out_source_id");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (registry->count == UINT32_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "source registry exhausted source ids");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_source_registry_grow(registry, registry->count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    file = &registry->files[registry->count];
    vigil_string_init(&file->path, registry->runtime);
    vigil_string_init(&file->text, registry->runtime);

    status = vigil_string_assign(&file->path, path, path_length, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_string_free(&file->path);
        vigil_string_free(&file->text);
        return status;
    }

    status = vigil_string_assign(&file->text, text, text_length, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_string_free(&file->path);
        vigil_string_free(&file->text);
        return status;
    }

    file->id = (vigil_source_id_t)(registry->count + 1U);
    registry->count += 1U;
    *out_source_id = file->id;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_source_registry_register_cstr(vigil_source_registry_t *registry, const char *path,
                                                   const char *text, vigil_source_id_t *out_source_id,
                                                   vigil_error_t *error)
{
    if (path == NULL || text == NULL || out_source_id == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "source register requires path, text, and out_source_id");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_source_registry_register(registry, path, strlen(path), text, strlen(text), out_source_id, error);
}
