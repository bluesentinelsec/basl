#ifndef VIGIL_SOURCE_H
#define VIGIL_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/status.h"
#include "vigil/string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef uint32_t vigil_source_id_t;

    typedef struct vigil_source_span
    {
        vigil_source_id_t source_id;
        size_t start_offset;
        size_t end_offset;
    } vigil_source_span_t;

    typedef struct vigil_source_file
    {
        vigil_source_id_t id;
        vigil_string_t path;
        vigil_string_t text;
    } vigil_source_file_t;

    typedef struct vigil_source_registry
    {
        vigil_runtime_t *runtime;
        vigil_source_file_t *files;
        size_t count;
        size_t capacity;
    } vigil_source_registry_t;

    VIGIL_API void vigil_source_span_clear(vigil_source_span_t *span);
    VIGIL_API void vigil_source_registry_init(vigil_source_registry_t *registry, vigil_runtime_t *runtime);
    VIGIL_API void vigil_source_registry_free(vigil_source_registry_t *registry);
    VIGIL_API size_t vigil_source_registry_count(const vigil_source_registry_t *registry);
    /*
     * The returned pointer is invalidated by any subsequent registry mutation,
     * including register calls that may reallocate the backing array.
     */
    VIGIL_API const vigil_source_file_t *vigil_source_registry_get(const vigil_source_registry_t *registry,
                                                                   vigil_source_id_t source_id);
    VIGIL_API vigil_status_t vigil_source_registry_resolve_location(const vigil_source_registry_t *registry,
                                                                    vigil_source_location_t *location,
                                                                    vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_source_registry_resolve_span_start(const vigil_source_registry_t *registry,
                                                                      vigil_source_span_t span,
                                                                      vigil_source_location_t *out_location,
                                                                      vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_source_registry_register(vigil_source_registry_t *registry, const char *path,
                                                            size_t path_length, const char *text, size_t text_length,
                                                            vigil_source_id_t *out_source_id, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_source_registry_register_cstr(vigil_source_registry_t *registry, const char *path,
                                                                 const char *text, vigil_source_id_t *out_source_id,
                                                                 vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
