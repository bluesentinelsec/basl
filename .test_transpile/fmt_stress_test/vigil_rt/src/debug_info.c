/* VIGIL debug info: local variable tables and program symbol tables.
 *
 * Used by debuggers (variable inspection) and LSP (go-to-definition,
 * find-references, hover).
 */
#include <string.h>

#include "vigil/debug_info.h"
#include "vigil/status.h"

/* ── Local variable table ────────────────────────────────────────── */

void vigil_debug_local_table_init(vigil_debug_local_table_t *table, vigil_runtime_t *runtime)
{
    if (table == NULL)
        return;
    memset(table, 0, sizeof(*table));
    table->runtime = runtime;
}

void vigil_debug_local_table_free(vigil_debug_local_table_t *table)
{
    void *memory;
    if (table == NULL)
        return;
    if (table->locals != NULL)
    {
        memory = table->locals;
        vigil_runtime_free(table->runtime, &memory);
        table->locals = NULL;
    }
    table->count = 0U;
    table->capacity = 0U;
}

vigil_status_t vigil_debug_local_table_add(vigil_debug_local_table_t *table, const char *name, size_t name_length,
                                           size_t slot, size_t scope_start_ip, vigil_error_t *error)
{
    vigil_debug_local_t *entry;

    if (table == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (table->count >= table->capacity)
    {
        size_t new_cap = table->capacity == 0U ? 8U : table->capacity * 2U;
        void *memory = NULL;
        vigil_status_t status =
            vigil_runtime_alloc(table->runtime, new_cap * sizeof(vigil_debug_local_t), &memory, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        if (table->locals != NULL)
        {
            memcpy(memory, table->locals, table->count * sizeof(vigil_debug_local_t));
            void *old = table->locals;
            vigil_runtime_free(table->runtime, &old);
        }
        table->locals = (vigil_debug_local_t *)memory;
        table->capacity = new_cap;
    }

    entry = &table->locals[table->count];
    entry->name = name;
    entry->name_length = name_length;
    entry->slot = slot;
    entry->scope_start_ip = scope_start_ip;
    entry->scope_end_ip = SIZE_MAX; /* open until closed */
    table->count += 1U;
    return VIGIL_STATUS_OK;
}

void vigil_debug_local_table_close_scope(vigil_debug_local_table_t *table, size_t min_slot, size_t scope_end_ip)
{
    size_t i;
    if (table == NULL)
        return;
    for (i = table->count; i > 0U; i -= 1U)
    {
        vigil_debug_local_t *entry = &table->locals[i - 1U];
        if (entry->scope_end_ip != SIZE_MAX)
            continue;
        if (entry->slot < min_slot)
            break;
        entry->scope_end_ip = scope_end_ip;
    }
}

const vigil_debug_local_t *vigil_debug_local_table_find(const vigil_debug_local_table_t *table, size_t ip)
{
    /* Returns the first local whose scope contains the given IP.
     * For a full list, callers should iterate with _get/_count. */
    size_t i;
    if (table == NULL)
        return NULL;
    for (i = 0U; i < table->count; i += 1U)
    {
        const vigil_debug_local_t *entry = &table->locals[i];
        if (ip >= entry->scope_start_ip && ip < entry->scope_end_ip)
        {
            return entry;
        }
    }
    return NULL;
}

size_t vigil_debug_local_table_count(const vigil_debug_local_table_t *table)
{
    return table != NULL ? table->count : 0U;
}

const vigil_debug_local_t *vigil_debug_local_table_get(const vigil_debug_local_table_t *table, size_t index)
{
    if (table == NULL || index >= table->count)
        return NULL;
    return &table->locals[index];
}

/* ── Symbol table ────────────────────────────────────────────────── */

void vigil_debug_symbol_table_init(vigil_debug_symbol_table_t *table, vigil_runtime_t *runtime)
{
    if (table == NULL)
        return;
    memset(table, 0, sizeof(*table));
    table->runtime = runtime;
}

void vigil_debug_symbol_table_free(vigil_debug_symbol_table_t *table)
{
    void *memory;
    if (table == NULL)
        return;
    if (table->symbols != NULL)
    {
        memory = table->symbols;
        vigil_runtime_free(table->runtime, &memory);
        table->symbols = NULL;
    }
    table->count = 0U;
    table->capacity = 0U;
}

vigil_status_t vigil_debug_symbol_table_add(vigil_debug_symbol_table_t *table, vigil_debug_symbol_kind_t kind,
                                            const char *name, size_t name_length, vigil_source_span_t span,
                                            int is_public, size_t parent_index, vigil_error_t *error)
{
    vigil_debug_symbol_t *entry;

    if (table == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (table->count >= table->capacity)
    {
        size_t new_cap = table->capacity == 0U ? 16U : table->capacity * 2U;
        void *memory = NULL;
        vigil_status_t status =
            vigil_runtime_alloc(table->runtime, new_cap * sizeof(vigil_debug_symbol_t), &memory, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        if (table->symbols != NULL)
        {
            memcpy(memory, table->symbols, table->count * sizeof(vigil_debug_symbol_t));
            void *old = table->symbols;
            vigil_runtime_free(table->runtime, &old);
        }
        table->symbols = (vigil_debug_symbol_t *)memory;
        table->capacity = new_cap;
    }

    entry = &table->symbols[table->count];
    entry->kind = kind;
    entry->name = name;
    entry->name_length = name_length;
    entry->span = span;
    entry->is_public = is_public;
    entry->parent_index = parent_index;
    table->count += 1U;
    return VIGIL_STATUS_OK;
}

size_t vigil_debug_symbol_table_count(const vigil_debug_symbol_table_t *table)
{
    return table != NULL ? table->count : 0U;
}

const vigil_debug_symbol_t *vigil_debug_symbol_table_get(const vigil_debug_symbol_table_t *table, size_t index)
{
    if (table == NULL || index >= table->count)
        return NULL;
    return &table->symbols[index];
}
