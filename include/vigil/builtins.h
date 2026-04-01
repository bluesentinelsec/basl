#ifndef VIGIL_BUILTINS_H
#define VIGIL_BUILTINS_H

#include <stddef.h>

#include "vigil/doc_registry.h"
#include "vigil/export.h"
#include "vigil/type.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum vigil_builtin_kind
    {
        VIGIL_BUILTIN_LEN = 0,
        VIGIL_BUILTIN_CHAR,
        VIGIL_BUILTIN_ERR,
        VIGIL_BUILTIN_I32,
        VIGIL_BUILTIN_I64,
        VIGIL_BUILTIN_U8,
        VIGIL_BUILTIN_U32,
        VIGIL_BUILTIN_U64,
        VIGIL_BUILTIN_F64,
        VIGIL_BUILTIN_BOOL,
        VIGIL_BUILTIN_STRING
    } vigil_builtin_kind_t;

    typedef struct vigil_builtin_descriptor
    {
        vigil_builtin_kind_t kind;
        const char *name;
        size_t name_length;
        int is_conversion;
        vigil_type_kind_t conversion_target;
        const vigil_doc_entry_t *doc_entry;
    } vigil_builtin_descriptor_t;

    typedef struct vigil_string_method_descriptor
    {
        const char *name;
        size_t name_length;
        const vigil_doc_entry_t *doc_entry;
    } vigil_string_method_descriptor_t;

    VIGIL_API const vigil_builtin_descriptor_t *vigil_builtin_descriptors(size_t *count);
    VIGIL_API const vigil_builtin_descriptor_t *vigil_builtin_find(const char *name, size_t name_length);
    VIGIL_API const vigil_doc_entry_t *vigil_builtin_doc_entries(size_t *count);
    VIGIL_API const vigil_doc_entry_t *vigil_builtin_doc_lookup(const char *name);

    VIGIL_API const vigil_string_method_descriptor_t *vigil_string_method_descriptors(size_t *count);
    VIGIL_API const vigil_doc_entry_t *vigil_string_method_doc_entries(size_t *count);
    VIGIL_API const vigil_doc_entry_t *vigil_string_method_doc_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
