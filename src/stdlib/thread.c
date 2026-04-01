/* VIGIL standard library: thread module.
 *
 * Provides threading primitives: mutexes, condition variables,
 * read-write locks, and utilities.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* ── Dynamic handle registry ─────────────────────────────────── */

#define INITIAL_CAPACITY 256

typedef struct
{
    void **items;
    int64_t count;
    int64_t capacity;
    vigil_platform_mutex_t *lock;
} handle_registry_t;

static vigil_status_t registry_init(handle_registry_t *r, vigil_error_t *error)
{
    vigil_status_t st;

    r->capacity = INITIAL_CAPACITY;
    r->items = calloc((size_t)r->capacity, sizeof(void *));
    r->count = 0;
    if (r->items == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "thread registry allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    st = vigil_platform_mutex_create(&r->lock, error);
    if (st != VIGIL_STATUS_OK)
    {
        free(r->items);
        r->items = NULL;
        r->capacity = 0;
    }
    return st;
}

static vigil_status_t registry_ensure_capacity_locked(handle_registry_t *r, int64_t needed, vigil_error_t *error)
{
    if (needed <= r->capacity)
    {
        return VIGIL_STATUS_OK;
    }

    {
        int64_t new_cap = r->capacity * 2;
        while (new_cap < needed)
            new_cap *= 2;
        void **new_items = calloc((size_t)new_cap, sizeof(void *));
        if (new_items == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "thread registry growth failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        memcpy(new_items, r->items, (size_t)r->capacity * sizeof(void *));
        free(r->items);
        r->items = new_items;
        r->capacity = new_cap;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t registry_alloc(handle_registry_t *r, int64_t *out_handle, vigil_error_t *error)
{
    vigil_status_t st;

    if (out_handle == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "thread registry out_handle is null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_platform_mutex_lock(r->lock);
    st = registry_ensure_capacity_locked(r, r->count + 1, error);
    if (st == VIGIL_STATUS_OK)
    {
        *out_handle = r->count++;
    }
    vigil_platform_mutex_unlock(r->lock);
    return st;
}

static void *registry_get(handle_registry_t *r, int64_t handle)
{
    void *value = NULL;

    vigil_platform_mutex_lock(r->lock);
    if (handle >= 0 && handle < r->count)
    {
        value = r->items[handle];
    }
    vigil_platform_mutex_unlock(r->lock);
    return value;
}

static void registry_set(handle_registry_t *r, int64_t handle, void *val)
{
    vigil_platform_mutex_lock(r->lock);
    if (handle >= 0 && handle < r->count)
    {
        r->items[handle] = val;
    }
    vigil_platform_mutex_unlock(r->lock);
}

static handle_registry_t g_mutexes;
static handle_registry_t g_conds;
static handle_registry_t g_rwlocks;

static volatile int64_t g_registries_state = 0;

static vigil_status_t ensure_registries(vigil_error_t *error)
{
    for (;;)
    {
        int64_t state = vigil_atomic_load(&g_registries_state);
        if (state == 2)
        {
            return VIGIL_STATUS_OK;
        }
        if (state == 0 && vigil_atomic_cas(&g_registries_state, 0, 1))
        {
            vigil_status_t st = registry_init(&g_mutexes, error);
            if (st == VIGIL_STATUS_OK)
            {
                st = registry_init(&g_conds, error);
            }
            if (st == VIGIL_STATUS_OK)
            {
                st = registry_init(&g_rwlocks, error);
            }
            if (st != VIGIL_STATUS_OK)
            {
                vigil_atomic_store(&g_registries_state, 0);
                return st;
            }
            vigil_atomic_store(&g_registries_state, 2);
            return VIGIL_STATUS_OK;
        }
        vigil_platform_thread_yield();
    }
}

/* ── Helpers ─────────────────────────────────────────────────── */

static vigil_status_t push_bool(vigil_vm_t *vm, int val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_bool(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_int(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static int64_t get_i64_arg(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (vigil_nanbox_is_int(v))
        return vigil_nanbox_decode_int(v);
    return 0;
}

/* ── Spawn support ───────────────────────────────────────────── */

#define INITIAL_THREAD_CAPACITY 256

typedef struct
{
    vigil_runtime_t *runtime;
    vigil_object_t *function;
    vigil_platform_thread_t *thread;
    volatile int64_t in_use;
    volatile int64_t result; /* return value from thread function */
} thread_slot_t;

static thread_slot_t **g_threads = NULL;
static int64_t g_thread_count = 0;
static int64_t g_thread_capacity = 0;
static vigil_platform_mutex_t *g_thread_lock = NULL;
static volatile int64_t g_threads_state = 0;

static vigil_status_t ensure_threads(vigil_error_t *error)
{
    for (;;)
    {
        int64_t state = vigil_atomic_load(&g_threads_state);
        if (state == 2)
        {
            return VIGIL_STATUS_OK;
        }
        if (state == 0 && vigil_atomic_cas(&g_threads_state, 0, 1))
        {
            vigil_status_t st;

            g_thread_capacity = INITIAL_THREAD_CAPACITY;
            g_threads = calloc((size_t)g_thread_capacity, sizeof(*g_threads));
            if (g_threads == NULL)
            {
                vigil_atomic_store(&g_threads_state, 0);
                vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "thread slot allocation failed");
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }

            st = vigil_platform_mutex_create(&g_thread_lock, error);
            if (st != VIGIL_STATUS_OK)
            {
                free(g_threads);
                g_threads = NULL;
                g_thread_capacity = 0;
                vigil_atomic_store(&g_threads_state, 0);
                return st;
            }

            vigil_atomic_store(&g_threads_state, 2);
            return VIGIL_STATUS_OK;
        }
        vigil_platform_thread_yield();
    }
}

static vigil_status_t ensure_thread_capacity_locked(int64_t needed, vigil_error_t *error)
{
    if (needed <= g_thread_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    {
        int64_t new_cap = g_thread_capacity * 2;
        while (new_cap < needed)
            new_cap *= 2;
        thread_slot_t **new_slots = calloc((size_t)new_cap, sizeof(*new_slots));
        if (new_slots == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "thread slot growth failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        memcpy(new_slots, g_threads, (size_t)g_thread_capacity * sizeof(*new_slots));
        free(g_threads);
        g_threads = new_slots;
        g_thread_capacity = new_cap;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t alloc_thread_slot(thread_slot_t **out_slot, int64_t *out_handle, vigil_error_t *error)
{
    vigil_status_t st = VIGIL_STATUS_OK;
    thread_slot_t *slot = NULL;

    if (out_slot == NULL || out_handle == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "thread slot output is null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_platform_mutex_lock(g_thread_lock);
    st = ensure_thread_capacity_locked(g_thread_count + 1, error);
    if (st == VIGIL_STATUS_OK)
    {
        slot = calloc(1U, sizeof(*slot));
        if (slot == NULL)
        {
            st = VIGIL_STATUS_OUT_OF_MEMORY;
            vigil_error_set_literal(error, st, "thread slot allocation failed");
        }
        else
        {
            *out_handle = g_thread_count;
            g_threads[g_thread_count++] = slot;
            *out_slot = slot;
        }
    }
    vigil_platform_mutex_unlock(g_thread_lock);
    return st;
}

static thread_slot_t *thread_get_slot(int64_t handle)
{
    thread_slot_t *slot = NULL;

    if (g_thread_lock == NULL)
    {
        return NULL;
    }

    vigil_platform_mutex_lock(g_thread_lock);
    if (handle >= 0 && handle < g_thread_count)
    {
        slot = g_threads[handle];
    }
    vigil_platform_mutex_unlock(g_thread_lock);
    return slot;
}

static void thread_spawn_entry(void *arg)
{
    thread_slot_t *slot = (thread_slot_t *)arg;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_value_t out = {0};

    if (vigil_vm_open(&vm, slot->runtime, NULL, &error) == VIGIL_STATUS_OK)
    {
        vigil_vm_execute_function(vm, slot->function, &out, &error);
        /* Capture return value if it's an integer */
        if (vigil_nanbox_is_int(out))
        {
            vigil_atomic_store(&slot->result, vigil_nanbox_decode_int(out));
        }
        vigil_vm_close(&vm);
    }
    vigil_object_release(&slot->function);
}

static vigil_status_t thread_spawn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    thread_slot_t *slot = NULL;
    int64_t idx = -1;
    vigil_status_t st;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t val = vigil_vm_stack_get(vm, base);

    st = ensure_threads(error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    vigil_object_t *fn = vigil_value_as_object(&val);
    if (fn == NULL || (vigil_object_type(fn) != VIGIL_OBJECT_FUNCTION && vigil_object_type(fn) != VIGIL_OBJECT_CLOSURE))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    st = alloc_thread_slot(&slot, &idx, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    /* Retain before popping so the closure isn't freed */
    vigil_object_retain(fn);
    vigil_vm_stack_pop_n(vm, arg_count);

    slot->runtime = vigil_vm_runtime(vm);
    slot->function = fn;
    vigil_atomic_store(&slot->in_use, 1);
    vigil_atomic_store(&slot->result, 0);

    st = vigil_platform_thread_create(&slot->thread, thread_spawn_entry, slot, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_object_release(&slot->function);
        vigil_atomic_store(&slot->in_use, 0);
        return push_i64(vm, -1, error);
    }
    return push_i64(vm, idx, error);
}

/* thread.join(handle) -> i64 (returns thread's return value) */
static vigil_status_t thread_join(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    thread_slot_t *slot = NULL;
    vigil_status_t st;
    vigil_vm_stack_pop_n(vm, arg_count);

    slot = thread_get_slot(handle);
    if (slot == NULL || !vigil_atomic_cas(&slot->in_use, 1, 0))
    {
        return push_i64(vm, INT64_MIN, error);
    }

    st = vigil_platform_thread_join(slot->thread, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_atomic_store(&slot->in_use, 1);
        return push_i64(vm, INT64_MIN, error);
    }

    slot->thread = NULL;
    {
        int64_t result = vigil_atomic_load(&slot->result);
        return push_i64(vm, result, error);
    }
}

/* thread.detach(handle) -> bool */
static vigil_status_t thread_detach(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    thread_slot_t *slot = NULL;
    vigil_status_t st;
    vigil_vm_stack_pop_n(vm, arg_count);

    slot = thread_get_slot(handle);
    if (slot == NULL || !vigil_atomic_cas(&slot->in_use, 1, 0))
    {
        return push_bool(vm, 0, error);
    }

    st = vigil_platform_thread_detach(slot->thread, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_atomic_store(&slot->in_use, 1);
        return push_bool(vm, 0, error);
    }

    slot->thread = NULL;
    return push_bool(vm, 1, error);
}

/* ── Thread functions ────────────────────────────────────────── */

static vigil_status_t thread_current_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (int64_t)vigil_platform_thread_current_id(), error);
}

static vigil_status_t thread_yield(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_platform_thread_yield();
    return push_bool(vm, 1, error);
}

static vigil_status_t thread_sleep(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ms = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (ms > 0)
        vigil_platform_thread_sleep((uint64_t)ms);
    return push_bool(vm, 1, error);
}

/* ── Mutex functions ─────────────────────────────────────────── */

static vigil_status_t thread_mutex(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    int64_t handle = -1;
    vigil_status_t s;
    vigil_vm_stack_pop_n(vm, arg_count);

    s = ensure_registries(error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    s = registry_alloc(&g_mutexes, &handle, error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    vigil_platform_mutex_t *m = NULL;
    s = vigil_platform_mutex_create(&m, error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    registry_set(&g_mutexes, handle, m);
    return push_i64(vm, handle, error);
}

static vigil_status_t mutex_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_mutex_t *m = registry_get(&g_mutexes, handle);
    if (!m)
        return push_bool(vm, 0, error);

    vigil_platform_mutex_lock(m);
    return push_bool(vm, 1, error);
}

static vigil_status_t mutex_unlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_mutex_t *m = registry_get(&g_mutexes, handle);
    if (!m)
        return push_bool(vm, 0, error);

    vigil_platform_mutex_unlock(m);
    return push_bool(vm, 1, error);
}

static vigil_status_t mutex_try_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_mutex_t *m = registry_get(&g_mutexes, handle);
    if (!m)
        return push_bool(vm, 0, error);

    int acquired = vigil_platform_mutex_trylock(m);
    return push_bool(vm, acquired, error);
}

static vigil_status_t mutex_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_mutex_t *m = registry_get(&g_mutexes, handle);
    if (!m)
        return push_bool(vm, 0, error);

    vigil_platform_mutex_destroy(m);
    registry_set(&g_mutexes, handle, NULL);
    return push_bool(vm, 1, error);
}

/* ── Condition variable functions ────────────────────────────── */

static vigil_status_t thread_cond(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    int64_t handle = -1;
    vigil_status_t s;
    vigil_vm_stack_pop_n(vm, arg_count);

    s = ensure_registries(error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    s = registry_alloc(&g_conds, &handle, error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    vigil_platform_cond_t *c = NULL;
    s = vigil_platform_cond_create(&c, error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    registry_set(&g_conds, handle, c);
    return push_i64(vm, handle, error);
}

static vigil_status_t cond_wait(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cond_h = get_i64_arg(vm, base, 0);
    int64_t mutex_h = arg_count > 1 ? get_i64_arg(vm, base, 1) : -1;
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_cond_t *c = registry_get(&g_conds, cond_h);
    vigil_platform_mutex_t *m = registry_get(&g_mutexes, mutex_h);
    if (!c || !m)
        return push_bool(vm, 0, error);

    vigil_platform_cond_wait(c, m);
    return push_bool(vm, 1, error);
}

/* thread.wait_timeout(cond, mutex, ms) -> bool (true if signalled, false on timeout) */
static vigil_status_t cond_wait_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cond_h = get_i64_arg(vm, base, 0);
    int64_t mutex_h = arg_count > 1 ? get_i64_arg(vm, base, 1) : -1;
    int64_t ms = arg_count > 2 ? get_i64_arg(vm, base, 2) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_cond_t *c = registry_get(&g_conds, cond_h);
    vigil_platform_mutex_t *m = registry_get(&g_mutexes, mutex_h);
    if (!c || !m)
        return push_bool(vm, 0, error);

    int signalled = vigil_platform_cond_timedwait(c, m, (uint64_t)(ms > 0 ? ms : 0));
    return push_bool(vm, signalled, error);
}

static vigil_status_t cond_signal(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_cond_t *c = registry_get(&g_conds, handle);
    if (!c)
        return push_bool(vm, 0, error);

    vigil_platform_cond_signal(c);
    return push_bool(vm, 1, error);
}

static vigil_status_t cond_broadcast(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_cond_t *c = registry_get(&g_conds, handle);
    if (!c)
        return push_bool(vm, 0, error);

    vigil_platform_cond_broadcast(c);
    return push_bool(vm, 1, error);
}

static vigil_status_t cond_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_cond_t *c = registry_get(&g_conds, handle);
    if (!c)
        return push_bool(vm, 0, error);

    vigil_platform_cond_destroy(c);
    registry_set(&g_conds, handle, NULL);
    return push_bool(vm, 1, error);
}

/* ── RWLock functions ────────────────────────────────────────── */

static vigil_status_t thread_rwlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    int64_t handle = -1;
    vigil_status_t s;
    vigil_vm_stack_pop_n(vm, arg_count);

    s = ensure_registries(error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    s = registry_alloc(&g_rwlocks, &handle, error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    vigil_platform_rwlock_t *rw = NULL;
    s = vigil_platform_rwlock_create(&rw, error);
    if (s != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    registry_set(&g_rwlocks, handle, rw);
    return push_i64(vm, handle, error);
}

static vigil_status_t rwlock_read_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_rwlock_t *rw = registry_get(&g_rwlocks, handle);
    if (!rw)
        return push_bool(vm, 0, error);

    vigil_platform_rwlock_rdlock(rw);
    return push_bool(vm, 1, error);
}

static vigil_status_t rwlock_write_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_rwlock_t *rw = registry_get(&g_rwlocks, handle);
    if (!rw)
        return push_bool(vm, 0, error);

    vigil_platform_rwlock_wrlock(rw);
    return push_bool(vm, 1, error);
}

static vigil_status_t rwlock_unlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_rwlock_t *rw = registry_get(&g_rwlocks, handle);
    if (!rw)
        return push_bool(vm, 0, error);

    vigil_platform_rwlock_unlock(rw);
    return push_bool(vm, 1, error);
}

static vigil_status_t rwlock_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_platform_rwlock_t *rw = registry_get(&g_rwlocks, handle);
    if (!rw)
        return push_bool(vm, 0, error);

    vigil_platform_rwlock_destroy(rw);
    registry_set(&g_rwlocks, handle, NULL);
    return push_bool(vm, 1, error);
}

/* ── Module definition ───────────────────────────────────────── */

static const int i64_param[] = {VIGIL_TYPE_I64};
static const int i64_i64_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int i64_i64_i64_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int object_param[] = {VIGIL_TYPE_OBJECT};

static const vigil_native_module_function_t thread_funcs[] = {
    /* Thread management */
    {"spawn", 5U, thread_spawn, 1U, object_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"join", 4U, thread_join, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"detach", 6U, thread_detach, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"current_id", 10U, thread_current_id, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"yield", 5U, thread_yield, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"sleep", 5U, thread_sleep, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},

    /* Mutex */
    {"mutex", 5U, thread_mutex, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"lock", 4U, mutex_lock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"unlock", 6U, mutex_unlock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"try_lock", 8U, mutex_try_lock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"mutex_destroy", 13U, mutex_destroy, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},

    /* Condition variable */
    {"cond", 4U, thread_cond, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"wait", 4U, cond_wait, 2U, i64_i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"wait_timeout", 12U, cond_wait_timeout, 3U, i64_i64_i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    {"signal", 6U, cond_signal, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"broadcast", 9U, cond_broadcast, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"cond_destroy", 12U, cond_destroy, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},

    /* RWLock */
    {"rwlock", 6U, thread_rwlock, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"read_lock", 9U, rwlock_read_lock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"write_lock", 10U, rwlock_write_lock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"rw_unlock", 9U, rwlock_unlock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"rwlock_destroy", 14U, rwlock_destroy, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_thread = {
    "thread", 6U, thread_funcs, sizeof(thread_funcs) / sizeof(thread_funcs[0]), NULL, 0U, NULL};
