#include <limits.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/string.h"
#include "vigil/symbol.h"

static vigil_string_t *vigil_symbol_strings(vigil_symbol_table_t *table)
{
    return (vigil_string_t *)table->strings;
}

static const vigil_string_t *vigil_symbol_const_strings(const vigil_symbol_table_t *table)
{
    return (const vigil_string_t *)table->strings;
}

static int vigil_symbol_table_validate_mutable(const vigil_symbol_table_t *table, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (table == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "symbol table must not be null");
        return 0;
    }

    if (table->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "symbol table runtime must not be null");
        return 0;
    }

    return 1;
}

static size_t vigil_symbol_next_capacity(size_t current_capacity)
{
    if (current_capacity == 0U)
    {
        return 16U;
    }

    return current_capacity * 2U;
}

static vigil_status_t vigil_symbol_table_grow(vigil_symbol_table_t *table, size_t minimum_capacity,
                                              vigil_error_t *error)
{
    vigil_status_t status;
    vigil_string_t *strings;
    size_t new_capacity;
    size_t old_capacity;
    void *memory;

    if (table->capacity >= minimum_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    new_capacity = vigil_symbol_next_capacity(table->capacity);
    while (new_capacity < minimum_capacity)
    {
        if (new_capacity > SIZE_MAX / 2U)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "symbol table capacity would overflow");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }

        new_capacity *= 2U;
    }

    if (new_capacity > SIZE_MAX / sizeof(*strings))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "symbol table capacity would overflow");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    old_capacity = table->capacity;
    memory = table->strings;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(table->runtime, new_capacity * sizeof(*strings), &memory, error);
    }
    else
    {
        status = vigil_runtime_realloc(table->runtime, &memory, new_capacity * sizeof(*strings), error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_string_t *)memory + old_capacity, 0, (new_capacity - old_capacity) * sizeof(*strings));
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    table->strings = memory;
    table->capacity = new_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_symbol_table_init(vigil_symbol_table_t *table, vigil_runtime_t *runtime)
{
    if (table == NULL)
    {
        return;
    }

    memset(table, 0, sizeof(*table));
    table->runtime = runtime;
    vigil_map_init(&table->by_name, runtime);
}

void vigil_symbol_table_clear(vigil_symbol_table_t *table)
{
    size_t index;
    vigil_string_t *strings;

    if (table == NULL)
    {
        return;
    }

    strings = vigil_symbol_strings(table);
    for (index = 0U; index < table->count; index += 1U)
    {
        vigil_string_free(&strings[index]);
        memset(&strings[index], 0, sizeof(strings[index]));
    }

    vigil_map_clear(&table->by_name);
    table->count = 0U;
}

void vigil_symbol_table_free(vigil_symbol_table_t *table)
{
    void *memory;

    if (table == NULL)
    {
        return;
    }

    vigil_symbol_table_clear(table);
    memory = table->strings;
    if (table->runtime != NULL)
    {
        vigil_runtime_free(table->runtime, &memory);
    }
    vigil_map_free(&table->by_name);
    memset(table, 0, sizeof(*table));
}

size_t vigil_symbol_table_count(const vigil_symbol_table_t *table)
{
    if (table == NULL)
    {
        return 0U;
    }

    return table->count;
}

vigil_status_t vigil_symbol_table_intern(vigil_symbol_table_t *table, const char *text, size_t length,
                                         vigil_symbol_t *out_symbol, vigil_error_t *error)
{
    vigil_status_t status;
    const vigil_value_t *stored_value;
    vigil_value_t symbol_value;
    vigil_string_t *strings;
    vigil_symbol_t symbol;

    if (out_symbol != NULL)
    {
        *out_symbol = VIGIL_SYMBOL_INVALID;
    }

    if (!vigil_symbol_table_validate_mutable(table, error))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (text == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "symbol text must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (out_symbol == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_symbol must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    stored_value = vigil_map_get(&table->by_name, text, length);
    if (stored_value != NULL)
    {
        if (vigil_value_kind(stored_value) != VIGIL_VALUE_INT)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "symbol table map entry must be an integer");
            return VIGIL_STATUS_INTERNAL;
        }

        *out_symbol = (vigil_symbol_t)vigil_value_as_int(stored_value);
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    if (table->count == UINT32_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "symbol table is full");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_symbol_table_grow(table, table->count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    strings = vigil_symbol_strings(table);
    vigil_string_init(&strings[table->count], table->runtime);
    status = vigil_string_assign(&strings[table->count], text, length, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_string_free(&strings[table->count]);
        return status;
    }

    symbol = (vigil_symbol_t)(table->count + 1U);
    vigil_value_init_int(&symbol_value, (int64_t)symbol);
    status = vigil_map_set(&table->by_name, text, length, &symbol_value, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_string_free(&strings[table->count]);
        memset(&strings[table->count], 0, sizeof(strings[table->count]));
        return status;
    }

    table->count += 1U;
    *out_symbol = symbol;
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_symbol_table_intern_cstr(vigil_symbol_table_t *table, const char *text, vigil_symbol_t *out_symbol,
                                              vigil_error_t *error)
{
    if (text == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "symbol text must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_symbol_table_intern(table, text, strlen(text), out_symbol, error);
}

const char *vigil_symbol_table_c_str(const vigil_symbol_table_t *table, vigil_symbol_t symbol)
{
    const vigil_string_t *strings;

    if (!vigil_symbol_table_is_valid(table, symbol))
    {
        return NULL;
    }

    strings = vigil_symbol_const_strings(table);
    return vigil_string_c_str(&strings[symbol - 1U]);
}

size_t vigil_symbol_table_length(const vigil_symbol_table_t *table, vigil_symbol_t symbol)
{
    const vigil_string_t *strings;

    if (!vigil_symbol_table_is_valid(table, symbol))
    {
        return 0U;
    }

    strings = vigil_symbol_const_strings(table);
    return vigil_string_length(&strings[symbol - 1U]);
}

int vigil_symbol_table_is_valid(const vigil_symbol_table_t *table, vigil_symbol_t symbol)
{
    if (table == NULL || symbol == VIGIL_SYMBOL_INVALID)
    {
        return 0;
    }

    return (size_t)symbol <= table->count;
}
