/* VIGIL standard library: unsafe module.
 *
 * Low-level memory operations for FFI interop.
 *
 * Buffer-based (bounds-checked, slot-managed):
 *   unsafe.alloc(i32 size) -> i64          allocate a byte buffer
 *   unsafe.realloc(i64 buf, i32 size)->i64 resize a buffer
 *   unsafe.free(i64 buf)                   free a buffer
 *   unsafe.get(i64 buf, i32 index) -> i32  read byte at index
 *   unsafe.set(i64 buf, i32 index, i32 v)  write byte at index
 *   unsafe.get_i32 / set_i32               read/write i32 at offset
 *   unsafe.get_i64 / set_i64               read/write i64 at offset
 *   unsafe.get_f32 / set_f32               read/write f32 at offset
 *   unsafe.get_f64 / set_f64               read/write f64 at offset
 *   unsafe.ptr(i64 buf) -> i64             raw pointer to buffer data
 *   unsafe.len(i64 buf) -> i32             buffer size
 *   unsafe.null() -> i64                   null pointer constant
 *   unsafe.copy(dst,doff,src,soff,n)       copy between buffers
 *   unsafe.write_str(buf, off, string)     pack C string into buffer
 *
 * Raw-pointer (unchecked, for reading C-returned pointers):
 *   unsafe.peek_u8(ptr, off) -> i32        read u8
 *   unsafe.peek_i32(ptr, off) -> i32       read i32
 *   unsafe.peek_i64(ptr, off) -> i64       read i64
 *   unsafe.peek_f32(ptr, off) -> f64       read f32 (promoted to f64)
 *   unsafe.peek_f64(ptr, off) -> f64       read f64
 *   unsafe.peek_ptr(ptr, off) -> i64       read pointer
 *   unsafe.poke_u8(ptr, off, val)          write u8
 *   unsafe.poke_i32(ptr, off, val)         write i32
 *   unsafe.poke_i64(ptr, off, val)         write i64
 *   unsafe.poke_f32(ptr, off, f64 val)     write f32 (truncated from f64)
 *   unsafe.poke_f64(ptr, off, f64 val)     write f64
 *   unsafe.poke_ptr(ptr, off, val)         write pointer
 *
 * Utility:
 *   unsafe.str(i64 ptr) -> string          read NUL-terminated C string
 *   unsafe.sizeof_ptr() -> i32             pointer size on this platform
 *   unsafe.cb_alloc() -> i64               allocate a callback slot
 *   unsafe.cb_free(i32 slot)               free a callback slot
 */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/unsafe_buffer.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/ffi_callback.h"
#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* For vigil_string_object_new_cstr */
#include "vigil/runtime.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_int(v);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t push_i32(vigil_vm_t *vm, int32_t v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_i32(v);
    return vigil_vm_stack_push(vm, &val, error);
}

static int64_t arg_i64(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base + idx));
}

static int32_t arg_i32(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base + idx));
}

static double arg_f64(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + idx));
}

static vigil_status_t push_f64(vigil_vm_t *vm, double v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_double(v);
    return vigil_vm_stack_push(vm, &val, error);
}

/*
 * Read a string arg into a caller buffer (safe against stack_pop_n).
 */
static const char *arg_str_buf(vigil_vm_t *vm, size_t base, size_t idx, char *buf, size_t bufsz)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
    {
        const char *s = vigil_string_object_c_str(obj);
        size_t len = strlen(s);
        if (len >= bufsz)
            len = bufsz - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
        return buf;
    }
    buf[0] = '\0';
    return buf;
}

static vigil_status_t push_string(vigil_vm_t *vm, vigil_runtime_t *rt, const char *s, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_string_object_new_cstr(rt, s ? s : "", &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    st = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return st;
}

/* ── Buffer tracking ─────────────────────────────────────────────── */
/* Simple table so we can bounds-check and prevent use-after-free.    */

typedef struct
{
    uint8_t *data;
    int32_t size;
} buf_entry_t;

#define MAX_BUFS 256
static buf_entry_t g_bufs[MAX_BUFS];

static int buf_find_slot(void)
{
    for (int i = 0; i < MAX_BUFS; i++)
        if (!g_bufs[i].data)
            return i;
    return -1;
}

/* ── Public buffer API (see vigil/unsafe_buffer.h) ───────────────── */

void *vigil_unsafe_buffer_get(int64_t slot, int32_t *out_size)
{
    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data)
        return NULL;
    if (out_size)
        *out_size = g_bufs[slot].size;
    return g_bufs[slot].data;
}

int64_t vigil_unsafe_buffer_alloc(int32_t size)
{
    if (size <= 0)
        return -1;
    int slot = buf_find_slot();
    if (slot < 0)
        return -1;
    g_bufs[slot].data = (uint8_t *)calloc((size_t)size, 1);
    if (!g_bufs[slot].data)
        return -1;
    g_bufs[slot].size = size;
    return (int64_t)slot;
}

void vigil_unsafe_buffer_free(int64_t slot)
{
    if (slot >= 0 && slot < MAX_BUFS && g_bufs[slot].data)
    {
        free(g_bufs[slot].data);
        g_bufs[slot].data = NULL;
        g_bufs[slot].size = 0;
    }
}

int64_t vigil_unsafe_buffer_register(void *data, int32_t size)
{
    if (!data || size <= 0)
        return -1;
    int slot = buf_find_slot();
    if (slot < 0)
        return -1;
    g_bufs[slot].data = (uint8_t *)data;
    g_bufs[slot].size = size;
    return (int64_t)slot;
}

/* ── unsafe.alloc(i32 size) -> i64 ───────────────────────────────── */

static vigil_status_t vigil_unsafe_alloc(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t size = arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (size <= 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.alloc: size must be > 0");
        return VIGIL_STATUS_INTERNAL;
    }
    int slot = buf_find_slot();
    if (slot < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.alloc: too many buffers");
        return VIGIL_STATUS_INTERNAL;
    }
    g_bufs[slot].data = (uint8_t *)calloc((size_t)size, 1);
    if (!g_bufs[slot].data)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.alloc: out of memory");
        return VIGIL_STATUS_INTERNAL;
    }
    g_bufs[slot].size = size;
    return push_i64(vm, (int64_t)slot, error);
}

/* ── unsafe.free(i64 buf) ────────────────────────────────────────── */

static vigil_status_t vigil_unsafe_free(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (slot >= 0 && slot < MAX_BUFS && g_bufs[slot].data)
    {
        free(g_bufs[slot].data);
        g_bufs[slot].data = NULL;
        g_bufs[slot].size = 0;
    }
    return VIGIL_STATUS_OK;
}

/* ── unsafe.get(i64 buf, i32 index) -> i32 ───────────────────────── */

static vigil_status_t vigil_unsafe_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t idx = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || idx < 0 || idx >= g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.get: index out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    return push_i32(vm, (int32_t)g_bufs[slot].data[idx], error);
}

/* ── unsafe.set(i64 buf, i32 index, i32 value) ──────────────────── */

static vigil_status_t vigil_unsafe_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t idx = arg_i32(vm, base, 1);
    int32_t val = arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || idx < 0 || idx >= g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.set: index out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    g_bufs[slot].data[idx] = (uint8_t)val;
    return VIGIL_STATUS_OK;
}

/* ── unsafe.get_i32(i64 buf, i32 offset) -> i32 ─────────────────── */

static vigil_status_t vigil_unsafe_get_i32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + 4 > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.get_i32: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    int32_t v;
    memcpy(&v, g_bufs[slot].data + off, 4);
    return push_i32(vm, v, error);
}

/* ── unsafe.set_i32(i64 buf, i32 offset, i32 value) ─────────────── */

static vigil_status_t vigil_unsafe_set_i32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    int32_t val = arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + 4 > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.set_i32: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    memcpy(g_bufs[slot].data + off, &val, 4);
    return VIGIL_STATUS_OK;
}

/* ── unsafe.get_i64(i64 buf, i32 offset) -> i64 ─────────────────── */

static vigil_status_t vigil_unsafe_get_i64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + 8 > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.get_i64: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    int64_t v;
    memcpy(&v, g_bufs[slot].data + off, 8);
    return push_i64(vm, v, error);
}

/* ── unsafe.set_i64(i64 buf, i32 offset, i64 value) ─────────────── */

static vigil_status_t vigil_unsafe_set_i64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    int64_t val = arg_i64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + 8 > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.set_i64: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    memcpy(g_bufs[slot].data + off, &val, 8);
    return VIGIL_STATUS_OK;
}

/* ── unsafe.ptr(i64 buf) -> i64 ──────────────────────────────────── */

static vigil_status_t vigil_unsafe_ptr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.ptr: invalid buffer");
        return VIGIL_STATUS_INTERNAL;
    }
    return push_i64(vm, (int64_t)(intptr_t)g_bufs[slot].data, error);
}

/* ── unsafe.null() -> i64 ────────────────────────────────────────── */

static vigil_status_t vigil_unsafe_null(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, 0, error);
}

/* ── unsafe.str(i64 ptr) -> string ───────────────────────────────── */
/* Read a NUL-terminated C string from a raw pointer.                 */

static vigil_status_t vigil_unsafe_str(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *p = (const char *)(intptr_t)arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    return push_string(vm, rt, p, error);
}

/* ── unsafe.copy(i64 dst, i32 dst_off, i64 src, i32 src_off, i32 n) ─ */
/* Copy bytes between buffers.                                          */

static vigil_status_t vigil_unsafe_copy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int dst_slot = (int)arg_i64(vm, base, 0);
    int32_t dst_off = arg_i32(vm, base, 1);
    int src_slot = (int)arg_i64(vm, base, 2);
    int32_t src_off = arg_i32(vm, base, 3);
    int32_t n = arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (dst_slot < 0 || dst_slot >= MAX_BUFS || !g_bufs[dst_slot].data || src_slot < 0 || src_slot >= MAX_BUFS ||
        !g_bufs[src_slot].data || dst_off < 0 || src_off < 0 || n < 0 || dst_off + n > g_bufs[dst_slot].size ||
        src_off + n > g_bufs[src_slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.copy: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    memmove(g_bufs[dst_slot].data + dst_off, g_bufs[src_slot].data + src_off, (size_t)n);
    return VIGIL_STATUS_OK;
}

/* ── unsafe.len(i64 buf) -> i32 ──────────────────────────────────── */

static vigil_status_t vigil_unsafe_len(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.len: invalid buffer");
        return VIGIL_STATUS_INTERNAL;
    }
    return push_i32(vm, g_bufs[slot].size, error);
}

/* ── unsafe.realloc(i64 buf, i32 new_size) -> i64 ───────────────── */

static vigil_status_t vigil_unsafe_realloc(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t new_size = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || new_size <= 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.realloc: invalid buffer or size");
        return VIGIL_STATUS_INTERNAL;
    }
    uint8_t *p = (uint8_t *)realloc(g_bufs[slot].data, (size_t)new_size);
    if (!p)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.realloc: out of memory");
        return VIGIL_STATUS_INTERNAL;
    }
    /* Zero-fill new bytes if grown. */
    if (new_size > g_bufs[slot].size)
        memset(p + g_bufs[slot].size, 0, (size_t)(new_size - g_bufs[slot].size));
    g_bufs[slot].data = p;
    g_bufs[slot].size = new_size;
    return push_i64(vm, (int64_t)slot, error);
}

/* ── unsafe.get_f32(i64 buf, i32 off) -> f64 ────────────────────── */

static vigil_status_t vigil_unsafe_get_f32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + 4 > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.get_f32: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    float f;
    memcpy(&f, g_bufs[slot].data + off, 4);
    return push_f64(vm, (double)f, error);
}

/* ── unsafe.set_f32(i64 buf, i32 off, f64 val) ──────────────────── */

static vigil_status_t vigil_unsafe_set_f32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    float val = (float)arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + 4 > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.set_f32: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    memcpy(g_bufs[slot].data + off, &val, 4);
    return VIGIL_STATUS_OK;
}

/* ── unsafe.get_f64(i64 buf, i32 off) -> f64 ────────────────────── */

static vigil_status_t vigil_unsafe_get_f64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + 8 > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.get_f64: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    double d;
    memcpy(&d, g_bufs[slot].data + off, 8);
    return push_f64(vm, d, error);
}

/* ── unsafe.set_f64(i64 buf, i32 off, f64 val) ──────────────────── */

static vigil_status_t vigil_unsafe_set_f64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    double val = arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + 8 > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.set_f64: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    memcpy(g_bufs[slot].data + off, &val, 8);
    return VIGIL_STATUS_OK;
}

/* ── unsafe.write_str(i64 buf, i32 off, string s) ───────────────── */
/* Pack a NUL-terminated C string into a buffer at offset.            */

static vigil_status_t vigil_unsafe_write_str(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    char str[512];
    arg_str_buf(vm, base, 2, str, sizeof(str));
    vigil_vm_stack_pop_n(vm, arg_count);

    size_t slen = strlen(str) + 1; /* include NUL */
    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data || off < 0 || off + (int32_t)slen > g_bufs[slot].size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.write_str: out of bounds");
        return VIGIL_STATUS_INTERNAL;
    }
    memcpy(g_bufs[slot].data + off, str, slen);
    return VIGIL_STATUS_OK;
}

/* ── unsafe.sizeof_ptr() -> i32 ──────────────────────────────────── */

static vigil_status_t vigil_unsafe_sizeof_ptr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, (int32_t)sizeof(void *), error);
}

/* ── Raw-pointer peek/poke (unchecked) ───────────────────────────── */
/* These operate on arbitrary pointers returned by C functions.        */
/* No bounds checking — caller is responsible for validity.            */

static vigil_status_t vigil_unsafe_peek_u8(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, (int32_t)p[off], error);
}

static vigil_status_t vigil_unsafe_peek_i32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t v;
    memcpy(&v, p + off, 4);
    return push_i32(vm, v, error);
}

static vigil_status_t vigil_unsafe_peek_i64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int64_t v;
    memcpy(&v, p + off, 8);
    return push_i64(vm, v, error);
}

static vigil_status_t vigil_unsafe_peek_f32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    float f;
    memcpy(&f, p + off, 4);
    return push_f64(vm, (double)f, error);
}

static vigil_status_t vigil_unsafe_peek_f64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    double d;
    memcpy(&d, p + off, 8);
    return push_f64(vm, d, error);
}

static vigil_status_t vigil_unsafe_peek_ptr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *v;
    memcpy(&v, p + off, sizeof(void *));
    return push_i64(vm, (int64_t)(intptr_t)v, error);
}

static vigil_status_t vigil_unsafe_poke_u8(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    uint8_t val = (uint8_t)arg_i32(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    p[off] = val;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_unsafe_poke_i32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    int32_t val = arg_i32(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    memcpy(p + off, &val, 4);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_unsafe_poke_i64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    int64_t val = arg_i64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    memcpy(p + off, &val, 8);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_unsafe_poke_f32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    float val = (float)arg_f64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    memcpy(p + off, &val, 4);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_unsafe_poke_f64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    double val = arg_f64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    memcpy(p + off, &val, 8);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_unsafe_poke_ptr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    uint8_t *p = (uint8_t *)(intptr_t)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    void *val = (void *)(intptr_t)arg_i64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    memcpy(p + off, &val, sizeof(void *));
    return VIGIL_STATUS_OK;
}

/* ── unsafe.sizeof(string type_name) -> i32 ──────────────────────── */
/* Returns the size in bytes of a C primitive type.                    */
/* Supported: u8, i8, i16, u16, i32, u32, i64, u64, f32, f64, ptr.   */

static int32_t type_sizeof(const char *t)
{
    if (strcmp(t, "u8") == 0 || strcmp(t, "i8") == 0)
        return 1;
    if (strcmp(t, "i16") == 0 || strcmp(t, "u16") == 0)
        return 2;
    if (strcmp(t, "i32") == 0 || strcmp(t, "u32") == 0)
        return 4;
    if (strcmp(t, "i64") == 0 || strcmp(t, "u64") == 0)
        return 8;
    if (strcmp(t, "f32") == 0)
        return 4;
    if (strcmp(t, "f64") == 0)
        return 8;
    if (strcmp(t, "ptr") == 0)
        return (int32_t)sizeof(void *);
    return 0;
}

static int32_t type_align(const char *t)
{
    /* On all modern platforms, alignment == size for primitives up to 8. */
    return type_sizeof(t);
}

static vigil_status_t vigil_unsafe_sizeof(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[32];
    arg_str_buf(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);

    int32_t sz = type_sizeof(name);
    if (sz == 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.sizeof: unknown type");
        return VIGIL_STATUS_INTERNAL;
    }
    return push_i32(vm, sz, error);
}

/* ── unsafe.alignof(string type_name) -> i32 ─────────────────────── */

static vigil_status_t vigil_unsafe_alignof(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[32];
    arg_str_buf(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);

    int32_t a = type_align(name);
    if (a == 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.alignof: unknown type");
        return VIGIL_STATUS_INTERNAL;
    }
    return push_i32(vm, a, error);
}

/* ── unsafe.offsetof(string types_csv, i32 field_index) -> i32 ───── */
/* Given a comma-separated list of type names (e.g. "i32,f32,ptr"),    */
/* compute the byte offset of the field at field_index, respecting     */
/* natural alignment and padding.                                      */

static vigil_status_t vigil_unsafe_offsetof(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char layout[256];
    arg_str_buf(vm, base, 0, layout, sizeof(layout));
    int32_t target = arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    int32_t offset = 0;
    int32_t idx = 0;
    char *p = layout;
    while (*p)
    {
        char *comma = strchr(p, ',');
        if (comma)
            *comma = '\0';
        /* Trim leading spaces. */
        while (*p == ' ')
            p++;

        int32_t sz = type_sizeof(p);
        int32_t al = type_align(p);
        if (sz == 0)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.offsetof: unknown type in layout");
            return VIGIL_STATUS_INTERNAL;
        }
        /* Align offset. */
        if (al > 0)
            offset = (offset + al - 1) & ~(al - 1);
        if (idx == target)
            return push_i32(vm, offset, error);
        offset += sz;
        idx++;
        p = comma ? comma + 1 : p + strlen(p);
    }
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.offsetof: field index out of range");
    return VIGIL_STATUS_INTERNAL;
}

/* ── unsafe.struct_size(string types_csv) -> i32 ─────────────────── */
/* Total size of a struct with the given field types, including tail   */
/* padding for alignment.                                              */

static vigil_status_t vigil_unsafe_struct_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char layout[256];
    arg_str_buf(vm, base, 0, layout, sizeof(layout));
    vigil_vm_stack_pop_n(vm, arg_count);

    int32_t offset = 0;
    int32_t max_align = 1;
    char *p = layout;
    while (*p)
    {
        char *comma = strchr(p, ',');
        if (comma)
            *comma = '\0';
        while (*p == ' ')
            p++;

        int32_t sz = type_sizeof(p);
        int32_t al = type_align(p);
        if (sz == 0)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.struct_size: unknown type in layout");
            return VIGIL_STATUS_INTERNAL;
        }
        if (al > max_align)
            max_align = al;
        offset = (offset + al - 1) & ~(al - 1);
        offset += sz;
        p = comma ? comma + 1 : p + strlen(p);
    }
    /* Tail padding: round up to max alignment. */
    offset = (offset + max_align - 1) & ~(max_align - 1);
    return push_i32(vm, offset, error);
}

/* ── unsafe.errno() -> i32 ───────────────────────────────────────── */

static vigil_status_t vigil_unsafe_errno(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, (int32_t)errno, error);
}

/* ── unsafe.set_errno(i32 val) ───────────────────────────────────── */

static vigil_status_t vigil_unsafe_set_errno(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t val = arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    errno = val;
    return VIGIL_STATUS_OK;
}

/* ── unsafe.cb_alloc() -> i64 ────────────────────────────────────── */

static vigil_status_t vigil_unsafe_cb_alloc(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    void *ptr = NULL;
    int slot = vigil_ffi_callback_alloc(&ptr);
    if (slot < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "unsafe.cb_alloc: all callback slots in use");
        return VIGIL_STATUS_INTERNAL;
    }
    return push_i64(vm, (int64_t)(intptr_t)ptr, error);
}

/* ── unsafe.cb_free(i32 slot) ────────────────────────────────────── */

static vigil_status_t vigil_unsafe_cb_free(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t slot = arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_ffi_callback_free(slot);
    return VIGIL_STATUS_OK;
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int p_i32[] = {VIGIL_TYPE_I32};
static const int p_i64[] = {VIGIL_TYPE_I64};
static const int p_i64_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_i64_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i32_i64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I64};
static const int p_i64_i32_f64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_F64};
static const int p_i64_i32_str[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_STRING};
static const int p_str[] = {VIGIL_TYPE_STRING};
static const int p_str_i32[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int p_copy[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const char *const unsafe_size_param_names[] = {"size"};
static const char *const unsafe_buf_param_names[] = {"buf"};
static const char *const unsafe_buf_size_param_names[] = {"buf", "size"};
static const char *const unsafe_buf_offset_param_names[] = {"buf", "offset"};
static const char *const unsafe_buf_offset_value_i32_param_names[] = {"buf", "offset", "value"};
static const char *const unsafe_buf_offset_value_i64_param_names[] = {"buf", "offset", "value"};
static const char *const unsafe_buf_offset_value_f64_param_names[] = {"buf", "offset", "value"};
static const char *const unsafe_buf_offset_value_str_param_names[] = {"buf", "offset", "value"};
static const char *const unsafe_ptr_offset_param_names[] = {"ptr", "offset"};
static const char *const unsafe_ptr_offset_value_i32_param_names[] = {"ptr", "offset", "value"};
static const char *const unsafe_ptr_offset_value_i64_param_names[] = {"ptr", "offset", "value"};
static const char *const unsafe_ptr_offset_value_f64_param_names[] = {"ptr", "offset", "value"};
static const char *const unsafe_type_param_names[] = {"type"};
static const char *const unsafe_type_field_param_names[] = {"type", "field"};
static const char *const unsafe_errno_param_names[] = {"value"};
static const char *const unsafe_copy_param_names[] = {"dst", "dst_off", "src", "src_off", "len"};
static const char *const unsafe_slot_param_names[] = {"slot"};

static const vigil_native_symbol_doc_t vigil_unsafe_module_doc = {
    "Low-level memory operations.",
    "Allocate, read, and write raw memory buffers. Use with care.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_unsafe_alloc_doc = {
    "Allocate a buffer.", "Returns a handle to a zero-initialized buffer.", "i64 buf = unsafe.alloc(256)"};
static const vigil_native_symbol_doc_t vigil_unsafe_realloc_doc = {
    "Resize a buffer.", "Returns the (possibly new) handle.", "buf = unsafe.realloc(buf, 512)"};
static const vigil_native_symbol_doc_t vigil_unsafe_free_doc = {"Free a buffer.", "Releases the buffer memory.",
                                                                "unsafe.free(buf)"};
static const vigil_native_symbol_doc_t vigil_unsafe_ptr_doc = {
    "Get raw pointer.", "Returns the underlying C pointer for FFI use.", "i64 p = unsafe.ptr(buf)"};
static const vigil_native_symbol_doc_t vigil_unsafe_len_doc = {
    "Get buffer length.", "Returns the allocated size in bytes.", "i32 n = unsafe.len(buf)"};
static const vigil_native_symbol_doc_t vigil_unsafe_get_doc = {
    "Read a byte.", "Returns the byte value at the given offset.", "i32 b = unsafe.get(buf, 0)"};
static const vigil_native_symbol_doc_t vigil_unsafe_set_doc = {"Write a byte.", "Sets the byte at the given offset.",
                                                               "unsafe.set(buf, 0, 0xFF)"};
static const vigil_native_symbol_doc_t vigil_unsafe_get_i32_doc = {
    "Read a 32-bit integer.", "Reads a native-endian i32 at the given byte offset.", "i32 v = unsafe.get_i32(buf, 0)"};
static const vigil_native_symbol_doc_t vigil_unsafe_set_i32_doc = {
    "Write a 32-bit integer.", "Writes a native-endian i32 at the given byte offset.", "unsafe.set_i32(buf, 0, 42)"};
static const vigil_native_symbol_doc_t vigil_unsafe_get_i64_doc = {
    "Read a 64-bit integer.", "Reads a native-endian i64 at the given byte offset.", "i64 v = unsafe.get_i64(buf, 0)"};
static const vigil_native_symbol_doc_t vigil_unsafe_set_i64_doc = {
    "Write a 64-bit integer.", "Writes a native-endian i64 at the given byte offset.", "unsafe.set_i64(buf, 0, 100)"};
static const vigil_native_symbol_doc_t vigil_unsafe_get_f32_doc = {
    "Read a 32-bit float.",
    "Reads a native-endian f32 at the given byte offset and returns it as f64.",
    "f64 v = unsafe.get_f32(buf, 0)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_set_f32_doc = {
    "Write a 32-bit float.",
    "Writes a native-endian f32 value at the given byte offset.",
    "unsafe.set_f32(buf, 0, 3.5)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_get_f64_doc = {
    "Read a 64-bit float.", "Reads a native-endian f64 at the given byte offset.", "f64 v = unsafe.get_f64(buf, 0)"};
static const vigil_native_symbol_doc_t vigil_unsafe_set_f64_doc = {
    "Write a 64-bit float.", "Writes a native-endian f64 at the given byte offset.", "unsafe.set_f64(buf, 0, 3.14)"};
static const vigil_native_symbol_doc_t vigil_unsafe_write_str_doc = {
    "Write a string into a buffer.",
    "Copies the string bytes into the buffer starting at the given byte offset.",
    "unsafe.write_str(buf, 0, \"hello\")",
};
static const vigil_native_symbol_doc_t vigil_unsafe_copy_doc = {"Copy bytes between buffers.",
                                                                "Copies len bytes from src+src_off to dst+dst_off.",
                                                                "unsafe.copy(dst, 0, src, 0, 64)"};
static const vigil_native_symbol_doc_t vigil_unsafe_peek_u8_doc = {
    "Read a byte from a raw pointer.", "Reads an unchecked u8 from ptr + offset.", "i32 b = unsafe.peek_u8(ptr, 0)"};
static const vigil_native_symbol_doc_t vigil_unsafe_peek_i32_doc = {
    "Read a 32-bit integer from a raw pointer.",
    "Reads an unchecked native-endian i32 from ptr + offset.",
    "i32 v = unsafe.peek_i32(ptr, 0)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_peek_i64_doc = {
    "Read a 64-bit integer from a raw pointer.",
    "Reads an unchecked native-endian i64 from ptr + offset.",
    "i64 v = unsafe.peek_i64(ptr, 0)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_peek_f32_doc = {
    "Read a 32-bit float from a raw pointer.",
    "Reads an unchecked native-endian f32 from ptr + offset and returns it as f64.",
    "f64 v = unsafe.peek_f32(ptr, 0)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_peek_f64_doc = {
    "Read a 64-bit float from a raw pointer.",
    "Reads an unchecked native-endian f64 from ptr + offset.",
    "f64 v = unsafe.peek_f64(ptr, 0)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_peek_ptr_doc = {
    "Read a pointer from a raw pointer.",
    "Reads an unchecked pointer-sized value from ptr + offset.",
    "i64 p = unsafe.peek_ptr(ptr, 0)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_poke_u8_doc = {
    "Write a byte to a raw pointer.", "Writes an unchecked u8 value to ptr + offset.", "unsafe.poke_u8(ptr, 0, 0xff)"};
static const vigil_native_symbol_doc_t vigil_unsafe_poke_i32_doc = {
    "Write a 32-bit integer to a raw pointer.",
    "Writes an unchecked native-endian i32 to ptr + offset.",
    "unsafe.poke_i32(ptr, 0, 42)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_poke_i64_doc = {
    "Write a 64-bit integer to a raw pointer.",
    "Writes an unchecked native-endian i64 to ptr + offset.",
    "unsafe.poke_i64(ptr, 0, 99)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_poke_f32_doc = {
    "Write a 32-bit float to a raw pointer.",
    "Writes an unchecked native-endian f32 to ptr + offset.",
    "unsafe.poke_f32(ptr, 0, 1.5)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_poke_f64_doc = {
    "Write a 64-bit float to a raw pointer.",
    "Writes an unchecked native-endian f64 to ptr + offset.",
    "unsafe.poke_f64(ptr, 0, 1.5)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_poke_ptr_doc = {
    "Write a pointer to a raw pointer.",
    "Writes an unchecked pointer-sized value to ptr + offset.",
    "unsafe.poke_ptr(ptr, 0, other_ptr)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_null_doc = {
    "Get null pointer.", "Returns 0 (null pointer constant).", "i64 p = unsafe.null()"};
static const vigil_native_symbol_doc_t vigil_unsafe_sizeof_doc = {
    "Get type size.", "Returns the size in bytes of a C type name.", "i32 n = unsafe.sizeof(\"int\")"};
static const vigil_native_symbol_doc_t vigil_unsafe_sizeof_ptr_doc = {
    "Get pointer size.",
    "Returns the size of a pointer on this platform (4 or 8).",
    "i32 n = unsafe.sizeof_ptr()",
};
static const vigil_native_symbol_doc_t vigil_unsafe_alignof_doc = {
    "Get type alignment.",
    "Returns the alignment requirement in bytes of a C type name.",
    "i32 n = unsafe.alignof(\"double\")",
};
static const vigil_native_symbol_doc_t vigil_unsafe_offsetof_doc = {
    "Get field offset.",
    "Returns the byte offset of a generated struct field index within the named C struct layout.",
    "i32 off = unsafe.offsetof(\"sockaddr_in\", 0)",
};
static const vigil_native_symbol_doc_t vigil_unsafe_struct_size_doc = {
    "Get struct size.",
    "Returns the size in bytes of a named C struct layout.",
    "i32 n = unsafe.struct_size(\"sockaddr_in\")",
};
static const vigil_native_symbol_doc_t vigil_unsafe_errno_doc = {"Get errno.", "Returns the current C errno value.",
                                                                 "i32 e = unsafe.errno()"};
static const vigil_native_symbol_doc_t vigil_unsafe_set_errno_doc = {"Set errno.", "Sets the C errno value.",
                                                                     "unsafe.set_errno(0)"};
static const vigil_native_symbol_doc_t vigil_unsafe_str_doc = {
    "Read C string.", "Reads a null-terminated string from a raw pointer.", "string s = unsafe.str(ptr)"};
static const vigil_native_symbol_doc_t vigil_unsafe_cb_alloc_doc = {
    "Allocate a callback slot.",
    "Returns an FFI callback slot handle for advanced unsafe callback plumbing.",
    "i64 slot = unsafe.cb_alloc()",
};
static const vigil_native_symbol_doc_t vigil_unsafe_cb_free_doc = {
    "Free a callback slot.",
    "Releases a callback slot previously allocated with unsafe.cb_alloc.",
    "unsafe.cb_free(0)",
};

#define F(n, nl, fn, pc, pt, rt)                                                                                       \
    {                                                                                                                  \
        n, nl, fn, pc, pt, rt, 1, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL                                      \
    }
#define FV(n, nl, fn, pc, pt)                                                                                          \
    {                                                                                                                  \
        n, nl, fn, pc, pt, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL                         \
    }

static const vigil_native_module_function_t vigil_unsafe_functions[] = {
    /* Buffer management */
    {"alloc", 5U, vigil_unsafe_alloc, 1U, p_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, unsafe_size_param_names,
     NULL, NULL, &vigil_unsafe_alloc_doc},
    {"realloc", 7U, vigil_unsafe_realloc, 2U, p_i64_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_size_param_names, NULL, NULL, &vigil_unsafe_realloc_doc},
    {"free", 4U, vigil_unsafe_free, 1U, p_i64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U, unsafe_buf_param_names,
     NULL, NULL, &vigil_unsafe_free_doc},
    {"ptr", 3U, vigil_unsafe_ptr, 1U, p_i64, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, unsafe_buf_param_names, NULL,
     NULL, &vigil_unsafe_ptr_doc},
    {"len", 3U, vigil_unsafe_len, 1U, p_i64, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, unsafe_buf_param_names, NULL,
     NULL, &vigil_unsafe_len_doc},
    /* Buffer byte access */
    {"get", 3U, vigil_unsafe_get, 2U, p_i64_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_param_names, NULL, NULL, &vigil_unsafe_get_doc},
    {"set", 3U, vigil_unsafe_set, 3U, p_i64_i32_i32, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_value_i32_param_names, NULL, NULL, &vigil_unsafe_set_doc},
    /* Buffer typed access */
    {"get_i32", 7U, vigil_unsafe_get_i32, 2U, p_i64_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_param_names, NULL, NULL, &vigil_unsafe_get_i32_doc},
    {"set_i32", 7U, vigil_unsafe_set_i32, 3U, p_i64_i32_i32, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_value_i32_param_names, NULL, NULL, &vigil_unsafe_set_i32_doc},
    {"get_i64", 7U, vigil_unsafe_get_i64, 2U, p_i64_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_param_names, NULL, NULL, &vigil_unsafe_get_i64_doc},
    {"set_i64", 7U, vigil_unsafe_set_i64, 3U, p_i64_i32_i64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_value_i64_param_names, NULL, NULL, &vigil_unsafe_set_i64_doc},
    {"get_f32", 7U, vigil_unsafe_get_f32, 2U, p_i64_i32, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_param_names, NULL, NULL, &vigil_unsafe_get_f32_doc},
    {"set_f32", 7U, vigil_unsafe_set_f32, 3U, p_i64_i32_f64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_value_f64_param_names, NULL, NULL, &vigil_unsafe_set_f32_doc},
    {"get_f64", 7U, vigil_unsafe_get_f64, 2U, p_i64_i32, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_param_names, NULL, NULL, &vigil_unsafe_get_f64_doc},
    {"set_f64", 7U, vigil_unsafe_set_f64, 3U, p_i64_i32_f64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_value_f64_param_names, NULL, NULL, &vigil_unsafe_set_f64_doc},
    {"write_str", 9U, vigil_unsafe_write_str, 3U, p_i64_i32_str, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_buf_offset_value_str_param_names, NULL, NULL, &vigil_unsafe_write_str_doc},
    {"copy", 4U, vigil_unsafe_copy, 5U, p_copy, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U, unsafe_copy_param_names,
     NULL, NULL, &vigil_unsafe_copy_doc},
    /* Raw pointer peek/poke (unchecked) */
    {"peek_u8", 7U, vigil_unsafe_peek_u8, 2U, p_i64_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_param_names, NULL, NULL, &vigil_unsafe_peek_u8_doc},
    {"peek_i32", 8U, vigil_unsafe_peek_i32, 2U, p_i64_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_param_names, NULL, NULL, &vigil_unsafe_peek_i32_doc},
    {"peek_i64", 8U, vigil_unsafe_peek_i64, 2U, p_i64_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_param_names, NULL, NULL, &vigil_unsafe_peek_i64_doc},
    {"peek_f32", 8U, vigil_unsafe_peek_f32, 2U, p_i64_i32, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_param_names, NULL, NULL, &vigil_unsafe_peek_f32_doc},
    {"peek_f64", 8U, vigil_unsafe_peek_f64, 2U, p_i64_i32, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_param_names, NULL, NULL, &vigil_unsafe_peek_f64_doc},
    {"peek_ptr", 8U, vigil_unsafe_peek_ptr, 2U, p_i64_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_param_names, NULL, NULL, &vigil_unsafe_peek_ptr_doc},
    {"poke_u8", 7U, vigil_unsafe_poke_u8, 3U, p_i64_i32_i32, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_value_i32_param_names, NULL, NULL, &vigil_unsafe_poke_u8_doc},
    {"poke_i32", 8U, vigil_unsafe_poke_i32, 3U, p_i64_i32_i32, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_value_i32_param_names, NULL, NULL, &vigil_unsafe_poke_i32_doc},
    {"poke_i64", 8U, vigil_unsafe_poke_i64, 3U, p_i64_i32_i64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_value_i64_param_names, NULL, NULL, &vigil_unsafe_poke_i64_doc},
    {"poke_f32", 8U, vigil_unsafe_poke_f32, 3U, p_i64_i32_f64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_value_f64_param_names, NULL, NULL, &vigil_unsafe_poke_f32_doc},
    {"poke_f64", 8U, vigil_unsafe_poke_f64, 3U, p_i64_i32_f64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_value_f64_param_names, NULL, NULL, &vigil_unsafe_poke_f64_doc},
    {"poke_ptr", 8U, vigil_unsafe_poke_ptr, 3U, p_i64_i32_i64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_value_i64_param_names, NULL, NULL, &vigil_unsafe_poke_ptr_doc},
    /* Utility */
    {"null", 4U, vigil_unsafe_null, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_unsafe_null_doc},
    {"sizeof_ptr", 10U, vigil_unsafe_sizeof_ptr, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, &vigil_unsafe_sizeof_ptr_doc},
    {"sizeof", 6U, vigil_unsafe_sizeof, 1U, p_str, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, unsafe_type_param_names,
     NULL, NULL, &vigil_unsafe_sizeof_doc},
    {"alignof", 7U, vigil_unsafe_alignof, 1U, p_str, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_type_param_names, NULL, NULL, &vigil_unsafe_alignof_doc},
    {"offsetof", 8U, vigil_unsafe_offsetof, 2U, p_str_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_type_field_param_names, NULL, NULL, &vigil_unsafe_offsetof_doc},
    {"struct_size", 11U, vigil_unsafe_struct_size, 1U, p_str, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_type_param_names, NULL, NULL, &vigil_unsafe_struct_size_doc},
    {"errno", 5U, vigil_unsafe_errno, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_unsafe_errno_doc},
    {"set_errno", 9U, vigil_unsafe_set_errno, 1U, p_i32, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_errno_param_names, NULL, NULL, &vigil_unsafe_set_errno_doc},
    {"str", 3U, vigil_unsafe_str, 1U, p_i64, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     unsafe_ptr_offset_param_names, NULL, NULL, &vigil_unsafe_str_doc},
    {"cb_alloc", 8U, vigil_unsafe_cb_alloc, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_unsafe_cb_alloc_doc},
    {"cb_free", 7U, vigil_unsafe_cb_free, 1U, p_i32, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U,
     unsafe_slot_param_names, NULL, NULL, &vigil_unsafe_cb_free_doc},
};

#undef F
#undef FV

VIGIL_API const vigil_native_module_t vigil_stdlib_unsafe = {
    "unsafe", 6U, vigil_unsafe_functions,   sizeof(vigil_unsafe_functions) / sizeof(vigil_unsafe_functions[0]),
    NULL,     0U, &vigil_unsafe_module_doc, NULL,
    0U};
