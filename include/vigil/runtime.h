#ifndef VIGIL_RUNTIME_H
#define VIGIL_RUNTIME_H

#include <stddef.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/log.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct vigil_runtime_options
    {
        const vigil_allocator_t *allocator;
        const vigil_logger_t *logger;
    } vigil_runtime_options_t;

    typedef struct vigil_runtime vigil_runtime_t;

    VIGIL_API void vigil_runtime_options_init(vigil_runtime_options_t *options);
    VIGIL_API vigil_status_t vigil_runtime_open(vigil_runtime_t **out_runtime, const vigil_runtime_options_t *options,
                                                vigil_error_t *error);
    VIGIL_API void vigil_runtime_close(vigil_runtime_t **runtime);
    VIGIL_API int64_t vigil_runtime_alloc_count(const vigil_runtime_t *runtime);
    VIGIL_API int64_t vigil_runtime_alloc_bytes(const vigil_runtime_t *runtime);
    VIGIL_API const vigil_allocator_t *vigil_runtime_allocator(const vigil_runtime_t *runtime);
    VIGIL_API const vigil_logger_t *vigil_runtime_logger(const vigil_runtime_t *runtime);
    VIGIL_API vigil_status_t vigil_runtime_set_logger(vigil_runtime_t *runtime, const vigil_logger_t *logger,
                                                      vigil_error_t *error);
    /* vigil_runtime_alloc zero-initializes the returned memory on success. */
    VIGIL_API vigil_status_t vigil_runtime_alloc(vigil_runtime_t *runtime, size_t size, void **out_memory,
                                                 vigil_error_t *error);
    /*
     * On vigil_runtime_realloc failure, *memory is left unchanged and still points
     * to the original allocation.
     */
    VIGIL_API vigil_status_t vigil_runtime_realloc(vigil_runtime_t *runtime, void **memory, size_t size,
                                                   vigil_error_t *error);
    VIGIL_API void vigil_runtime_free(vigil_runtime_t *runtime, void **memory);

#ifdef __cplusplus
}
#endif

#endif
