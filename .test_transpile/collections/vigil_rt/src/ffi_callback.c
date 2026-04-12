/* ffi_callback.c — Callback trampoline pool for the VIGIL FFI.
 *
 * Provides a fixed pool of C function pointers that dispatch through
 * a user-registered callback function.  This allows VIGIL closures to
 * be passed as C function pointers to foreign code.
 */
#include "internal/ffi_callback.h"

#include <string.h>

#include "platform/platform.h"

static vigil_ffi_callback_dispatch_fn g_cb_dispatch;
static volatile int64_t g_cb_lock = 0;
static int g_cb_used[VIGIL_FFI_MAX_CALLBACKS];
static vigil_object_t *g_cb_closures[VIGIL_FFI_MAX_CALLBACKS];

static void callback_lock(void)
{
    while (!vigil_atomic_cas(&g_cb_lock, 0, 1))
    {
    }
}

static void callback_unlock(void)
{
    vigil_atomic_store(&g_cb_lock, 0);
}

void vigil_ffi_callback_set_dispatch(vigil_ffi_callback_dispatch_fn fn)
{
    callback_lock();
    g_cb_dispatch = fn;
    callback_unlock();
}

void vigil_ffi_callback_set_closure(int slot, vigil_object_t *closure)
{
    if (slot < 0 || slot >= VIGIL_FFI_MAX_CALLBACKS)
    {
        return;
    }

    callback_lock();
    if (g_cb_closures[slot])
    {
        vigil_object_release(&g_cb_closures[slot]);
    }
    g_cb_closures[slot] = closure;
    if (closure)
    {
        vigil_object_retain(closure);
    }
    callback_unlock();
}

vigil_object_t *vigil_ffi_callback_get_closure(int slot)
{
    vigil_object_t *closure = NULL;

    if (slot < 0 || slot >= VIGIL_FFI_MAX_CALLBACKS)
    {
        return NULL;
    }

    callback_lock();
    closure = g_cb_closures[slot];
    callback_unlock();
    return closure;
}

vigil_object_t *vigil_ffi_callback_retain_closure(int slot)
{
    vigil_object_t *closure = NULL;

    if (slot < 0 || slot >= VIGIL_FFI_MAX_CALLBACKS)
    {
        return NULL;
    }

    callback_lock();
    closure = g_cb_closures[slot];
    if (closure)
    {
        vigil_object_retain(closure);
    }
    callback_unlock();
    return closure;
}

static intptr_t cb_invoke(int slot, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3)
{
    vigil_ffi_callback_dispatch_fn dispatch = NULL;

    callback_lock();
    dispatch = g_cb_dispatch;
    callback_unlock();
    if (dispatch)
        return dispatch(slot, a0, a1, a2, a3);
    return 0;
}

#define CB_SLOT(N)                                                                                                     \
    static intptr_t cb_slot##N(intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3)                                     \
    {                                                                                                                  \
        return cb_invoke(N, a0, a1, a2, a3);                                                                           \
    }

CB_SLOT(0)
CB_SLOT(1)
CB_SLOT(2)
CB_SLOT(3)
CB_SLOT(4)
CB_SLOT(5)
CB_SLOT(6)
CB_SLOT(7)

#undef CB_SLOT

typedef intptr_t (*cb_fn_t)(intptr_t, intptr_t, intptr_t, intptr_t);

static cb_fn_t cb_table[VIGIL_FFI_MAX_CALLBACKS] = {
    cb_slot0, cb_slot1, cb_slot2, cb_slot3, cb_slot4, cb_slot5, cb_slot6, cb_slot7,
};

/*
 * Pedantic-safe conversion: function pointer -> void*.
 * ISO C forbids direct casts; use a union.
 */
static void *fnptr_to_obj(void (*f)(void))
{
    union {
        void *obj;
        void (*fn)(void);
    } u;
    u.fn = f;
    return u.obj;
}

int vigil_ffi_callback_alloc(void **out_ptr)
{
    if (out_ptr == NULL)
    {
        return -1;
    }

    callback_lock();
    for (int i = 0; i < VIGIL_FFI_MAX_CALLBACKS; i++)
    {
        if (!g_cb_used[i])
        {
            g_cb_used[i] = 1;
            *out_ptr = fnptr_to_obj((void (*)(void))cb_table[i]);
            callback_unlock();
            return i;
        }
    }
    callback_unlock();
    return -1;
}

void vigil_ffi_callback_free(int slot)
{
    if (slot < 0 || slot >= VIGIL_FFI_MAX_CALLBACKS)
    {
        return;
    }

    callback_lock();
    if (g_cb_closures[slot])
    {
        vigil_object_release(&g_cb_closures[slot]);
    }
    g_cb_used[slot] = 0;
    callback_unlock();
}

int vigil_ffi_callback_slot_from_ptr(void *ptr)
{
    if (ptr == NULL)
    {
        return -1;
    }

    for (int i = 0; i < VIGIL_FFI_MAX_CALLBACKS; i++)
    {
        if (fnptr_to_obj((void (*)(void))cb_table[i]) == ptr)
        {
            return i;
        }
    }
    return -1;
}

int vigil_ffi_callback_is_allocated(int slot)
{
    int used = 0;

    if (slot < 0 || slot >= VIGIL_FFI_MAX_CALLBACKS)
    {
        return 0;
    }

    callback_lock();
    used = g_cb_used[slot];
    callback_unlock();
    return used;
}
