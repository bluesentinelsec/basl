#ifndef VIGIL_SYMBOL_H
#define VIGIL_SYMBOL_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"
#include "vigil/map.h"
#include "vigil/runtime.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef uint32_t vigil_symbol_t;

#define VIGIL_SYMBOL_INVALID ((vigil_symbol_t)0U)

    typedef struct vigil_symbol_table
    {
        vigil_runtime_t *runtime;
        vigil_map_t by_name;
        void *strings;
        size_t count;
        size_t capacity;
    } vigil_symbol_table_t;

    VIGIL_API void vigil_symbol_table_init(vigil_symbol_table_t *table, vigil_runtime_t *runtime);
    VIGIL_API void vigil_symbol_table_clear(vigil_symbol_table_t *table);
    VIGIL_API void vigil_symbol_table_free(vigil_symbol_table_t *table);
    VIGIL_API size_t vigil_symbol_table_count(const vigil_symbol_table_t *table);
    VIGIL_API vigil_status_t vigil_symbol_table_intern(vigil_symbol_table_t *table, const char *text, size_t length,
                                                       vigil_symbol_t *out_symbol, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_symbol_table_intern_cstr(vigil_symbol_table_t *table, const char *text,
                                                            vigil_symbol_t *out_symbol, vigil_error_t *error);
    VIGIL_API const char *vigil_symbol_table_c_str(const vigil_symbol_table_t *table, vigil_symbol_t symbol);
    VIGIL_API size_t vigil_symbol_table_length(const vigil_symbol_table_t *table, vigil_symbol_t symbol);
    VIGIL_API int vigil_symbol_table_is_valid(const vigil_symbol_table_t *table, vigil_symbol_t symbol);

#ifdef __cplusplus
}
#endif

#endif
