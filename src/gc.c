/*
 * vigil_gc.c — Cycle-detecting garbage collector.
 *
 * Supplements reference counting by detecting and collecting cycles
 * among container objects (instances, arrays, maps, closures).
 *
 * Algorithm (simplified CPython-style trial deletion):
 *   1. Copy each tracked object's ref_count into a scratch gc_refs.
 *   2. For each tracked container, subtract internal references
 *      (references from other tracked containers) from gc_refs.
 *   3. Objects with gc_refs == 0 are tentatively unreachable.
 *   4. Transitively restore objects reachable from any object with
 *      gc_refs > 0 (those are externally referenced).
 *   5. Remaining objects with gc_refs == 0 are true garbage — destroy.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "platform/platform.h"
#include "vigil/gc.h"
#include "vigil/map.h"
#include "vigil/value.h"

/* ── Per-object GC metadata ──────────────────────────────────────── */

typedef struct vigil_gc_node
{
    struct vigil_gc_node *prev;
    struct vigil_gc_node *next;
    void *object;    /* vigil_object_t* */
    int64_t gc_refs; /* scratch ref count for trial deletion */
} vigil_gc_node_t;

struct vigil_gc
{
    vigil_runtime_t *runtime;
    vigil_gc_node_t sentinel; /* doubly-linked list sentinel */
    size_t tracked_count;
};

/* ── Forward declarations of object layout (mirrors value.c) ─────── */

typedef struct
{
    vigil_runtime_t *runtime;
    vigil_object_type_t type;
    volatile int64_t ref_count;
} gc_object_header_t;

typedef struct
{
    gc_object_header_t base;
    size_t class_index;
    vigil_value_t *fields;
    size_t field_count;
} gc_instance_t;

typedef struct
{
    gc_object_header_t base;
    vigil_value_t *items;
    size_t item_count;
} gc_array_t;

typedef struct
{
    gc_object_header_t base;
    vigil_map_t entries;
} gc_map_t;

typedef struct
{
    gc_object_header_t base;
    vigil_object_t *function;
    vigil_value_t *captures;
    size_t capture_count;
} gc_closure_t;

/* ── Helpers ─────────────────────────────────────────────────────── */

static vigil_gc_node_t *find_node(vigil_gc_t *gc, void *object)
{
    vigil_gc_node_t *node;

    for (node = gc->sentinel.next; node != &gc->sentinel; node = node->next)
    {
        if (node->object == object)
            return node;
    }
    return NULL;
}

static vigil_gc_node_t *find_node_for_value(vigil_gc_t *gc, vigil_value_t value)
{
    void *ptr;

    if (!vigil_nanbox_has_object(value))
        return NULL;
    ptr = vigil_nanbox_decode_ptr(value);
    return find_node(gc, ptr);
}

/*
 * Visit every child value of a container object.  For each child that
 * is a tracked container, call visitor(gc, child_node, userdata).
 */
typedef void (*gc_child_visitor_t)(vigil_gc_t *gc, vigil_gc_node_t *child, void *userdata);

static void visit_value(vigil_gc_t *gc, vigil_value_t value, gc_child_visitor_t visitor, void *userdata)
{
    vigil_gc_node_t *child = find_node_for_value(gc, value);
    if (child != NULL)
        visitor(gc, child, userdata);
}

static void visit_children(vigil_gc_t *gc, vigil_gc_node_t *node, gc_child_visitor_t visitor, void *userdata)
{
    gc_object_header_t *header = (gc_object_header_t *)node->object;
    size_t i;

    switch (header->type)
    {
    case VIGIL_OBJECT_INSTANCE: {
        gc_instance_t *inst = (gc_instance_t *)node->object;
        for (i = 0; i < inst->field_count; i++)
            visit_value(gc, inst->fields[i], visitor, userdata);
        break;
    }
    case VIGIL_OBJECT_ARRAY: {
        gc_array_t *arr = (gc_array_t *)node->object;
        for (i = 0; i < arr->item_count; i++)
            visit_value(gc, arr->items[i], visitor, userdata);
        break;
    }
    case VIGIL_OBJECT_MAP: {
        gc_map_t *map = (gc_map_t *)node->object;
        size_t cap = map->entries.capacity;
        /* Walk the raw entry table to visit both keys and values. */
        if (map->entries.entries != NULL)
        {
            typedef struct
            {
                int state;
                uint64_t hash;
                vigil_value_t key;
                vigil_value_t value;
            } gc_map_entry_t;
            gc_map_entry_t *entries = (gc_map_entry_t *)map->entries.entries;
            for (i = 0; i < cap; i++)
            {
                if (entries[i].state == 1) /* OCCUPIED */
                {
                    visit_value(gc, entries[i].key, visitor, userdata);
                    visit_value(gc, entries[i].value, visitor, userdata);
                }
            }
        }
        break;
    }
    case VIGIL_OBJECT_CLOSURE: {
        gc_closure_t *cls = (gc_closure_t *)node->object;
        for (i = 0; i < cls->capture_count; i++)
            visit_value(gc, cls->captures[i], visitor, userdata);
        break;
    }
    default:
        break;
    }
}

/* ── Visitor callbacks ───────────────────────────────────────────── */

static void subtract_ref(vigil_gc_t *gc, vigil_gc_node_t *child, void *userdata)
{
    (void)gc;
    (void)userdata;
    child->gc_refs -= 1;
}

static void restore_ref(vigil_gc_t *gc, vigil_gc_node_t *child, void *userdata)
{
    (void)userdata;
    if (child->gc_refs == 0)
    {
        child->gc_refs = 1; /* mark as reachable */
        visit_children(gc, child, restore_ref, NULL);
    }
}

/*
 * Break a garbage→garbage reference: if *slot points to a tracked
 * garbage object (gc_refs == 0), directly decrement the target's real
 * ref_count and nil the slot.  This avoids vigil_value_release which
 * would trigger recursive destruction and stack overflow on cycles.
 */
static void break_if_garbage(vigil_gc_t *gc, vigil_value_t *slot)
{
    vigil_gc_node_t *child;
    gc_object_header_t *child_hdr;

    child = find_node_for_value(gc, *slot);
    if (child == NULL || child->gc_refs != 0)
        return;
    child_hdr = (gc_object_header_t *)child->object;
    vigil_atomic_sub(&child_hdr->ref_count, 1);
    *slot = VIGIL_NANBOX_NIL;
}

static void break_garbage_children(vigil_gc_t *gc, vigil_gc_node_t *node)
{
    gc_object_header_t *header = (gc_object_header_t *)node->object;
    size_t i;

    switch (header->type)
    {
    case VIGIL_OBJECT_INSTANCE: {
        gc_instance_t *inst = (gc_instance_t *)node->object;
        for (i = 0; i < inst->field_count; i++)
            break_if_garbage(gc, &inst->fields[i]);
        break;
    }
    case VIGIL_OBJECT_ARRAY: {
        gc_array_t *arr = (gc_array_t *)node->object;
        for (i = 0; i < arr->item_count; i++)
            break_if_garbage(gc, &arr->items[i]);
        break;
    }
    case VIGIL_OBJECT_MAP: {
        gc_map_t *map = (gc_map_t *)node->object;
        size_t cap = map->entries.capacity;
        if (map->entries.entries != NULL)
        {
            typedef struct
            {
                int state;
                uint64_t hash;
                vigil_value_t key;
                vigil_value_t value;
            } gc_map_entry_t;
            gc_map_entry_t *entries = (gc_map_entry_t *)map->entries.entries;
            for (i = 0; i < cap; i++)
            {
                if (entries[i].state == 1)
                {
                    break_if_garbage(gc, &entries[i].key);
                    break_if_garbage(gc, &entries[i].value);
                }
            }
        }
        break;
    }
    case VIGIL_OBJECT_CLOSURE: {
        gc_closure_t *cls = (gc_closure_t *)node->object;
        for (i = 0; i < cls->capture_count; i++)
            break_if_garbage(gc, &cls->captures[i]);
        break;
    }
    default:
        break;
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

vigil_status_t vigil_gc_create(vigil_gc_t **out_gc, vigil_runtime_t *runtime, vigil_error_t *error)
{
    vigil_status_t status;
    void *memory = NULL;

    vigil_error_clear(error);
    if (out_gc == NULL || runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "gc output and runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_gc = NULL;

    status = vigil_runtime_alloc(runtime, sizeof(vigil_gc_t), &memory, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    vigil_gc_t *gc = (vigil_gc_t *)memory;
    gc->runtime = runtime;
    gc->sentinel.prev = &gc->sentinel;
    gc->sentinel.next = &gc->sentinel;
    gc->sentinel.object = NULL;
    gc->tracked_count = 0;
    *out_gc = gc;
    return VIGIL_STATUS_OK;
}

void vigil_gc_destroy(vigil_gc_t **gc)
{
    vigil_gc_t *g;
    vigil_gc_node_t *node;
    vigil_gc_node_t *next;
    void *memory;

    if (gc == NULL || *gc == NULL)
        return;
    g = *gc;
    *gc = NULL;

    /* Free all tracking nodes (but not the objects themselves). */
    for (node = g->sentinel.next; node != &g->sentinel; node = next)
    {
        next = node->next;
        memory = node;
        vigil_runtime_free(g->runtime, &memory);
    }

    memory = g;
    vigil_runtime_free(g->runtime, &memory);
}

void vigil_gc_track(vigil_gc_t *gc, void *object)
{
    vigil_gc_node_t *node;
    void *memory = NULL;
    vigil_error_t error = {0};

    if (gc == NULL || object == NULL)
        return;
    /* Don't double-track. */
    if (find_node(gc, object) != NULL)
        return;

    if (vigil_runtime_alloc(gc->runtime, sizeof(vigil_gc_node_t), &memory, &error) != VIGIL_STATUS_OK)
        return;

    node = (vigil_gc_node_t *)memory;
    node->object = object;
    node->gc_refs = 0;

    /* Insert at tail of list. */
    node->prev = gc->sentinel.prev;
    node->next = &gc->sentinel;
    gc->sentinel.prev->next = node;
    gc->sentinel.prev = node;
    gc->tracked_count += 1;
}

void vigil_gc_untrack(vigil_gc_t *gc, void *object)
{
    vigil_gc_node_t *node;
    void *memory;

    if (gc == NULL || object == NULL)
        return;
    node = find_node(gc, object);
    if (node == NULL)
        return;

    node->prev->next = node->next;
    node->next->prev = node->prev;
    gc->tracked_count -= 1;

    memory = node;
    vigil_runtime_free(gc->runtime, &memory);
}

size_t vigil_gc_tracked_count(const vigil_gc_t *gc)
{
    if (gc == NULL)
        return 0;
    return gc->tracked_count;
}

size_t vigil_gc_collect(vigil_gc_t *gc)
{
    vigil_gc_node_t *node;
    vigil_gc_node_t *next;
    gc_object_header_t *header;
    size_t freed = 0;
    void *memory;

    if (gc == NULL || gc->tracked_count == 0)
        return 0;

    /* Phase 1: Copy ref counts into gc_refs. */
    for (node = gc->sentinel.next; node != &gc->sentinel; node = node->next)
    {
        header = (gc_object_header_t *)node->object;
        node->gc_refs = vigil_atomic_load(&header->ref_count);
    }

    /* Phase 2: Subtract internal references. */
    for (node = gc->sentinel.next; node != &gc->sentinel; node = node->next)
    {
        visit_children(gc, node, subtract_ref, NULL);
    }

    /* Phase 3: Restore objects reachable from external roots. */
    for (node = gc->sentinel.next; node != &gc->sentinel; node = node->next)
    {
        if (node->gc_refs > 0)
        {
            visit_children(gc, node, restore_ref, NULL);
        }
    }

    /*
     * Phase 4: Collect garbage.
     *
     * Objects with gc_refs == 0 are unreachable.  We must break cycles
     * before releasing so that the normal ref-count release path can
     * destroy them without infinite recursion.
     *
     * Strategy: for every garbage→garbage reference, directly decrement
     * the target's real ref_count and nil the slot.  This severs cycles
     * so that the subsequent vigil_object_release in 4b can destroy
     * each object without recursing into other garbage objects.
     */

    /* 4a: Break garbage→garbage references. */
    for (node = gc->sentinel.next; node != &gc->sentinel; node = node->next)
    {
        if (node->gc_refs != 0)
            continue;
        break_garbage_children(gc, node);
    }

    /* 4b: Release the garbage objects and remove their tracking nodes. */
    for (node = gc->sentinel.next; node != &gc->sentinel; node = next)
    {
        next = node->next;
        if (node->gc_refs != 0)
            continue;

        /* Unlink from tracking list. */
        node->prev->next = node->next;
        node->next->prev = node->prev;
        gc->tracked_count -= 1;

        /* Release the object (should now be destroyed since cycles are broken). */
        vigil_object_release((vigil_object_t **)&node->object);

        memory = node;
        vigil_runtime_free(gc->runtime, &memory);
        freed += 1;
    }

    return freed;
}
