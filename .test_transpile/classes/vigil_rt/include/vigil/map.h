#ifndef VIGIL_MAP_H
#define VIGIL_MAP_H

#include <stddef.h>

#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/status.h"
#include "vigil/value.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct vigil_map
    {
        vigil_runtime_t *runtime;
        void *entries;
        size_t count;
        size_t capacity;
        size_t tombstone_count;
    } vigil_map_t;

    VIGIL_API void vigil_map_init(vigil_map_t *map, vigil_runtime_t *runtime);
    VIGIL_API void vigil_map_clear(vigil_map_t *map);
    VIGIL_API void vigil_map_free(vigil_map_t *map);
    VIGIL_API size_t vigil_map_count(const vigil_map_t *map);
    VIGIL_API vigil_status_t vigil_map_set_value(vigil_map_t *map, const vigil_value_t *key, const vigil_value_t *value,
                                                 vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_map_set(vigil_map_t *map, const char *key, size_t key_length,
                                           const vigil_value_t *value, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_map_set_cstr(vigil_map_t *map, const char *key, const vigil_value_t *value,
                                                vigil_error_t *error);
    /*
     * Returns a pointer into the map's internal storage. Any later mutating map
     * operation may invalidate the returned pointer.
     */
    VIGIL_API const vigil_value_t *vigil_map_get(const vigil_map_t *map, const char *key, size_t key_length);
    VIGIL_API const vigil_value_t *vigil_map_get_value(const vigil_map_t *map, const vigil_value_t *key);
    /*
     * Returns a pointer into the map's internal storage. Any later mutating map
     * operation may invalidate the returned pointer.
     */
    VIGIL_API const vigil_value_t *vigil_map_get_cstr(const vigil_map_t *map, const char *key);
    VIGIL_API int vigil_map_contains(const vigil_map_t *map, const char *key, size_t key_length);
    VIGIL_API int vigil_map_contains_value(const vigil_map_t *map, const vigil_value_t *key);
    VIGIL_API int vigil_map_contains_cstr(const vigil_map_t *map, const char *key);
    /*
     * Iterates occupied entries by stable slot order. The returned key and value
     * point into the map's internal storage and are invalidated by later
     * mutations.
     */
    VIGIL_API int vigil_map_entry_at(const vigil_map_t *map, size_t index, const char **out_key, size_t *out_key_length,
                                     const vigil_value_t **out_value);
    VIGIL_API int vigil_map_entry_value_at(const vigil_map_t *map, size_t index, const vigil_value_t **out_key,
                                           const vigil_value_t **out_value);
    VIGIL_API vigil_status_t vigil_map_remove_value(vigil_map_t *map, const vigil_value_t *key, int *out_removed,
                                                    vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_map_remove(vigil_map_t *map, const char *key, size_t key_length, int *out_removed,
                                              vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_map_remove_cstr(vigil_map_t *map, const char *key, int *out_removed,
                                                   vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
