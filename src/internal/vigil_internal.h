#ifndef VIGIL_INTERNAL_H
#define VIGIL_INTERNAL_H

#include <stddef.h>

#include "vigil/runtime.h"
#include "vigil/status.h"
#include "vigil/type.h"
#include "vigil/value.h"

/* Forward declarations for opaque platform types used by the runtime struct.
 * The full definitions live in platform/platform.h; callers that need them
 * must include that header directly. */
typedef struct vigil_platform_mutex vigil_platform_mutex_t;

/* ── Regex pattern cache ─────────────────────────────────────────────
 * Fixed-size open-addressing LRU-approximation cache for compiled regex
 * patterns.  Stored inline in vigil_runtime to avoid extra allocation.
 * VIGIL_REGEX_CACHE_SIZE must be a power of two. */
#define VIGIL_REGEX_CACHE_SIZE 32U

struct vigil_regex; /* forward-declared; defined in stdlib/regex_engine.c */

typedef struct vigil_regex_cache_entry
{
    char *pattern; /* heap-allocated copy; NULL = empty slot */
    size_t pattern_len;
    struct vigil_regex *re; /* compiled regex; NULL = empty slot */
    unsigned int lru_clock; /* incremented on each access */
} vigil_regex_cache_entry_t;

typedef struct vigil_regex_cache
{
    vigil_regex_cache_entry_t entries[VIGIL_REGEX_CACHE_SIZE];
    unsigned int clock; /* global access counter */
} vigil_regex_cache_t;

struct vigil_runtime
{
    vigil_allocator_t allocator;
    vigil_logger_t logger;
    vigil_regex_cache_t regex_cache;
    /* Singleton "ok" error object — reused by stdlib functions that return
       (value, err) on the success path to avoid a heap allocation per call. */
    vigil_object_t *ok_error;

    /* Debug hook propagated to every VM opened from this runtime.  Set by the
     * debugger on attach so that thread-spawned VMs inherit instrumentation. */
    int (*debug_hook)(vigil_vm_t *vm, void *userdata);
    void *debug_hook_userdata;

    /* Thread-aware VM registry.  Every vigil_vm_open / vigil_vm_close call
     * adds / removes the VM here so the debugger can enumerate live threads. */
    vigil_platform_mutex_t *vm_registry_mutex;
    vigil_vm_t **vm_registry;
    size_t vm_registry_count;
    size_t vm_registry_capacity;

    /* Allocation counters for profiling. */
    int64_t alloc_count;
    int64_t alloc_bytes;
};

typedef struct vigil_runtime_interface_impl_init
{
    size_t interface_index;
    const size_t *function_indices;
    size_t function_count;
} vigil_runtime_interface_impl_init_t;

typedef enum vigil_runtime_object_kind
{
    VIGIL_RUNTIME_OBJECT_NONE = 0,
    VIGIL_RUNTIME_OBJECT_CLASS = 1,
    VIGIL_RUNTIME_OBJECT_INTERFACE = 2,
    VIGIL_RUNTIME_OBJECT_ENUM = 3,
    VIGIL_RUNTIME_OBJECT_ARRAY = 4,
    VIGIL_RUNTIME_OBJECT_MAP = 5,
    VIGIL_RUNTIME_OBJECT_FUNCTION = 6
} vigil_runtime_object_kind_t;

typedef struct vigil_runtime_resolved_type
{
    vigil_type_kind_t kind;
    vigil_runtime_object_kind_t object_kind;
    size_t object_index;
} vigil_runtime_resolved_type_t;

typedef struct vigil_runtime_class_field_init
{
    const char *name;
    size_t name_length;
    vigil_runtime_resolved_type_t type;
    int is_public;
} vigil_runtime_class_field_init_t;

typedef struct vigil_runtime_class_init
{
    const char *name;
    size_t name_length;
    const vigil_runtime_class_field_init_t *fields;
    size_t field_count;
    const vigil_runtime_interface_impl_init_t *interface_impls;
    size_t interface_impl_count;
} vigil_runtime_class_init_t;

typedef struct vigil_runtime_array_type_init
{
    vigil_runtime_resolved_type_t element_type;
} vigil_runtime_array_type_init_t;

typedef struct vigil_runtime_map_type_init
{
    vigil_runtime_resolved_type_t key_type;
    vigil_runtime_resolved_type_t value_type;
} vigil_runtime_map_type_init_t;

typedef struct vigil_runtime_function_attach_init
{
    const vigil_value_t *initial_globals;
    size_t global_count;
    const vigil_runtime_class_init_t *classes;
    size_t class_count;
    const vigil_runtime_array_type_init_t *array_types;
    size_t array_type_count;
    const vigil_runtime_map_type_init_t *map_types;
    size_t map_type_count;
} vigil_runtime_function_attach_init_t;

vigil_allocator_t vigil_default_allocator(void);
int vigil_allocator_is_valid(const vigil_allocator_t *allocator);
void vigil_error_set_literal(vigil_error_t *error, vigil_status_t type, const char *value);
vigil_status_t vigil_function_object_attach_siblings(vigil_object_t *owner_function, vigil_object_t **functions,
                                                     size_t function_count, size_t owner_index,
                                                     const vigil_runtime_function_attach_init_t *init,
                                                     vigil_error_t *error);
const vigil_object_t *vigil_function_object_sibling(const vigil_object_t *function, size_t index);
const vigil_object_t *vigil_function_object_resolve_interface_method(const vigil_object_t *function, size_t class_index,
                                                                     size_t interface_index, size_t method_index);
int vigil_function_object_get_global(const vigil_object_t *function, size_t index, vigil_value_t *out_value);
size_t vigil_function_object_global_count(const vigil_object_t *function);
vigil_status_t vigil_function_object_set_global(const vigil_object_t *function, size_t index,
                                                const vigil_value_t *value, vigil_error_t *error);
int vigil_function_object_get_class_field(const vigil_object_t *function, size_t class_index, size_t field_index,
                                          const char **out_name, size_t *out_name_length,
                                          vigil_runtime_resolved_type_t *out_type, int *out_is_public);
size_t vigil_function_object_class_field_count(const vigil_object_t *function, size_t class_index);
int vigil_function_object_get_array_type(const vigil_object_t *function, size_t array_index,
                                         vigil_runtime_resolved_type_t *out_element_type);
int vigil_function_object_get_map_type(const vigil_object_t *function, size_t map_index,
                                       vigil_runtime_resolved_type_t *out_key_type,
                                       vigil_runtime_resolved_type_t *out_value_type);
const vigil_object_t *vigil_callable_object_function(const vigil_object_t *callable);
size_t vigil_callable_object_arity(const vigil_object_t *callable);
size_t vigil_callable_object_return_count(const vigil_object_t *callable);
const vigil_chunk_t *vigil_callable_object_chunk(const vigil_object_t *callable);

/* Push the singleton "ok" error value onto the VM stack.
   Avoids allocating a new error object on every stdlib success path. */
vigil_status_t vigil_runtime_push_ok_error(vigil_runtime_t *runtime, vigil_vm_t *vm, vigil_error_t *error);

/* Return the pre-encoded nanbox value for the singleton "ok" error.
   Callers can push this directly with VIGIL_VM_PUSH to skip the
   retain/release overhead of vigil_runtime_push_ok_error(). */
vigil_value_t vigil_runtime_ok_error_value(vigil_runtime_t *runtime);

/* ── VM registry (internal) ──────────────────────────────────────────
 * Called by vigil_vm_open / vigil_vm_close to maintain the per-runtime
 * list of live VMs.  The debugger uses this to enumerate threads. */

/** Register a VM with its runtime's thread registry.  Called from
 *  vigil_vm_open after the VM is fully initialised. */
vigil_status_t vigil_runtime_register_vm(vigil_runtime_t *runtime, vigil_vm_t *vm, vigil_error_t *error);

/** Remove a VM from its runtime's thread registry.  Called from
 *  vigil_vm_close before the VM is freed. */
void vigil_runtime_unregister_vm(vigil_runtime_t *runtime, vigil_vm_t *vm);

/** Copy at most max_vms live VM pointers into out_vms.  Returns the
 *  number written.  Thread-safe: holds the registry mutex while copying. */
size_t vigil_runtime_list_vms(vigil_runtime_t *runtime, vigil_vm_t **out_vms, size_t max_vms);

/** Propagate the debug hook to every currently-registered VM.
 *  Called by vigil_debugger_attach / vigil_debugger_detach. */
void vigil_runtime_set_debug_hook(vigil_runtime_t *runtime, int (*hook)(vigil_vm_t *vm, void *userdata),
                                  void *userdata);

#endif
