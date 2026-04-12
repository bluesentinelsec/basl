/*
 * Internal header for VM-level access to function object fields.
 *
 * This exposes the vigil_object and vigil_function_object_t layouts so
 * the VM dispatch loop can inline sibling/chunk lookups instead of
 * calling through the public API.  Only vm.c should include this.
 */
#ifndef VIGIL_VALUE_INTERNAL_H
#define VIGIL_VALUE_INTERNAL_H

#include "vigil/chunk.h"
#include "vigil/string.h"
#include "vigil/value.h"

/* Mirror of the opaque vigil_object struct from value.c. */
struct vigil_object_internal
{
    vigil_runtime_t *runtime;
    vigil_object_type_t type;
    volatile int64_t ref_count; /* Atomic for thread safety */
};

struct vigil_function_object_internal
{
    struct vigil_object_internal base;
    vigil_string_t name;
    size_t arity;
    size_t return_count;
    vigil_chunk_t chunk;
    vigil_object_t **functions;
    size_t function_count;
};

/*
 * Inline sibling lookup — equivalent to vigil_function_object_sibling().
 */
static inline const vigil_object_t *vigil_vm_function_sibling(const vigil_object_t *function, size_t index)
{
    const struct vigil_function_object_internal *fo = (const struct vigil_function_object_internal *)function;
    if (fo->functions == NULL || index >= fo->function_count)
    {
        return NULL;
    }
    return fo->functions[index];
}

/*
 * Inline chunk accessor — equivalent to vigil_function_object_chunk().
 */
static inline const vigil_chunk_t *vigil_vm_function_chunk(const vigil_object_t *function)
{
    const struct vigil_function_object_internal *fo = (const struct vigil_function_object_internal *)function;
    return &fo->chunk;
}

#endif /* VIGIL_VALUE_INTERNAL_H */
