/* Tests for the unsafe module: memory buffers, peek/poke, struct layout,
 * sizeof, errno, and related functions. */
#include "vigil_test.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "internal/vigil_nanbox.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/stdlib.h"
#include "vigil/unsafe_buffer.h"
#include "vigil/vm.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static void unsafe_vm_setup(vigil_runtime_t **rt, vigil_vm_t **vm, vigil_error_t *error)
{
    vigil_runtime_open(rt, NULL, error);
    vigil_vm_open(vm, *rt, NULL, error);
}

static void unsafe_vm_teardown(vigil_runtime_t **rt, vigil_vm_t **vm)
{
    vigil_vm_close(vm);
    vigil_runtime_close(rt);
}

static void u_push_i64(vigil_vm_t *vm, int64_t v, vigil_error_t *e)
{
    vigil_value_t val = vigil_nanbox_encode_int(v);
    vigil_vm_stack_push(vm, &val, e);
}

static void u_push_i32(vigil_vm_t *vm, int32_t v, vigil_error_t *e)
{
    vigil_value_t val = vigil_nanbox_encode_i32(v);
    vigil_vm_stack_push(vm, &val, e);
}

static void u_push_f64(vigil_vm_t *vm, double v, vigil_error_t *e)
{
    vigil_value_t val = vigil_nanbox_encode_double(v);
    vigil_vm_stack_push(vm, &val, e);
}

static void u_push_str(vigil_vm_t *vm, vigil_runtime_t *rt, const char *s, vigil_error_t *e)
{
    vigil_object_t *obj = NULL;
    vigil_string_object_new_cstr(rt, s, &obj, e);
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    vigil_vm_stack_push(vm, &val, e);
    vigil_value_release(&val);
}

static int64_t u_pop_i64(vigil_vm_t *vm)
{
    int64_t v = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    return v;
}

static int32_t u_pop_i32(vigil_vm_t *vm)
{
    int32_t v = vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    return v;
}

static double u_pop_f64(vigil_vm_t *vm)
{
    double v = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    return v;
}

static vigil_native_fn_t find_unsafe_fn(const char *name)
{
    for (size_t i = 0; i < vigil_stdlib_unsafe.function_count; i++)
    {
        if (strcmp(vigil_stdlib_unsafe.functions[i].name, name) == 0)
            return vigil_stdlib_unsafe.functions[i].native_fn;
    }
    return NULL;
}

#define CALL_OK(fn, vm, n, e) ASSERT_EQ((int)(fn)((vm), (n), (e)), (int)VIGIL_STATUS_OK)

/* ── Buffer alloc/free/len ───────────────────────────────────────── */

TEST(Unsafe, AllocFreeLen)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");
    vigil_native_fn_t fn_len = find_unsafe_fn("len");

    u_push_i32(vm, 64, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);
    EXPECT_TRUE(buf >= 0);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_len, vm, 1, &e);
    EXPECT_EQ(64, u_pop_i32(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, AllocZeroFails)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");

    u_push_i32(vm, 0, &e);
    vigil_status_t s = fn_alloc(vm, 1, &e);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);
    unsafe_vm_teardown(&rt, &vm);
}

/* ── Realloc ─────────────────────────────────────────────────────── */

TEST(Unsafe, Realloc)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_realloc = find_unsafe_fn("realloc");
    vigil_native_fn_t fn_len = find_unsafe_fn("len");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 128, &e);
    CALL_OK(fn_realloc, vm, 2, &e);
    int64_t buf2 = u_pop_i64(vm);
    EXPECT_EQ((int)buf, (int)buf2); /* same slot */

    u_push_i64(vm, buf2, &e);
    CALL_OK(fn_len, vm, 1, &e);
    EXPECT_EQ(128, u_pop_i32(vm));

    u_push_i64(vm, buf2, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

/* ── Byte get/set ────────────────────────────────────────────────── */

TEST(Unsafe, GetSetByte)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_set = find_unsafe_fn("set");
    vigil_native_fn_t fn_get = find_unsafe_fn("get");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 8, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 3, &e);
    u_push_i32(vm, 0xAB, &e);
    CALL_OK(fn_set, vm, 3, &e);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 3, &e);
    CALL_OK(fn_get, vm, 2, &e);
    EXPECT_EQ(0xAB, u_pop_i32(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, GetOutOfBounds)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_get = find_unsafe_fn("get");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 4, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 10, &e);
    vigil_status_t s = fn_get(vm, 2, &e);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

/* ── Typed get/set: i32, i64, f32, f64 ───────────────────────────── */

TEST(Unsafe, GetSetI32)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_set_i32 = find_unsafe_fn("set_i32");
    vigil_native_fn_t fn_get_i32 = find_unsafe_fn("get_i32");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 4, &e);
    u_push_i32(vm, 12345, &e);
    CALL_OK(fn_set_i32, vm, 3, &e);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 4, &e);
    CALL_OK(fn_get_i32, vm, 2, &e);
    EXPECT_EQ(12345, u_pop_i32(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, GetSetI64)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_set_i64 = find_unsafe_fn("set_i64");
    vigil_native_fn_t fn_get_i64 = find_unsafe_fn("get_i64");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    int64_t big = 0x123456789ABCLL;
    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 0, &e);
    u_push_i64(vm, big, &e);
    CALL_OK(fn_set_i64, vm, 3, &e);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_get_i64, vm, 2, &e);
    EXPECT_EQ(big, u_pop_i64(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, GetSetF32)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_set_f32 = find_unsafe_fn("set_f32");
    vigil_native_fn_t fn_get_f32 = find_unsafe_fn("get_f32");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 0, &e);
    u_push_f64(vm, 3.14, &e);
    CALL_OK(fn_set_f32, vm, 3, &e);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_get_f32, vm, 2, &e);
    EXPECT_NEAR(3.14, u_pop_f64(vm), 0.01);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, GetSetF64)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_set_f64 = find_unsafe_fn("set_f64");
    vigil_native_fn_t fn_get_f64 = find_unsafe_fn("get_f64");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 0, &e);
    u_push_f64(vm, 2.718281828, &e);
    CALL_OK(fn_set_f64, vm, 3, &e);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_get_f64, vm, 2, &e);
    EXPECT_NEAR(2.718281828, u_pop_f64(vm), 0.0001);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

/* ── Ptr, Null, Copy, WriteStr ───────────────────────────────────── */

TEST(Unsafe, PtrAndNull)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_ptr = find_unsafe_fn("ptr");
    vigil_native_fn_t fn_null = find_unsafe_fn("null");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    /* null returns 0 */
    CALL_OK(fn_null, vm, 0, &e);
    EXPECT_EQ(0, (int)u_pop_i64(vm));

    /* ptr returns non-zero for valid buffer */
    u_push_i32(vm, 8, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_ptr, vm, 1, &e);
    int64_t raw = u_pop_i64(vm);
    EXPECT_TRUE(raw != 0);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, Copy)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_set = find_unsafe_fn("set");
    vigil_native_fn_t fn_get = find_unsafe_fn("get");
    vigil_native_fn_t fn_copy = find_unsafe_fn("copy");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    /* Alloc src and dst */
    u_push_i32(vm, 8, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t src = u_pop_i64(vm);

    u_push_i32(vm, 8, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t dst = u_pop_i64(vm);

    /* Write 0xAA to src[0..3] */
    for (int i = 0; i < 4; i++)
    {
        u_push_i64(vm, src, &e);
        u_push_i32(vm, i, &e);
        u_push_i32(vm, 0xAA, &e);
        CALL_OK(fn_set, vm, 3, &e);
    }

    /* copy(dst, 0, src, 0, 4) */
    u_push_i64(vm, dst, &e);
    u_push_i32(vm, 0, &e);
    u_push_i64(vm, src, &e);
    u_push_i32(vm, 0, &e);
    u_push_i32(vm, 4, &e);
    CALL_OK(fn_copy, vm, 5, &e);

    /* Verify dst[2] == 0xAA */
    u_push_i64(vm, dst, &e);
    u_push_i32(vm, 2, &e);
    CALL_OK(fn_get, vm, 2, &e);
    EXPECT_EQ(0xAA, u_pop_i32(vm));

    u_push_i64(vm, src, &e);
    CALL_OK(fn_free, vm, 1, &e);
    u_push_i64(vm, dst, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, WriteStr)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_write_str = find_unsafe_fn("write_str");
    vigil_native_fn_t fn_get = find_unsafe_fn("get");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 32, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 0, &e);
    u_push_str(vm, rt, "Hi", &e);
    CALL_OK(fn_write_str, vm, 3, &e);

    /* Check 'H' at offset 0 */
    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_get, vm, 2, &e);
    EXPECT_EQ('H', u_pop_i32(vm));

    /* Check 'i' at offset 1 */
    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 1, &e);
    CALL_OK(fn_get, vm, 2, &e);
    EXPECT_EQ('i', u_pop_i32(vm));

    /* Check NUL at offset 2 */
    u_push_i64(vm, buf, &e);
    u_push_i32(vm, 2, &e);
    CALL_OK(fn_get, vm, 2, &e);
    EXPECT_EQ(0, u_pop_i32(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

/* ── Str (read C string from raw pointer) ────────────────────────── */

TEST(Unsafe, StrFromPointer)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_str = find_unsafe_fn("str");

    const char *cstr = "test string";
    u_push_i64(vm, (int64_t)(intptr_t)cstr, &e);
    CALL_OK(fn_str, vm, 1, &e);

    vigil_value_t v = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1);
    const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
    EXPECT_STREQ("test string", vigil_string_object_c_str(obj));
    vigil_vm_stack_pop_n(vm, 1);

    unsafe_vm_teardown(&rt, &vm);
}

/* ── Peek/Poke (raw pointer operations) ──────────────────────────── */

TEST(Unsafe, PeekPokeU8)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_ptr = find_unsafe_fn("ptr");
    vigil_native_fn_t fn_poke_u8 = find_unsafe_fn("poke_u8");
    vigil_native_fn_t fn_peek_u8 = find_unsafe_fn("peek_u8");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 8, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_ptr, vm, 1, &e);
    int64_t raw = u_pop_i64(vm);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 2, &e);
    u_push_i32(vm, 0xFE, &e);
    CALL_OK(fn_poke_u8, vm, 3, &e);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 2, &e);
    CALL_OK(fn_peek_u8, vm, 2, &e);
    EXPECT_EQ(0xFE, u_pop_i32(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, PeekPokeI32)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_ptr = find_unsafe_fn("ptr");
    vigil_native_fn_t fn_poke_i32 = find_unsafe_fn("poke_i32");
    vigil_native_fn_t fn_peek_i32 = find_unsafe_fn("peek_i32");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);
    u_push_i64(vm, buf, &e);
    CALL_OK(fn_ptr, vm, 1, &e);
    int64_t raw = u_pop_i64(vm);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 4, &e);
    u_push_i32(vm, -99999, &e);
    CALL_OK(fn_poke_i32, vm, 3, &e);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 4, &e);
    CALL_OK(fn_peek_i32, vm, 2, &e);
    EXPECT_EQ(-99999, u_pop_i32(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, PeekPokeI64)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_ptr = find_unsafe_fn("ptr");
    vigil_native_fn_t fn_poke_i64 = find_unsafe_fn("poke_i64");
    vigil_native_fn_t fn_peek_i64 = find_unsafe_fn("peek_i64");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);
    u_push_i64(vm, buf, &e);
    CALL_OK(fn_ptr, vm, 1, &e);
    int64_t raw = u_pop_i64(vm);

    int64_t big = 0x00001234ABCD5678LL; /* fits in 48-bit, bit 47 clear */
    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 0, &e);
    u_push_i64(vm, big, &e);
    CALL_OK(fn_poke_i64, vm, 3, &e);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_peek_i64, vm, 2, &e);
    EXPECT_EQ(big, u_pop_i64(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, PeekPokeF32)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_ptr = find_unsafe_fn("ptr");
    vigil_native_fn_t fn_poke_f32 = find_unsafe_fn("poke_f32");
    vigil_native_fn_t fn_peek_f32 = find_unsafe_fn("peek_f32");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);
    u_push_i64(vm, buf, &e);
    CALL_OK(fn_ptr, vm, 1, &e);
    int64_t raw = u_pop_i64(vm);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 0, &e);
    u_push_f64(vm, 1.5, &e);
    CALL_OK(fn_poke_f32, vm, 3, &e);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_peek_f32, vm, 2, &e);
    EXPECT_NEAR(1.5, u_pop_f64(vm), 0.001);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, PeekPokeF64)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_ptr = find_unsafe_fn("ptr");
    vigil_native_fn_t fn_poke_f64 = find_unsafe_fn("poke_f64");
    vigil_native_fn_t fn_peek_f64 = find_unsafe_fn("peek_f64");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 16, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);
    u_push_i64(vm, buf, &e);
    CALL_OK(fn_ptr, vm, 1, &e);
    int64_t raw = u_pop_i64(vm);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 0, &e);
    u_push_f64(vm, 9.87654321, &e);
    CALL_OK(fn_poke_f64, vm, 3, &e);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_peek_f64, vm, 2, &e);
    EXPECT_NEAR(9.87654321, u_pop_f64(vm), 0.0001);

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, PeekPokePtr)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alloc = find_unsafe_fn("alloc");
    vigil_native_fn_t fn_ptr = find_unsafe_fn("ptr");
    vigil_native_fn_t fn_poke_ptr = find_unsafe_fn("poke_ptr");
    vigil_native_fn_t fn_peek_ptr = find_unsafe_fn("peek_ptr");
    vigil_native_fn_t fn_free = find_unsafe_fn("free");

    u_push_i32(vm, 32, &e);
    CALL_OK(fn_alloc, vm, 1, &e);
    int64_t buf = u_pop_i64(vm);
    u_push_i64(vm, buf, &e);
    CALL_OK(fn_ptr, vm, 1, &e);
    int64_t raw = u_pop_i64(vm);

    /* Store a known pointer value and read it back */
    int64_t fake_ptr = (int64_t)(intptr_t) "hello";
    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 0, &e);
    u_push_i64(vm, fake_ptr, &e);
    CALL_OK(fn_poke_ptr, vm, 3, &e);

    u_push_i64(vm, raw, &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_peek_ptr, vm, 2, &e);
    EXPECT_EQ(fake_ptr, u_pop_i64(vm));

    u_push_i64(vm, buf, &e);
    CALL_OK(fn_free, vm, 1, &e);
    unsafe_vm_teardown(&rt, &vm);
}

/* ── Sizeof, Alignof, Offsetof, StructSize ───────────────────────── */

TEST(Unsafe, Sizeof)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_sizeof = find_unsafe_fn("sizeof");

    u_push_str(vm, rt, "u8", &e);
    CALL_OK(fn_sizeof, vm, 1, &e);
    EXPECT_EQ(1, u_pop_i32(vm));

    u_push_str(vm, rt, "i32", &e);
    CALL_OK(fn_sizeof, vm, 1, &e);
    EXPECT_EQ(4, u_pop_i32(vm));

    u_push_str(vm, rt, "i64", &e);
    CALL_OK(fn_sizeof, vm, 1, &e);
    EXPECT_EQ(8, u_pop_i32(vm));

    u_push_str(vm, rt, "f32", &e);
    CALL_OK(fn_sizeof, vm, 1, &e);
    EXPECT_EQ(4, u_pop_i32(vm));

    u_push_str(vm, rt, "f64", &e);
    CALL_OK(fn_sizeof, vm, 1, &e);
    EXPECT_EQ(8, u_pop_i32(vm));

    u_push_str(vm, rt, "ptr", &e);
    CALL_OK(fn_sizeof, vm, 1, &e);
    EXPECT_EQ((int)sizeof(void *), u_pop_i32(vm));

    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, SizeofUnknownFails)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_sizeof = find_unsafe_fn("sizeof");

    u_push_str(vm, rt, "bogus", &e);
    vigil_status_t s = fn_sizeof(vm, 1, &e);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);

    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, SizeofPtr)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn = find_unsafe_fn("sizeof_ptr");

    CALL_OK(fn, vm, 0, &e);
    EXPECT_EQ((int)sizeof(void *), u_pop_i32(vm));

    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, Alignof)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_alignof = find_unsafe_fn("alignof");

    u_push_str(vm, rt, "i32", &e);
    CALL_OK(fn_alignof, vm, 1, &e);
    EXPECT_EQ(4, u_pop_i32(vm));

    u_push_str(vm, rt, "f64", &e);
    CALL_OK(fn_alignof, vm, 1, &e);
    EXPECT_EQ(8, u_pop_i32(vm));

    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, Offsetof)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_offsetof = find_unsafe_fn("offsetof");

    /* struct { i32, f64 } — f64 at offset 8 due to alignment */
    u_push_str(vm, rt, "i32,f64", &e);
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_offsetof, vm, 2, &e);
    EXPECT_EQ(0, u_pop_i32(vm));

    u_push_str(vm, rt, "i32,f64", &e);
    u_push_i32(vm, 1, &e);
    CALL_OK(fn_offsetof, vm, 2, &e);
    EXPECT_EQ(8, u_pop_i32(vm));

    /* struct { u8, i32 } — i32 at offset 4 */
    u_push_str(vm, rt, "u8,i32", &e);
    u_push_i32(vm, 1, &e);
    CALL_OK(fn_offsetof, vm, 2, &e);
    EXPECT_EQ(4, u_pop_i32(vm));

    /* struct { f32, f32, ptr } — ptr at offset 8 */
    u_push_str(vm, rt, "f32,f32,ptr", &e);
    u_push_i32(vm, 2, &e);
    CALL_OK(fn_offsetof, vm, 2, &e);
    EXPECT_EQ(8, u_pop_i32(vm));

    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, OffsetofOutOfRange)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_offsetof = find_unsafe_fn("offsetof");

    u_push_str(vm, rt, "i32,i32", &e);
    u_push_i32(vm, 5, &e);
    vigil_status_t s = fn_offsetof(vm, 2, &e);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);

    unsafe_vm_teardown(&rt, &vm);
}

TEST(Unsafe, StructSize)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_struct_size = find_unsafe_fn("struct_size");

    /* struct { f32, f32 } = 8 bytes, no tail padding needed */
    u_push_str(vm, rt, "f32,f32", &e);
    CALL_OK(fn_struct_size, vm, 1, &e);
    EXPECT_EQ(8, u_pop_i32(vm));

    /* struct { i32, f64 } = 16 bytes (4 + 4pad + 8, tail pad to 8) */
    u_push_str(vm, rt, "i32,f64", &e);
    CALL_OK(fn_struct_size, vm, 1, &e);
    EXPECT_EQ(16, u_pop_i32(vm));

    /* struct { u8 } = 1 byte */
    u_push_str(vm, rt, "u8", &e);
    CALL_OK(fn_struct_size, vm, 1, &e);
    EXPECT_EQ(1, u_pop_i32(vm));

    /* struct { u8, i64 } = 16 (1 + 7pad + 8, tail pad to 8) */
    u_push_str(vm, rt, "u8,i64", &e);
    CALL_OK(fn_struct_size, vm, 1, &e);
    EXPECT_EQ(16, u_pop_i32(vm));

    unsafe_vm_teardown(&rt, &vm);
}

/* ── Errno ───────────────────────────────────────────────────────── */

TEST(Unsafe, Errno)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t e = {0};
    unsafe_vm_setup(&rt, &vm, &e);
    vigil_native_fn_t fn_errno = find_unsafe_fn("errno");
    vigil_native_fn_t fn_set_errno = find_unsafe_fn("set_errno");

    /* Set errno to 0, read it back */
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_set_errno, vm, 1, &e);

    CALL_OK(fn_errno, vm, 0, &e);
    EXPECT_EQ(0, u_pop_i32(vm));

    /* Set errno to ENOENT (2), read it back */
    u_push_i32(vm, ENOENT, &e);
    CALL_OK(fn_set_errno, vm, 1, &e);

    CALL_OK(fn_errno, vm, 0, &e);
    EXPECT_EQ(ENOENT, u_pop_i32(vm));

    /* Clean up */
    u_push_i32(vm, 0, &e);
    CALL_OK(fn_set_errno, vm, 1, &e);

    unsafe_vm_teardown(&rt, &vm);
}

/* ── Public buffer API tests ─────────────────────────────────────── */

TEST(Unsafe, BufferPublicAPI)
{
    /* Alloc via public API */
    int64_t slot = vigil_unsafe_buffer_alloc(64);
    EXPECT_TRUE(slot >= 0);

    /* Get pointer and size */
    int32_t size = 0;
    void *ptr = vigil_unsafe_buffer_get(slot, &size);
    EXPECT_TRUE(ptr != NULL);
    EXPECT_EQ(64, size);

    /* Write through pointer, read back */
    ((uint8_t *)ptr)[0] = 0xAB;
    ((uint8_t *)ptr)[63] = 0xCD;

    void *ptr2 = vigil_unsafe_buffer_get(slot, NULL);
    EXPECT_EQ(0xAB, ((uint8_t *)ptr2)[0]);
    EXPECT_EQ(0xCD, ((uint8_t *)ptr2)[63]);

    /* Invalid slot returns NULL */
    EXPECT_TRUE(vigil_unsafe_buffer_get(-1, NULL) == NULL);
    EXPECT_TRUE(vigil_unsafe_buffer_get(9999, NULL) == NULL);

    /* Free */
    vigil_unsafe_buffer_free(slot);
    EXPECT_TRUE(vigil_unsafe_buffer_get(slot, NULL) == NULL);
}

TEST(Unsafe, BufferRegister)
{
    /* Register externally-allocated buffer */
    void *data = calloc(32, 1);
    EXPECT_TRUE(data != NULL);
    ((uint8_t *)data)[0] = 0x42;

    int64_t slot = vigil_unsafe_buffer_register(data, 32);
    EXPECT_TRUE(slot >= 0);

    int32_t size = 0;
    void *ptr = vigil_unsafe_buffer_get(slot, &size);
    EXPECT_EQ(data, ptr);
    EXPECT_EQ(32, size);
    EXPECT_EQ(0x42, ((uint8_t *)ptr)[0]);

    /* Free releases the registered buffer */
    vigil_unsafe_buffer_free(slot);
    EXPECT_TRUE(vigil_unsafe_buffer_get(slot, NULL) == NULL);

    /* NULL/zero rejects */
    EXPECT_EQ(-1, vigil_unsafe_buffer_register(NULL, 10));
    EXPECT_EQ(-1, vigil_unsafe_buffer_register((void *)1, 0));
    EXPECT_EQ(-1, vigil_unsafe_buffer_alloc(0));
    EXPECT_EQ(-1, vigil_unsafe_buffer_alloc(-1));
}

/* ── Registration ────────────────────────────────────────────────── */

void register_unsafe_tests(void)
{
    REGISTER_TEST(Unsafe, AllocFreeLen);
    REGISTER_TEST(Unsafe, AllocZeroFails);
    REGISTER_TEST(Unsafe, Realloc);
    REGISTER_TEST(Unsafe, GetSetByte);
    REGISTER_TEST(Unsafe, GetOutOfBounds);
    REGISTER_TEST(Unsafe, GetSetI32);
    REGISTER_TEST(Unsafe, GetSetI64);
    REGISTER_TEST(Unsafe, GetSetF32);
    REGISTER_TEST(Unsafe, GetSetF64);
    REGISTER_TEST(Unsafe, PtrAndNull);
    REGISTER_TEST(Unsafe, Copy);
    REGISTER_TEST(Unsafe, WriteStr);
    REGISTER_TEST(Unsafe, StrFromPointer);
    REGISTER_TEST(Unsafe, PeekPokeU8);
    REGISTER_TEST(Unsafe, PeekPokeI32);
    REGISTER_TEST(Unsafe, PeekPokeI64);
    REGISTER_TEST(Unsafe, PeekPokeF32);
    REGISTER_TEST(Unsafe, PeekPokeF64);
    REGISTER_TEST(Unsafe, PeekPokePtr);
    REGISTER_TEST(Unsafe, Sizeof);
    REGISTER_TEST(Unsafe, SizeofUnknownFails);
    REGISTER_TEST(Unsafe, SizeofPtr);
    REGISTER_TEST(Unsafe, Alignof);
    REGISTER_TEST(Unsafe, Offsetof);
    REGISTER_TEST(Unsafe, OffsetofOutOfRange);
    REGISTER_TEST(Unsafe, StructSize);
    REGISTER_TEST(Unsafe, Errno);
    REGISTER_TEST(Unsafe, BufferPublicAPI);
    REGISTER_TEST(Unsafe, BufferRegister);
}
