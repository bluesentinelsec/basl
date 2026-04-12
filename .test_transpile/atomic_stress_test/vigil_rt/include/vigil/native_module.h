#ifndef VIGIL_NATIVE_MODULE_H
#define VIGIL_NATIVE_MODULE_H

#include <stddef.h>

#include "vigil/allocator.h"
#include "vigil/debug_info.h"
#include "vigil/diagnostic.h"
#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/value.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Describes a type for native module parameters and returns.
     * Supports primitives, arrays, and maps with full type parameters.
     */
    typedef struct vigil_native_type
    {
        int kind;         /* vigil_type_kind_t */
        int object_kind;  /* 0=primitive, 4=array, 5=map (matches vigil_binding_object_kind_t) */
        int element_type; /* For arrays: element vigil_type_kind_t */
        int key_type;     /* For maps: key vigil_type_kind_t */
        int value_type;   /* For maps: value vigil_type_kind_t */
    } vigil_native_type_t;

    /* Helper macros for defining native types */
    /* clang-format off */
#define VIGIL_NATIVE_TYPE_PRIMITIVE(k)                                                                                 \
    {                                                                                                                  \
        (k), 0, 0, 0, 0                                                                                               \
    }
#define VIGIL_NATIVE_TYPE_ARRAY(elem)                                                                                  \
    {                                                                                                                  \
        VIGIL_TYPE_OBJECT, 4, (elem), 0, 0                                                                             \
    }
#define VIGIL_NATIVE_TYPE_MAP(k, v)                                                                                    \
    {                                                                                                                  \
        VIGIL_TYPE_OBJECT, 5, 0, (k), (v)                                                                              \
    }
    /* clang-format on */

    typedef struct vigil_native_symbol_doc
    {
        const char *summary;
        const char *description;
        const char *example;
    } vigil_native_symbol_doc_t;

    /**
     * Describes one function exported by a native module.
     * The compiler uses name, param_types, param_count, and return_type
     * for type checking.  The VM uses native_fn at runtime.
     */
    typedef struct vigil_native_module_function
    {
        const char *name;
        size_t name_length;
        vigil_native_fn_t native_fn;
        size_t param_count;
        const int *param_types;  /* vigil_type_kind_t values (legacy, for simple types) */
        int return_type;         /* vigil_type_kind_t */
        size_t return_count;     /* number of return values (1 or 2 for err) */
        const int *return_types; /* array of vigil_type_kind_t, length = return_count */
        int return_element_type; /* For array returns: element type (vigil_type_kind_t) */
        /* Extended type info (optional, NULL = use legacy param_types) */
        const vigil_native_type_t *param_types_ext;
        const vigil_native_type_t *return_type_ext;
        size_t return_same_as_param_1based; /* 0 = static return type, otherwise same type as this arg (1-based) */
        const char *const *doc_param_names;
        const char *const *doc_param_type_names;
        const char *doc_return_type_name;
        const vigil_native_symbol_doc_t *doc;
    } vigil_native_module_function_t;

    /**
     * Describes a native module (e.g. "fmt", "math").
     */

    typedef struct vigil_native_class_field
    {
        const char *name;
        size_t name_length;
        int type;               /* vigil_type_kind_t */
        int object_kind;        /* 0 = primitive. See VIGIL_NATIVE_FIELD_* below. */
        const char *class_name; /* object_kind == CLASS: class name to resolve */
        size_t class_name_length;
        int element_type; /* object_kind == ARRAY: element vigil_type_kind_t */
        const char *doc_type_name;
        const vigil_native_symbol_doc_t *doc;
    } vigil_native_class_field_t;

/* Object kind constants for native class fields.
 * Values match vigil_binding_object_kind_t in the internal API. */
#define VIGIL_NATIVE_FIELD_PRIMITIVE 0
#define VIGIL_NATIVE_FIELD_CLASS 1
#define VIGIL_NATIVE_FIELD_ARRAY 4

    typedef struct vigil_native_class_method
    {
        const char *name;
        size_t name_length;
        vigil_native_fn_t native_fn;
        size_t param_count;     /* excluding self for instance methods */
        const int *param_types; /* vigil_type_kind_t values, excluding self */
        int return_type;        /* vigil_type_kind_t */
        size_t return_count;
        const int *return_types;
        int is_static;                 /* 1 = no self argument */
        const char *return_class_name; /* non-NULL: resolve to this class */
        size_t return_class_name_length;
        int return_element_type; /* For array returns: element type (vigil_type_kind_t) */
        const char *const *doc_param_names;
        const char *const *doc_param_type_names;
        const char *doc_return_type_name;
        const vigil_native_symbol_doc_t *doc;
    } vigil_native_class_method_t;

    typedef struct vigil_native_class
    {
        const char *name;
        size_t name_length;
        const vigil_native_class_field_t *fields;
        size_t field_count;
        const vigil_native_class_method_t *methods;
        size_t method_count;
        vigil_native_fn_t constructor; /* NULL = default (field-per-arg) */
        const vigil_native_symbol_doc_t *doc;
    } vigil_native_class_t;

    typedef struct vigil_native_module_constant
    {
        const char *name;
        size_t name_length;
        int type;           /* vigil_type_kind_t */
        int64_t int_value;  /* for i32/i64/u8/u32/u64 */
        double float_value; /* for f64 */
        const vigil_native_symbol_doc_t *doc;
    } vigil_native_module_constant_t;

    typedef struct vigil_native_module
    {
        const char *name;
        size_t name_length;
        const vigil_native_module_function_t *functions;
        size_t function_count;
        const vigil_native_class_t *classes;
        size_t class_count;
        const vigil_native_symbol_doc_t *doc;
        const vigil_native_module_constant_t *constants;
        size_t constant_count;
    } vigil_native_module_t;

    /**
     * Registry of native modules, shared between compiler and VM.
     */
    typedef struct vigil_native_registry
    {
        const vigil_native_module_t **modules;
        size_t module_count;
        size_t module_capacity;
        vigil_allocator_t allocator;
    } vigil_native_registry_t;

    VIGIL_API void vigil_native_registry_init(vigil_native_registry_t *registry);
    VIGIL_API void vigil_native_registry_free(vigil_native_registry_t *registry);
    VIGIL_API vigil_status_t vigil_native_registry_add(vigil_native_registry_t *registry,
                                                       const vigil_native_module_t *module, vigil_error_t *error);
    VIGIL_API const vigil_native_module_t *vigil_native_registry_find(const vigil_native_registry_t *registry,
                                                                      const char *name, size_t name_length);
    VIGIL_API int vigil_native_registry_find_index(const vigil_native_registry_t *registry, const char *name,
                                                   size_t name_length, size_t *out_index);

/* Synthetic source IDs for native modules: 0xFFFF0000 + index. */
#define VIGIL_NATIVE_SOURCE_ID_BASE ((vigil_source_id_t)0xFFFF0000U)
#define VIGIL_NATIVE_SOURCE_ID(idx) (VIGIL_NATIVE_SOURCE_ID_BASE + (vigil_source_id_t)(idx))
#define VIGIL_IS_NATIVE_SOURCE_ID(id) ((id) >= VIGIL_NATIVE_SOURCE_ID_BASE)
#define VIGIL_NATIVE_SOURCE_INDEX(id) ((size_t)((id) - VIGIL_NATIVE_SOURCE_ID_BASE))

    /**
     * Extended compile API that accepts a native module registry.
     */
    VIGIL_API vigil_status_t vigil_compile_source_with_natives(
        const vigil_source_registry_t *registry, vigil_source_id_t source_id, const vigil_native_registry_t *natives,
        vigil_object_t **out_function, vigil_diagnostic_list_t *diagnostics, vigil_error_t *error);

    /**
     * Compile with debug symbol table output.
     * If out_symbols is non-NULL, it will be populated with all program-level
     * symbol definitions (functions, classes, interfaces, enums, fields,
     * methods, globals). The caller must free it with
     * vigil_debug_symbol_table_free().
     */
    VIGIL_API vigil_status_t vigil_compile_source_with_debug_info(
        const vigil_source_registry_t *registry, vigil_source_id_t source_id, const vigil_native_registry_t *natives,
        vigil_object_t **out_function, vigil_diagnostic_list_t *diagnostics, vigil_debug_symbol_table_t *out_symbols,
        vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
