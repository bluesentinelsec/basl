/* Vigil plugin: sdl
 *
 * SDL3 bindings for Vigil.
 * See: https://github.com/bluesentinelsec/vigil/issues/307
 */
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* ── Handle registry ─────────────────────────────────────────────────
 * Generic slot table mapping i64 handle → void* pointer.
 * Same pattern as fs.c Reader/Writer registries but reusable via macros.
 *
 * Usage:
 *   SDL_HANDLE_REGISTRY(windows);          // declare static registry
 *   SDL_HANDLE_STORE(windows, ptr, &h)     // store pointer, get handle
 *   SDL_HANDLE_GET(windows, h)             // get pointer from handle
 *   SDL_HANDLE_CLEAR(windows, h)           // set slot to NULL
 */

/* All infrastructure helpers are used across slices; suppress until wired up. */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define SDL_HANDLE_MAX 256

typedef struct
{
    void *items[SDL_HANDLE_MAX];
    int32_t count;
} sdl_handle_registry_t;

#define SDL_HANDLE_REGISTRY(name) static sdl_handle_registry_t g_##name = {{0}, 0}

/* Store a pointer, return handle via *out. Returns 0 on success, -1 if full. */
static int sdl_handle_store(sdl_handle_registry_t *r, void *ptr, int64_t *out)
{
    /* Reuse a cleared slot first. */
    for (int32_t i = 0; i < r->count; i++)
    {
        if (r->items[i] == NULL)
        {
            r->items[i] = ptr;
            *out = (int64_t)i;
            return 0;
        }
    }
    if (r->count >= SDL_HANDLE_MAX)
        return -1;
    r->items[r->count] = ptr;
    *out = (int64_t)r->count++;
    return 0;
}

static void *sdl_handle_get(sdl_handle_registry_t *r, int64_t handle)
{
    if (handle < 0 || handle >= (int64_t)r->count)
        return NULL;
    return r->items[handle];
}

static void sdl_handle_clear(sdl_handle_registry_t *r, int64_t handle)
{
    if (handle >= 0 && handle < (int64_t)r->count)
        r->items[handle] = NULL;
}

#define SDL_HANDLE_STORE(name, ptr, out) sdl_handle_store(&g_##name, (ptr), (out))
#define SDL_HANDLE_GET(name, h) sdl_handle_get(&g_##name, (h))
#define SDL_HANDLE_CLEAR(name, h) sdl_handle_clear(&g_##name, (h))

/* ── Stack helpers ───────────────────────────────────────────────── */

static int32_t sdl_arg_i32(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base + idx));
}

static int64_t sdl_arg_i64(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base + idx));
}

static double sdl_arg_f64(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + idx));
}

static const char *sdl_arg_str(vigil_vm_t *vm, size_t base, size_t idx, char *buf, size_t bufsz)
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

static vigil_status_t sdl_push_i32(vigil_vm_t *vm, int32_t v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_i32(v);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t sdl_push_i64(vigil_vm_t *vm, int64_t v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_int(v);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t sdl_push_bool(vigil_vm_t *vm, int v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_from_bool(v);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t sdl_push_f64(vigil_vm_t *vm, double v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_double(v);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t sdl_push_string(vigil_vm_t *vm, const char *s, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_string_object_new_cstr(rt, s ? s : "", &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    vigil_object_release(&obj);
    st = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return st;
}

/* ── Error helpers ───────────────────────────────────────────────── */

/* Vigil error kinds matching err.* constants. */
#define SDL_ERR_IO 5     /* err.io */
#define SDL_ERR_ARG 8    /* err.arg */
#define SDL_ERR_STATE 11 /* err.state */

static vigil_status_t sdl_push_err(vigil_vm_t *vm, const char *msg, int64_t kind, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, kind, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    st = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return st;
}

static vigil_status_t sdl_push_ok(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* Push SDL_GetError() as a Vigil err with the given kind. */
static vigil_status_t sdl_push_sdl_err(vigil_vm_t *vm, int64_t kind, vigil_error_t *error)
{
    const char *msg = SDL_GetError();
    return sdl_push_err(vm, (msg && *msg) ? msg : "unknown SDL error", kind, error);
}

/* For (bool, err) return: push true + ok. */
static vigil_status_t sdl_push_bool_ok(vigil_vm_t *vm, vigil_error_t *error)
{
    vigil_status_t st = sdl_push_bool(vm, 1, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* For (bool, err) return: push false + SDL error. */
static vigil_status_t sdl_push_bool_sdl_err(vigil_vm_t *vm, int64_t kind, vigil_error_t *error)
{
    vigil_status_t st = sdl_push_bool(vm, 0, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_sdl_err(vm, kind, error);
}

/* For (Object, err) fail: push nil + error. */
static vigil_status_t sdl_push_nil_and_err(vigil_vm_t *vm, const char *msg, int64_t kind, vigil_error_t *error)
{
    vigil_value_t nil;
    vigil_value_init_nil(&nil);
    vigil_status_t st = vigil_vm_stack_push(vm, &nil, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_err(vm, msg, kind, error);
}

static vigil_status_t sdl_push_nil_and_sdl_err(vigil_vm_t *vm, int64_t kind, vigil_error_t *error)
{
    const char *msg = SDL_GetError();
    return sdl_push_nil_and_err(vm, (msg && *msg) ? msg : "unknown SDL error", kind, error);
}

/* ── Struct marshaling helpers ───────────────────────────────────── */

/* Read an i32 field from a native class instance on the stack. */
static int32_t sdl_field_i32(vigil_vm_t *vm, size_t slot, size_t idx)
{
    vigil_value_t val = vigil_vm_stack_get(vm, slot);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(val);
    vigil_value_t field;
    vigil_instance_object_get_field(obj, idx, &field);
    int32_t result = vigil_nanbox_decode_i32(field);
    vigil_value_release(&field);
    return result;
}

/* Read an i64 field from a native class instance on the stack. */
static int64_t sdl_field_i64(vigil_vm_t *vm, size_t slot, size_t idx)
{
    vigil_value_t val = vigil_vm_stack_get(vm, slot);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(val);
    vigil_value_t field;
    vigil_instance_object_get_field(obj, idx, &field);
    int64_t result = vigil_nanbox_decode_int(field);
    vigil_value_release(&field);
    return result;
}

/* Read an f64 field from a native class instance on the stack. */
static double sdl_field_f64(vigil_vm_t *vm, size_t slot, size_t idx)
{
    vigil_value_t val = vigil_vm_stack_get(vm, slot);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(val);
    vigil_value_t field;
    vigil_instance_object_get_field(obj, idx, &field);
    double result = vigil_nanbox_decode_double(field);
    vigil_value_release(&field);
    return result;
}

/* Get class_index from hidden first arg (static methods). */
static size_t sdl_static_class_index(vigil_vm_t *vm, size_t base)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    return (size_t)vigil_nanbox_decode_i32(v);
}

/* Get class_index from self instance (instance methods). */
static size_t sdl_self_class_index(vigil_vm_t *vm, size_t base)
{
    vigil_value_t val = vigil_vm_stack_get(vm, base);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(val);
    return vigil_instance_object_class_index(obj);
}

/* Push a new native class instance with one i64 field (handle). */
static vigil_status_t sdl_push_handle_instance(vigil_vm_t *vm, size_t class_index, int64_t handle, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_value_t field;
    vigil_value_init_int(&field, handle);
    vigil_object_t *inst = NULL;
    vigil_status_t st = vigil_instance_object_new(rt, class_index, &field, 1U, &inst, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t result;
    vigil_value_init_object(&result, &inst);
    st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

/* ── Constant export helper ──────────────────────────────────────── */

#define SDL_CONST_FN(cname, value)                                                                                     \
    static vigil_status_t sdl_const_##cname(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                    \
    {                                                                                                                  \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        return sdl_push_i32(vm, (int32_t)(value), error);                                                              \
    }

/* clang-format off */
#define SDL_CONST_ENTRY(vname, cname)                                                                                  \
    {vname, sizeof(vname) - 1U, sdl_const_##cname, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0}

/* ── Native class helper macros ──────────────────────────────────── */

#define SDL_PFIELD(n, nl, t) {n, nl, t, 0, NULL, 0U, 0}

#define SDL_METHOD(n, nl, fn, pc, pt, rt, rc, rts) {n, nl, fn, pc, pt, rt, rc, rts, 0, NULL, 0U, 0}

#define SDL_STATIC(n, nl, fn, pc, pt, rt, rc, rts) {n, nl, fn, pc, pt, rt, rc, rts, 1, NULL, 0U, 0}
/* clang-format on */

/* ── Slice 1: Init, Version, Constants ────────────────────────────── */

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

/* ── sdl.init(i32 flags) -> (bool, err) ──────────────────────────── */

static vigil_status_t sdl_fn_init(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t flags = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_Init((SDL_InitFlags)flags))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── sdl.init_sub_system(i32 flags) -> (bool, err) ───────────────── */

static vigil_status_t sdl_fn_init_sub_system(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t flags = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_InitSubSystem((SDL_InitFlags)flags))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── sdl.quit_sub_system(i32 flags) ──────────────────────────────── */

static vigil_status_t sdl_fn_quit_sub_system(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t flags = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_QuitSubSystem((SDL_InitFlags)flags);
    return VIGIL_STATUS_OK;
}

/* ── sdl.was_init(i32 flags) -> i32 ──────────────────────────────── */

static vigil_status_t sdl_fn_was_init(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t flags = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_WasInit((SDL_InitFlags)flags), error);
}

/* ── sdl.quit() ──────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_quit(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Quit();
    return VIGIL_STATUS_OK;
}

/* ── sdl.get_error() -> string ───────────────────────────────────── */

static vigil_status_t sdl_fn_get_error(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetError(), error);
}

/* ── sdl.get_version() -> i32 ────────────────────────────────────── */

static vigil_status_t sdl_fn_get_version(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetVersion(), error);
}

/* ── sdl.get_revision() -> string ────────────────────────────────── */

static vigil_status_t sdl_fn_get_revision(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetRevision(), error);
}

/* ── Init flag constants ─────────────────────────────────────────── */

SDL_CONST_FN(INIT_AUDIO, SDL_INIT_AUDIO)
SDL_CONST_FN(INIT_VIDEO, SDL_INIT_VIDEO)
SDL_CONST_FN(INIT_JOYSTICK, SDL_INIT_JOYSTICK)
SDL_CONST_FN(INIT_HAPTIC, SDL_INIT_HAPTIC)
SDL_CONST_FN(INIT_GAMEPAD, SDL_INIT_GAMEPAD)
SDL_CONST_FN(INIT_EVENTS, SDL_INIT_EVENTS)
SDL_CONST_FN(INIT_SENSOR, SDL_INIT_SENSOR)
SDL_CONST_FN(INIT_CAMERA, SDL_INIT_CAMERA)

/* ── Window flag constants ───────────────────────────────────────── */

SDL_CONST_FN(WINDOW_FULLSCREEN, SDL_WINDOW_FULLSCREEN)
SDL_CONST_FN(WINDOW_OPENGL, SDL_WINDOW_OPENGL)
SDL_CONST_FN(WINDOW_HIDDEN, SDL_WINDOW_HIDDEN)
SDL_CONST_FN(WINDOW_BORDERLESS, SDL_WINDOW_BORDERLESS)
SDL_CONST_FN(WINDOW_RESIZABLE, SDL_WINDOW_RESIZABLE)
SDL_CONST_FN(WINDOW_MINIMIZED, SDL_WINDOW_MINIMIZED)
SDL_CONST_FN(WINDOW_MAXIMIZED, SDL_WINDOW_MAXIMIZED)
SDL_CONST_FN(WINDOW_ALWAYS_ON_TOP, SDL_WINDOW_ALWAYS_ON_TOP)
SDL_CONST_FN(WINDOW_VULKAN, SDL_WINDOW_VULKAN)
SDL_CONST_FN(WINDOW_METAL, SDL_WINDOW_METAL)
SDL_CONST_FN(WINDOW_HIGH_PIXEL_DENSITY, SDL_WINDOW_HIGH_PIXEL_DENSITY)

/* ── Window handle registry ──────────────────────────────────────── */

SDL_HANDLE_REGISTRY(windows);

enum
{
    WIN_HANDLE = 0,
    WIN_FIELD_COUNT
};

/* ── Window.create(string title, i32 w, i32 h, i32 flags) -> (Window, err) */

static vigil_status_t sdl_window_create(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    char title[256];
    sdl_arg_str(vm, base, 1, title, sizeof(title));
    int32_t w = sdl_arg_i32(vm, base, 2);
    int32_t h = sdl_arg_i32(vm, base, 3);
    int32_t flags = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Window *win = SDL_CreateWindow(title, w, h, (SDL_WindowFlags)flags);
    if (!win)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(windows, win, &handle) < 0)
    {
        SDL_DestroyWindow(win);
        return sdl_push_nil_and_err(vm, "too many windows", SDL_ERR_STATE, error);
    }

    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* ── win.destroy() ───────────────────────────────────────────────── */

static vigil_status_t sdl_window_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
    {
        SDL_DestroyWindow(win);
        SDL_HANDLE_CLEAR(windows, h);
    }
    return VIGIL_STATUS_OK;
}

/* ── win.get_id() -> i32 ─────────────────────────────────────────── */

static vigil_status_t sdl_window_get_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_i32(vm, win ? (int32_t)SDL_GetWindowID(win) : 0, error);
}

/* ── win.set_title(string title) -> (bool, err) ──────────────────── */

static vigil_status_t sdl_window_set_title(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    char title[256];
    sdl_arg_str(vm, base, 1, title, sizeof(title));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowTitle(win, title))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── win.get_title() -> string ───────────────────────────────────── */

static vigil_status_t sdl_window_get_title(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_string(vm, win ? SDL_GetWindowTitle(win) : "", error);
}

/* ── win.set_position(i32 x, i32 y) -> (bool, err) ───────────────── */

static vigil_status_t sdl_window_set_position(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1);
    int32_t y = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowPosition(win, x, y))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── win.get_position() -> (i32, i32) ────────────────────────────── */

static vigil_status_t sdl_window_get_position(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int x = 0, y = 0;
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_GetWindowPosition(win, &x, &y);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)y, error);
}

/* ── win.set_size(i32 w, i32 h) -> (bool, err) ───────────────────── */

static vigil_status_t sdl_window_set_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t w = sdl_arg_i32(vm, base, 1);
    int32_t ht = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowSize(win, w, ht))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── win.get_size() -> (i32, i32) ────────────────────────────────── */

static vigil_status_t sdl_window_get_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int w = 0, ht = 0;
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_GetWindowSize(win, &w, &ht);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)ht, error);
}

/* ── win.set_fullscreen(bool fs) -> (bool, err) ──────────────────── */

static vigil_status_t sdl_window_set_fullscreen(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t fs = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowFullscreen(win, fs != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Simple void window methods ──────────────────────────────────── */

#define WIN_VOID_METHOD(name, sdl_fn)                                                                                  \
    static vigil_status_t sdl_window_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                    \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);                                                               \
        (void)error;                                                                                                   \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);                                                    \
        if (win)                                                                                                       \
            sdl_fn(win);                                                                                               \
        return VIGIL_STATUS_OK;                                                                                        \
    }

WIN_VOID_METHOD(show, SDL_ShowWindow)
WIN_VOID_METHOD(hide, SDL_HideWindow)
WIN_VOID_METHOD(raise, SDL_RaiseWindow)
WIN_VOID_METHOD(minimize, SDL_MinimizeWindow)
WIN_VOID_METHOD(maximize, SDL_MaximizeWindow)
WIN_VOID_METHOD(restore, SDL_RestoreWindow)

#undef WIN_VOID_METHOD

/* ── win.set_resizable(bool r) ───────────────────────────────────── */

static vigil_status_t sdl_window_set_resizable(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t r = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_SetWindowResizable(win, r != 0);
    return VIGIL_STATUS_OK;
}

/* ── win.set_bordered(bool b) ────────────────────────────────────── */

static vigil_status_t sdl_window_set_bordered(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t b = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_SetWindowBordered(win, b != 0);
    return VIGIL_STATUS_OK;
}

/* ── win.get_flags() -> i32 ──────────────────────────────────────── */

static vigil_status_t sdl_window_get_flags(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_i32(vm, win ? (int32_t)SDL_GetWindowFlags(win) : 0, error);
}

/* ── Parameter type arrays ───────────────────────────────────────── */

static const int p_i32[] = {VIGIL_TYPE_I32};
static const int p_str[] = {VIGIL_TYPE_STRING};
static const int p_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_str_i32_i32_i32[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_bool_err[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_ERR};
static const int rt_obj_err[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int rt_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};

/* ── Function helper macros ──────────────────────────────────────── */

/* clang-format off */

/* Function returning (bool, err). */
#define SDL_FN_BOOL_ERR(n, nl, fn, pc, pt) {n, nl, fn, pc, pt, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, NULL, 0}

/* Function returning a single value. */
#define SDL_FN(n, nl, fn, pc, pt, rt) {n, nl, fn, pc, pt, rt, 1U, NULL, 0, NULL, NULL, 0}

/* Void function. */
#define SDL_FN_VOID(n, nl, fn, pc, pt) {n, nl, fn, pc, pt, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0}

/* clang-format on */

/* ── Function table ──────────────────────────────────────────────── */

static const vigil_native_module_function_t sdl_functions[] = {
    /* Init / shutdown */
    SDL_FN_BOOL_ERR("init", 4U, sdl_fn_init, 1U, p_i32),
    SDL_FN_BOOL_ERR("init_sub_system", 15U, sdl_fn_init_sub_system, 1U, p_i32),
    SDL_FN_VOID("quit_sub_system", 15U, sdl_fn_quit_sub_system, 1U, p_i32),
    SDL_FN("was_init", 8U, sdl_fn_was_init, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN_VOID("quit", 4U, sdl_fn_quit, 0U, NULL),
    /* Error / version */
    SDL_FN("get_error", 9U, sdl_fn_get_error, 0U, NULL, VIGIL_TYPE_STRING),
    SDL_FN("get_version", 11U, sdl_fn_get_version, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_revision", 12U, sdl_fn_get_revision, 0U, NULL, VIGIL_TYPE_STRING),
    /* Init flag constants */
    SDL_CONST_ENTRY("INIT_AUDIO", INIT_AUDIO),
    SDL_CONST_ENTRY("INIT_VIDEO", INIT_VIDEO),
    SDL_CONST_ENTRY("INIT_JOYSTICK", INIT_JOYSTICK),
    SDL_CONST_ENTRY("INIT_HAPTIC", INIT_HAPTIC),
    SDL_CONST_ENTRY("INIT_GAMEPAD", INIT_GAMEPAD),
    SDL_CONST_ENTRY("INIT_EVENTS", INIT_EVENTS),
    SDL_CONST_ENTRY("INIT_SENSOR", INIT_SENSOR),
    SDL_CONST_ENTRY("INIT_CAMERA", INIT_CAMERA),
    /* Window flag constants */
    SDL_CONST_ENTRY("WINDOW_FULLSCREEN", WINDOW_FULLSCREEN),
    SDL_CONST_ENTRY("WINDOW_OPENGL", WINDOW_OPENGL),
    SDL_CONST_ENTRY("WINDOW_HIDDEN", WINDOW_HIDDEN),
    SDL_CONST_ENTRY("WINDOW_BORDERLESS", WINDOW_BORDERLESS),
    SDL_CONST_ENTRY("WINDOW_RESIZABLE", WINDOW_RESIZABLE),
    SDL_CONST_ENTRY("WINDOW_MINIMIZED", WINDOW_MINIMIZED),
    SDL_CONST_ENTRY("WINDOW_MAXIMIZED", WINDOW_MAXIMIZED),
    SDL_CONST_ENTRY("WINDOW_ALWAYS_ON_TOP", WINDOW_ALWAYS_ON_TOP),
    SDL_CONST_ENTRY("WINDOW_VULKAN", WINDOW_VULKAN),
    SDL_CONST_ENTRY("WINDOW_METAL", WINDOW_METAL),
    SDL_CONST_ENTRY("WINDOW_HIGH_PIXEL_DENSITY", WINDOW_HIGH_PIXEL_DENSITY),
};

#define SDL_FUNCTION_COUNT (sizeof(sdl_functions) / sizeof(sdl_functions[0]))

/* ── Window class descriptor ─────────────────────────────────────── */

/* clang-format off */
static const vigil_native_class_field_t sdl_window_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_window_methods[] = {
    SDL_STATIC("create", 6U, sdl_window_create, 4U, p_str_i32_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("destroy", 7U, sdl_window_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("get_id", 6U, sdl_window_get_id, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("set_title", 9U, sdl_window_set_title, 1U, p_str, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_title", 9U, sdl_window_get_title, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("set_position", 12U, sdl_window_set_position, 2U, p_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_position", 12U, sdl_window_get_position, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("set_size", 8U, sdl_window_set_size, 2U, p_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_size", 8U, sdl_window_get_size, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("set_fullscreen", 14U, sdl_window_set_fullscreen, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("show", 4U, sdl_window_show, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("hide", 4U, sdl_window_hide, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("raise", 5U, sdl_window_raise, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("minimize", 8U, sdl_window_minimize, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("maximize", 8U, sdl_window_maximize, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("restore", 7U, sdl_window_restore, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("set_resizable", 13U, sdl_window_set_resizable, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("set_bordered", 12U, sdl_window_set_bordered, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("get_flags", 9U, sdl_window_get_flags, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
};

static const vigil_native_class_t sdl_classes[] = {
    {"Window", 6U, sdl_window_fields, WIN_FIELD_COUNT, sdl_window_methods,
     sizeof(sdl_window_methods) / sizeof(sdl_window_methods[0]), NULL},
};
/* clang-format on */

#define SDL_CLASS_COUNT (sizeof(sdl_classes) / sizeof(sdl_classes[0]))

/* ── Module export ───────────────────────────────────────────────── */

VIGIL_API const vigil_native_module_t vigil_plugin_sdl = {
    "sdl", 3U, sdl_functions, SDL_FUNCTION_COUNT, sdl_classes, SDL_CLASS_COUNT};
