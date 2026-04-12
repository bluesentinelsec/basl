#ifndef VIGIL_GC_H
#define VIGIL_GC_H

#include <stddef.h>

#include "vigil/export.h"
#include "vigil/runtime.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Cycle-detecting garbage collector for reference-counted objects.
     *
     * Vigil uses reference counting for deterministic cleanup.  Pure
     * ref-counting cannot reclaim cycles (A -> B -> A).  This collector
     * supplements ref-counting by periodically scanning "container"
     * objects (instances, arrays, maps, closures) that may participate
     * in reference cycles and breaking any that are found.
     *
     * The algorithm is a simplified variant of the one used by CPython:
     *   1. For every tracked container, tentatively subtract the
     *      references that come from other tracked containers.
     *   2. Any object whose adjusted ref-count drops to zero is
     *      reachable only from the tracked set — it is garbage.
     *   3. Objects still reachable from outside the set are restored.
     *   4. Garbage objects are destroyed.
     */

    typedef struct vigil_gc vigil_gc_t;

    VIGIL_API vigil_status_t vigil_gc_create(vigil_gc_t **out_gc, vigil_runtime_t *runtime, vigil_error_t *error);
    VIGIL_API void vigil_gc_destroy(vigil_gc_t **gc);

    /** Register a container object for cycle tracking. */
    VIGIL_API void vigil_gc_track(vigil_gc_t *gc, void *object);

    /** Remove an object from cycle tracking (called before destroy). */
    VIGIL_API void vigil_gc_untrack(vigil_gc_t *gc, void *object);

    /** Run the cycle collector.  Returns the number of objects freed. */
    VIGIL_API size_t vigil_gc_collect(vigil_gc_t *gc);

    /** Return the number of tracked objects. */
    VIGIL_API size_t vigil_gc_tracked_count(const vigil_gc_t *gc);

#ifdef __cplusplus
}
#endif

#endif
