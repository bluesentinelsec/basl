#ifndef VIGIL_TOML_H
#define VIGIL_TOML_H

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

    typedef enum vigil_toml_type
    {
        VIGIL_TOML_STRING = 0,
        VIGIL_TOML_INTEGER = 1,
        VIGIL_TOML_FLOAT = 2,
        VIGIL_TOML_BOOL = 3,
        VIGIL_TOML_DATETIME = 4,
        VIGIL_TOML_ARRAY = 5,
        VIGIL_TOML_TABLE = 6
    } vigil_toml_type_t;

    /*
     * Date-time representation.
     * Fields are set based on which components are present:
     *   Offset Date-Time:  all fields valid, has_date=has_time=has_offset=1
     *   Local Date-Time:   has_date=has_time=1, has_offset=0
     *   Local Date:        has_date=1, has_time=has_offset=0
     *   Local Time:        has_time=1, has_date=has_offset=0
     */
    typedef struct vigil_toml_datetime
    {
        int year;
        int month;
        int day;
        int hour;
        int minute;
        int second;
        int nanosecond;
        int offset_minutes; /* signed: UTC offset in minutes */
        int has_date;
        int has_time;
        int has_offset;
    } vigil_toml_datetime_t;

    typedef struct vigil_toml_value vigil_toml_value_t;

    /* ── Lifecycle ───────────────────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_toml_string_new(const vigil_allocator_t *allocator, const char *value, size_t length,
                                                   vigil_toml_value_t **out, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_toml_integer_new(const vigil_allocator_t *allocator, int64_t value,
                                                    vigil_toml_value_t **out, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_toml_float_new(const vigil_allocator_t *allocator, double value,
                                                  vigil_toml_value_t **out, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_toml_bool_new(const vigil_allocator_t *allocator, int value,
                                                 vigil_toml_value_t **out, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_toml_datetime_new(const vigil_allocator_t *allocator,
                                                     const vigil_toml_datetime_t *value, vigil_toml_value_t **out,
                                                     vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_toml_array_new(const vigil_allocator_t *allocator, vigil_toml_value_t **out,
                                                  vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_toml_table_new(const vigil_allocator_t *allocator, vigil_toml_value_t **out,
                                                  vigil_error_t *error);

    VIGIL_API void vigil_toml_free(vigil_toml_value_t **value);

    /* ── Type inspection ─────────────────────────────────────────────── */

    VIGIL_API vigil_toml_type_t vigil_toml_type(const vigil_toml_value_t *value);

    /* ── Scalar accessors ────────────────────────────────────────────── */

    VIGIL_API const char *vigil_toml_string_value(const vigil_toml_value_t *value);
    VIGIL_API size_t vigil_toml_string_length(const vigil_toml_value_t *value);
    VIGIL_API int64_t vigil_toml_integer_value(const vigil_toml_value_t *value);
    VIGIL_API double vigil_toml_float_value(const vigil_toml_value_t *value);
    VIGIL_API int vigil_toml_bool_value(const vigil_toml_value_t *value);
    VIGIL_API const vigil_toml_datetime_t *vigil_toml_datetime_value(const vigil_toml_value_t *value);

    /* ── Array operations ────────────────────────────────────────────── */

    VIGIL_API size_t vigil_toml_array_count(const vigil_toml_value_t *array);

    VIGIL_API const vigil_toml_value_t *vigil_toml_array_get(const vigil_toml_value_t *array, size_t index);

    /** Takes ownership of element. */
    VIGIL_API vigil_status_t vigil_toml_array_push(vigil_toml_value_t *array, vigil_toml_value_t *element,
                                                   vigil_error_t *error);

    /* ── Table operations ────────────────────────────────────────────── */

    VIGIL_API size_t vigil_toml_table_count(const vigil_toml_value_t *table);

    /** Returns NULL if key not found. */
    VIGIL_API const vigil_toml_value_t *vigil_toml_table_get(const vigil_toml_value_t *table, const char *key);

    /** Takes ownership of value.  Errors if key already exists. */
    VIGIL_API vigil_status_t vigil_toml_table_set(vigil_toml_value_t *table, const char *key, size_t key_length,
                                                  vigil_toml_value_t *value, vigil_error_t *error);

    /** Remove a key from a table.  Returns OK if removed, INVALID_ARGUMENT if not found. */
    VIGIL_API vigil_status_t vigil_toml_table_remove(vigil_toml_value_t *table, const char *key, vigil_error_t *error);

    /** Iteration: returns key/value at position index (insertion order). */
    VIGIL_API vigil_status_t vigil_toml_table_entry(const vigil_toml_value_t *table, size_t index, const char **out_key,
                                                    size_t *out_key_length, const vigil_toml_value_t **out_value);

    /* ── Dotted key convenience ──────────────────────────────────────── */

    /**
     * Traverse nested tables by dotted path.
     * vigil_toml_table_get_path(root, "deps.json_schema") is equivalent to
     * vigil_toml_table_get(vigil_toml_table_get(root, "deps"), "json_schema").
     * Returns NULL if any segment is missing or not a table.
     */
    VIGIL_API const vigil_toml_value_t *vigil_toml_table_get_path(const vigil_toml_value_t *table,
                                                                  const char *dotted_key);

    /* ── Parser ──────────────────────────────────────────────────────── */

    /**
     * Parse a TOML document.  Returns a table value representing the root.
     * Conforms to TOML v1.0.0.
     */
    VIGIL_API vigil_status_t vigil_toml_parse(const vigil_allocator_t *allocator, const char *input, size_t length,
                                              vigil_toml_value_t **out, vigil_error_t *error);

    /* ── Emitter ─────────────────────────────────────────────────────── */

    /**
     * Serialize a TOML table to a string.  The root value must be a table.
     * Caller must free *out_string via the allocator (or free() if NULL).
     */
    VIGIL_API vigil_status_t vigil_toml_emit(const vigil_toml_value_t *value, char **out_string, size_t *out_length,
                                             vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
