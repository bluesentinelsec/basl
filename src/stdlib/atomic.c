/* VIGIL standard library: atomic module.
 *
 * Provides atomic operations for lock-free programming.
 *
 * Functions:
 *   atomic.new(i64 initial) -> i64       allocate atomic, return handle
 *   atomic.load(i64 handle) -> i64       atomic load
 *   atomic.store(i64 handle, i64 val)    atomic store
 *   atomic.add(i64 h, i64 v) -> i64      fetch-add (returns old)
 *   atomic.sub(i64 h, i64 v) -> i64      fetch-sub (returns old)
 *   atomic.cas(i64 h, i64 exp, i64 des)  compare-and-swap -> bool
 *   atomic.exchange(i64 h, i64 v) -> i64 atomic swap (returns old)
 *   atomic.fetch_or(i64 h, i64 v) -> i64
 *   atomic.fetch_and(i64 h, i64 v) -> i64
 *   atomic.fetch_xor(i64 h, i64 v) -> i64
 *   atomic.inc(i64 h) -> i64             fetch-add 1
 *   atomic.dec(i64 h) -> i64             fetch-sub 1
 *   atomic.fence() -> void               seq_cst memory fence
 */
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "platform/platform.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

/* ── Dynamic handle registry ─────────────────────────────────────── */

#define INITIAL_CAPACITY 1024

static volatile int64_t **g_atomics = NULL;
static volatile int64_t g_atomic_count = 0;
static size_t g_atomic_capacity = 0;

/* Protects handle table growth and lookup. */
static volatile int64_t g_registry_lock = 0;

static void registry_lock(void)
{
    while (!vigil_atomic_cas(&g_registry_lock, 0, 1))
    { /* spin */
    }
}

static void registry_unlock(void)
{
    vigil_atomic_store(&g_registry_lock, 0);
}

static int ensure_capacity_locked(size_t needed)
{
    if (needed <= g_atomic_capacity)
        return 1;
    size_t new_cap = g_atomic_capacity ? g_atomic_capacity : INITIAL_CAPACITY;
    while (new_cap < needed)
        new_cap *= 2;
    volatile int64_t **new_buf = (volatile int64_t **)calloc(new_cap, sizeof(*new_buf));
    if (new_buf == NULL)
    {
        return 0;
    }
    if (g_atomics != NULL)
    {
        memcpy((void *)new_buf, (const void *)g_atomics, (size_t)g_atomic_count * sizeof(*new_buf));
        free((void *)g_atomics);
    }
    g_atomics = new_buf;
    g_atomic_capacity = new_cap;
    return 1;
}

/* ── Helpers ─────────────────────────────────────────────────── */

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_int(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t push_bool(vigil_vm_t *vm, int val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_bool(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static int64_t get_i64_arg(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (vigil_nanbox_is_int(v))
        return vigil_nanbox_decode_int(v);
    return 0;
}

static int valid_handle(int64_t handle)
{
    return handle >= 0 && handle < vigil_atomic_load(&g_atomic_count);
}

static volatile int64_t *lookup_atomic_cell(int64_t handle)
{
    volatile int64_t *cell = NULL;

    if (!valid_handle(handle))
        return NULL;

    registry_lock();
    if (handle >= 0 && (size_t)handle < g_atomic_capacity)
    {
        cell = g_atomics[handle];
    }
    registry_unlock();
    return cell;
}

static vigil_status_t push_i64_and_ok(vigil_vm_t *vm, int64_t val, vigil_error_t *error)
{
    vigil_status_t s = push_i64(vm, val, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_i64_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_status_t s = push_i64(vm, 0, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_object_t *obj = NULL;
    s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── Functions ───────────────────────────────────────────────── */

static vigil_status_t atomic_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t initial = arg_count > 0 ? get_i64_arg(vm, base, 0) : 0;
    volatile int64_t *cell = NULL;
    int64_t handle = -1;
    vigil_vm_stack_pop_n(vm, arg_count);

    registry_lock();
    if (!ensure_capacity_locked((size_t)(vigil_atomic_load(&g_atomic_count) + 1)))
    {
        registry_unlock();
        return push_i64_and_err(vm, "atomic.new: registry capacity exceeded", error);
    }

    void *mem = NULL;
    vigil_status_t st = vigil_runtime_alloc(vigil_vm_runtime(vm), sizeof(int64_t), &mem, error);
    if (st != VIGIL_STATUS_OK || mem == NULL)
    {
        registry_unlock();
        return push_i64_and_err(vm, "atomic.new: allocation failed", error);
    }
    cell = (volatile int64_t *)mem;
    handle = g_atomic_count;
    vigil_atomic_store(cell, initial);
    g_atomics[handle] = cell;
    g_atomic_count++;
    registry_unlock();
    return push_i64_and_ok(vm, handle, error);
}

static vigil_status_t atomic_load_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_load(cell), error);
}

static vigil_status_t atomic_store_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_bool(vm, 0, error);
    vigil_atomic_store(cell, val);
    return push_bool(vm, 1, error);
}

static vigil_status_t atomic_add_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_add(cell, val), error);
}

static vigil_status_t atomic_sub_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_sub(cell, val), error);
}

static vigil_status_t atomic_cas_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t expected = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    int64_t desired = arg_count > 2 ? get_i64_arg(vm, base, 2) : 0;
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_bool(vm, 0, error);
    return push_bool(vm, vigil_atomic_cas(cell, expected, desired), error);
}

static vigil_status_t atomic_exchange_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_exchange(cell, val), error);
}

static vigil_status_t atomic_fetch_or_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_fetch_or(cell, val), error);
}

static vigil_status_t atomic_fetch_and_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_fetch_and(cell, val), error);
}

static vigil_status_t atomic_fetch_xor_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_fetch_xor(cell, val), error);
}

static vigil_status_t atomic_inc_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_add(cell, 1), error);
}

static vigil_status_t atomic_dec_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    volatile int64_t *cell = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    cell = lookup_atomic_cell(handle);
    if (cell == NULL)
        return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_sub(cell, 1), error);
}

static vigil_status_t atomic_fence_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_atomic_fence();
    return VIGIL_STATUS_OK;
}

/* ── Module definition ───────────────────────────────────────── */

static const int i64_param[] = {VIGIL_TYPE_I64};
static const int i64_i64_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int i64_i64_i64_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int i64_err_returns[] = {VIGIL_TYPE_I64, VIGIL_TYPE_ERR};
static const char *const atomic_initial_param_names[] = {"initial"};
static const char *const atomic_handle_param_names[] = {"a"};
static const char *const atomic_handle_value_param_names[] = {"a", "val"};
static const char *const atomic_handle_expected_desired_param_names[] = {"a", "expected", "desired"};

static const vigil_native_symbol_doc_t vigil_atomic_module_doc = {
    "Atomic operations.",
    "The atomic module provides lock-free atomic integer operations for thread-safe programming without mutexes.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_atomic_new_doc = {
    "Create atomic integer handle.",
    "Creates an atomic integer cell with the given initial value and returns its handle.",
    "i64 a = atomic.new(0)",
};

static const vigil_native_symbol_doc_t vigil_atomic_load_doc = {
    "Atomically load value.",
    "Returns the current value stored in the atomic cell handle.",
    "atomic.load(a)",
};

static const vigil_native_symbol_doc_t vigil_atomic_store_doc = {
    "Atomically store value.",
    "Sets the atomic cell to the given value.",
    "atomic.store(a, 42)",
};

static const vigil_native_symbol_doc_t vigil_atomic_add_doc = {
    "Atomically add.",
    "Adds val to the cell and returns the previous value.",
    "atomic.add(a, 1)",
};

static const vigil_native_symbol_doc_t vigil_atomic_sub_doc = {
    "Atomically subtract.",
    "Subtracts val from the cell and returns the previous value.",
    "atomic.sub(a, 1)",
};

static const vigil_native_symbol_doc_t vigil_atomic_cas_doc = {
    "Compare and swap.",
    "Sets the cell to desired if the current value equals expected.",
    "atomic.cas(a, 0, 1)",
};

static const vigil_native_symbol_doc_t vigil_atomic_exchange_doc = {
    "Atomically exchange value.",
    "Stores val in the cell and returns the previous value.",
    "atomic.exchange(a, 5)",
};

static const vigil_native_symbol_doc_t vigil_atomic_fetch_or_doc = {
    "Atomically bitwise-OR value.",
    "Applies bitwise OR with val and returns the previous value.",
    "atomic.fetch_or(a, 0x10)",
};

static const vigil_native_symbol_doc_t vigil_atomic_fetch_and_doc = {
    "Atomically bitwise-AND value.",
    "Applies bitwise AND with val and returns the previous value.",
    "atomic.fetch_and(a, 0xff)",
};

static const vigil_native_symbol_doc_t vigil_atomic_fetch_xor_doc = {
    "Atomically bitwise-XOR value.",
    "Applies bitwise XOR with val and returns the previous value.",
    "atomic.fetch_xor(a, 0x01)",
};

static const vigil_native_symbol_doc_t vigil_atomic_inc_doc = {
    "Atomically increment.",
    "Increments the cell by 1 and returns the previous value.",
    "atomic.inc(a)",
};

static const vigil_native_symbol_doc_t vigil_atomic_dec_doc = {
    "Atomically decrement.",
    "Decrements the cell by 1 and returns the previous value.",
    "atomic.dec(a)",
};

static const vigil_native_symbol_doc_t vigil_atomic_fence_doc = {
    "Issue a full memory fence.",
    "Forces a full memory barrier for ordering surrounding atomic operations.",
    "atomic.fence()",
};

static const vigil_native_module_function_t atomic_funcs[] = {
    {"new", 3U, atomic_new, 1U, i64_param, VIGIL_TYPE_I64, 2U, i64_err_returns, 0, NULL, NULL, 0U,
     atomic_initial_param_names, NULL, NULL, &vigil_atomic_new_doc},
    {"load", 4U, atomic_load_fn, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, atomic_handle_param_names,
     NULL, NULL, &vigil_atomic_load_doc},
    {"store", 5U, atomic_store_fn, 2U, i64_i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     atomic_handle_value_param_names, NULL, NULL, &vigil_atomic_store_doc},
    {"add", 3U, atomic_add_fn, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     atomic_handle_value_param_names, NULL, NULL, &vigil_atomic_add_doc},
    {"sub", 3U, atomic_sub_fn, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     atomic_handle_value_param_names, NULL, NULL, &vigil_atomic_sub_doc},
    {"cas", 3U, atomic_cas_fn, 3U, i64_i64_i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     atomic_handle_expected_desired_param_names, NULL, NULL, &vigil_atomic_cas_doc},
    {"exchange", 8U, atomic_exchange_fn, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     atomic_handle_value_param_names, NULL, NULL, &vigil_atomic_exchange_doc},
    {"fetch_or", 8U, atomic_fetch_or_fn, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     atomic_handle_value_param_names, NULL, NULL, &vigil_atomic_fetch_or_doc},
    {"fetch_and", 9U, atomic_fetch_and_fn, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     atomic_handle_value_param_names, NULL, NULL, &vigil_atomic_fetch_and_doc},
    {"fetch_xor", 9U, atomic_fetch_xor_fn, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     atomic_handle_value_param_names, NULL, NULL, &vigil_atomic_fetch_xor_doc},
    {"inc", 3U, atomic_inc_fn, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, atomic_handle_param_names,
     NULL, NULL, &vigil_atomic_inc_doc},
    {"dec", 3U, atomic_dec_fn, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, atomic_handle_param_names,
     NULL, NULL, &vigil_atomic_dec_doc},
    {"fence", 5U, atomic_fence_fn, 0U, NULL, VIGIL_TYPE_VOID, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_atomic_fence_doc},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_atomic = {
    "atomic", 6U, atomic_funcs, sizeof(atomic_funcs) / sizeof(atomic_funcs[0]), NULL, 0U, &vigil_atomic_module_doc,
    NULL,     0U};
