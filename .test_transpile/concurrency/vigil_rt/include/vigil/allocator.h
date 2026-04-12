#ifndef VIGIL_ALLOCATOR_H
#define VIGIL_ALLOCATOR_H

#include <stddef.h>

#include "vigil/export.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void *(*vigil_allocate_fn)(void *user_data, size_t size);
    typedef void *(*vigil_reallocate_fn)(void *user_data, void *memory, size_t size);
    typedef void (*vigil_deallocate_fn)(void *user_data, void *memory);

    typedef struct vigil_allocator
    {
        void *user_data;
        vigil_allocate_fn allocate;
        vigil_reallocate_fn reallocate;
        vigil_deallocate_fn deallocate;
    } vigil_allocator_t;

#ifdef __cplusplus
}
#endif

#endif
