#ifndef VIGIL_UNSAFE_BUFFER_H
#define VIGIL_UNSAFE_BUFFER_H

/**
 * Public API for the unsafe buffer registry.
 *
 * The unsafe module maintains a slot-based registry of raw byte buffers.
 * Vigil scripts manage these via unsafe.alloc/free/get/set.  This header
 * exposes the registry to native plugins so they can:
 *
 *   - Resolve a Vigil buffer handle (i64 slot) to a raw (ptr, size) pair
 *     for passing to C APIs that require void* parameters.
 *   - Allocate buffers from C code and return handles to Vigil.
 *
 * All functions are safe to call from any native module or plugin.
 * Buffer handles are i64 slot indices; invalid handles return NULL/error.
 */

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Look up a buffer by slot handle.
     * Returns the raw data pointer and size, or NULL if the handle is invalid.
     *
     * @param slot   Buffer handle (from unsafe.alloc or vigil_unsafe_buffer_alloc).
     * @param out_size  If non-NULL, receives the buffer size in bytes.
     * @return  Pointer to the buffer data, or NULL if slot is invalid.
     */
    VIGIL_API void *vigil_unsafe_buffer_get(int64_t slot, int32_t *out_size);

    /**
     * Allocate a new buffer in the unsafe registry.
     * Returns the slot handle (>= 0) or -1 if the registry is full.
     *
     * @param size  Buffer size in bytes (must be > 0).
     * @return  Slot handle, or -1 on failure.
     */
    VIGIL_API int64_t vigil_unsafe_buffer_alloc(int32_t size);

    /**
     * Free a buffer in the unsafe registry.
     *
     * @param slot  Buffer handle to free.
     */
    VIGIL_API void vigil_unsafe_buffer_free(int64_t slot);

    /**
     * Register an externally-allocated buffer in the unsafe registry.
     * The registry takes ownership — the buffer will be freed with free()
     * when the slot is released via unsafe.free or vigil_unsafe_buffer_free.
     *
     * @param data  Pointer to the buffer (must be malloc/calloc-allocated).
     * @param size  Buffer size in bytes.
     * @return  Slot handle, or -1 if the registry is full.
     */
    VIGIL_API int64_t vigil_unsafe_buffer_register(void *data, int32_t size);

#ifdef __cplusplus
}
#endif

#endif
