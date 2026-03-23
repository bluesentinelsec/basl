/* VIGIL standard library: ffi module.
 *
 * Provides dynamic loading of C shared libraries and calling of
 * foreign functions via libffi.  All library handles and function
 * pointers are represented as i64 values in VIGIL.
 *
 * API:
 *   ffi.open(string path) -> i64
 *   ffi.sym(i64 lib, string name) -> i64
 *   ffi.bind(i64 lib, string name, string sig) -> i64
 *   ffi.call(i64 h, i64 a0..a5) -> i64
 *   ffi.call_f(i64 h, f64 a0, f64 a1) -> f64
 *   ffi.call_s(i64 h, i64 a0, i64 a1) -> string
 *   ffi.callback(fn, string sig) -> i64
 *   ffi.callback_free(i64 callback)
 *   ffi.close(i64 lib)
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/ffi_callback.h"
#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "platform/platform.h"

#ifdef VIGIL_HAS_LIBFFI
#include <ffi.h>
#endif

/* ── helpers ─────────────────────────────────────────────────────── */

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_int(v);
    return vigil_vm_stack_push(vm, &val, error);
}

#ifdef VIGIL_HAS_LIBFFI
static vigil_status_t push_f64(vigil_vm_t *vm, double v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_double(v);
    return vigil_vm_stack_push(vm, &val, error);
}
#endif

static int64_t pop_i64(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    return vigil_nanbox_decode_int(v);
}

static double pop_f64(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    return vigil_nanbox_decode_double(v);
}

/*
 * Extract a C string from a VIGIL string on the stack into a buffer.
 * Must copy before stack_pop_n, which releases the string object.
 */
static const char *pop_str_buf(vigil_vm_t *vm, size_t base, size_t idx, char *buf, size_t bufsz)
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

#ifdef VIGIL_HAS_LIBFFI
static vigil_status_t push_string(vigil_vm_t *vm, const char *s, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
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
#endif

/* ── Bind table ──────────────────────────────────────────────────── */

typedef struct
{
    void *fn;
    char sig[64]; /* e.g. "i32(i32,i32)", "f64(f64)", "void()" */
} ffi_bound_t;

#define FFI_MAX_BOUND 256
static ffi_bound_t g_bound[FFI_MAX_BOUND];
static int g_bound_count;

/* ── ffi.open / ffi.sym / ffi.close ──────────────────────────────── */

static vigil_status_t vigil_ffi_open(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    pop_str_buf(vm, base, 0, path, sizeof(path));
    void *handle = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t s = vigil_platform_dlopen(path, &handle, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return push_i64(vm, (int64_t)(intptr_t)handle, error);
}

static vigil_status_t vigil_ffi_sym(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    char name[256];
    pop_str_buf(vm, base, 1, name, sizeof(name));
    void *sym = NULL;
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t s = vigil_platform_dlsym(handle, name, &sym, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return push_i64(vm, (int64_t)(intptr_t)sym, error);
}

static vigil_status_t vigil_ffi_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return vigil_platform_dlclose(handle, error);
}

/* ── ffi.bind(i64 lib, string name, string sig) -> i64 ───────────── */

static vigil_status_t vigil_ffi_bind(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    char name[256], sig[64];
    pop_str_buf(vm, base, 1, name, sizeof(name));
    pop_str_buf(vm, base, 2, sig, sizeof(sig));
    vigil_vm_stack_pop_n(vm, arg_count);

    if (g_bound_count >= FFI_MAX_BOUND)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.bind: too many bound functions");
        return VIGIL_STATUS_INTERNAL;
    }

    void *sym = NULL;
    vigil_status_t s = vigil_platform_dlsym(handle, name, &sym, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    int idx = g_bound_count++;
    g_bound[idx].fn = sym;
    size_t len = strlen(sig);
    if (len >= sizeof(g_bound[idx].sig))
        len = sizeof(g_bound[idx].sig) - 1;
    memcpy(g_bound[idx].sig, sig, len);
    g_bound[idx].sig[len] = '\0';

    return push_i64(vm, (int64_t)idx, error);
}

/* ── libffi-based generic call ───────────────────────────────────── */

#ifdef VIGIL_HAS_LIBFFI

/*
 * Pedantic-safe replacement for libffi's FFI_FN() macro, which does
 * a direct void* -> function-pointer cast that GCC -Wpedantic rejects.
 */
static void (*fn_to_fnptr(void *p))(void)
{
    union {
        void *obj;
        void (*fn)(void);
    } u;
    u.obj = p;
    return u.fn;
}

static ffi_type *sig_to_ffi_type(const char *t, size_t len)
{
    if (len == 3 && memcmp(t, "i32", 3) == 0)
        return &ffi_type_sint32;
    if (len == 3 && memcmp(t, "i64", 3) == 0)
        return &ffi_type_sint64;
    if (len == 3 && memcmp(t, "u32", 3) == 0)
        return &ffi_type_uint32;
    if (len == 2 && memcmp(t, "u8", 2) == 0)
        return &ffi_type_uint8;
    if (len == 3 && memcmp(t, "f64", 3) == 0)
        return &ffi_type_double;
    if (len == 3 && memcmp(t, "f32", 3) == 0)
        return &ffi_type_float;
    if (len == 3 && memcmp(t, "ptr", 3) == 0)
        return &ffi_type_pointer;
    if (len == 4 && memcmp(t, "void", 4) == 0)
        return &ffi_type_void;
    return &ffi_type_pointer; /* fallback: covers string and unknown types */
}

/* Parse "[stdcall:]ret(p1,p2,...)" and call fn via ffi_call.
 * Returns raw i64 bits.  Prefix "stdcall:" selects FFI_STDCALL on
 * platforms that support it (32-bit x86 Windows); ignored elsewhere. */
typedef struct
{
    ffi_type **atypes;
    int64_t *args_i;
    double *args_d;
    void **args_p;
    void **avalues;
    ffi_type *atypes_s[16];
    int64_t args_i_s[16];
    double args_d_s[16];
    void *args_p_s[16];
    void *avalues_s[16];
} ffi_call_bufs_t;

static int ffi_call_bufs_init(ffi_call_bufs_t *b, int nargs)
{
    b->atypes = nargs <= 16 ? b->atypes_s : malloc((size_t)nargs * sizeof(*b->atypes));
    b->args_i = nargs <= 16 ? b->args_i_s : malloc((size_t)nargs * sizeof(*b->args_i));
    b->args_d = nargs <= 16 ? b->args_d_s : malloc((size_t)nargs * sizeof(*b->args_d));
    b->args_p = nargs <= 16 ? b->args_p_s : malloc((size_t)nargs * sizeof(*b->args_p));
    b->avalues = nargs <= 16 ? b->avalues_s : malloc((size_t)nargs * sizeof(*b->avalues));
    return b->atypes && b->args_i && b->args_d && b->args_p && b->avalues;
}

static void ffi_call_bufs_free(ffi_call_bufs_t *b)
{
    if (b->atypes != b->atypes_s)
        free(b->atypes);
    if (b->args_i != b->args_i_s)
        free(b->args_i);
    if (b->args_d != b->args_d_s)
        free(b->args_d);
    if (b->args_p != b->args_p_s)
        free(b->args_p);
    if (b->avalues != b->avalues_s)
        free(b->avalues);
}

static int64_t ffi_call_generic(void *fn, const char *sig, const int64_t *args, int nargs_avail)
{
    ffi_abi abi = FFI_DEFAULT_ABI;
    const char *s = sig;
#ifdef FFI_STDCALL
    if (strncmp(s, "stdcall:", 8) == 0)
    {
        abi = FFI_STDCALL;
        s += 8;
    }
#else
    if (strncmp(s, "stdcall:", 8) == 0)
        s += 8; /* skip prefix, use default ABI */
#endif

    const char *paren = strchr(s, '(');
    if (!paren)
        return 0;
    size_t ret_len = (size_t)(paren - s);
    ffi_type *rtype = sig_to_ffi_type(s, ret_len);

    const char *p = paren + 1;
    const char *end = strchr(p, ')');
    if (!end)
        end = p + strlen(p);

    int nargs = 0;
    {
        const char *q = p;
        while (q < end)
        {
            const char *c = q;
            while (c < end && *c != ',')
                c++;
            if ((size_t)(c - q) > 0)
                nargs++;
            q = c + 1;
        }
    }
    if (nargs > nargs_avail)
        nargs = nargs_avail;

    ffi_call_bufs_t b;
    if (!ffi_call_bufs_init(&b, nargs))
    {
        ffi_call_bufs_free(&b);
        return 0;
    }

    int idx = 0;
    p = paren + 1;
    while (p < end && idx < nargs)
    {
        const char *comma = p;
        while (comma < end && *comma != ',')
            comma++;
        size_t tlen = (size_t)(comma - p);
        if (tlen > 0)
            b.atypes[idx++] = sig_to_ffi_type(p, tlen);
        p = comma + 1;
    }

    ffi_cif cif;
    int64_t result = 0;
    if (ffi_prep_cif(&cif, abi, (unsigned)nargs, rtype, nargs ? b.atypes : NULL) != FFI_OK)
        goto done;

    for (int i = 0; i < nargs; i++)
    {
        b.args_i[i] = args[i];
        if (b.atypes[i] == &ffi_type_double)
        {
            memcpy(&b.args_d[i], &b.args_i[i], sizeof(double));
            b.avalues[i] = &b.args_d[i];
        }
        else if (b.atypes[i] == &ffi_type_pointer)
        {
            b.args_p[i] = (void *)(intptr_t)b.args_i[i];
            b.avalues[i] = &b.args_p[i];
        }
        else
        {
            b.avalues[i] = &b.args_i[i];
        }
    }

    if (rtype == &ffi_type_void)
    {
        ffi_call(&cif, fn_to_fnptr(fn), NULL, b.avalues);
    }
    else if (rtype == &ffi_type_double)
    {
        double rv;
        ffi_call(&cif, fn_to_fnptr(fn), &rv, b.avalues);
        memcpy(&result, &rv, sizeof(result));
    }
    else if (rtype == &ffi_type_pointer)
    {
        void *rv;
        ffi_call(&cif, fn_to_fnptr(fn), &rv, b.avalues);
        result = (int64_t)(intptr_t)rv;
    }
    else
    {
        ffi_call(&cif, fn_to_fnptr(fn), &result, b.avalues);
    }

done:
    ffi_call_bufs_free(&b);
    return result;
}

#endif /* VIGIL_HAS_LIBFFI */

/* ── ffi.call(i64 h, i64 a0..a5) -> i64 ─────────────────────────── */

static vigil_status_t vigil_ffi_call(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int idx = (int)pop_i64(vm, base, 0);
    /* Remaining args (after the handle) are the C function args. */
    int nargs = (int)arg_count - 1;
    int64_t args_s[16];
    int64_t *args = nargs <= 16 ? args_s : malloc((size_t)nargs * sizeof(*args));
    if (!args)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call: alloc failed");
        return VIGIL_STATUS_INTERNAL;
    }
    for (int i = 0; i < nargs; i++)
        args[i] = pop_i64(vm, base, (size_t)(i + 1));
    vigil_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count)
    {
        if (args != args_s)
            free(args);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call: invalid handle");
        return VIGIL_STATUS_INTERNAL;
    }

#ifdef VIGIL_HAS_LIBFFI
    {
        int64_t rv = ffi_call_generic(g_bound[idx].fn, g_bound[idx].sig, args, nargs);
        if (args != args_s)
            free(args);
        return push_i64(vm, rv, error);
    }
#else
    if (args != args_s)
        free(args);
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call: not supported on this platform");
    return VIGIL_STATUS_INTERNAL;
#endif
}

/* ── ffi.call_f(i64 h, f64 a0, f64 a1) -> f64 ───────────────────── */

static vigil_status_t vigil_ffi_call_f(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int idx = (int)pop_i64(vm, base, 0);
    int nargs = (int)arg_count - 1;
    int64_t args_s[16];
    int64_t *args = nargs <= 16 ? args_s : malloc((size_t)nargs * sizeof(*args));
    if (!args)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call_f: alloc failed");
        return VIGIL_STATUS_INTERNAL;
    }
    for (int i = 0; i < nargs; i++)
    {
        double d = pop_f64(vm, base, (size_t)(i + 1));
        memcpy(&args[i], &d, sizeof(d));
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count)
    {
        if (args != args_s)
            free(args);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call_f: invalid handle");
        return VIGIL_STATUS_INTERNAL;
    }

#ifdef VIGIL_HAS_LIBFFI
    {
        int64_t rbits = ffi_call_generic(g_bound[idx].fn, g_bound[idx].sig, args, nargs);
        if (args != args_s)
            free(args);
        double rv;
        memcpy(&rv, &rbits, sizeof(rv));
        return push_f64(vm, rv, error);
    }
#else
    if (args != args_s)
        free(args);
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call_f: not supported on this platform");
    return VIGIL_STATUS_INTERNAL;
#endif
}

/* ── ffi.call_s(i64 h, i64 a0, i64 a1) -> string ────────────────── */

static vigil_status_t vigil_ffi_call_s(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int idx = (int)pop_i64(vm, base, 0);
    int nargs = (int)arg_count - 1;
    int64_t args_s[16];
    int64_t *args = nargs <= 16 ? args_s : malloc((size_t)nargs * sizeof(*args));
    if (!args)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call_s: alloc failed");
        return VIGIL_STATUS_INTERNAL;
    }
    for (int i = 0; i < nargs; i++)
        args[i] = pop_i64(vm, base, (size_t)(i + 1));
    vigil_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count)
    {
        if (args != args_s)
            free(args);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call_s: invalid handle");
        return VIGIL_STATUS_INTERNAL;
    }

#ifdef VIGIL_HAS_LIBFFI
    {
        int64_t rbits = ffi_call_generic(g_bound[idx].fn, g_bound[idx].sig, args, nargs);
        if (args != args_s)
            free(args);
        const char *r = (const char *)(intptr_t)rbits;
        return push_string(vm, r, error);
    }
#else
    if (args != args_s)
        free(args);
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.call_s: not supported on this platform");
    return VIGIL_STATUS_INTERNAL;
#endif
}

/* ── CALL_EXTERN runtime implementation ──────────────────────────── */

/* Cache of opened libraries and resolved extern functions. */
#define EXTERN_LIB_CACHE_SIZE 16
#define EXTERN_FN_CACHE_SIZE 64

typedef struct
{
    char path[256];
    void *handle;
} extern_lib_entry_t;

typedef struct
{
    char key[384];
    size_t key_len;
    void *fn_ptr;
    char sig[128];
} extern_fn_entry_t;

static extern_lib_entry_t g_ext_libs[EXTERN_LIB_CACHE_SIZE];
static int g_ext_lib_count;
static extern_fn_entry_t g_ext_fns[EXTERN_FN_CACHE_SIZE];
static int g_ext_fn_count;

static void *ext_open_lib(const char *path, vigil_error_t *error)
{
    for (int i = 0; i < g_ext_lib_count; i++)
    {
        if (strcmp(g_ext_libs[i].path, path) == 0)
            return g_ext_libs[i].handle;
    }
    void *handle = NULL;
    vigil_status_t s = vigil_platform_dlopen(path, &handle, error);
    if (s != VIGIL_STATUS_OK)
        return NULL;
    if (g_ext_lib_count < EXTERN_LIB_CACHE_SIZE)
    {
        size_t n = strlen(path);
        if (n >= sizeof(g_ext_libs[0].path))
            n = sizeof(g_ext_libs[0].path) - 1;
        memcpy(g_ext_libs[g_ext_lib_count].path, path, n);
        g_ext_libs[g_ext_lib_count].path[n] = '\0';
        g_ext_libs[g_ext_lib_count].handle = handle;
        g_ext_lib_count++;
    }
    return handle;
}

static extern_fn_entry_t *ext_resolve(const char *desc, size_t desc_len, vigil_error_t *error)
{
    for (int i = 0; i < g_ext_fn_count; i++)
    {
        if (g_ext_fns[i].key_len == desc_len && memcmp(g_ext_fns[i].key, desc, desc_len) == 0)
            return &g_ext_fns[i];
    }
    const char *lib_path = desc;
    size_t lib_len = strlen(lib_path);
    const char *c_name = lib_path + lib_len + 1;
    size_t name_len = strlen(c_name);
    const char *sig = c_name + name_len + 1;

    void *handle = ext_open_lib(lib_path, error);
    if (!handle)
        return NULL;
    void *sym = NULL;
    vigil_status_t s = vigil_platform_dlsym(handle, c_name, &sym, error);
    if (s != VIGIL_STATUS_OK)
        return NULL;

    if (g_ext_fn_count >= EXTERN_FN_CACHE_SIZE)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "extern fn cache full");
        return NULL;
    }
    extern_fn_entry_t *e = &g_ext_fns[g_ext_fn_count++];
    if (desc_len >= sizeof(e->key))
        desc_len = sizeof(e->key) - 1;
    memcpy(e->key, desc, desc_len);
    e->key_len = desc_len;
    e->fn_ptr = sym;
    size_t slen = strlen(sig);
    if (slen >= sizeof(e->sig))
        slen = sizeof(e->sig) - 1;
    memcpy(e->sig, sig, slen);
    e->sig[slen] = '\0';
    return e;
}

VIGIL_API vigil_status_t vigil_extern_call(vigil_vm_t *vm, const char *desc, size_t desc_len, size_t arg_count,
                                           vigil_error_t *error)
{
    extern_fn_entry_t *entry = ext_resolve(desc, desc_len, error);
    if (!entry)
        return VIGIL_STATUS_INTERNAL;

#ifdef VIGIL_HAS_LIBFFI
    {
        size_t base = vigil_vm_stack_depth(vm) - arg_count;
        int64_t args_s[16] = {0};
        int64_t *args = arg_count <= 16 ? args_s : malloc(arg_count * sizeof(*args));
        if (!args)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "extern call: alloc failed");
            return VIGIL_STATUS_INTERNAL;
        }
        for (size_t i = 0; i < arg_count; i++)
        {
            vigil_value_t v = vigil_vm_stack_get(vm, base + i);
            if (vigil_nanbox_is_double(v))
            {
                double d = vigil_nanbox_decode_double(v);
                memcpy(&args[i], &d, sizeof(double));
            }
            else
            {
                args[i] = vigil_nanbox_decode_int(v);
            }
        }
        vigil_vm_stack_pop_n(vm, arg_count);

        int64_t result = ffi_call_generic(entry->fn_ptr, entry->sig, args, (int)arg_count);
        if (args != args_s)
            free(args);

        /* Determine return type from sig. */
        const char *paren = strchr(entry->sig, '(');
        size_t ret_len = paren ? (size_t)(paren - entry->sig) : strlen(entry->sig);
        int is_void = (ret_len == 4 && memcmp(entry->sig, "void", 4) == 0);
        int is_f64 = (ret_len == 3 && memcmp(entry->sig, "f64", 3) == 0);
        int is_string = (ret_len == 6 && memcmp(entry->sig, "string", 6) == 0);

        if (is_void)
        {
            return VIGIL_STATUS_OK;
        }
        else if (is_f64)
        {
            double rv;
            memcpy(&rv, &result, sizeof(rv));
            return push_f64(vm, rv, error);
        }
        else if (is_string)
        {
            const char *s = (const char *)(intptr_t)result;
            return push_string(vm, s, error);
        }
        else
        {
            return push_i64(vm, result, error);
        }
    }
#else
    (void)entry;
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "extern fn: libffi not available");
    return VIGIL_STATUS_INTERNAL;
#endif
}

/* ── ffi.callback — wrap a Vigil closure as a C function pointer ── */

/*
 * Global VM + runtime for callback dispatch.  Set once by the first
 * ffi.callback() call.  Callbacks create a fresh VM to avoid
 * re-entering the running interpreter.
 */
static vigil_runtime_t *g_cb_runtime;
static volatile int64_t g_cb_state_lock = 0;
static int64_t g_cb_registered_callbacks = 0;
static int64_t g_cb_active_invocations = 0;
static int g_cb_runtime_shutdown_pending = 0;

static intptr_t vigil_ffi_callback_dispatch(int slot, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3);

static void cb_state_lock(void)
{
    while (!vigil_atomic_cas(&g_cb_state_lock, 0, 1))
    {
    }
}

static void cb_state_unlock(void)
{
    vigil_atomic_store(&g_cb_state_lock, 0);
}

static vigil_status_t ensure_callback_runtime(vigil_vm_t *vm, vigil_error_t *error)
{
    vigil_status_t st = VIGIL_STATUS_OK;
    int needs_dispatch_install = 0;

    cb_state_lock();
    if (g_cb_runtime == NULL)
    {
        vigil_runtime_options_t options;
        const vigil_runtime_t *source_runtime = vigil_vm_runtime(vm);

        vigil_runtime_options_init(&options);
        options.allocator = vigil_runtime_allocator(source_runtime);
        options.logger = vigil_runtime_logger(source_runtime);
        st = vigil_runtime_open(&g_cb_runtime, &options, error);
        if (st == VIGIL_STATUS_OK)
        {
            needs_dispatch_install = 1;
            g_cb_runtime_shutdown_pending = 0;
        }
    }
    cb_state_unlock();

    if (st == VIGIL_STATUS_OK && needs_dispatch_install)
    {
        vigil_ffi_callback_set_dispatch(vigil_ffi_callback_dispatch);
    }

    return st;
}

static void maybe_shutdown_callback_runtime(void)
{
    vigil_runtime_t *runtime_to_close = NULL;

    cb_state_lock();
    if (g_cb_registered_callbacks == 0 && g_cb_active_invocations == 0 && g_cb_runtime != NULL)
    {
        runtime_to_close = g_cb_runtime;
        g_cb_runtime = NULL;
        g_cb_runtime_shutdown_pending = 0;
    }
    cb_state_unlock();

    if (runtime_to_close != NULL)
    {
        vigil_runtime_close(&runtime_to_close);
    }
}

static intptr_t vigil_ffi_callback_dispatch(int slot, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3)
{
    vigil_object_t *closure = vigil_ffi_callback_retain_closure(slot);
    vigil_vm_t *cb_vm = NULL;
    vigil_error_t err = {0};
    vigil_value_t out = {0};
    vigil_runtime_t *runtime = NULL;
    intptr_t result = 0;

    if (!closure)
        return 0;

    cb_state_lock();
    runtime = g_cb_runtime;
    if (runtime != NULL)
    {
        g_cb_active_invocations++;
    }
    cb_state_unlock();

    if (runtime == NULL)
    {
        vigil_object_release(&closure);
        return 0;
    }

    if (vigil_vm_open(&cb_vm, runtime, NULL, &err) != VIGIL_STATUS_OK)
    {
        cb_state_lock();
        g_cb_active_invocations--;
        cb_state_unlock();
        vigil_object_release(&closure);
        maybe_shutdown_callback_runtime();
        return 0;
    }

    /* Push arguments onto the callback VM stack.
     * The closure's arity determines how many args to pass (max 4). */
    const vigil_object_t *fn = vigil_callable_object_function(closure);
    size_t arity = vigil_function_object_arity(fn);
    intptr_t argv[4] = {a0, a1, a2, a3};
    for (size_t i = 0; i < arity && i < 4; i++)
    {
        vigil_value_t v = vigil_nanbox_encode_int((int64_t)argv[i]);
        vigil_vm_stack_push(cb_vm, &v, &err);
    }

    vigil_vm_execute_function(cb_vm, closure, &out, &err);

    if (vigil_nanbox_is_int(out))
        result = (intptr_t)vigil_nanbox_decode_int(out);

    vigil_vm_close(&cb_vm);
    vigil_object_release(&closure);

    cb_state_lock();
    g_cb_active_invocations--;
    cb_state_unlock();
    maybe_shutdown_callback_runtime();
    return result;
}

/* ffi.callback(fn, string sig) -> i64
 * Returns a C function pointer (as i64) that, when called by C code,
 * invokes the given Vigil closure.  Up to 4 intptr_t args are passed. */
static vigil_status_t vigil_ffi_callback(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t fn_val = vigil_vm_stack_get(vm, base);
    /* sig is arg 1 — read but currently unused (reserved for future
     * libffi closure support with typed signatures). */
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *fn = vigil_value_as_object(&fn_val);
    void *ptr = NULL;
    int slot = -1;
    vigil_status_t st;

    if (fn == NULL || (vigil_object_type(fn) != VIGIL_OBJECT_FUNCTION && vigil_object_type(fn) != VIGIL_OBJECT_CLOSURE))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "ffi.callback: first argument must be a function");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    slot = vigil_ffi_callback_alloc(&ptr);
    if (slot < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "ffi.callback: all callback slots in use");
        return VIGIL_STATUS_INTERNAL;
    }

    st = ensure_callback_runtime(vm, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_ffi_callback_free(slot);
        return st;
    }

    vigil_ffi_callback_set_closure(slot, fn);

    cb_state_lock();
    g_cb_registered_callbacks++;
    cb_state_unlock();

    return push_i64(vm, (int64_t)(intptr_t)ptr, error);
}

/* ffi.callback_free(i64 callback) */
static vigil_status_t vigil_ffi_callback_free_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t callback = pop_i64(vm, base, 0);
    int slot = -1;
    int was_allocated = 0;
    vigil_vm_stack_pop_n(vm, arg_count);

    if (callback >= 0 && callback < VIGIL_FFI_MAX_CALLBACKS)
    {
        slot = (int)callback;
    }
    else
    {
        slot = vigil_ffi_callback_slot_from_ptr((void *)(intptr_t)callback);
    }

    if (slot >= 0)
    {
        was_allocated = vigil_ffi_callback_is_allocated(slot);
        if (was_allocated)
        {
            vigil_ffi_callback_free(slot);
            cb_state_lock();
            if (g_cb_registered_callbacks > 0)
            {
                g_cb_registered_callbacks--;
            }
            if (g_cb_registered_callbacks == 0)
            {
                g_cb_runtime_shutdown_pending = 1;
            }
            cb_state_unlock();
            maybe_shutdown_callback_runtime();
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};
static const int p_i64[] = {VIGIL_TYPE_I64};
static const int p_i64_str[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING};
static const int p_i64_str_str[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_call[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64,
                             VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int p_call_f[] = {VIGIL_TYPE_I64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_call_s[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int p_obj_str[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING};

// clang-format off
#define F(n, nl, fn, pc, pt, rt) {n, nl, fn, pc, pt, rt, 1, NULL, 0, NULL, NULL}
#define FV(n, nl, fn, pc, pt) {n, nl, fn, pc, pt, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL}
// clang-format on

static const vigil_native_module_function_t vigil_ffi_functions[] = {
    F("open", 4U, vigil_ffi_open, 1U, p_str, VIGIL_TYPE_I64),
    F("sym", 3U, vigil_ffi_sym, 2U, p_i64_str, VIGIL_TYPE_I64),
    FV("close", 5U, vigil_ffi_close, 1U, p_i64),
    F("bind", 4U, vigil_ffi_bind, 3U, p_i64_str_str, VIGIL_TYPE_I64),
    F("call", 4U, vigil_ffi_call, 7U, p_call, VIGIL_TYPE_I64),
    F("call_f", 6U, vigil_ffi_call_f, 3U, p_call_f, VIGIL_TYPE_F64),
    F("call_s", 6U, vigil_ffi_call_s, 3U, p_call_s, VIGIL_TYPE_STRING),
    F("callback", 8U, vigil_ffi_callback, 2U, p_obj_str, VIGIL_TYPE_I64),
    FV("callback_free", 13U, vigil_ffi_callback_free_fn, 1U, p_i64),
};

#undef F
#undef FV

VIGIL_API const vigil_native_module_t vigil_stdlib_ffi = {
    "ffi", 3U, vigil_ffi_functions, sizeof(vigil_ffi_functions) / sizeof(vigil_ffi_functions[0]), NULL, 0U};
