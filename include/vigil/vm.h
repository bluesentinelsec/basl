#ifndef VIGIL_VM_H
#define VIGIL_VM_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/chunk.h"
#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/status.h"
#include "vigil/value.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct vigil_vm_options
    {
        size_t initial_stack_capacity;
    } vigil_vm_options_t;

    typedef struct vigil_vm vigil_vm_t;

    VIGIL_API void vigil_vm_options_init(vigil_vm_options_t *options);
    VIGIL_API vigil_status_t vigil_vm_open(vigil_vm_t **out_vm, vigil_runtime_t *runtime,
                                           const vigil_vm_options_t *options, vigil_error_t *error);
    VIGIL_API void vigil_vm_close(vigil_vm_t **vm);
    VIGIL_API vigil_runtime_t *vigil_vm_runtime(const vigil_vm_t *vm);
    VIGIL_API size_t vigil_vm_stack_depth(const vigil_vm_t *vm);
    VIGIL_API size_t vigil_vm_frame_depth(const vigil_vm_t *vm);
    /*
     * out_value must not be null and should be initialized to nil before reuse if
     * it may already own an object reference.
     */
    VIGIL_API vigil_status_t vigil_vm_execute(vigil_vm_t *vm, const vigil_chunk_t *chunk, vigil_value_t *out_value,
                                              vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_vm_execute_function(vigil_vm_t *vm, const vigil_object_t *function,
                                                       vigil_value_t *out_value, vigil_error_t *error);

    /* Stack access for native function implementations. */
    VIGIL_API vigil_value_t vigil_vm_stack_get(const vigil_vm_t *vm, size_t index);
    VIGIL_API vigil_status_t vigil_vm_stack_push(vigil_vm_t *vm, const vigil_value_t *value, vigil_error_t *error);
    VIGIL_API void vigil_vm_stack_pop_n(vigil_vm_t *vm, size_t count);

    /* Debug hook — set by debugger, NULL when not debugging. */
    VIGIL_API void vigil_vm_set_debug_hook(vigil_vm_t *vm, int (*hook)(vigil_vm_t *vm, void *userdata), void *userdata);

    /** Return the platform thread ID of the OS thread running this VM.
     *  Matches vigil_platform_thread_current_id() from the calling thread. */
    VIGIL_API uint64_t vigil_vm_thread_id(const vigil_vm_t *vm);

    VIGIL_API void vigil_vm_set_args(vigil_vm_t *vm, const char *const *argv, size_t argc);

    VIGIL_API void vigil_vm_get_args(const vigil_vm_t *vm, const char *const **out_argv, size_t *out_argc);

    /* Frame inspection for debugger. */
    VIGIL_API const vigil_chunk_t *vigil_vm_frame_chunk(const vigil_vm_t *vm, size_t frame_index);
    VIGIL_API size_t vigil_vm_frame_ip(const vigil_vm_t *vm, size_t frame_index);
    VIGIL_API size_t vigil_vm_frame_base_slot(const vigil_vm_t *vm, size_t frame_index);
    VIGIL_API const vigil_object_t *vigil_vm_frame_function(const vigil_vm_t *vm, size_t frame_index);

#ifdef __cplusplus
}
#endif

#endif
