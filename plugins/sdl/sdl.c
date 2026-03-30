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
static const int p_i64[] = {VIGIL_TYPE_I64};
static const int p_str[] = {VIGIL_TYPE_STRING};
static const int p_str_str[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_i32_str_str[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i32_i32_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_str_i32_i32_i32[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_obj_str[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING};
static const int p_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_f64_f64_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int rt_bool_err[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_ERR};
static const int rt_obj_err[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int rt_i64_err[] = {VIGIL_TYPE_I64, VIGIL_TYPE_ERR};
static const int rt_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_i32_i32_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_obj_f64_f64[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int rt_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_i32_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_obj_i32_i32_i32_i32[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                            VIGIL_TYPE_I32};
static const int p_obj_obj[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_OBJECT};
/* render_texture: obj tex + 8x f64 (src/dst rects) */
static const int p_obj_f64x8[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                  VIGIL_TYPE_F64,    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
/* render_texture_rotated: obj tex + 8x f64 + angle f64 + cx f64 + cy f64 + flip i32 */
static const int p_obj_f64x11_i32[] = {
    VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
    VIGIL_TYPE_F64,    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_I32};

/* ── Renderer handle registry ────────────────────────────────────── */

SDL_HANDLE_REGISTRY(renderers);

enum
{
    REN_HANDLE = 0,
    REN_FIELD_COUNT
};

/* Renderer.create(sdl.Window win, string driver) -> (Renderer, err) */

static vigil_status_t sdl_renderer_create(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    char driver[64];
    sdl_arg_str(vm, base, 2, driver, sizeof(driver));
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (!win)
        return sdl_push_nil_and_err(vm, "invalid window handle", SDL_ERR_ARG, error);

    const char *drv = driver[0] ? driver : NULL;
    SDL_Renderer *ren = SDL_CreateRenderer(win, drv);
    if (!ren)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(renderers, ren, &handle) < 0)
    {
        SDL_DestroyRenderer(ren);
        return sdl_push_nil_and_err(vm, "too many renderers", SDL_ERR_STATE, error);
    }

    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_renderer_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren)
    {
        SDL_DestroyRenderer(ren);
        SDL_HANDLE_CLEAR(renderers, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_renderer_clear(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren && SDL_RenderClear(ren))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_present(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren && SDL_RenderPresent(ren))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_set_draw_color(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    int32_t r = sdl_arg_i32(vm, base, 1);
    int32_t g = sdl_arg_i32(vm, base, 2);
    int32_t b = sdl_arg_i32(vm, base, 3);
    int32_t a = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren && SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_get_draw_color(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren)
        SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
    vigil_status_t st = sdl_push_i32(vm, r, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, g, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, b, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, a, error);
}

static vigil_status_t sdl_renderer_draw_point(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    float x = (float)sdl_arg_f64(vm, base, 1);
    float y = (float)sdl_arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren && SDL_RenderPoint(ren, x, y))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_draw_line(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    float x1 = (float)sdl_arg_f64(vm, base, 1);
    float y1 = (float)sdl_arg_f64(vm, base, 2);
    float x2 = (float)sdl_arg_f64(vm, base, 3);
    float y2 = (float)sdl_arg_f64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren && SDL_RenderLine(ren, x1, y1, x2, y2))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_draw_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    SDL_FRect rect = {(float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2), (float)sdl_arg_f64(vm, base, 3),
                      (float)sdl_arg_f64(vm, base, 4)};
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren && SDL_RenderRect(ren, &rect))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_fill_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    SDL_FRect rect = {(float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2), (float)sdl_arg_f64(vm, base, 3),
                      (float)sdl_arg_f64(vm, base, 4)};
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren && SDL_RenderFillRect(ren, &rect))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_set_vsync(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    int32_t vsync = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    if (ren && SDL_SetRenderVSync(ren, vsync))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Event system ────────────────────────────────────────────────── */

#define EVT_MAX 64
static SDL_Event g_events[EVT_MAX];
static int32_t g_event_count = 0;

enum
{
    EVT_HANDLE = 0,
    EVT_FIELD_COUNT
};

static vigil_status_t sdl_event_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_event_count >= EVT_MAX)
        return sdl_push_nil_and_err(vm, "too many event objects", SDL_ERR_STATE, error);
    int64_t slot = g_event_count++;
    memset(&g_events[slot], 0, sizeof(SDL_Event));
    vigil_status_t st = sdl_push_handle_instance(vm, ci, slot, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static SDL_Event *evt_get(vigil_vm_t *vm, size_t base)
{
    int64_t h = sdl_field_i64(vm, base, EVT_HANDLE);
    if (h < 0 || h >= g_event_count)
        return NULL;
    return &g_events[h];
}

static vigil_status_t sdl_event_poll(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, ev && SDL_PollEvent(ev), error);
}

static vigil_status_t sdl_event_wait(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, ev && SDL_WaitEvent(ev), error);
}

static vigil_status_t sdl_event_wait_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    int32_t ms = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, ev && SDL_WaitEventTimeout(ev, ms), error);
}

static vigil_status_t sdl_event_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->type : 0, error);
}

/* Keyboard accessors */
static vigil_status_t sdl_event_key_scancode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->key.scancode : 0, error);
}

static vigil_status_t sdl_event_key_keycode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->key.key : 0, error);
}

static vigil_status_t sdl_event_key_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->key.mod : 0, error);
}

static vigil_status_t sdl_event_key_repeat(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, ev && ev->key.repeat, error);
}

/* Mouse accessors */
static vigil_status_t sdl_event_mouse_x(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, ev ? (double)ev->motion.x : 0.0, error);
}

static vigil_status_t sdl_event_mouse_y(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, ev ? (double)ev->motion.y : 0.0, error);
}

static vigil_status_t sdl_event_mouse_button(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->button.button : 0, error);
}

static vigil_status_t sdl_event_wheel_x(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, ev ? (double)ev->wheel.x : 0.0, error);
}

static vigil_status_t sdl_event_wheel_y(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, ev ? (double)ev->wheel.y : 0.0, error);
}

/* ── Slice 5: Keyboard and Mouse Queries ──────────────────────────── */

/* sdl.is_key_pressed(i32 scancode) -> bool */
static vigil_status_t sdl_fn_is_key_pressed(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t sc = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int numkeys = 0;
    const bool *state = SDL_GetKeyboardState(&numkeys);
    return sdl_push_bool(vm, (state && sc >= 0 && sc < numkeys && state[sc]), error);
}

/* sdl.get_mod_state() -> i32 */
static vigil_status_t sdl_fn_get_mod_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetModState(), error);
}

/* sdl.get_key_name(i32 keycode) -> string */
static vigil_status_t sdl_fn_get_key_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t key = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetKeyName((SDL_Keycode)key), error);
}

/* sdl.get_scancode_name(i32 scancode) -> string */
static vigil_status_t sdl_fn_get_scancode_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t sc = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetScancodeName((SDL_Scancode)sc), error);
}

/* sdl.get_mouse_state() -> (f64, f64) — returns x, y */
static vigil_status_t sdl_fn_get_mouse_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    vigil_status_t st = sdl_push_f64(vm, (double)x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)y, error);
}

/* sdl.get_mouse_buttons() -> i32 — returns button mask */
static vigil_status_t sdl_fn_get_mouse_buttons(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(NULL, NULL);
    return sdl_push_i32(vm, (int32_t)buttons, error);
}

/* sdl.get_global_mouse_state() -> (f64, f64) — returns x, y */
static vigil_status_t sdl_fn_get_global_mouse_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    float x = 0, y = 0;
    SDL_GetGlobalMouseState(&x, &y);
    vigil_status_t st = sdl_push_f64(vm, (double)x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)y, error);
}

/* sdl.warp_mouse(sdl.Window win, f64 x, f64 y) */
static vigil_status_t sdl_fn_warp_mouse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    float x = (float)sdl_arg_f64(vm, base, 1);
    float y = (float)sdl_arg_f64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (win)
        SDL_WarpMouseInWindow(win, x, y);
    return VIGIL_STATUS_OK;
}

/* sdl.show_cursor() -> bool */
static vigil_status_t sdl_fn_show_cursor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_ShowCursor(), error);
}

/* sdl.hide_cursor() -> bool */
static vigil_status_t sdl_fn_hide_cursor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HideCursor(), error);
}

/* sdl.cursor_visible() -> bool */
static vigil_status_t sdl_fn_cursor_visible(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_CursorVisible(), error);
}

/* sdl.delay(i32 ms) — pulled forward from slice 7 for frame timing */
static vigil_status_t sdl_fn_delay(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t ms = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Delay((Uint32)ms);
    return VIGIL_STATUS_OK;
}

/* ── Slice 7: Timer and Delay ─────────────────────────────────────── */

/* sdl.get_ticks() -> i64 */
static vigil_status_t sdl_fn_get_ticks(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_GetTicks(), error);
}

/* sdl.get_ticks_ns() -> i64 */
static vigil_status_t sdl_fn_get_ticks_ns(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_GetTicksNS(), error);
}

/* sdl.get_performance_counter() -> i64 */
static vigil_status_t sdl_fn_get_performance_counter(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_GetPerformanceCounter(), error);
}

/* sdl.get_performance_frequency() -> i64 */
static vigil_status_t sdl_fn_get_performance_frequency(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_GetPerformanceFrequency(), error);
}

/* sdl.delay_ns(i64 ns) */
static vigil_status_t sdl_fn_delay_ns(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ns = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_DelayNS((Uint64)ns);
    return VIGIL_STATUS_OK;
}

/* sdl.delay_precise(i64 ns) */
static vigil_status_t sdl_fn_delay_precise(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ns = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_DelayPrecise((Uint64)ns);
    return VIGIL_STATUS_OK;
}

/* ── Slice 8: Audio ───────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(audio_streams);

enum
{
    ASTREAM_HANDLE = 0,
    ASTREAM_FIELD_COUNT
};

/* Internal WAV buffer registry — stores (buf, len) pairs from SDL_LoadWAV.
 * The Vigil side gets an i64 handle; put_wav_data uses it to push into a stream. */
#define WAV_BUF_MAX 64
static struct
{
    Uint8 *buf;
    Uint32 len;
} g_wav_bufs[WAV_BUF_MAX];
static int32_t g_wav_buf_count = 0;

/* sdl.load_wav(string path) -> (i64, i32, i32, i32, err)
 * Returns (wav_handle, format, channels, freq, err).
 * But we only have 2-return. So: return (i64 wav_handle, err).
 * Caller queries format info separately or we embed it in the handle. */

/* Actually, with the 2-return limit, let's return (i64 wav_handle, err)
 * and provide sdl.wav_format/wav_channels/wav_freq accessors. */

static SDL_AudioSpec g_wav_specs[WAV_BUF_MAX];

/* sdl.load_wav(string path) -> (i64, err) */
static vigil_status_t sdl_fn_load_wav(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);

    if (g_wav_buf_count >= WAV_BUF_MAX)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many WAV buffers", SDL_ERR_STATE, error);
    }

    Uint8 *buf = NULL;
    Uint32 len = 0;
    SDL_AudioSpec spec;
    if (!SDL_LoadWAV(path, &spec, &buf, &len))
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }

    int32_t slot = g_wav_buf_count++;
    g_wav_bufs[slot].buf = buf;
    g_wav_bufs[slot].len = len;
    g_wav_specs[slot] = spec;

    vigil_status_t st = sdl_push_i64(vm, (int64_t)slot, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.wav_free(i64 handle) */
static vigil_status_t sdl_fn_wav_free(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < g_wav_buf_count && g_wav_bufs[h].buf)
    {
        SDL_free(g_wav_bufs[h].buf);
        g_wav_bufs[h].buf = NULL;
        g_wav_bufs[h].len = 0;
    }
    return VIGIL_STATUS_OK;
}

/* sdl.wav_format(i64 handle) -> i32 */
static vigil_status_t sdl_fn_wav_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t fmt = (h >= 0 && h < g_wav_buf_count) ? (int32_t)g_wav_specs[h].format : 0;
    return sdl_push_i32(vm, fmt, error);
}

/* sdl.wav_channels(i64 handle) -> i32 */
static vigil_status_t sdl_fn_wav_channels(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t ch = (h >= 0 && h < g_wav_buf_count) ? g_wav_specs[h].channels : 0;
    return sdl_push_i32(vm, ch, error);
}

/* sdl.wav_freq(i64 handle) -> i32 */
static vigil_status_t sdl_fn_wav_freq(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t freq = (h >= 0 && h < g_wav_buf_count) ? g_wav_specs[h].freq : 0;
    return sdl_push_i32(vm, freq, error);
}

/* AudioStream.open(i32 format, i32 channels, i32 freq) -> (AudioStream, err)
 * Opens the default playback device with the given spec. */
static vigil_status_t sdl_audio_stream_open(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int32_t format = sdl_arg_i32(vm, base, 1);
    int32_t channels = sdl_arg_i32(vm, base, 2);
    int32_t freq = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_AudioSpec spec;
    spec.format = (SDL_AudioFormat)format;
    spec.channels = channels;
    spec.freq = freq;

    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(audio_streams, stream, &handle) < 0)
    {
        SDL_DestroyAudioStream(stream);
        return sdl_push_nil_and_err(vm, "too many audio streams", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* stream.destroy() */
static vigil_status_t sdl_audio_stream_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s)
    {
        SDL_DestroyAudioStream(s);
        SDL_HANDLE_CLEAR(audio_streams, h);
    }
    return VIGIL_STATUS_OK;
}

/* stream.put_wav(i64 wav_handle) -> (bool, err) — push WAV data into stream */
static vigil_status_t sdl_audio_stream_put_wav(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    int64_t wh = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, sh);
    if (!s)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    if (wh < 0 || wh >= g_wav_buf_count || !g_wav_bufs[wh].buf)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);

    if (SDL_PutAudioStreamData(s, g_wav_bufs[wh].buf, (int)g_wav_bufs[wh].len))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* stream.get_queued() -> i32 */
static vigil_status_t sdl_audio_stream_get_queued(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    return sdl_push_i32(vm, s ? SDL_GetAudioStreamQueued(s) : 0, error);
}

/* stream.resume() -> (bool, err) */
static vigil_status_t sdl_audio_stream_resume(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s && SDL_ResumeAudioStreamDevice(s))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* stream.pause() -> (bool, err) */
static vigil_status_t sdl_audio_stream_pause(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s && SDL_PauseAudioStreamDevice(s))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* Audio format constants */
SDL_CONST_FN(AUDIO_S16, SDL_AUDIO_S16)
SDL_CONST_FN(AUDIO_S32, SDL_AUDIO_S32)
SDL_CONST_FN(AUDIO_F32, SDL_AUDIO_F32)

/* ── Slice 9: Gamepad and Joystick ────────────────────────────────── */

SDL_HANDLE_REGISTRY(gamepads);

enum
{
    GP_HANDLE = 0,
    GP_FIELD_COUNT
};

/* sdl.has_gamepad() -> bool */
static vigil_status_t sdl_fn_has_gamepad(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasGamepad(), error);
}

/* sdl.get_gamepads() -> i32 — returns count of connected gamepads.
 * (Array returns not yet supported for native fns; return count and let
 *  caller open by index via get_gamepad_id.) */

/* Internal: cache the gamepad ID list for indexed access. */
static SDL_JoystickID *g_gamepad_ids = NULL;
static int g_gamepad_count = 0;

static void sdl_refresh_gamepad_list(void)
{
    if (g_gamepad_ids)
    {
        SDL_free(g_gamepad_ids);
        g_gamepad_ids = NULL;
    }
    g_gamepad_ids = SDL_GetGamepads(&g_gamepad_count);
}

/* sdl.get_gamepad_count() -> i32 */
static vigil_status_t sdl_fn_get_gamepad_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    sdl_refresh_gamepad_list();
    return sdl_push_i32(vm, (int32_t)g_gamepad_count, error);
}

/* sdl.get_gamepad_id(i32 index) -> i32 — get instance ID at index */
static vigil_status_t sdl_fn_get_gamepad_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_gamepad_count && g_gamepad_ids)
        return sdl_push_i32(vm, (int32_t)g_gamepad_ids[idx], error);
    return sdl_push_i32(vm, 0, error);
}

/* Gamepad.open(i32 instance_id) -> (Gamepad, err) */
static vigil_status_t sdl_gamepad_open(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int32_t id = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Gamepad *gp = SDL_OpenGamepad((SDL_JoystickID)id);
    if (!gp)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(gamepads, gp, &handle) < 0)
    {
        SDL_CloseGamepad(gp);
        return sdl_push_nil_and_err(vm, "too many gamepads", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* gp.close() */
static vigil_status_t sdl_gamepad_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    if (gp)
    {
        SDL_CloseGamepad(gp);
        SDL_HANDLE_CLEAR(gamepads, h);
    }
    return VIGIL_STATUS_OK;
}

/* gp.get_name() -> string */
static vigil_status_t sdl_gamepad_get_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_string(vm, gp ? SDL_GetGamepadName(gp) : "", error);
}

/* gp.get_axis(i32 axis) -> i32 */
static vigil_status_t sdl_gamepad_get_axis(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t axis = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetGamepadAxis(gp, (SDL_GamepadAxis)axis) : 0, error);
}

/* gp.get_button(i32 button) -> bool */
static vigil_status_t sdl_gamepad_get_button(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t btn = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_bool(vm, gp && SDL_GetGamepadButton(gp, (SDL_GamepadButton)btn), error);
}

/* gp.rumble(i32 low, i32 high, i32 duration_ms) -> (bool, err) */
static vigil_status_t sdl_gamepad_rumble(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t low = sdl_arg_i32(vm, base, 1);
    int32_t high = sdl_arg_i32(vm, base, 2);
    int32_t dur = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    if (gp && SDL_RumbleGamepad(gp, (Uint16)low, (Uint16)high, (Uint32)dur))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* Gamepad event accessors on Event */
static vigil_status_t sdl_event_gamepad_which(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->gaxis.which : 0, error);
}

static vigil_status_t sdl_event_gamepad_axis(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->gaxis.axis : 0, error);
}

static vigil_status_t sdl_event_gamepad_axis_value(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->gaxis.value : 0, error);
}

static vigil_status_t sdl_event_gamepad_button(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, ev ? (int32_t)ev->gbutton.button : 0, error);
}

/* Gamepad axis/button constants */
SDL_CONST_FN(GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTX)
SDL_CONST_FN(GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_LEFTY)
SDL_CONST_FN(GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTX)
SDL_CONST_FN(GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_RIGHTY)
SDL_CONST_FN(GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
SDL_CONST_FN(GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)

SDL_CONST_FN(GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_SOUTH)
SDL_CONST_FN(GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_EAST)
SDL_CONST_FN(GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_WEST)
SDL_CONST_FN(GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_NORTH)
SDL_CONST_FN(GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_BACK)
SDL_CONST_FN(GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_GUIDE)
SDL_CONST_FN(GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_START)
SDL_CONST_FN(GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_LEFT_STICK)
SDL_CONST_FN(GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK)
SDL_CONST_FN(GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
SDL_CONST_FN(GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
SDL_CONST_FN(GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_UP)
SDL_CONST_FN(GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN)
SDL_CONST_FN(GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT)
SDL_CONST_FN(GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)

/* Gamepad event type constants */
SDL_CONST_FN(EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_AXIS_MOTION)
SDL_CONST_FN(EVENT_GAMEPAD_BUTTON_DOWN, SDL_EVENT_GAMEPAD_BUTTON_DOWN)
SDL_CONST_FN(EVENT_GAMEPAD_BUTTON_UP, SDL_EVENT_GAMEPAD_BUTTON_UP)
SDL_CONST_FN(EVENT_GAMEPAD_ADDED, SDL_EVENT_GAMEPAD_ADDED)
SDL_CONST_FN(EVENT_GAMEPAD_REMOVED, SDL_EVENT_GAMEPAD_REMOVED)

/* ── Slice 10: Clipboard, MessageBox, Misc ────────────────────────── */

/* sdl.set_clipboard_text(string text) -> (bool, err) */
static vigil_status_t sdl_fn_set_clipboard_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char text[4096];
    sdl_arg_str(vm, base, 0, text, sizeof(text));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetClipboardText(text))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* sdl.get_clipboard_text() -> string */
static vigil_status_t sdl_fn_get_clipboard_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    char *text = SDL_GetClipboardText();
    vigil_status_t st = sdl_push_string(vm, text ? text : "", error);
    SDL_free(text);
    return st;
}

/* sdl.has_clipboard_text() -> bool */
static vigil_status_t sdl_fn_has_clipboard_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasClipboardText(), error);
}

/* sdl.show_simple_message_box(i32 flags, string title, string message) -> (bool, err)
 * Window parameter omitted (passes NULL) — modal to desktop. */
static vigil_status_t sdl_fn_show_simple_message_box(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t flags = sdl_arg_i32(vm, base, 0);
    char title[256];
    sdl_arg_str(vm, base, 1, title, sizeof(title));
    char message[4096];
    sdl_arg_str(vm, base, 2, message, sizeof(message));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_ShowSimpleMessageBox((SDL_MessageBoxFlags)flags, title, message, NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* sdl.open_url(string url) -> (bool, err) */
static vigil_status_t sdl_fn_open_url(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char url[2048];
    sdl_arg_str(vm, base, 0, url, sizeof(url));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_OpenURL(url))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* sdl.get_base_path() -> string */
static vigil_status_t sdl_fn_get_base_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetBasePath(), error);
}

/* sdl.get_pref_path(string org, string app) -> string */
static vigil_status_t sdl_fn_get_pref_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char org[256];
    sdl_arg_str(vm, base, 0, org, sizeof(org));
    char app[256];
    sdl_arg_str(vm, base, 1, app, sizeof(app));
    vigil_vm_stack_pop_n(vm, arg_count);
    char *path = SDL_GetPrefPath(org, app);
    vigil_status_t st = sdl_push_string(vm, path ? path : "", error);
    SDL_free(path);
    return st;
}

/* sdl.get_num_cpu_cores() -> i32 */
static vigil_status_t sdl_fn_get_num_cpu_cores(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetNumLogicalCPUCores(), error);
}

/* sdl.get_system_ram() -> i32 */
static vigil_status_t sdl_fn_get_system_ram(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetSystemRAM(), error);
}

/* sdl.log(string msg) */
static vigil_status_t sdl_fn_log(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char msg[4096];
    sdl_arg_str(vm, base, 0, msg, sizeof(msg));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Log("%s", msg);
    return VIGIL_STATUS_OK;
}

/* sdl.log_error(string msg) */
static vigil_status_t sdl_fn_log_error(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char msg[4096];
    sdl_arg_str(vm, base, 0, msg, sizeof(msg));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", msg);
    return VIGIL_STATUS_OK;
}

/* sdl.log_warn(string msg) */
static vigil_status_t sdl_fn_log_warn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char msg[4096];
    sdl_arg_str(vm, base, 0, msg, sizeof(msg));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", msg);
    return VIGIL_STATUS_OK;
}

/* MessageBox flag constants */
SDL_CONST_FN(MESSAGEBOX_ERROR, SDL_MESSAGEBOX_ERROR)
SDL_CONST_FN(MESSAGEBOX_WARNING, SDL_MESSAGEBOX_WARNING)
SDL_CONST_FN(MESSAGEBOX_INFORMATION, SDL_MESSAGEBOX_INFORMATION)

/* Scancode constants */
SDL_CONST_FN(SCANCODE_A, SDL_SCANCODE_A)
SDL_CONST_FN(SCANCODE_B, SDL_SCANCODE_B)
SDL_CONST_FN(SCANCODE_C, SDL_SCANCODE_C)
SDL_CONST_FN(SCANCODE_D, SDL_SCANCODE_D)
SDL_CONST_FN(SCANCODE_E, SDL_SCANCODE_E)
SDL_CONST_FN(SCANCODE_F, SDL_SCANCODE_F)
SDL_CONST_FN(SCANCODE_G, SDL_SCANCODE_G)
SDL_CONST_FN(SCANCODE_H, SDL_SCANCODE_H)
SDL_CONST_FN(SCANCODE_I, SDL_SCANCODE_I)
SDL_CONST_FN(SCANCODE_J, SDL_SCANCODE_J)
SDL_CONST_FN(SCANCODE_K, SDL_SCANCODE_K)
SDL_CONST_FN(SCANCODE_L, SDL_SCANCODE_L)
SDL_CONST_FN(SCANCODE_M, SDL_SCANCODE_M)
SDL_CONST_FN(SCANCODE_N, SDL_SCANCODE_N)
SDL_CONST_FN(SCANCODE_O, SDL_SCANCODE_O)
SDL_CONST_FN(SCANCODE_P, SDL_SCANCODE_P)
SDL_CONST_FN(SCANCODE_Q, SDL_SCANCODE_Q)
SDL_CONST_FN(SCANCODE_R, SDL_SCANCODE_R)
SDL_CONST_FN(SCANCODE_S, SDL_SCANCODE_S)
SDL_CONST_FN(SCANCODE_T, SDL_SCANCODE_T)
SDL_CONST_FN(SCANCODE_U, SDL_SCANCODE_U)
SDL_CONST_FN(SCANCODE_V, SDL_SCANCODE_V)
SDL_CONST_FN(SCANCODE_W, SDL_SCANCODE_W)
SDL_CONST_FN(SCANCODE_X, SDL_SCANCODE_X)
SDL_CONST_FN(SCANCODE_Y, SDL_SCANCODE_Y)
SDL_CONST_FN(SCANCODE_Z, SDL_SCANCODE_Z)
SDL_CONST_FN(SCANCODE_0, SDL_SCANCODE_0)
SDL_CONST_FN(SCANCODE_1, SDL_SCANCODE_1)
SDL_CONST_FN(SCANCODE_2, SDL_SCANCODE_2)
SDL_CONST_FN(SCANCODE_3, SDL_SCANCODE_3)
SDL_CONST_FN(SCANCODE_4, SDL_SCANCODE_4)
SDL_CONST_FN(SCANCODE_5, SDL_SCANCODE_5)
SDL_CONST_FN(SCANCODE_6, SDL_SCANCODE_6)
SDL_CONST_FN(SCANCODE_7, SDL_SCANCODE_7)
SDL_CONST_FN(SCANCODE_8, SDL_SCANCODE_8)
SDL_CONST_FN(SCANCODE_9, SDL_SCANCODE_9)
SDL_CONST_FN(SCANCODE_RETURN, SDL_SCANCODE_RETURN)
SDL_CONST_FN(SCANCODE_ESCAPE, SDL_SCANCODE_ESCAPE)
SDL_CONST_FN(SCANCODE_BACKSPACE, SDL_SCANCODE_BACKSPACE)
SDL_CONST_FN(SCANCODE_TAB, SDL_SCANCODE_TAB)
SDL_CONST_FN(SCANCODE_SPACE, SDL_SCANCODE_SPACE)
SDL_CONST_FN(SCANCODE_RIGHT, SDL_SCANCODE_RIGHT)
SDL_CONST_FN(SCANCODE_LEFT, SDL_SCANCODE_LEFT)
SDL_CONST_FN(SCANCODE_DOWN, SDL_SCANCODE_DOWN)
SDL_CONST_FN(SCANCODE_UP, SDL_SCANCODE_UP)
SDL_CONST_FN(SCANCODE_DELETE, SDL_SCANCODE_DELETE)
SDL_CONST_FN(SCANCODE_LCTRL, SDL_SCANCODE_LCTRL)
SDL_CONST_FN(SCANCODE_LSHIFT, SDL_SCANCODE_LSHIFT)
SDL_CONST_FN(SCANCODE_LALT, SDL_SCANCODE_LALT)
SDL_CONST_FN(SCANCODE_RCTRL, SDL_SCANCODE_RCTRL)
SDL_CONST_FN(SCANCODE_RSHIFT, SDL_SCANCODE_RSHIFT)
SDL_CONST_FN(SCANCODE_RALT, SDL_SCANCODE_RALT)
SDL_CONST_FN(SCANCODE_F1, SDL_SCANCODE_F1)
SDL_CONST_FN(SCANCODE_F2, SDL_SCANCODE_F2)
SDL_CONST_FN(SCANCODE_F3, SDL_SCANCODE_F3)
SDL_CONST_FN(SCANCODE_F4, SDL_SCANCODE_F4)
SDL_CONST_FN(SCANCODE_F5, SDL_SCANCODE_F5)
SDL_CONST_FN(SCANCODE_F6, SDL_SCANCODE_F6)
SDL_CONST_FN(SCANCODE_F7, SDL_SCANCODE_F7)
SDL_CONST_FN(SCANCODE_F8, SDL_SCANCODE_F8)
SDL_CONST_FN(SCANCODE_F9, SDL_SCANCODE_F9)
SDL_CONST_FN(SCANCODE_F10, SDL_SCANCODE_F10)
SDL_CONST_FN(SCANCODE_F11, SDL_SCANCODE_F11)
SDL_CONST_FN(SCANCODE_F12, SDL_SCANCODE_F12)

/* Keycode constants */
SDL_CONST_FN(KEY_RETURN, SDLK_RETURN)
SDL_CONST_FN(KEY_ESCAPE, SDLK_ESCAPE)
SDL_CONST_FN(KEY_BACKSPACE, SDLK_BACKSPACE)
SDL_CONST_FN(KEY_TAB, SDLK_TAB)
SDL_CONST_FN(KEY_SPACE, SDLK_SPACE)
SDL_CONST_FN(KEY_DELETE, SDLK_DELETE)

/* Mouse button constants */
SDL_CONST_FN(BUTTON_LEFT, SDL_BUTTON_LEFT)
SDL_CONST_FN(BUTTON_MIDDLE, SDL_BUTTON_MIDDLE)
SDL_CONST_FN(BUTTON_RIGHT, SDL_BUTTON_RIGHT)
SDL_CONST_FN(BUTTON_X1, SDL_BUTTON_X1)
SDL_CONST_FN(BUTTON_X2, SDL_BUTTON_X2)

/* Event type constants */
SDL_CONST_FN(EVENT_QUIT, SDL_EVENT_QUIT)
SDL_CONST_FN(EVENT_KEY_DOWN, SDL_EVENT_KEY_DOWN)
SDL_CONST_FN(EVENT_KEY_UP, SDL_EVENT_KEY_UP)
SDL_CONST_FN(EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_MOTION)
SDL_CONST_FN(EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_DOWN)
SDL_CONST_FN(EVENT_MOUSE_BUTTON_UP, SDL_EVENT_MOUSE_BUTTON_UP)
SDL_CONST_FN(EVENT_MOUSE_WHEEL, SDL_EVENT_MOUSE_WHEEL)
SDL_CONST_FN(EVENT_WINDOW_CLOSE_REQUESTED, SDL_EVENT_WINDOW_CLOSE_REQUESTED)
SDL_CONST_FN(EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_RESIZED)

/* ── Slice 6: Textures and Surface Loading ────────────────────────── */

SDL_HANDLE_REGISTRY(surfaces);
SDL_HANDLE_REGISTRY(textures);

enum
{
    SURF_HANDLE = 0,
    SURF_FIELD_COUNT
};

enum
{
    TEX_HANDLE = 0,
    TEX_FIELD_COUNT
};

/* Surface.load(string path) -> (Surface, err) */
static vigil_status_t sdl_surface_load(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Surface *surf = SDL_LoadSurface(path);
    if (!surf)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(surfaces, surf, &handle) < 0)
    {
        SDL_DestroySurface(surf);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* Surface.load_bmp(string path) -> (Surface, err) */
static vigil_status_t sdl_surface_load_bmp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Surface *surf = SDL_LoadBMP(path);
    if (!surf)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(surfaces, surf, &handle) < 0)
    {
        SDL_DestroySurface(surf);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* surf.destroy() */
static vigil_status_t sdl_surface_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *surf = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (surf)
    {
        SDL_DestroySurface(surf);
        SDL_HANDLE_CLEAR(surfaces, h);
    }
    return VIGIL_STATUS_OK;
}

/* Texture.create(Renderer ren, i32 format, i32 access, i32 w, i32 h) -> (Texture, err) */
static vigil_status_t sdl_texture_create(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int64_t rh = sdl_field_i64(vm, base + 1, REN_HANDLE);
    int32_t format = sdl_arg_i32(vm, base, 2);
    int32_t access = sdl_arg_i32(vm, base, 3);
    int32_t w = sdl_arg_i32(vm, base, 4);
    int32_t h = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (!ren)
        return sdl_push_nil_and_err(vm, "invalid renderer handle", SDL_ERR_ARG, error);

    SDL_Texture *tex = SDL_CreateTexture(ren, (SDL_PixelFormat)format, (SDL_TextureAccess)access, w, h);
    if (!tex)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(textures, tex, &handle) < 0)
    {
        SDL_DestroyTexture(tex);
        return sdl_push_nil_and_err(vm, "too many textures", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* Texture.from_surface(Renderer ren, Surface surf) -> (Texture, err) */
static vigil_status_t sdl_texture_from_surface(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int64_t rh = sdl_field_i64(vm, base + 1, REN_HANDLE);
    int64_t sh = sdl_field_i64(vm, base + 2, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (!ren)
        return sdl_push_nil_and_err(vm, "invalid renderer handle", SDL_ERR_ARG, error);
    SDL_Surface *surf = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    if (!surf)
        return sdl_push_nil_and_err(vm, "invalid surface handle", SDL_ERR_ARG, error);

    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    if (!tex)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(textures, tex, &handle) < 0)
    {
        SDL_DestroyTexture(tex);
        return sdl_push_nil_and_err(vm, "too many textures", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* tex.destroy() */
static vigil_status_t sdl_texture_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
    {
        SDL_DestroyTexture(tex);
        SDL_HANDLE_CLEAR(textures, h);
    }
    return VIGIL_STATUS_OK;
}

/* tex.get_size() -> (f64, f64) */
static vigil_status_t sdl_texture_get_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    float w = 0, ht = 0;
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_GetTextureSize(tex, &w, &ht);
    vigil_status_t st = sdl_push_f64(vm, (double)w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)ht, error);
}

/* tex.set_color_mod(i32 r, i32 g, i32 b) */
static vigil_status_t sdl_texture_set_color_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    int32_t r = sdl_arg_i32(vm, base, 1);
    int32_t g = sdl_arg_i32(vm, base, 2);
    int32_t b = sdl_arg_i32(vm, base, 3);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_SetTextureColorMod(tex, (Uint8)r, (Uint8)g, (Uint8)b);
    return VIGIL_STATUS_OK;
}

/* tex.set_alpha_mod(i32 alpha) */
static vigil_status_t sdl_texture_set_alpha_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    int32_t alpha = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_SetTextureAlphaMod(tex, (Uint8)alpha);
    return VIGIL_STATUS_OK;
}

/* tex.set_blend_mode(i32 mode) */
static vigil_status_t sdl_texture_set_blend_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    int32_t mode = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_SetTextureBlendMode(tex, (SDL_BlendMode)mode);
    return VIGIL_STATUS_OK;
}

/* ren.render_texture(Texture tex, f64 sx, f64 sy, f64 sw, f64 sh,
 *                    f64 dx, f64 dy, f64 dw, f64 dh) -> (bool, err)
 * Pass all zeros for src to use the full texture. */
static vigil_status_t sdl_renderer_render_texture(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_field_i64(vm, base + 1, TEX_HANDLE);
    float sx = (float)sdl_arg_f64(vm, base, 2);
    float sy = (float)sdl_arg_f64(vm, base, 3);
    float sw = (float)sdl_arg_f64(vm, base, 4);
    float sh = (float)sdl_arg_f64(vm, base, 5);
    float dx = (float)sdl_arg_f64(vm, base, 6);
    float dy = (float)sdl_arg_f64(vm, base, 7);
    float dw = (float)sdl_arg_f64(vm, base, 8);
    float dh = (float)sdl_arg_f64(vm, base, 9);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (!ren || !tex)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);

    SDL_FRect src = {sx, sy, sw, sh};
    SDL_FRect dst = {dx, dy, dw, dh};
    int use_src = (sw > 0.0f || sh > 0.0f);
    int use_dst = (dw > 0.0f || dh > 0.0f);

    if (SDL_RenderTexture(ren, tex, use_src ? &src : NULL, use_dst ? &dst : NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.render_texture_rotated(Texture tex, f64 sx, f64 sy, f64 sw, f64 sh,
 *     f64 dx, f64 dy, f64 dw, f64 dh, f64 angle, f64 cx, f64 cy, i32 flip)
 *     -> (bool, err) */
static vigil_status_t sdl_renderer_render_texture_rotated(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_field_i64(vm, base + 1, TEX_HANDLE);
    float sx = (float)sdl_arg_f64(vm, base, 2);
    float sy = (float)sdl_arg_f64(vm, base, 3);
    float sw = (float)sdl_arg_f64(vm, base, 4);
    float sh = (float)sdl_arg_f64(vm, base, 5);
    float dx = (float)sdl_arg_f64(vm, base, 6);
    float dy = (float)sdl_arg_f64(vm, base, 7);
    float dw = (float)sdl_arg_f64(vm, base, 8);
    float dh = (float)sdl_arg_f64(vm, base, 9);
    double angle = sdl_arg_f64(vm, base, 10);
    float cx = (float)sdl_arg_f64(vm, base, 11);
    float cy = (float)sdl_arg_f64(vm, base, 12);
    int32_t flip = sdl_arg_i32(vm, base, 13);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (!ren || !tex)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);

    SDL_FRect src = {sx, sy, sw, sh};
    SDL_FRect dst = {dx, dy, dw, dh};
    SDL_FPoint center = {cx, cy};
    int use_src = (sw > 0.0f || sh > 0.0f);
    int use_dst = (dw > 0.0f || dh > 0.0f);

    if (SDL_RenderTextureRotated(ren, tex, use_src ? &src : NULL, use_dst ? &dst : NULL, angle, &center,
                                 (SDL_FlipMode)flip))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 11: Renderer Extras & Display Info ─────────────────────── */

/* ren.set_target(Texture tex) -> (bool, err) — pass nil/0-handle for default */
static vigil_status_t sdl_renderer_set_target(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (!ren)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Texture *tex = (th >= 0) ? (SDL_Texture *)SDL_HANDLE_GET(textures, th) : NULL;
    if (SDL_SetRenderTarget(ren, tex))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.set_scale(f64 sx, f64 sy) -> (bool, err) */
static vigil_status_t sdl_renderer_set_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    float sx = (float)sdl_arg_f64(vm, base, 1);
    float sy = (float)sdl_arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_SetRenderScale(ren, sx, sy))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.get_scale() -> (f64, f64) */
static vigil_status_t sdl_renderer_get_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    float sx = 1.0f, sy = 1.0f;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderScale(ren, &sx, &sy);
    vigil_status_t st = sdl_push_f64(vm, (double)sx, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)sy, error);
}

/* ren.set_clip_rect(i32 x, i32 y, i32 w, i32 h) -> (bool, err) — all zeros to clear */
static vigil_status_t sdl_renderer_set_clip_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1);
    int32_t y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3);
    int32_t h = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (!ren)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    if (w == 0 && h == 0)
    {
        if (SDL_SetRenderClipRect(ren, NULL))
            return sdl_push_bool_ok(vm, error);
    }
    else
    {
        SDL_Rect rect = {x, y, w, h};
        if (SDL_SetRenderClipRect(ren, &rect))
            return sdl_push_bool_ok(vm, error);
    }
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.set_logical_size(i32 w, i32 h, i32 mode) -> (bool, err) */
static vigil_status_t sdl_renderer_set_logical_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int32_t w = sdl_arg_i32(vm, base, 1);
    int32_t h = sdl_arg_i32(vm, base, 2);
    int32_t mode = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_SetRenderLogicalPresentation(ren, w, h, (SDL_RendererLogicalPresentation)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Display info ─────────────────────────────────────────────────── */

static SDL_DisplayID *g_display_ids = NULL;
static int g_display_count = 0;

static void sdl_refresh_display_list(void)
{
    if (g_display_ids)
    {
        SDL_free(g_display_ids);
        g_display_ids = NULL;
    }
    g_display_ids = SDL_GetDisplays(&g_display_count);
}

/* sdl.get_display_count() -> i32 */
static vigil_status_t sdl_fn_get_display_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    sdl_refresh_display_list();
    return sdl_push_i32(vm, (int32_t)g_display_count, error);
}

/* sdl.get_display_name(i32 index) -> string */
static vigil_status_t sdl_fn_get_display_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_display_count && g_display_ids)
        return sdl_push_string(vm, SDL_GetDisplayName(g_display_ids[idx]), error);
    return sdl_push_string(vm, "", error);
}

/* sdl.get_display_bounds(i32 index) -> (i32, i32, i32, i32) — x, y, w, h
 * 2-return limit: return (i32 w, i32 h) only. */
static vigil_status_t sdl_fn_get_display_bounds(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect rect = {0, 0, 0, 0};
    if (idx >= 0 && idx < g_display_count && g_display_ids)
        SDL_GetDisplayBounds(g_display_ids[idx], &rect);
    vigil_status_t st = sdl_push_i32(vm, rect.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, rect.h, error);
}

/* Logical presentation mode constants */
SDL_CONST_FN(LOGICAL_DISABLED, SDL_LOGICAL_PRESENTATION_DISABLED)
SDL_CONST_FN(LOGICAL_STRETCH, SDL_LOGICAL_PRESENTATION_STRETCH)
SDL_CONST_FN(LOGICAL_LETTERBOX, SDL_LOGICAL_PRESENTATION_LETTERBOX)
SDL_CONST_FN(LOGICAL_OVERSCAN, SDL_LOGICAL_PRESENTATION_OVERSCAN)
SDL_CONST_FN(LOGICAL_INTEGER_SCALE, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)

/* Texture access constants */
SDL_CONST_FN(TEXTUREACCESS_STATIC, SDL_TEXTUREACCESS_STATIC)
SDL_CONST_FN(TEXTUREACCESS_STREAMING, SDL_TEXTUREACCESS_STREAMING)
SDL_CONST_FN(TEXTUREACCESS_TARGET, SDL_TEXTUREACCESS_TARGET)

/* Blend mode constants */
SDL_CONST_FN(BLENDMODE_NONE, SDL_BLENDMODE_NONE)
SDL_CONST_FN(BLENDMODE_BLEND, SDL_BLENDMODE_BLEND)
SDL_CONST_FN(BLENDMODE_ADD, SDL_BLENDMODE_ADD)
SDL_CONST_FN(BLENDMODE_MOD, SDL_BLENDMODE_MOD)
SDL_CONST_FN(BLENDMODE_MUL, SDL_BLENDMODE_MUL)

/* Flip mode constants */
SDL_CONST_FN(FLIP_NONE, SDL_FLIP_NONE)
SDL_CONST_FN(FLIP_HORIZONTAL, SDL_FLIP_HORIZONTAL)
SDL_CONST_FN(FLIP_VERTICAL, SDL_FLIP_VERTICAL)

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
    /* Event type constants */
    SDL_CONST_ENTRY("EVENT_QUIT", EVENT_QUIT),
    SDL_CONST_ENTRY("EVENT_KEY_DOWN", EVENT_KEY_DOWN),
    SDL_CONST_ENTRY("EVENT_KEY_UP", EVENT_KEY_UP),
    SDL_CONST_ENTRY("EVENT_MOUSE_MOTION", EVENT_MOUSE_MOTION),
    SDL_CONST_ENTRY("EVENT_MOUSE_BUTTON_DOWN", EVENT_MOUSE_BUTTON_DOWN),
    SDL_CONST_ENTRY("EVENT_MOUSE_BUTTON_UP", EVENT_MOUSE_BUTTON_UP),
    SDL_CONST_ENTRY("EVENT_MOUSE_WHEEL", EVENT_MOUSE_WHEEL),
    SDL_CONST_ENTRY("EVENT_WINDOW_CLOSE_REQUESTED", EVENT_WINDOW_CLOSE_REQUESTED),
    SDL_CONST_ENTRY("EVENT_WINDOW_RESIZED", EVENT_WINDOW_RESIZED),
    /* Keyboard / mouse queries (slice 5) */
    SDL_FN("is_key_pressed", 14U, sdl_fn_is_key_pressed, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("get_mod_state", 13U, sdl_fn_get_mod_state, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_key_name", 12U, sdl_fn_get_key_name, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_scancode_name", 17U, sdl_fn_get_scancode_name, 1U, p_i32, VIGIL_TYPE_STRING),
    {"get_mouse_state", 15U, sdl_fn_get_mouse_state, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64, 0, NULL, NULL, 0},
    SDL_FN("get_mouse_buttons", 17U, sdl_fn_get_mouse_buttons, 0U, NULL, VIGIL_TYPE_I32),
    {"get_global_mouse_state", 22U, sdl_fn_get_global_mouse_state, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64, 0, NULL,
     NULL, 0},
    SDL_FN_VOID("warp_mouse", 10U, sdl_fn_warp_mouse, 3U, p_obj_f64_f64),
    SDL_FN("show_cursor", 11U, sdl_fn_show_cursor, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("hide_cursor", 11U, sdl_fn_hide_cursor, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("cursor_visible", 14U, sdl_fn_cursor_visible, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("delay", 5U, sdl_fn_delay, 1U, p_i32),
    /* Timer / timing (slice 7) */
    SDL_FN("get_ticks", 9U, sdl_fn_get_ticks, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN("get_ticks_ns", 12U, sdl_fn_get_ticks_ns, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN("get_performance_counter", 23U, sdl_fn_get_performance_counter, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN("get_performance_frequency", 25U, sdl_fn_get_performance_frequency, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN_VOID("delay_ns", 8U, sdl_fn_delay_ns, 1U, p_i64),
    SDL_FN_VOID("delay_precise", 13U, sdl_fn_delay_precise, 1U, p_i64),
    /* Audio (slice 8) */
    {"load_wav", 8U, sdl_fn_load_wav, 1U, p_str, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0},
    SDL_FN_VOID("wav_free", 8U, sdl_fn_wav_free, 1U, p_i64),
    SDL_FN("wav_format", 10U, sdl_fn_wav_format, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("wav_channels", 12U, sdl_fn_wav_channels, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("wav_freq", 8U, sdl_fn_wav_freq, 1U, p_i64, VIGIL_TYPE_I32),
    /* Scancode constants */
    SDL_CONST_ENTRY("SCANCODE_A", SCANCODE_A),
    SDL_CONST_ENTRY("SCANCODE_B", SCANCODE_B),
    SDL_CONST_ENTRY("SCANCODE_C", SCANCODE_C),
    SDL_CONST_ENTRY("SCANCODE_D", SCANCODE_D),
    SDL_CONST_ENTRY("SCANCODE_E", SCANCODE_E),
    SDL_CONST_ENTRY("SCANCODE_F", SCANCODE_F),
    SDL_CONST_ENTRY("SCANCODE_G", SCANCODE_G),
    SDL_CONST_ENTRY("SCANCODE_H", SCANCODE_H),
    SDL_CONST_ENTRY("SCANCODE_I", SCANCODE_I),
    SDL_CONST_ENTRY("SCANCODE_J", SCANCODE_J),
    SDL_CONST_ENTRY("SCANCODE_K", SCANCODE_K),
    SDL_CONST_ENTRY("SCANCODE_L", SCANCODE_L),
    SDL_CONST_ENTRY("SCANCODE_M", SCANCODE_M),
    SDL_CONST_ENTRY("SCANCODE_N", SCANCODE_N),
    SDL_CONST_ENTRY("SCANCODE_O", SCANCODE_O),
    SDL_CONST_ENTRY("SCANCODE_P", SCANCODE_P),
    SDL_CONST_ENTRY("SCANCODE_Q", SCANCODE_Q),
    SDL_CONST_ENTRY("SCANCODE_R", SCANCODE_R),
    SDL_CONST_ENTRY("SCANCODE_S", SCANCODE_S),
    SDL_CONST_ENTRY("SCANCODE_T", SCANCODE_T),
    SDL_CONST_ENTRY("SCANCODE_U", SCANCODE_U),
    SDL_CONST_ENTRY("SCANCODE_V", SCANCODE_V),
    SDL_CONST_ENTRY("SCANCODE_W", SCANCODE_W),
    SDL_CONST_ENTRY("SCANCODE_X", SCANCODE_X),
    SDL_CONST_ENTRY("SCANCODE_Y", SCANCODE_Y),
    SDL_CONST_ENTRY("SCANCODE_Z", SCANCODE_Z),
    SDL_CONST_ENTRY("SCANCODE_0", SCANCODE_0),
    SDL_CONST_ENTRY("SCANCODE_1", SCANCODE_1),
    SDL_CONST_ENTRY("SCANCODE_2", SCANCODE_2),
    SDL_CONST_ENTRY("SCANCODE_3", SCANCODE_3),
    SDL_CONST_ENTRY("SCANCODE_4", SCANCODE_4),
    SDL_CONST_ENTRY("SCANCODE_5", SCANCODE_5),
    SDL_CONST_ENTRY("SCANCODE_6", SCANCODE_6),
    SDL_CONST_ENTRY("SCANCODE_7", SCANCODE_7),
    SDL_CONST_ENTRY("SCANCODE_8", SCANCODE_8),
    SDL_CONST_ENTRY("SCANCODE_9", SCANCODE_9),
    SDL_CONST_ENTRY("SCANCODE_RETURN", SCANCODE_RETURN),
    SDL_CONST_ENTRY("SCANCODE_ESCAPE", SCANCODE_ESCAPE),
    SDL_CONST_ENTRY("SCANCODE_BACKSPACE", SCANCODE_BACKSPACE),
    SDL_CONST_ENTRY("SCANCODE_TAB", SCANCODE_TAB),
    SDL_CONST_ENTRY("SCANCODE_SPACE", SCANCODE_SPACE),
    SDL_CONST_ENTRY("SCANCODE_RIGHT", SCANCODE_RIGHT),
    SDL_CONST_ENTRY("SCANCODE_LEFT", SCANCODE_LEFT),
    SDL_CONST_ENTRY("SCANCODE_DOWN", SCANCODE_DOWN),
    SDL_CONST_ENTRY("SCANCODE_UP", SCANCODE_UP),
    SDL_CONST_ENTRY("SCANCODE_DELETE", SCANCODE_DELETE),
    SDL_CONST_ENTRY("SCANCODE_LCTRL", SCANCODE_LCTRL),
    SDL_CONST_ENTRY("SCANCODE_LSHIFT", SCANCODE_LSHIFT),
    SDL_CONST_ENTRY("SCANCODE_LALT", SCANCODE_LALT),
    SDL_CONST_ENTRY("SCANCODE_RCTRL", SCANCODE_RCTRL),
    SDL_CONST_ENTRY("SCANCODE_RSHIFT", SCANCODE_RSHIFT),
    SDL_CONST_ENTRY("SCANCODE_RALT", SCANCODE_RALT),
    SDL_CONST_ENTRY("SCANCODE_F1", SCANCODE_F1),
    SDL_CONST_ENTRY("SCANCODE_F2", SCANCODE_F2),
    SDL_CONST_ENTRY("SCANCODE_F3", SCANCODE_F3),
    SDL_CONST_ENTRY("SCANCODE_F4", SCANCODE_F4),
    SDL_CONST_ENTRY("SCANCODE_F5", SCANCODE_F5),
    SDL_CONST_ENTRY("SCANCODE_F6", SCANCODE_F6),
    SDL_CONST_ENTRY("SCANCODE_F7", SCANCODE_F7),
    SDL_CONST_ENTRY("SCANCODE_F8", SCANCODE_F8),
    SDL_CONST_ENTRY("SCANCODE_F9", SCANCODE_F9),
    SDL_CONST_ENTRY("SCANCODE_F10", SCANCODE_F10),
    SDL_CONST_ENTRY("SCANCODE_F11", SCANCODE_F11),
    SDL_CONST_ENTRY("SCANCODE_F12", SCANCODE_F12),
    /* Keycode constants */
    SDL_CONST_ENTRY("KEY_RETURN", KEY_RETURN),
    SDL_CONST_ENTRY("KEY_ESCAPE", KEY_ESCAPE),
    SDL_CONST_ENTRY("KEY_BACKSPACE", KEY_BACKSPACE),
    SDL_CONST_ENTRY("KEY_TAB", KEY_TAB),
    SDL_CONST_ENTRY("KEY_SPACE", KEY_SPACE),
    SDL_CONST_ENTRY("KEY_DELETE", KEY_DELETE),
    /* Mouse button constants */
    SDL_CONST_ENTRY("BUTTON_LEFT", BUTTON_LEFT),
    SDL_CONST_ENTRY("BUTTON_MIDDLE", BUTTON_MIDDLE),
    SDL_CONST_ENTRY("BUTTON_RIGHT", BUTTON_RIGHT),
    SDL_CONST_ENTRY("BUTTON_X1", BUTTON_X1),
    SDL_CONST_ENTRY("BUTTON_X2", BUTTON_X2),
    /* Texture access constants */
    SDL_CONST_ENTRY("TEXTUREACCESS_STATIC", TEXTUREACCESS_STATIC),
    SDL_CONST_ENTRY("TEXTUREACCESS_STREAMING", TEXTUREACCESS_STREAMING),
    SDL_CONST_ENTRY("TEXTUREACCESS_TARGET", TEXTUREACCESS_TARGET),
    /* Blend mode constants */
    SDL_CONST_ENTRY("BLENDMODE_NONE", BLENDMODE_NONE),
    SDL_CONST_ENTRY("BLENDMODE_BLEND", BLENDMODE_BLEND),
    SDL_CONST_ENTRY("BLENDMODE_ADD", BLENDMODE_ADD),
    SDL_CONST_ENTRY("BLENDMODE_MOD", BLENDMODE_MOD),
    SDL_CONST_ENTRY("BLENDMODE_MUL", BLENDMODE_MUL),
    /* Flip mode constants */
    SDL_CONST_ENTRY("FLIP_NONE", FLIP_NONE),
    SDL_CONST_ENTRY("FLIP_HORIZONTAL", FLIP_HORIZONTAL),
    SDL_CONST_ENTRY("FLIP_VERTICAL", FLIP_VERTICAL),
    /* Audio format constants */
    SDL_CONST_ENTRY("AUDIO_S16", AUDIO_S16),
    SDL_CONST_ENTRY("AUDIO_S32", AUDIO_S32),
    SDL_CONST_ENTRY("AUDIO_F32", AUDIO_F32),
    /* Gamepad (slice 9) */
    SDL_FN("has_gamepad", 11U, sdl_fn_has_gamepad, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("get_gamepad_count", 17U, sdl_fn_get_gamepad_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_id", 14U, sdl_fn_get_gamepad_id, 1U, p_i32, VIGIL_TYPE_I32),
    /* Gamepad axis/button constants */
    SDL_CONST_ENTRY("GAMEPAD_AXIS_LEFTX", GAMEPAD_AXIS_LEFTX),
    SDL_CONST_ENTRY("GAMEPAD_AXIS_LEFTY", GAMEPAD_AXIS_LEFTY),
    SDL_CONST_ENTRY("GAMEPAD_AXIS_RIGHTX", GAMEPAD_AXIS_RIGHTX),
    SDL_CONST_ENTRY("GAMEPAD_AXIS_RIGHTY", GAMEPAD_AXIS_RIGHTY),
    SDL_CONST_ENTRY("GAMEPAD_AXIS_LEFT_TRIGGER", GAMEPAD_AXIS_LEFT_TRIGGER),
    SDL_CONST_ENTRY("GAMEPAD_AXIS_RIGHT_TRIGGER", GAMEPAD_AXIS_RIGHT_TRIGGER),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_SOUTH", GAMEPAD_BUTTON_SOUTH),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_EAST", GAMEPAD_BUTTON_EAST),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_WEST", GAMEPAD_BUTTON_WEST),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_NORTH", GAMEPAD_BUTTON_NORTH),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_BACK", GAMEPAD_BUTTON_BACK),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_GUIDE", GAMEPAD_BUTTON_GUIDE),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_START", GAMEPAD_BUTTON_START),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_LEFT_STICK", GAMEPAD_BUTTON_LEFT_STICK),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_RIGHT_STICK", GAMEPAD_BUTTON_RIGHT_STICK),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_LEFT_SHOULDER", GAMEPAD_BUTTON_LEFT_SHOULDER),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_RIGHT_SHOULDER", GAMEPAD_BUTTON_RIGHT_SHOULDER),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_DPAD_UP", GAMEPAD_BUTTON_DPAD_UP),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_DPAD_DOWN", GAMEPAD_BUTTON_DPAD_DOWN),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_DPAD_LEFT", GAMEPAD_BUTTON_DPAD_LEFT),
    SDL_CONST_ENTRY("GAMEPAD_BUTTON_DPAD_RIGHT", GAMEPAD_BUTTON_DPAD_RIGHT),
    /* Gamepad event type constants */
    SDL_CONST_ENTRY("EVENT_GAMEPAD_AXIS_MOTION", EVENT_GAMEPAD_AXIS_MOTION),
    SDL_CONST_ENTRY("EVENT_GAMEPAD_BUTTON_DOWN", EVENT_GAMEPAD_BUTTON_DOWN),
    SDL_CONST_ENTRY("EVENT_GAMEPAD_BUTTON_UP", EVENT_GAMEPAD_BUTTON_UP),
    SDL_CONST_ENTRY("EVENT_GAMEPAD_ADDED", EVENT_GAMEPAD_ADDED),
    SDL_CONST_ENTRY("EVENT_GAMEPAD_REMOVED", EVENT_GAMEPAD_REMOVED),
    /* Clipboard, MessageBox, Misc (slice 10) */
    SDL_FN_BOOL_ERR("set_clipboard_text", 18U, sdl_fn_set_clipboard_text, 1U, p_str),
    SDL_FN("get_clipboard_text", 18U, sdl_fn_get_clipboard_text, 0U, NULL, VIGIL_TYPE_STRING),
    SDL_FN("has_clipboard_text", 18U, sdl_fn_has_clipboard_text, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN_BOOL_ERR("show_simple_message_box", 23U, sdl_fn_show_simple_message_box, 3U, p_i32_str_str),
    SDL_FN_BOOL_ERR("open_url", 8U, sdl_fn_open_url, 1U, p_str),
    SDL_FN("get_base_path", 13U, sdl_fn_get_base_path, 0U, NULL, VIGIL_TYPE_STRING),
    {"get_pref_path", 13U, sdl_fn_get_pref_path, 2U, p_str_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0},
    SDL_FN("get_num_cpu_cores", 17U, sdl_fn_get_num_cpu_cores, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_system_ram", 14U, sdl_fn_get_system_ram, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN_VOID("log", 3U, sdl_fn_log, 1U, p_str),
    SDL_FN_VOID("log_error", 9U, sdl_fn_log_error, 1U, p_str),
    SDL_FN_VOID("log_warn", 8U, sdl_fn_log_warn, 1U, p_str),
    /* MessageBox flag constants */
    SDL_CONST_ENTRY("MESSAGEBOX_ERROR", MESSAGEBOX_ERROR),
    SDL_CONST_ENTRY("MESSAGEBOX_WARNING", MESSAGEBOX_WARNING),
    SDL_CONST_ENTRY("MESSAGEBOX_INFORMATION", MESSAGEBOX_INFORMATION),
    /* Display info (slice 11) */
    SDL_FN("get_display_count", 17U, sdl_fn_get_display_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_display_name", 16U, sdl_fn_get_display_name, 1U, p_i32, VIGIL_TYPE_STRING),
    {"get_display_bounds", 18U, sdl_fn_get_display_bounds, 1U, p_i32, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL, NULL, 0},
    /* Logical presentation mode constants */
    SDL_CONST_ENTRY("LOGICAL_DISABLED", LOGICAL_DISABLED),
    SDL_CONST_ENTRY("LOGICAL_STRETCH", LOGICAL_STRETCH),
    SDL_CONST_ENTRY("LOGICAL_LETTERBOX", LOGICAL_LETTERBOX),
    SDL_CONST_ENTRY("LOGICAL_OVERSCAN", LOGICAL_OVERSCAN),
    SDL_CONST_ENTRY("LOGICAL_INTEGER_SCALE", LOGICAL_INTEGER_SCALE),
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

/* ── Renderer class descriptor ───────────────────────────────────── */

static const vigil_native_class_field_t sdl_renderer_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_renderer_methods[] = {
    SDL_STATIC("create", 6U, sdl_renderer_create, 2U, p_obj_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("destroy", 7U, sdl_renderer_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("clear", 5U, sdl_renderer_clear, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("present", 7U, sdl_renderer_present, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_draw_color", 14U, sdl_renderer_set_draw_color, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_draw_color", 14U, sdl_renderer_get_draw_color, 0U, NULL, VIGIL_TYPE_I32, 4U, rt_i32_i32_i32_i32),
    SDL_METHOD("draw_point", 10U, sdl_renderer_draw_point, 2U, p_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("draw_line", 9U, sdl_renderer_draw_line, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("draw_rect", 9U, sdl_renderer_draw_rect, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("fill_rect", 9U, sdl_renderer_fill_rect, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_vsync", 9U, sdl_renderer_set_vsync, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("render_texture", 14U, sdl_renderer_render_texture, 9U, p_obj_f64x8, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("render_texture_rotated", 22U, sdl_renderer_render_texture_rotated, 13U, p_obj_f64x11_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_target", 10U, sdl_renderer_set_target, 1U, p_i64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_scale", 9U, sdl_renderer_set_scale, 2U, p_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_scale", 9U, sdl_renderer_get_scale, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64),
    SDL_METHOD("set_clip_rect", 13U, sdl_renderer_set_clip_rect, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_logical_size", 16U, sdl_renderer_set_logical_size, 3U, p_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
};

/* ── Event class descriptor ──────────────────────────────────────── */

static const vigil_native_class_field_t sdl_event_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_event_methods[] = {
    SDL_STATIC("new", 3U, sdl_event_new, 0U, NULL, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("poll", 4U, sdl_event_poll, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("wait", 4U, sdl_event_wait, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("wait_timeout", 12U, sdl_event_wait_timeout, 1U, p_i32, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("type", 4U, sdl_event_type, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("key_scancode", 12U, sdl_event_key_scancode, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("key_keycode", 11U, sdl_event_key_keycode, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("key_mod", 7U, sdl_event_key_mod, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("key_repeat", 10U, sdl_event_key_repeat, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("mouse_x", 7U, sdl_event_mouse_x, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("mouse_y", 7U, sdl_event_mouse_y, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("mouse_button", 12U, sdl_event_mouse_button, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("wheel_x", 7U, sdl_event_wheel_x, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("wheel_y", 7U, sdl_event_wheel_y, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("gamepad_which", 13U, sdl_event_gamepad_which, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("gamepad_axis", 12U, sdl_event_gamepad_axis, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("gamepad_axis_value", 18U, sdl_event_gamepad_axis_value, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("gamepad_button", 14U, sdl_event_gamepad_button, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
};

/* ── Surface class descriptor ────────────────────────────────────── */

static const vigil_native_class_field_t sdl_surface_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_surface_methods[] = {
    SDL_STATIC("load", 4U, sdl_surface_load, 1U, p_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_STATIC("load_bmp", 8U, sdl_surface_load_bmp, 1U, p_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("destroy", 7U, sdl_surface_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Texture class descriptor ────────────────────────────────────── */

static const vigil_native_class_field_t sdl_texture_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_texture_methods[] = {
    SDL_STATIC("create", 6U, sdl_texture_create, 5U, p_obj_i32_i32_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_STATIC("from_surface", 12U, sdl_texture_from_surface, 2U, p_obj_obj, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("destroy", 7U, sdl_texture_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("get_size", 8U, sdl_texture_get_size, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64),
    SDL_METHOD("set_color_mod", 13U, sdl_texture_set_color_mod, 3U, p_i32_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("set_alpha_mod", 13U, sdl_texture_set_alpha_mod, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("set_blend_mode", 14U, sdl_texture_set_blend_mode, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── AudioStream class descriptor ────────────────────────────────── */

static const vigil_native_class_field_t sdl_audio_stream_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_audio_stream_methods[] = {
    SDL_STATIC("open", 4U, sdl_audio_stream_open, 3U, p_i32_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("destroy", 7U, sdl_audio_stream_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("put_wav", 7U, sdl_audio_stream_put_wav, 1U, p_i64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_queued", 10U, sdl_audio_stream_get_queued, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("resume", 6U, sdl_audio_stream_resume, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("pause", 5U, sdl_audio_stream_pause, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
};

/* ── Gamepad class descriptor ────────────────────────────────────── */

static const vigil_native_class_field_t sdl_gamepad_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_gamepad_methods[] = {
    SDL_STATIC("open", 4U, sdl_gamepad_open, 1U, p_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("close", 5U, sdl_gamepad_close, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("get_name", 8U, sdl_gamepad_get_name, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("get_axis", 8U, sdl_gamepad_get_axis, 1U, p_i32, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_button", 10U, sdl_gamepad_get_button, 1U, p_i32, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("rumble", 6U, sdl_gamepad_rumble, 3U, p_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
};

static const vigil_native_class_t sdl_classes[] = {
    {"Window", 6U, sdl_window_fields, WIN_FIELD_COUNT, sdl_window_methods,
     sizeof(sdl_window_methods) / sizeof(sdl_window_methods[0]), NULL},
    {"Renderer", 8U, sdl_renderer_fields, REN_FIELD_COUNT, sdl_renderer_methods,
     sizeof(sdl_renderer_methods) / sizeof(sdl_renderer_methods[0]), NULL},
    {"Event", 5U, sdl_event_fields, EVT_FIELD_COUNT, sdl_event_methods,
     sizeof(sdl_event_methods) / sizeof(sdl_event_methods[0]), NULL},
    {"Surface", 7U, sdl_surface_fields, SURF_FIELD_COUNT, sdl_surface_methods,
     sizeof(sdl_surface_methods) / sizeof(sdl_surface_methods[0]), NULL},
    {"Texture", 7U, sdl_texture_fields, TEX_FIELD_COUNT, sdl_texture_methods,
     sizeof(sdl_texture_methods) / sizeof(sdl_texture_methods[0]), NULL},
    {"AudioStream", 11U, sdl_audio_stream_fields, ASTREAM_FIELD_COUNT, sdl_audio_stream_methods,
     sizeof(sdl_audio_stream_methods) / sizeof(sdl_audio_stream_methods[0]), NULL},
    {"Gamepad", 7U, sdl_gamepad_fields, GP_FIELD_COUNT, sdl_gamepad_methods,
     sizeof(sdl_gamepad_methods) / sizeof(sdl_gamepad_methods[0]), NULL},
};
/* clang-format on */

#define SDL_CLASS_COUNT (sizeof(sdl_classes) / sizeof(sdl_classes[0]))

/* ── Module export ───────────────────────────────────────────────── */

VIGIL_API const vigil_native_module_t vigil_plugin_sdl = {
    "sdl", 3U, sdl_functions, SDL_FUNCTION_COUNT, sdl_classes, SDL_CLASS_COUNT};
