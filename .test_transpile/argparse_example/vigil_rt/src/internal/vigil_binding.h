#ifndef VIGIL_BINDING_H
#define VIGIL_BINDING_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/token.h"
#include "vigil/type.h"
#include "vigil/value.h"

typedef enum vigil_binding_object_kind
{
    VIGIL_BINDING_OBJECT_NONE = 0,
    VIGIL_BINDING_OBJECT_CLASS = 1,
    VIGIL_BINDING_OBJECT_INTERFACE = 2,
    VIGIL_BINDING_OBJECT_ENUM = 3,
    VIGIL_BINDING_OBJECT_ARRAY = 4,
    VIGIL_BINDING_OBJECT_MAP = 5,
    VIGIL_BINDING_OBJECT_FUNCTION = 6
} vigil_binding_object_kind_t;

typedef struct vigil_binding_type
{
    vigil_type_kind_t kind;
    vigil_binding_object_kind_t object_kind;
    size_t object_index;
} vigil_binding_type_t;

#define VIGIL_BINDING_INVALID_CLASS_INDEX SIZE_MAX

typedef struct vigil_binding_function_param
{
    const char *name;
    size_t length;
    vigil_binding_type_t type;
    vigil_source_span_t span;
} vigil_binding_function_param_t;

typedef struct vigil_binding_function_param_spec
{
    const char *name;
    size_t name_length;
    vigil_source_span_t span;
    vigil_binding_type_t type;
} vigil_binding_function_param_spec_t;

typedef struct vigil_binding_capture
{
    const char *name;
    size_t name_length;
    vigil_binding_type_t type;
    size_t source_local_index;
    int source_is_capture;
} vigil_binding_capture_t;

typedef enum vigil_binding_function_body_kind
{
    VIGIL_BINDING_FUNCTION_BODY_BLOCK = 0,
    VIGIL_BINDING_FUNCTION_BODY_EXPRESSION = 1
} vigil_binding_function_body_kind_t;

typedef struct vigil_binding_function
{
    const char *name;
    size_t name_length;
    vigil_source_span_t name_span;
    int is_public;
    vigil_binding_type_t return_type;
    vigil_binding_type_t *return_types;
    size_t return_count;
    size_t return_capacity;
    size_t owner_class_index;
    vigil_binding_function_param_t *params;
    size_t param_count;
    size_t param_capacity;
    vigil_binding_capture_t *captures;
    size_t capture_count;
    size_t capture_capacity;
    size_t body_start;
    size_t body_end;
    vigil_binding_function_body_kind_t body_kind;
    const vigil_source_file_t *source;
    const vigil_token_list_t *tokens;
    vigil_object_t *object;
    int is_local;
} vigil_binding_function_t;

typedef struct vigil_binding_function_table
{
    vigil_runtime_t *runtime;
    vigil_binding_function_t *functions;
    size_t count;
    size_t capacity;
    size_t main_index;
    int has_main;
} vigil_binding_function_table_t;

typedef struct vigil_binding_local
{
    const char *name;
    size_t length;
    size_t depth;
    vigil_binding_type_t type;
    int is_const;
    int is_param;
} vigil_binding_local_t;

typedef struct vigil_binding_local_spec
{
    const char *name;
    size_t name_length;
    vigil_binding_type_t type;
    int is_const;
    int is_param;
} vigil_binding_local_spec_t;

typedef struct vigil_binding_scope_stack
{
    vigil_runtime_t *runtime;
    vigil_binding_local_t *locals;
    size_t local_count;
    size_t local_capacity;
    size_t scope_depth;
} vigil_binding_scope_stack_t;

VIGIL_API vigil_binding_type_t vigil_binding_type_invalid(void);
VIGIL_API vigil_binding_type_t vigil_binding_type_primitive(vigil_type_kind_t kind);
VIGIL_API vigil_binding_type_t vigil_binding_type_class(size_t class_index);
VIGIL_API vigil_binding_type_t vigil_binding_type_interface(size_t interface_index);
VIGIL_API vigil_binding_type_t vigil_binding_type_enum(size_t enum_index);
VIGIL_API vigil_binding_type_t vigil_binding_type_array(size_t array_index);
VIGIL_API vigil_binding_type_t vigil_binding_type_map(size_t map_index);
VIGIL_API vigil_binding_type_t vigil_binding_type_function(size_t function_type_index);
VIGIL_API int vigil_binding_type_is_valid(vigil_binding_type_t type);
VIGIL_API int vigil_binding_type_equal(vigil_binding_type_t left, vigil_binding_type_t right);

VIGIL_API void vigil_binding_function_init(vigil_binding_function_t *function);
VIGIL_API void vigil_binding_function_free(vigil_runtime_t *runtime, vigil_binding_function_t *function);
VIGIL_API vigil_status_t vigil_binding_function_add_param(vigil_runtime_t *runtime, vigil_binding_function_t *function,
                                                          const vigil_binding_function_param_spec_t *spec,
                                                          vigil_error_t *error);
VIGIL_API vigil_status_t vigil_binding_function_add_return_type(vigil_runtime_t *runtime,
                                                                vigil_binding_function_t *function,
                                                                vigil_binding_type_t type, vigil_error_t *error);

VIGIL_API void vigil_binding_function_table_init(vigil_binding_function_table_t *table, vigil_runtime_t *runtime);
VIGIL_API void vigil_binding_function_table_free(vigil_binding_function_table_t *table);
VIGIL_API vigil_status_t vigil_binding_function_table_append(vigil_binding_function_table_t *table,
                                                             vigil_binding_function_t *function, size_t *out_index,
                                                             vigil_error_t *error);
VIGIL_API int vigil_binding_function_table_find(const vigil_binding_function_table_t *table, const char *name,
                                                size_t name_length, size_t *out_index,
                                                const vigil_binding_function_t **out_function);
VIGIL_API const vigil_binding_function_t *vigil_binding_function_table_get(const vigil_binding_function_table_t *table,
                                                                           size_t index);
VIGIL_API vigil_binding_function_t *vigil_binding_function_table_get_mutable(vigil_binding_function_table_t *table,
                                                                             size_t index);

VIGIL_API void vigil_binding_scope_stack_init(vigil_binding_scope_stack_t *stack, vigil_runtime_t *runtime);
VIGIL_API void vigil_binding_scope_stack_free(vigil_binding_scope_stack_t *stack);
VIGIL_API void vigil_binding_scope_stack_begin_scope(vigil_binding_scope_stack_t *stack);
VIGIL_API void vigil_binding_scope_stack_end_scope(vigil_binding_scope_stack_t *stack, size_t *out_popped_count);
VIGIL_API size_t vigil_binding_scope_stack_depth(const vigil_binding_scope_stack_t *stack);
VIGIL_API size_t vigil_binding_scope_stack_count(const vigil_binding_scope_stack_t *stack);
VIGIL_API size_t vigil_binding_scope_stack_count_above_depth(const vigil_binding_scope_stack_t *stack,
                                                             size_t target_depth);
VIGIL_API vigil_status_t vigil_binding_scope_stack_declare_local(vigil_binding_scope_stack_t *stack,
                                                                 const vigil_binding_local_spec_t *spec,
                                                                 size_t *out_index, vigil_error_t *error);
VIGIL_API vigil_status_t vigil_binding_scope_stack_declare_hidden_local(vigil_binding_scope_stack_t *stack,
                                                                        vigil_binding_type_t type, int is_const,
                                                                        size_t *out_index, vigil_error_t *error);
VIGIL_API int vigil_binding_scope_stack_find_local(const vigil_binding_scope_stack_t *stack, const char *name,
                                                   size_t name_length, size_t *out_index,
                                                   vigil_binding_type_t *out_type);
VIGIL_API const vigil_binding_local_t *vigil_binding_scope_stack_local_at(const vigil_binding_scope_stack_t *stack,
                                                                          size_t index);

#endif
