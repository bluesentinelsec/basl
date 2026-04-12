#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"

static void *vigil_default_allocate(void *user_data, size_t size)
{
    (void)user_data;
    return calloc(1U, size);
}

static void *vigil_default_reallocate(void *user_data, void *memory, size_t size)
{
    (void)user_data;
    return realloc(memory, size);
}

static void vigil_default_deallocate(void *user_data, void *memory)
{
    (void)user_data;
    free(memory);
}

vigil_allocator_t vigil_default_allocator(void)
{
    vigil_allocator_t allocator;

    allocator.user_data = NULL;
    allocator.allocate = vigil_default_allocate;
    allocator.reallocate = vigil_default_reallocate;
    allocator.deallocate = vigil_default_deallocate;
    return allocator;
}

int vigil_allocator_is_valid(const vigil_allocator_t *allocator)
{
    if (allocator == NULL)
    {
        return 0;
    }

    return allocator->allocate != NULL && allocator->deallocate != NULL;
}

vigil_status_t vigil_runtime_alloc(vigil_runtime_t *runtime, size_t size, void **out_memory, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (runtime == NULL || out_memory == NULL || size == 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "runtime_alloc requires runtime, out_memory, and non-zero size");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_memory = runtime->allocator.allocate(runtime->allocator.user_data, size);
    if (*out_memory == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    runtime->alloc_count++;
    runtime->alloc_bytes += (int64_t)size;
    memset(*out_memory, 0, size);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_runtime_realloc(vigil_runtime_t *runtime, void **memory, size_t size, vigil_error_t *error)
{
    void *reallocated;

    vigil_error_clear(error);

    if (runtime == NULL || memory == NULL || *memory == NULL || size == 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "runtime_realloc requires runtime, memory, and non-zero size");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (runtime->allocator.reallocate == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "allocator does not support reallocate");
        return VIGIL_STATUS_UNSUPPORTED;
    }

    reallocated = runtime->allocator.reallocate(runtime->allocator.user_data, *memory, size);
    if (reallocated == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "reallocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    *memory = reallocated;
    return VIGIL_STATUS_OK;
}

void vigil_runtime_free(vigil_runtime_t *runtime, void **memory)
{
    if (runtime == NULL || memory == NULL || *memory == NULL)
    {
        return;
    }

    runtime->allocator.deallocate(runtime->allocator.user_data, *memory);
    *memory = NULL;
}
