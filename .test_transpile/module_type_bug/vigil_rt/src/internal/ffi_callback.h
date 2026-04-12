#ifndef VIGIL_FFI_CALLBACK_H
#define VIGIL_FFI_CALLBACK_H

/* Callback trampoline pool for the VIGIL FFI.
 *
 * Provides a fixed pool of C function pointers that dispatch through
 * a user-registered callback function, allowing VIGIL closures to be
 * passed as C function pointers to foreign code.
 */

#include "vigil/export.h"
#include "vigil/value.h"
#include <stdint.h>

#define VIGIL_FFI_MAX_CALLBACKS 8

typedef intptr_t (*vigil_ffi_callback_dispatch_fn)(int slot, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3);

VIGIL_API void vigil_ffi_callback_set_dispatch(vigil_ffi_callback_dispatch_fn fn);
VIGIL_API int vigil_ffi_callback_alloc(void **out_ptr);
VIGIL_API void vigil_ffi_callback_free(int slot);
VIGIL_API int vigil_ffi_callback_slot_from_ptr(void *ptr);
VIGIL_API int vigil_ffi_callback_is_allocated(int slot);

/* Per-slot closure storage for VM-based dispatch. */
VIGIL_API void vigil_ffi_callback_set_closure(int slot, vigil_object_t *closure);
VIGIL_API vigil_object_t *vigil_ffi_callback_get_closure(int slot);
VIGIL_API vigil_object_t *vigil_ffi_callback_retain_closure(int slot);

#endif
