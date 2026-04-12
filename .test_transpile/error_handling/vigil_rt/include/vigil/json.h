#ifndef VIGIL_JSON_H
#define VIGIL_JSON_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Value types ─────────────────────────────────────────────────── */

    typedef enum vigil_json_type
    {
        VIGIL_JSON_NULL = 0,
        VIGIL_JSON_BOOL = 1,
        VIGIL_JSON_NUMBER = 2,
        VIGIL_JSON_STRING = 3,
        VIGIL_JSON_ARRAY = 4,
        VIGIL_JSON_OBJECT = 5
    } vigil_json_type_t;

    typedef struct vigil_json_value vigil_json_value_t;

    /* ── Lifecycle ───────────────────────────────────────────────────── */

    /*
     * Create a JSON value.  If allocator is NULL, malloc/realloc/free are used.
     * The allocator pointer is stored and must remain valid for the value's
     * lifetime.  All child values share the root's allocator.
     */
    VIGIL_API vigil_status_t vigil_json_null_new(const vigil_allocator_t *allocator, vigil_json_value_t **out,
                                                 vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_json_bool_new(const vigil_allocator_t *allocator, int value,
                                                 vigil_json_value_t **out, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_json_number_new(const vigil_allocator_t *allocator, double value,
                                                   vigil_json_value_t **out, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_json_string_new(const vigil_allocator_t *allocator, const char *value, size_t length,
                                                   vigil_json_value_t **out, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_json_array_new(const vigil_allocator_t *allocator, vigil_json_value_t **out,
                                                  vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_json_object_new(const vigil_allocator_t *allocator, vigil_json_value_t **out,
                                                   vigil_error_t *error);

    /* Recursively frees the value and all children. */
    VIGIL_API void vigil_json_free(vigil_json_value_t **value);

    /* ── Type inspection ─────────────────────────────────────────────── */

    VIGIL_API vigil_json_type_t vigil_json_type(const vigil_json_value_t *value);

    /* ── Scalar accessors ────────────────────────────────────────────── */

    VIGIL_API int vigil_json_bool_value(const vigil_json_value_t *value);
    VIGIL_API double vigil_json_number_value(const vigil_json_value_t *value);
    VIGIL_API const char *vigil_json_string_value(const vigil_json_value_t *value);
    VIGIL_API size_t vigil_json_string_length(const vigil_json_value_t *value);

    /* ── Array operations ────────────────────────────────────────────── */

    VIGIL_API size_t vigil_json_array_count(const vigil_json_value_t *array);

    VIGIL_API const vigil_json_value_t *vigil_json_array_get(const vigil_json_value_t *array, size_t index);

    /* Takes ownership of element. */
    VIGIL_API vigil_status_t vigil_json_array_push(vigil_json_value_t *array, vigil_json_value_t *element,
                                                   vigil_error_t *error);

    /* ── Object operations ───────────────────────────────────────────── */

    VIGIL_API size_t vigil_json_object_count(const vigil_json_value_t *object);

    /* Returns NULL if key not found. */
    VIGIL_API const vigil_json_value_t *vigil_json_object_get(const vigil_json_value_t *object, const char *key);

    /* Takes ownership of value.  Replaces existing key if present. */
    VIGIL_API vigil_status_t vigil_json_object_set(vigil_json_value_t *object, const char *key, size_t key_length,
                                                   vigil_json_value_t *value, vigil_error_t *error);

    /* Iteration: returns key/value at position index (insertion order). */
    VIGIL_API vigil_status_t vigil_json_object_entry(const vigil_json_value_t *object, size_t index,
                                                     const char **out_key, size_t *out_key_length,
                                                     const vigil_json_value_t **out_value);

    /* ── Parser ──────────────────────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_json_parse(const vigil_allocator_t *allocator, const char *input, size_t length,
                                              vigil_json_value_t **out, vigil_error_t *error);

    /* ── Emitter ─────────────────────────────────────────────────────── */

    /*
     * Serialize a JSON value to a string.  The caller must free *out_string
     * via the allocator (or free() if allocator was NULL).
     */
    VIGIL_API vigil_status_t vigil_json_emit(const vigil_json_value_t *value, char **out_string, size_t *out_length,
                                             vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
