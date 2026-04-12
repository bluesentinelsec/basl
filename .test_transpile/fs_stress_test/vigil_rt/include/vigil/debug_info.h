#ifndef VIGIL_DEBUG_INFO_H
#define VIGIL_DEBUG_INFO_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/source.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Per-chunk local variable debug info ─────────────────────────── */

    typedef struct vigil_debug_local
    {
        const char *name;
        size_t name_length;
        size_t slot;
        size_t scope_start_ip; /* IP when local comes into scope */
        size_t scope_end_ip;   /* IP when local goes out of scope */
    } vigil_debug_local_t;

    typedef struct vigil_debug_local_table
    {
        vigil_runtime_t *runtime;
        vigil_debug_local_t *locals;
        size_t count;
        size_t capacity;
    } vigil_debug_local_table_t;

    VIGIL_API void vigil_debug_local_table_init(vigil_debug_local_table_t *table, vigil_runtime_t *runtime);
    VIGIL_API void vigil_debug_local_table_free(vigil_debug_local_table_t *table);
    VIGIL_API vigil_status_t vigil_debug_local_table_add(vigil_debug_local_table_t *table, const char *name,
                                                         size_t name_length, size_t slot, size_t scope_start_ip,
                                                         vigil_error_t *error);
    VIGIL_API void vigil_debug_local_table_close_scope(vigil_debug_local_table_t *table, size_t min_slot,
                                                       size_t scope_end_ip);
    VIGIL_API const vigil_debug_local_t *vigil_debug_local_table_find(const vigil_debug_local_table_t *table,
                                                                      size_t ip);
    VIGIL_API size_t vigil_debug_local_table_count(const vigil_debug_local_table_t *table);
    VIGIL_API const vigil_debug_local_t *vigil_debug_local_table_get(const vigil_debug_local_table_t *table,
                                                                     size_t index);

    /* ── Program-level symbol table ──────────────────────────────────── */

    typedef enum vigil_debug_symbol_kind
    {
        VIGIL_DEBUG_SYMBOL_FUNCTION = 0,
        VIGIL_DEBUG_SYMBOL_CLASS = 1,
        VIGIL_DEBUG_SYMBOL_INTERFACE = 2,
        VIGIL_DEBUG_SYMBOL_ENUM = 3,
        VIGIL_DEBUG_SYMBOL_ENUM_MEMBER = 4,
        VIGIL_DEBUG_SYMBOL_FIELD = 5,
        VIGIL_DEBUG_SYMBOL_METHOD = 6,
        VIGIL_DEBUG_SYMBOL_GLOBAL_CONST = 7,
        VIGIL_DEBUG_SYMBOL_GLOBAL_VAR = 8
    } vigil_debug_symbol_kind_t;

    typedef struct vigil_debug_symbol
    {
        vigil_debug_symbol_kind_t kind;
        const char *name;
        size_t name_length;
        vigil_source_span_t span;
        int is_public;
        size_t parent_index; /* index of owning class/interface/enum, or SIZE_MAX */
    } vigil_debug_symbol_t;

    typedef struct vigil_debug_symbol_table
    {
        vigil_runtime_t *runtime;
        vigil_debug_symbol_t *symbols;
        size_t count;
        size_t capacity;
    } vigil_debug_symbol_table_t;

    VIGIL_API void vigil_debug_symbol_table_init(vigil_debug_symbol_table_t *table, vigil_runtime_t *runtime);
    VIGIL_API void vigil_debug_symbol_table_free(vigil_debug_symbol_table_t *table);
    VIGIL_API vigil_status_t vigil_debug_symbol_table_add(vigil_debug_symbol_table_t *table,
                                                          vigil_debug_symbol_kind_t kind, const char *name,
                                                          size_t name_length, vigil_source_span_t span, int is_public,
                                                          size_t parent_index, vigil_error_t *error);
    VIGIL_API size_t vigil_debug_symbol_table_count(const vigil_debug_symbol_table_t *table);
    VIGIL_API const vigil_debug_symbol_t *vigil_debug_symbol_table_get(const vigil_debug_symbol_table_t *table,
                                                                       size_t index);

#ifdef __cplusplus
}
#endif

#endif
