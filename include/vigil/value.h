#ifndef VIGIL_VALUE_H
#define VIGIL_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum vigil_value_kind
    {
        VIGIL_VALUE_NIL = 0,
        VIGIL_VALUE_BOOL = 1,
        VIGIL_VALUE_INT = 2,
        VIGIL_VALUE_UINT = 3,
        VIGIL_VALUE_FLOAT = 4,
        VIGIL_VALUE_OBJECT = 5
    } vigil_value_kind_t;

    typedef enum vigil_object_type
    {
        VIGIL_OBJECT_INVALID = 0,
        VIGIL_OBJECT_STRING = 1,
        VIGIL_OBJECT_FUNCTION = 2,
        VIGIL_OBJECT_CLOSURE = 3,
        VIGIL_OBJECT_INSTANCE = 4,
        VIGIL_OBJECT_ERROR = 5,
        VIGIL_OBJECT_ARRAY = 6,
        VIGIL_OBJECT_MAP = 7,
        VIGIL_OBJECT_BIGINT = 8,
        VIGIL_OBJECT_NATIVE_FUNCTION = 9
    } vigil_object_type_t;

    typedef struct vigil_object vigil_object_t;
    typedef struct vigil_chunk vigil_chunk_t;
    typedef struct vigil_vm vigil_vm_t;

    /*
     * NaN-boxed value representation.  Every value is a single uint64_t.
     * Doubles are stored as raw IEEE 754 bits.  All other types (nil, bool,
     * int, uint, object pointer) are encoded in the quiet-NaN space.
     * See src/internal/vigil_nanbox.h for the encoding details.
     */
    typedef uint64_t vigil_value_t;

    VIGIL_API void vigil_value_init_nil(vigil_value_t *value);
    VIGIL_API void vigil_value_init_bool(vigil_value_t *value, bool boolean);
    VIGIL_API void vigil_value_init_int(vigil_value_t *value, int64_t integer);
    VIGIL_API void vigil_value_init_uint(vigil_value_t *value, uint64_t integer);
    VIGIL_API void vigil_value_init_float(vigil_value_t *value, double number);
    /*
     * Runtime-aware integer init — heap-boxes values that exceed the
     * 48-bit inline range.  Use these when the value may be large.
     */
    VIGIL_API vigil_status_t vigil_value_init_int_rt(vigil_value_t *value, int64_t integer, vigil_runtime_t *runtime,
                                                     vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_value_init_uint_rt(vigil_value_t *value, uint64_t integer, vigil_runtime_t *runtime,
                                                      vigil_error_t *error);
    /*
     * Transfers one owned object reference into the value and clears *object.
     * Passing a null object pointer initializes the value to nil.
     */
    VIGIL_API void vigil_value_init_object(vigil_value_t *value, vigil_object_t **object);
    VIGIL_API vigil_value_t vigil_value_copy(const vigil_value_t *value);
    VIGIL_API void vigil_value_release(vigil_value_t *value);
    VIGIL_API vigil_value_kind_t vigil_value_kind(const vigil_value_t *value);
    VIGIL_API bool vigil_value_as_bool(const vigil_value_t *value);
    VIGIL_API int64_t vigil_value_as_int(const vigil_value_t *value);
    VIGIL_API uint64_t vigil_value_as_uint(const vigil_value_t *value);
    VIGIL_API double vigil_value_as_float(const vigil_value_t *value);
    VIGIL_API vigil_object_t *vigil_value_as_object(const vigil_value_t *value);

    VIGIL_API vigil_object_type_t vigil_object_type(const vigil_object_t *object);
    VIGIL_API size_t vigil_object_ref_count(const vigil_object_t *object);
    /*
     * Retains one additional reference. Ref-count overflow is treated as an
     * internal fatal error.
     */
    VIGIL_API void vigil_object_retain(vigil_object_t *object);
    /*
     * Consumes the caller's reference and always clears *object. The underlying
     * object is destroyed only when the released reference was the last one.
     */
    VIGIL_API void vigil_object_release(vigil_object_t **object);
    VIGIL_API void vigil_object_make_immortal(vigil_object_t *object);
    VIGIL_API void vigil_object_force_destroy(vigil_object_t **object);

    VIGIL_API vigil_status_t vigil_string_object_new(vigil_runtime_t *runtime, const char *value, size_t length,
                                                     vigil_object_t **out_object, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_string_object_new_cstr(vigil_runtime_t *runtime, const char *value,
                                                          vigil_object_t **out_object, vigil_error_t *error);
    VIGIL_API const char *vigil_string_object_c_str(const vigil_object_t *object);
    VIGIL_API size_t vigil_string_object_length(const vigil_object_t *object);

    VIGIL_API vigil_status_t vigil_error_object_new(vigil_runtime_t *runtime, const char *message, size_t length,
                                                    int64_t kind, vigil_object_t **out_object, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_error_object_new_cstr(vigil_runtime_t *runtime, const char *message, int64_t kind,
                                                         vigil_object_t **out_object, vigil_error_t *error);
    VIGIL_API const char *vigil_error_object_message(const vigil_object_t *object);
    VIGIL_API size_t vigil_error_object_message_length(const vigil_object_t *object);
    VIGIL_API int64_t vigil_error_object_kind(const vigil_object_t *object);

    VIGIL_API vigil_status_t vigil_function_object_new(vigil_runtime_t *runtime, const char *name, size_t name_length,
                                                       size_t arity, size_t return_count, vigil_chunk_t *chunk,
                                                       vigil_object_t **out_object, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_function_object_new_cstr(vigil_runtime_t *runtime, const char *name, size_t arity,
                                                            size_t return_count, vigil_chunk_t *chunk,
                                                            vigil_object_t **out_object, vigil_error_t *error);
    VIGIL_API const char *vigil_function_object_name(const vigil_object_t *object);
    VIGIL_API size_t vigil_function_object_arity(const vigil_object_t *object);
    VIGIL_API size_t vigil_function_object_return_count(const vigil_object_t *object);
    VIGIL_API const vigil_chunk_t *vigil_function_object_chunk(const vigil_object_t *object);

    VIGIL_API vigil_status_t vigil_closure_object_new(vigil_runtime_t *runtime, vigil_object_t *function,
                                                      const vigil_value_t *captures, size_t capture_count,
                                                      vigil_object_t **out_object, vigil_error_t *error);
    VIGIL_API const vigil_object_t *vigil_closure_object_function(const vigil_object_t *object);
    VIGIL_API size_t vigil_closure_object_capture_count(const vigil_object_t *object);
    VIGIL_API int vigil_closure_object_get_capture(const vigil_object_t *object, size_t index,
                                                   vigil_value_t *out_value);
    VIGIL_API vigil_status_t vigil_closure_object_set_capture(vigil_object_t *object, size_t index,
                                                              const vigil_value_t *value, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_instance_object_new(vigil_runtime_t *runtime, size_t class_index,
                                                       const vigil_value_t *fields, size_t field_count,
                                                       vigil_object_t **out_object, vigil_error_t *error);
    VIGIL_API size_t vigil_instance_object_class_index(const vigil_object_t *object);
    VIGIL_API size_t vigil_instance_object_field_count(const vigil_object_t *object);
    VIGIL_API int vigil_instance_object_get_field(const vigil_object_t *object, size_t index, vigil_value_t *out_value);
    VIGIL_API vigil_status_t vigil_instance_object_set_field(vigil_object_t *object, size_t index,
                                                             const vigil_value_t *value, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_array_object_new(vigil_runtime_t *runtime, const vigil_value_t *items,
                                                    size_t item_count, vigil_object_t **out_object,
                                                    vigil_error_t *error);
    VIGIL_API size_t vigil_array_object_length(const vigil_object_t *object);
    VIGIL_API int vigil_array_object_get(const vigil_object_t *object, size_t index, vigil_value_t *out_value);
    VIGIL_API vigil_status_t vigil_array_object_append(vigil_object_t *object, const vigil_value_t *value,
                                                       vigil_error_t *error);
    VIGIL_API int vigil_array_object_pop(vigil_object_t *object, vigil_value_t *out_value);
    VIGIL_API vigil_status_t vigil_array_object_set(vigil_object_t *object, size_t index, const vigil_value_t *value,
                                                    vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_array_object_slice(const vigil_object_t *object, size_t start, size_t end,
                                                      vigil_object_t **out_object, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_map_object_new(vigil_runtime_t *runtime, vigil_object_t **out_object,
                                                  vigil_error_t *error);
    VIGIL_API size_t vigil_map_object_count(const vigil_object_t *object);
    VIGIL_API int vigil_map_object_get(const vigil_object_t *object, const vigil_value_t *key,
                                       vigil_value_t *out_value);
    VIGIL_API int vigil_map_object_key_at(const vigil_object_t *object, size_t index, vigil_value_t *out_key);
    VIGIL_API int vigil_map_object_value_at(const vigil_object_t *object, size_t index, vigil_value_t *out_value);
    VIGIL_API vigil_status_t vigil_map_object_set(vigil_object_t *object, const vigil_value_t *key,
                                                  const vigil_value_t *value, vigil_error_t *error);
    VIGIL_API int vigil_map_object_remove(vigil_object_t *object, const vigil_value_t *key, vigil_value_t *out_value,
                                          vigil_error_t *error);

    /**
     * Native function callback.  The implementation reads `arg_count`
     * arguments from the top of the VM stack (bottom-up, first arg is
     * deepest) and pushes its return value(s) before returning.
     */
    typedef vigil_status_t (*vigil_native_fn_t)(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_native_function_object_create(vigil_runtime_t *runtime, const char *name,
                                                                 size_t name_length, size_t arity,
                                                                 vigil_native_fn_t function,
                                                                 vigil_object_t **out_object, vigil_error_t *error);

    VIGIL_API vigil_native_fn_t vigil_native_function_get(const vigil_object_t *object);

#ifdef __cplusplus
}
#endif

#endif
