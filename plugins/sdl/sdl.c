/* Vigil plugin: sdl
 *
 * SDL3 bindings for Vigil.
 * See: https://github.com/bluesentinelsec/vigil/issues/307
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_vulkan.h>

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

static vigil_status_t sdl_push_joined_lines(vigil_vm_t *vm, char **items, int count, vigil_error_t *error)
{
    char *joined = NULL;
    size_t capacity = 0;
    size_t length = 0;
    vigil_status_t st = VIGIL_STATUS_OK;

    if (!items || count <= 0)
        return sdl_push_string(vm, "", error);

    for (int i = 0; i < count; i++)
    {
        const char *item = items[i] ? items[i] : "";
        size_t item_len = strlen(item);
        size_t needed = length + item_len + (i > 0 ? 1U : 0U) + 1U;
        if (needed > capacity)
        {
            size_t new_capacity = capacity == 0 ? 256U : capacity;
            while (new_capacity < needed)
                new_capacity *= 2U;
            char *next = (char *)realloc(joined, new_capacity);
            if (!next)
            {
                free(joined);
                vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }
            joined = next;
            capacity = new_capacity;
        }
        if (i > 0)
            joined[length++] = '\n';
        memcpy(joined + length, item, item_len);
        length += item_len;
    }

    if (!joined)
        return sdl_push_string(vm, "", error);

    joined[length] = '\0';
    st = sdl_push_string(vm, joined, error);
    free(joined);
    return st;
}

static int sdl_append_bytes(char **buffer, size_t *length, size_t *capacity, const char *text, size_t text_len)
{
    size_t needed = *length + text_len + 1U;
    if (needed > *capacity)
    {
        size_t new_capacity = *capacity == 0 ? 256U : *capacity;
        while (new_capacity < needed)
            new_capacity *= 2U;
        char *next = (char *)realloc(*buffer, new_capacity);
        if (!next)
            return -1;
        *buffer = next;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, text, text_len);
    *length += text_len;
    (*buffer)[*length] = '\0';
    return 0;
}

static int sdl_append_cstr(char **buffer, size_t *length, size_t *capacity, const char *text)
{
    return sdl_append_bytes(buffer, length, capacity, text, strlen(text));
}

static int sdl_append_format(char **buffer, size_t *length, size_t *capacity, const char *fmt, ...)
{
    va_list args;
    va_list copy;
    int required = 0;

    va_start(args, fmt);
    va_copy(copy, args);
    required = SDL_vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (required < 0)
    {
        va_end(args);
        return -1;
    }

    size_t needed = *length + (size_t)required + 1U;
    if (needed > *capacity)
    {
        size_t new_capacity = *capacity == 0 ? 256U : *capacity;
        while (new_capacity < needed)
            new_capacity *= 2U;
        char *next = (char *)realloc(*buffer, new_capacity);
        if (!next)
        {
            va_end(args);
            return -1;
        }
        *buffer = next;
        *capacity = new_capacity;
    }

    SDL_vsnprintf(*buffer + *length, *capacity - *length, fmt, args);
    *length += (size_t)required;
    va_end(args);
    return 0;
}

typedef struct
{
    char *mime_type;
    uint8_t *data;
    size_t size;
} sdl_clipboard_payload_t;

static const void *SDLCALL sdl_clipboard_data_callback(void *userdata, const char *mime_type, size_t *size)
{
    sdl_clipboard_payload_t *payload = (sdl_clipboard_payload_t *)userdata;
    if (!payload || !mime_type || strcmp(payload->mime_type, mime_type) != 0)
    {
        if (size)
            *size = 0;
        return NULL;
    }
    if (size)
        *size = payload->size;
    return payload->data;
}

static void SDLCALL sdl_clipboard_cleanup(void *userdata)
{
    sdl_clipboard_payload_t *payload = (sdl_clipboard_payload_t *)userdata;
    if (!payload)
        return;
    free(payload->mime_type);
    free(payload->data);
    free(payload);
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

/* Class indexes must stay aligned with the sdl_classes[] table. */
enum
{
    SDL_WINDOW_CLASS_INDEX = 0U,
    SDL_RENDERER_CLASS_INDEX = 1U,
    SDL_EVENT_CLASS_INDEX = 2U,
    SDL_SURFACE_CLASS_INDEX = 3U,
    SDL_TEXTURE_CLASS_INDEX = 4U,
    SDL_AUDIO_STREAM_CLASS_INDEX = 5U,
    SDL_GAMEPAD_CLASS_INDEX = 6U,
    SDL_JOYSTICK_CLASS_INDEX = 7U,
    SDL_HAPTIC_CLASS_INDEX = 8U,
    SDL_CAMERA_CLASS_INDEX = 9U,
};

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
    {                                                                                                                  \
        vname, sizeof(vname) - 1U, sdl_const_##cname, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL,  \
            NULL, NULL, NULL                                                                                           \
    }

/* ── Native class helper macros ──────────────────────────────────── */

#define SDL_PFIELD(n, nl, t)                                                                                           \
    {                                                                                                                  \
        n, nl, t, 0, NULL, 0U, 0, NULL, NULL                                                                          \
    }

#define SDL_METHOD(n, nl, fn, pc, pt, rt, rc, rts)                                                                    \
    {                                                                                                                  \
        n, nl, fn, pc, pt, rt, rc, rts, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL                                       \
    }

#define SDL_STATIC(n, nl, fn, pc, pt, rt, rc, rts)                                                                    \
    {                                                                                                                  \
        n, nl, fn, pc, pt, rt, rc, rts, 1, NULL, 0U, 0, NULL, NULL, NULL, NULL                                       \
    }
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
static const int p_str_i32[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int p_i32_str[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING};
static const int p_i32_obj[] = {VIGIL_TYPE_I32, VIGIL_TYPE_OBJECT};
static const int p_i32_str_str[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_i32_str_i64[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_I64};
static const int p_i32_str_f64[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_F64};
static const int p_i32_str_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int p_str_str_i32[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int p_str_str_str[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_i32_str_str_str[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_f64_f64_str[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_STRING};
static const int p_f64[] = {VIGIL_TYPE_F64};
static const int p_obj[] = {VIGIL_TYPE_OBJECT};
static const int p_obj_i32_i32[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_obj_i64[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_I64};
static const int p_i32_i32_i32_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                            VIGIL_TYPE_I32};
static const int p_i32_i32_i32_i32_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
/* render_texture_tiled: obj tex + 9x f64 */
/* clang-format off */
static const int p_i32x8[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                               VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_f64x6[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                               VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_f64x8[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                               VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
/* clang-format off */
static const int p_f64x9[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                               VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_str_i64_i32[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_convert_pixels[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I64,
                                        VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int rt_f64x4[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int rt_bool_i32x4[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_bool_f64x4[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int rt_i64_i64_err[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_ERR};
/* clang-format off */
static const int rt_bool_f64x3[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int rt_bool_i32[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_I32};
static const int rt_i32x4[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_i32x5[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_i32x6[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_i64_i64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int rt_i32_i64[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I64};
static const int rt_f64_i64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_I64};
static const int rt_i64_bool[] = {VIGIL_TYPE_I64, VIGIL_TYPE_BOOL};
static const int p_i64_i32x6[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                   VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
/* clang-format on */
/* clang-format off */
static const int p_i32x6[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i64_i32_i32_f64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_F64};
static const int p_premultiply[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32,
                                     VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_vtouch[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_blit_uscaled[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                      VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_blit_tscaled[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                      VIGIL_TYPE_F64, VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_blit_9grid[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                    VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                    VIGIL_TYPE_F64, VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_i32x3[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_f64x3[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
/* clang-format on */
/* clang-format on */
static const int p_i64_i64_i64_i32_i64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32,
                                            VIGIL_TYPE_I64};
/* clang-format on */
static const int p_obj_f64x9[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                  VIGIL_TYPE_F64,    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
/* clang-format off */
static const int p_obj_f64x11[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                    VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_obj_f64x13[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_f64_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
/* clang-format off */
static const int p_obj_f64x14[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_obj_i32x8[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                   VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i32_i32_f64_f64_f64_f64[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                                  VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_i32x4_i64_i32_i64_i32_i64_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                        VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32,
                                                        VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_i32x4_i64_i32_i64_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_i64_i64_i32_i32_i64_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                      VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
/* clang-format on */
static const int p_i32_i32_i32_i32_i64_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_i64_i64_i32_i64_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I64,
                                            VIGIL_TYPE_I32};
/* clang-format on */
static const int p_i32_f64[] = {VIGIL_TYPE_I32, VIGIL_TYPE_F64};
static const int p_f64_i32[] = {VIGIL_TYPE_F64, VIGIL_TYPE_I32};
/* blit: obj dst + sx, sy, sw, sh, dx, dy */
static const int p_obj_i32x6[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                  VIGIL_TYPE_I32,    VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_obj_i32x9[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                  VIGIL_TYPE_I32,    VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i32_i32_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_str_i32_i32_i32[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_obj_str[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING};
static const int p_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_f64_f64_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int rt_bool_err[] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_ERR};
static const int rt_obj_err[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int rt_i64_err[] = {VIGIL_TYPE_I64, VIGIL_TYPE_ERR};
static const int rt_i32_err[] = {VIGIL_TYPE_I32, VIGIL_TYPE_ERR};
static const int rt_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_i32_i32_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_obj_f64_f64[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int rt_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_i32_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_i64_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i32_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int p_i64_obj[] = {VIGIL_TYPE_I64, VIGIL_TYPE_OBJECT};
static const int p_i64_i64_i32_i32_i32_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                    VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i64_f64_f64_f64_f64_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                                        VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i64_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_i64_i64_str[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_STRING};
static const int p_i64_f64_f64_f64_f64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                            VIGIL_TYPE_F64};
static const int p_i64_i64_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_obj_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32};
static const int p_i64_i64_i32x9[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                      VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                      VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i64_i32_i32_i64_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32,
                                                VIGIL_TYPE_I32, VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_i64_i64_i64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int p_i64_i64_i64_i32_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64,
                                                VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i32_i64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I64};
static const int p_i64_str[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING};
static const int p_i64_str_i64_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING, VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int p_i64_str_str[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_i64_str_str_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int p_i64_i32_str_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int p_i64_i32_i32_i32_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_f64_f64_f64_f64_f64_f64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64,
                                                    VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_i64_i32_i32_i32_i32_i32_i32_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                            VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                                            VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_obj_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i64_i32_i32_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                            VIGIL_TYPE_I32};
static const int p_i64_i64_i32_i64_i32_i32[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I32,
                                                VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
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
SDL_HANDLE_REGISTRY(environments);

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

static vigil_status_t sdl_fn_get_gamepads(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    SDL_JoystickID *ids = NULL;
    int count = 0;
    char *joined = NULL;
    size_t joined_len = 0;
    size_t joined_cap = 0;
    vigil_status_t st = VIGIL_STATUS_OK;

    vigil_vm_stack_pop_n(vm, arg_count);
    ids = SDL_GetGamepads(&count);
    if (!ids || count <= 0)
        return sdl_push_string(vm, "", error);

    for (int i = 0; i < count; i++)
    {
        SDL_JoystickID id = ids[i];
        const char *name = SDL_GetGamepadNameForID(id);
        const char *path = SDL_GetGamepadPathForID(id);
        int player_index = SDL_GetGamepadPlayerIndexForID(id);
        SDL_GamepadType type = SDL_GetGamepadTypeForID(id);

        if (joined_len > 0 && sdl_append_bytes(&joined, &joined_len, &joined_cap, "\n", 1U) != 0)
            goto oom;
        if (sdl_append_format(&joined, &joined_len, &joined_cap, "%d,%s,%d,%d,%s", (int)id, name ? name : "", (int)type,
                              player_index, path ? path : "") != 0)
            goto oom;
    }

    st = sdl_push_string(vm, joined ? joined : "", error);
    SDL_free(ids);
    free(joined);
    return st;

oom:
    SDL_free(ids);
    free(joined);
    vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
    return VIGIL_STATUS_OUT_OF_MEMORY;
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

static vigil_status_t sdl_show_message_box_common(vigil_vm_t *vm, SDL_Window *window, int32_t flags, const char *title,
                                                  const char *message, const char *buttons_text, vigil_error_t *error)
{
    vigil_status_t st;
    SDL_MessageBoxData data;
    SDL_MessageBoxButtonData buttons[8];
    char *storage = NULL;
    char *labels[8];
    size_t label_count = 0;
    int button_id = -1;
    char default_button[] = "OK";
    char *cursor;

    memset(&data, 0, sizeof(data));
    memset(buttons, 0, sizeof(buttons));
    memset(labels, 0, sizeof(labels));

    if (buttons_text != NULL && buttons_text[0] != '\0')
    {
        storage = strdup(buttons_text);
        if (storage == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }

        cursor = storage;
        while (*cursor != '\0' && label_count < 8U)
        {
            char *line = cursor;
            while (*cursor != '\0' && *cursor != '\n')
                cursor++;
            if (*cursor == '\n')
            {
                *cursor = '\0';
                cursor++;
            }
            if (*line != '\0')
                labels[label_count++] = line;
        }
    }

    if (label_count == 0U)
        labels[label_count++] = default_button;

    for (size_t i = 0; i < label_count; i++)
    {
        buttons[i].flags = 0;
        if (i == 0U)
            buttons[i].flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (i + 1U == label_count)
            buttons[i].flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        buttons[i].buttonID = (int)i;
        buttons[i].text = labels[i];
    }

    data.flags = (SDL_MessageBoxFlags)flags;
    data.window = window;
    data.title = title;
    data.message = message;
    data.numbuttons = (int)label_count;
    data.buttons = buttons;
    data.colorScheme = NULL;

    if (!SDL_ShowMessageBox(&data, &button_id))
    {
        st = sdl_push_i32(vm, -1, error);
        free(storage);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }

    st = sdl_push_i32(vm, button_id, error);
    free(storage);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_show_message_box(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t flags = sdl_arg_i32(vm, base, 0);
    char title[256];
    char message[4096];
    char buttons[1024];
    sdl_arg_str(vm, base, 1, title, sizeof(title));
    sdl_arg_str(vm, base, 2, message, sizeof(message));
    sdl_arg_str(vm, base, 3, buttons, sizeof(buttons));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_show_message_box_common(vm, NULL, flags, title, message, buttons, error);
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
SDL_CONST_FN(MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT)
SDL_CONST_FN(MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT, SDL_MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT)
SDL_CONST_FN(MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT)
SDL_CONST_FN(MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT)

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

/* ── Slice 12: Renderer Drawing Extras ────────────────────────────── */

/* ren.render_debug_text(f64 x, f64 y, string text) -> (bool, err) */
static vigil_status_t sdl_renderer_render_debug_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    float x = (float)sdl_arg_f64(vm, base, 1);
    float y = (float)sdl_arg_f64(vm, base, 2);
    char text[4096];
    sdl_arg_str(vm, base, 3, text, sizeof(text));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_RenderDebugText(ren, x, y, text))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.set_viewport(i32 x, i32 y, i32 w, i32 h) -> (bool, err) — zeros to reset */
static vigil_status_t sdl_renderer_set_viewport(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
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
        if (SDL_SetRenderViewport(ren, NULL))
            return sdl_push_bool_ok(vm, error);
    }
    else
    {
        SDL_Rect rect = {x, y, w, h};
        if (SDL_SetRenderViewport(ren, &rect))
            return sdl_push_bool_ok(vm, error);
    }
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.set_draw_blend_mode(i32 mode) -> (bool, err) */
static vigil_status_t sdl_renderer_set_draw_blend_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int32_t mode = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_SetRenderDrawBlendMode(ren, (SDL_BlendMode)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.get_draw_blend_mode() -> i32 */
static vigil_status_t sdl_renderer_get_draw_blend_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_BlendMode mode = SDL_BLENDMODE_NONE;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderDrawBlendMode(ren, &mode);
    return sdl_push_i32(vm, (int32_t)mode, error);
}

/* ren.set_color_scale(f64 scale) -> (bool, err) */
static vigil_status_t sdl_renderer_set_color_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    float scale = (float)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_SetRenderColorScale(ren, scale))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.get_color_scale() -> f64 */
static vigil_status_t sdl_renderer_get_color_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    float scale = 1.0f;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderColorScale(ren, &scale);
    return sdl_push_f64(vm, (double)scale, error);
}

/* ren.flush() -> (bool, err) */
static vigil_status_t sdl_renderer_flush(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_FlushRenderer(ren))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.get_output_size() -> (i32, i32) */
static vigil_status_t sdl_renderer_get_output_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int w = 0, h = 0;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderOutputSize(ren, &w, &h);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)h, error);
}

/* ren.get_current_output_size() -> (i32, i32) */
static vigil_status_t sdl_renderer_get_current_output_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int w = 0, h = 0;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetCurrentRenderOutputSize(ren, &w, &h);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)h, error);
}

/* ren.get_name() -> string */
static vigil_status_t sdl_renderer_get_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    return sdl_push_string(vm, ren ? SDL_GetRendererName(ren) : "", error);
}

/* ── Slice 13: Extended Window, Display, Power ────────────────────── */

/* win.flash(i32 op) -> (bool, err) */
static vigil_status_t sdl_window_flash(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t op = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_FlashWindow(win, (SDL_FlashOperation)op))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* win.set_icon(Surface surf) -> (bool, err) */
static vigil_status_t sdl_window_set_icon(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    int64_t sh = sdl_field_i64(vm, base + 1, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    SDL_Surface *surf = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    if (win && surf && SDL_SetWindowIcon(win, surf))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* win.set_opacity(f64 opacity) -> (bool, err) */
static vigil_status_t sdl_window_set_opacity(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    float op = (float)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowOpacity(win, op))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* win.get_opacity() -> f64 */
static vigil_status_t sdl_window_get_opacity(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_f64(vm, win ? (double)SDL_GetWindowOpacity(win) : 1.0, error);
}

/* win.set_min_size / get_min_size / set_max_size / get_max_size */
#define WIN_SIZE_LIMIT(name, sdl_set, sdl_get, is_set)                                                                 \
    static vigil_status_t sdl_window_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                    \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);                                                               \
        if (is_set)                                                                                                    \
        {                                                                                                              \
            int32_t w = sdl_arg_i32(vm, base, 1);                                                                      \
            int32_t ht = sdl_arg_i32(vm, base, 2);                                                                     \
            vigil_vm_stack_pop_n(vm, arg_count);                                                                       \
            SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);                                                \
            if (win && sdl_set(win, w, ht))                                                                            \
                return sdl_push_bool_ok(vm, error);                                                                    \
            return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);                                                       \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            vigil_vm_stack_pop_n(vm, arg_count);                                                                       \
            int w = 0, ht = 0;                                                                                         \
            SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);                                                \
            if (win)                                                                                                   \
                sdl_get(win, &w, &ht);                                                                                 \
            vigil_status_t st = sdl_push_i32(vm, (int32_t)w, error);                                                   \
            if (st != VIGIL_STATUS_OK)                                                                                 \
                return st;                                                                                             \
            return sdl_push_i32(vm, (int32_t)ht, error);                                                               \
        }                                                                                                              \
    }

WIN_SIZE_LIMIT(set_min_size, SDL_SetWindowMinimumSize, SDL_GetWindowMinimumSize, 1)
WIN_SIZE_LIMIT(get_min_size, SDL_SetWindowMinimumSize, SDL_GetWindowMinimumSize, 0)
WIN_SIZE_LIMIT(set_max_size, SDL_SetWindowMaximumSize, SDL_GetWindowMaximumSize, 1)
WIN_SIZE_LIMIT(get_max_size, SDL_SetWindowMaximumSize, SDL_GetWindowMaximumSize, 0)
#undef WIN_SIZE_LIMIT

/* Simple bool-arg window methods */
#define WIN_BOOL_METHOD(name, sdl_fn)                                                                                  \
    static vigil_status_t sdl_window_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                    \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);                                                               \
        int32_t v = sdl_arg_i32(vm, base, 1);                                                                          \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);                                                    \
        if (win && sdl_fn(win, v != 0))                                                                                \
            return sdl_push_bool_ok(vm, error);                                                                        \
        return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);                                                           \
    }

WIN_BOOL_METHOD(set_always_on_top, SDL_SetWindowAlwaysOnTop)
WIN_BOOL_METHOD(set_mouse_grab, SDL_SetWindowMouseGrab)
WIN_BOOL_METHOD(set_keyboard_grab, SDL_SetWindowKeyboardGrab)
WIN_BOOL_METHOD(set_relative_mouse, SDL_SetWindowRelativeMouseMode)
#undef WIN_BOOL_METHOD

/* win.get_size_in_pixels() -> (i32, i32) */
static vigil_status_t sdl_window_get_size_in_pixels(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int w = 0, ht = 0;
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_GetWindowSizeInPixels(win, &w, &ht);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)ht, error);
}

/* win.get_display_scale() -> f64 */
static vigil_status_t sdl_window_get_display_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_f64(vm, win ? (double)SDL_GetWindowDisplayScale(win) : 1.0, error);
}

/* Extended display functions */

static vigil_status_t sdl_fn_get_primary_display(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    sdl_refresh_display_list();
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int i = 0; i < g_display_count; i++)
    {
        if (g_display_ids && g_display_ids[i] == primary)
            return sdl_push_i32(vm, (int32_t)i, error);
    }
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_get_display_content_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    float scale = 1.0f;
    if (idx >= 0 && idx < g_display_count && g_display_ids)
        scale = SDL_GetDisplayContentScale(g_display_ids[idx]);
    return sdl_push_f64(vm, (double)scale, error);
}

static vigil_status_t sdl_fn_get_display_usable_bounds(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect rect = {0, 0, 0, 0};
    if (idx >= 0 && idx < g_display_count && g_display_ids)
        SDL_GetDisplayUsableBounds(g_display_ids[idx], &rect);
    vigil_status_t st = sdl_push_i32(vm, rect.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, rect.h, error);
}

/* Power / screensaver */

static vigil_status_t sdl_fn_get_power_info(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    int seconds = -1, percent = -1;
    SDL_GetPowerInfo(&seconds, &percent);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)percent, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)seconds, error);
}

static vigil_status_t sdl_fn_screen_saver_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_ScreenSaverEnabled(), error);
}

static vigil_status_t sdl_fn_enable_screen_saver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_EnableScreenSaver(), error);
}

static vigil_status_t sdl_fn_disable_screen_saver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_DisableScreenSaver(), error);
}

/* Flash operation constants */
SDL_CONST_FN(FLASH_CANCEL, SDL_FLASH_CANCEL)
SDL_CONST_FN(FLASH_BRIEFLY, SDL_FLASH_BRIEFLY)
SDL_CONST_FN(FLASH_UNTIL_FOCUSED, SDL_FLASH_UNTIL_FOCUSED)

/* ── Slice 14: Extended Audio ─────────────────────────────────────── */

static SDL_AudioDeviceID *g_audio_dev_ids = NULL;
static int g_audio_dev_count = 0;

static vigil_status_t sdl_fn_get_audio_playback_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_audio_dev_ids)
    {
        SDL_free(g_audio_dev_ids);
        g_audio_dev_ids = NULL;
    }
    g_audio_dev_ids = SDL_GetAudioPlaybackDevices(&g_audio_dev_count);
    return sdl_push_i32(vm, (int32_t)g_audio_dev_count, error);
}

static vigil_status_t sdl_fn_get_audio_device_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_audio_dev_count && g_audio_dev_ids)
        return sdl_push_string(vm, SDL_GetAudioDeviceName(g_audio_dev_ids[idx]), error);
    return sdl_push_string(vm, "", error);
}

static vigil_status_t sdl_fn_get_current_audio_driver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetCurrentAudioDriver(), error);
}

/* stream.set_gain(f64) -> (bool, err) */
static vigil_status_t sdl_audio_stream_set_gain(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    float gain = (float)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s && SDL_SetAudioStreamGain(s, gain))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_audio_stream_get_gain(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    return sdl_push_f64(vm, s ? (double)SDL_GetAudioStreamGain(s) : 1.0, error);
}

static vigil_status_t sdl_audio_stream_set_freq_ratio(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    float ratio = (float)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s && SDL_SetAudioStreamFrequencyRatio(s, ratio))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_audio_stream_get_freq_ratio(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    return sdl_push_f64(vm, s ? (double)SDL_GetAudioStreamFrequencyRatio(s) : 1.0, error);
}

static vigil_status_t sdl_audio_stream_get_available(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    return sdl_push_i32(vm, s ? SDL_GetAudioStreamAvailable(s) : 0, error);
}

static vigil_status_t sdl_audio_stream_flush(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s && SDL_FlushAudioStream(s))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_audio_stream_clear(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s && SDL_ClearAudioStream(s))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 15: Cursor Management ──────────────────────────────────── */

SDL_HANDLE_REGISTRY(cursors);

static vigil_status_t sdl_fn_create_system_cursor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Cursor *c = SDL_CreateSystemCursor((SDL_SystemCursor)id);
    if (!c)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t handle;
    if (SDL_HANDLE_STORE(cursors, c, &handle) < 0)
    {
        SDL_DestroyCursor(c);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many cursors", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_create_color_cursor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t hx = sdl_arg_i32(vm, base, 1);
    int32_t hy = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *surf = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    if (!surf)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid surface", SDL_ERR_ARG, error);
    }
    SDL_Cursor *c = SDL_CreateColorCursor(surf, hx, hy);
    if (!c)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t handle;
    if (SDL_HANDLE_STORE(cursors, c, &handle) < 0)
    {
        SDL_DestroyCursor(c);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many cursors", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_set_cursor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Cursor *c = (SDL_Cursor *)SDL_HANDLE_GET(cursors, h);
    if (c && SDL_SetCursor(c))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_destroy_cursor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Cursor *c = (SDL_Cursor *)SDL_HANDLE_GET(cursors, h);
    if (c)
    {
        SDL_DestroyCursor(c);
        SDL_HANDLE_CLEAR(cursors, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_capture_mouse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t en = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_CaptureMouse(en != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_relative_mouse_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    float x = 0, y = 0;
    SDL_GetRelativeMouseState(&x, &y);
    vigil_status_t st = sdl_push_f64(vm, (double)x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)y, error);
}

static vigil_status_t sdl_fn_warp_mouse_global(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    float x = (float)sdl_arg_f64(vm, base, 0);
    float y = (float)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_WarpMouseGlobal(x, y))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* System cursor constants */
SDL_CONST_FN(CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_DEFAULT)
SDL_CONST_FN(CURSOR_TEXT, SDL_SYSTEM_CURSOR_TEXT)
SDL_CONST_FN(CURSOR_WAIT, SDL_SYSTEM_CURSOR_WAIT)
SDL_CONST_FN(CURSOR_CROSSHAIR, SDL_SYSTEM_CURSOR_CROSSHAIR)
SDL_CONST_FN(CURSOR_PROGRESS, SDL_SYSTEM_CURSOR_PROGRESS)
SDL_CONST_FN(CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_NWSE_RESIZE)
SDL_CONST_FN(CURSOR_NESW_RESIZE, SDL_SYSTEM_CURSOR_NESW_RESIZE)
SDL_CONST_FN(CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_EW_RESIZE)
SDL_CONST_FN(CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE)
SDL_CONST_FN(CURSOR_MOVE, SDL_SYSTEM_CURSOR_MOVE)
SDL_CONST_FN(CURSOR_NOT_ALLOWED, SDL_SYSTEM_CURSOR_NOT_ALLOWED)
SDL_CONST_FN(CURSOR_POINTER, SDL_SYSTEM_CURSOR_POINTER)

/* ── Slice 16: Extended Events ────────────────────────────────────── */

static vigil_status_t sdl_fn_pump_events(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_PumpEvents();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_has_event(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t type = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasEvent((Uint32)type), error);
}

static vigil_status_t sdl_fn_flush_event(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t type = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_FlushEvent((Uint32)type);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_flush_events(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t min_type = sdl_arg_i32(vm, base, 0);
    int32_t max_type = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_FlushEvents((Uint32)min_type, (Uint32)max_type);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_event_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t type = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_EventEnabled((Uint32)type), error);
}

static vigil_status_t sdl_fn_set_event_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t type = sdl_arg_i32(vm, base, 0);
    int32_t enabled = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_SetEventEnabled((Uint32)type, enabled != 0);
    return VIGIL_STATUS_OK;
}

/* ── Slice 17: Renderer Getters ────────────────────────────────────── */

static vigil_status_t sdl_renderer_get_vsync(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int vsync = 0;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderVSync(ren, &vsync);
    return sdl_push_i32(vm, (int32_t)vsync, error);
}

static vigil_status_t sdl_renderer_clip_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    return sdl_push_bool(vm, ren && SDL_RenderClipEnabled(ren), error);
}

static vigil_status_t sdl_renderer_get_viewport(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect rect = {0, 0, 0, 0};
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderViewport(ren, &rect);
    vigil_status_t st = sdl_push_i32(vm, rect.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, rect.h, error);
}

/* ── Slice 18: Texture Extras ─────────────────────────────────────── */

static vigil_status_t sdl_texture_get_color_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint8 r = 255, g = 255, b = 255;
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_GetTextureColorMod(tex, &r, &g, &b);
    return sdl_push_i32(vm, ((int32_t)r << 16) | ((int32_t)g << 8) | (int32_t)b, error);
}

static vigil_status_t sdl_texture_get_alpha_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint8 alpha = 255;
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_GetTextureAlphaMod(tex, &alpha);
    return sdl_push_i32(vm, (int32_t)alpha, error);
}

static vigil_status_t sdl_texture_get_blend_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_BlendMode mode = SDL_BLENDMODE_NONE;
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_GetTextureBlendMode(tex, &mode);
    return sdl_push_i32(vm, (int32_t)mode, error);
}

static vigil_status_t sdl_texture_set_scale_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    int32_t mode = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex && SDL_SetTextureScaleMode(tex, (SDL_ScaleMode)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_texture_get_scale_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_ScaleMode mode = SDL_SCALEMODE_NEAREST;
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_GetTextureScaleMode(tex, &mode);
    return sdl_push_i32(vm, (int32_t)mode, error);
}

SDL_CONST_FN(SCALEMODE_NEAREST, SDL_SCALEMODE_NEAREST)
SDL_CONST_FN(SCALEMODE_LINEAR, SDL_SCALEMODE_LINEAR)

/* ── Slice 19: System Info ────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_current_time(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Time ticks = 0;
    SDL_GetCurrentTime(&ticks);
    return sdl_push_i64(vm, (int64_t)ticks, error);
}

static vigil_status_t sdl_fn_get_user_folder(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t folder = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetUserFolder((SDL_Folder)folder), error);
}

static vigil_status_t sdl_fn_get_system_theme(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetSystemTheme(), error);
}

static vigil_status_t sdl_fn_is_tablet(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_IsTablet(), error);
}

static vigil_status_t sdl_fn_is_tv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_IsTV(), error);
}

static vigil_status_t sdl_fn_set_app_metadata(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256], ver[64], ident[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    sdl_arg_str(vm, base, 1, ver, sizeof(ver));
    sdl_arg_str(vm, base, 2, ident, sizeof(ident));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetAppMetadata(name, ver, ident))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_current_video_driver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetCurrentVideoDriver(), error);
}

SDL_CONST_FN(SYSTEM_THEME_UNKNOWN, SDL_SYSTEM_THEME_UNKNOWN)
SDL_CONST_FN(SYSTEM_THEME_LIGHT, SDL_SYSTEM_THEME_LIGHT)
SDL_CONST_FN(SYSTEM_THEME_DARK, SDL_SYSTEM_THEME_DARK)
SDL_CONST_FN(FOLDER_HOME, SDL_FOLDER_HOME)
SDL_CONST_FN(FOLDER_DESKTOP, SDL_FOLDER_DESKTOP)
SDL_CONST_FN(FOLDER_DOCUMENTS, SDL_FOLDER_DOCUMENTS)
SDL_CONST_FN(FOLDER_DOWNLOADS, SDL_FOLDER_DOWNLOADS)
SDL_CONST_FN(FOLDER_MUSIC, SDL_FOLDER_MUSIC)
SDL_CONST_FN(FOLDER_PICTURES, SDL_FOLDER_PICTURES)
SDL_CONST_FN(FOLDER_SAVEDGAMES, SDL_FOLDER_SAVEDGAMES)
SDL_CONST_FN(FOLDER_SCREENSHOTS, SDL_FOLDER_SCREENSHOTS)
SDL_CONST_FN(FOLDER_VIDEOS, SDL_FOLDER_VIDEOS)

/* ── Slice 20: Surface Operations ─────────────────────────────────── */

static vigil_status_t sdl_surface_clear(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    float r = (float)sdl_arg_f64(vm, base, 1);
    float g = (float)sdl_arg_f64(vm, base, 2);
    float b = (float)sdl_arg_f64(vm, base, 3);
    float a = (float)sdl_arg_f64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_ClearSurface(s, r, g, b, a))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_surface_fill_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), ht = sdl_arg_i32(vm, base, 4);
    int32_t color = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!s)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect rect = {x, y, w, ht};
    if (SDL_FillSurfaceRect(s, (w == 0 && ht == 0) ? NULL : &rect, (Uint32)color))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_surface_flip(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t flip = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_FlipSurface(s, (SDL_FlipMode)flip))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_surface_set_color_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    Uint8 r = (Uint8)sdl_arg_i32(vm, base, 1), g = (Uint8)sdl_arg_i32(vm, base, 2), b = (Uint8)sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_SetSurfaceColorMod(s, r, g, b))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_surface_set_alpha_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    Uint8 a = (Uint8)sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_SetSurfaceAlphaMod(s, a))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_surface_set_blend_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t mode = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_SetSurfaceBlendMode(s, (SDL_BlendMode)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_surface_set_color_key(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t enabled = sdl_arg_i32(vm, base, 1);
    int32_t key = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_SetSurfaceColorKey(s, enabled != 0, (Uint32)key))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.blit(dst_surf, i32 sx, sy, sw, sh, dx, dy) -> (bool, err) */
static vigil_status_t sdl_surface_blit(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, SURF_HANDLE);
    int64_t dh = sdl_field_i64(vm, base + 1, SURF_HANDLE);
    int32_t sx = sdl_arg_i32(vm, base, 2), sy = sdl_arg_i32(vm, base, 3);
    int32_t sw = sdl_arg_i32(vm, base, 4), sht = sdl_arg_i32(vm, base, 5);
    int32_t dx = sdl_arg_i32(vm, base, 6), dy = sdl_arg_i32(vm, base, 7);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    SDL_Surface *dst = (SDL_Surface *)SDL_HANDLE_GET(surfaces, dh);
    if (!src || !dst)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect srect = {sx, sy, sw, sht};
    SDL_Rect drect = {dx, dy, 0, 0};
    int use_src = (sw > 0 || sht > 0);
    if (SDL_BlitSurface(src, use_src ? &srect : NULL, dst, &drect))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 21: Window Getters ─────────────────────────────────────── */

static vigil_status_t sdl_window_get_pixel_density(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_f64(vm, win ? (double)SDL_GetWindowPixelDensity(win) : 1.0, error);
}

static vigil_status_t sdl_window_get_mouse_grab(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_bool(vm, win && SDL_GetWindowMouseGrab(win), error);
}

static vigil_status_t sdl_window_get_keyboard_grab(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_bool(vm, win && SDL_GetWindowKeyboardGrab(win), error);
}

static vigil_status_t sdl_window_get_relative_mouse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_bool(vm, win && SDL_GetWindowRelativeMouseMode(win), error);
}

static vigil_status_t sdl_window_set_progress(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t state = sdl_arg_i32(vm, base, 1);
    float value = (float)sdl_arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (!win)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    if (!SDL_SetWindowProgressState(win, (SDL_ProgressState)state))
        return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
    if (value >= 0.0f)
        SDL_SetWindowProgressValue(win, value);
    return sdl_push_bool_ok(vm, error);
}

static vigil_status_t sdl_window_set_aspect_ratio(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    float mn = (float)sdl_arg_f64(vm, base, 1);
    float mx = (float)sdl_arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowAspectRatio(win, mn, mx))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 22: Gamepad Extras ─────────────────────────────────────── */

static vigil_status_t sdl_gamepad_connected(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_bool(vm, gp && SDL_GamepadConnected(gp), error);
}

static vigil_status_t sdl_gamepad_get_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetGamepadType(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_get_power_percent(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int percent = -1;
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    if (gp)
        SDL_GetGamepadPowerInfo(gp, &percent);
    return sdl_push_i32(vm, (int32_t)percent, error);
}

static vigil_status_t sdl_gamepad_get_power_info(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int percent = -1;
    SDL_PowerState state = SDL_POWERSTATE_ERROR;
    SDL_Gamepad *gp = NULL;
    vigil_status_t st = VIGIL_STATUS_OK;

    vigil_vm_stack_pop_n(vm, arg_count);
    gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    if (gp)
        state = SDL_GetGamepadPowerInfo(gp, &percent);

    st = sdl_push_i32(vm, (int32_t)state, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)percent, error);
}

static vigil_status_t sdl_gamepad_set_led(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    Uint8 r = (Uint8)sdl_arg_i32(vm, base, 1);
    Uint8 g = (Uint8)sdl_arg_i32(vm, base, 2);
    Uint8 b = (Uint8)sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    if (gp && SDL_SetGamepadLED(gp, r, g, b))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_gamepad_rumble_triggers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t left = sdl_arg_i32(vm, base, 1), right = sdl_arg_i32(vm, base, 2), dur = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    if (gp && SDL_RumbleGamepadTriggers(gp, (Uint16)left, (Uint16)right, (Uint32)dur))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_gamepad_has_axis(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t axis = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_bool(vm, gp && SDL_GamepadHasAxis(gp, (SDL_GamepadAxis)axis), error);
}

static vigil_status_t sdl_gamepad_has_button(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t btn = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_bool(vm, gp && SDL_GamepadHasButton(gp, (SDL_GamepadButton)btn), error);
}

static vigil_status_t sdl_fn_update_gamepads(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_UpdateGamepads();
    return VIGIL_STATUS_OK;
}

/* Progress state constants */
SDL_CONST_FN(PROGRESS_NONE, SDL_PROGRESS_STATE_NONE)
SDL_CONST_FN(PROGRESS_INDETERMINATE, SDL_PROGRESS_STATE_INDETERMINATE)
SDL_CONST_FN(PROGRESS_NORMAL, SDL_PROGRESS_STATE_NORMAL)
SDL_CONST_FN(PROGRESS_PAUSED, SDL_PROGRESS_STATE_PAUSED)
SDL_CONST_FN(PROGRESS_ERROR, SDL_PROGRESS_STATE_ERROR)

/* ── Slice 23: Advanced Surface ───────────────────────────────────── */

/* Surface.create(i32 w, i32 h, i32 format) -> (Surface, err) */
static vigil_status_t sdl_surface_create(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int32_t w = sdl_arg_i32(vm, base, 1);
    int32_t h = sdl_arg_i32(vm, base, 2);
    int32_t fmt = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = SDL_CreateSurface(w, h, (SDL_PixelFormat)fmt);
    if (!s)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t handle;
    if (SDL_HANDLE_STORE(surfaces, s, &handle) < 0)
    {
        SDL_DestroySurface(s);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* surf.duplicate() -> (Surface, err) */
static vigil_status_t sdl_surface_duplicate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_self_class_index(vm, base);
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!src)
        return sdl_push_nil_and_err(vm, "invalid surface", SDL_ERR_ARG, error);
    SDL_Surface *dup = SDL_DuplicateSurface(src);
    if (!dup)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t nh;
    if (SDL_HANDLE_STORE(surfaces, dup, &nh) < 0)
    {
        SDL_DestroySurface(dup);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, nh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* surf.scale(i32 w, i32 h, i32 mode) -> (Surface, err) */
static vigil_status_t sdl_surface_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_self_class_index(vm, base);
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t w = sdl_arg_i32(vm, base, 1);
    int32_t ht = sdl_arg_i32(vm, base, 2);
    int32_t mode = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!src)
        return sdl_push_nil_and_err(vm, "invalid surface", SDL_ERR_ARG, error);
    SDL_Surface *scaled = SDL_ScaleSurface(src, w, ht, (SDL_ScaleMode)mode);
    if (!scaled)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t nh;
    if (SDL_HANDLE_STORE(surfaces, scaled, &nh) < 0)
    {
        SDL_DestroySurface(scaled);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, nh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* surf.rotate(f64 angle) -> (Surface, err) */
static vigil_status_t sdl_surface_rotate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_self_class_index(vm, base);
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    float angle = (float)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!src)
        return sdl_push_nil_and_err(vm, "invalid surface", SDL_ERR_ARG, error);
    SDL_Surface *rot = SDL_RotateSurface(src, angle);
    if (!rot)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t nh;
    if (SDL_HANDLE_STORE(surfaces, rot, &nh) < 0)
    {
        SDL_DestroySurface(rot);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, nh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* surf.save_bmp(string path) -> (bool, err) */
static vigil_status_t sdl_surface_save_bmp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_SaveBMP(s, path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.save_png(string path) -> (bool, err) */
static vigil_status_t sdl_surface_save_png(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_SavePNG(s, path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.read_pixel(i32 x, i32 y) -> i32 — packed 0xAARRGGBB */
static vigil_status_t sdl_surface_read_pixel(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1);
    int32_t y = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s)
        SDL_ReadSurfacePixel(s, x, y, &r, &g, &b, &a);
    return sdl_push_i32(vm, ((int32_t)a << 24) | ((int32_t)r << 16) | ((int32_t)g << 8) | (int32_t)b, error);
}

/* surf.write_pixel(i32 x, i32 y, i32 r, i32 g, i32 b, i32 a) -> (bool, err) */
static vigil_status_t sdl_surface_write_pixel(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    Uint8 r = (Uint8)sdl_arg_i32(vm, base, 3), g = (Uint8)sdl_arg_i32(vm, base, 4);
    Uint8 b = (Uint8)sdl_arg_i32(vm, base, 5), a = (Uint8)sdl_arg_i32(vm, base, 6);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_WriteSurfacePixel(s, x, y, r, g, b, a))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 24: Keyboard/Scancode Lookups ──────────────────────────── */

static vigil_status_t sdl_fn_get_key_from_scancode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t sc = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetKeyFromScancode((SDL_Scancode)sc, SDL_KMOD_NONE, false), error);
}

static vigil_status_t sdl_fn_get_scancode_from_key(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t key = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetScancodeFromKey((SDL_Keycode)key, NULL), error);
}

static vigil_status_t sdl_fn_get_key_from_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[64];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetKeyFromName(name), error);
}

static vigil_status_t sdl_fn_get_scancode_from_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[64];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetScancodeFromName(name), error);
}

static vigil_status_t sdl_fn_has_keyboard(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasKeyboard(), error);
}

static vigil_status_t sdl_fn_has_mouse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasMouse(), error);
}

static vigil_status_t sdl_fn_start_text_input(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (win && SDL_StartTextInput(win))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_stop_text_input(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (win && SDL_StopTextInput(win))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_text_input_active(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    return sdl_push_bool(vm, win && SDL_TextInputActive(win), error);
}

/* ── Slice 25: Texture Update & Tiled Rendering ───────────────────── */

/* ren.render_texture_tiled(tex, sx, sy, sw, sh, f64 scale, dx, dy, dw, dh) -> (bool, err) */
static vigil_status_t sdl_renderer_render_texture_tiled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_field_i64(vm, base + 1, TEX_HANDLE);
    float sx = (float)sdl_arg_f64(vm, base, 2), sy = (float)sdl_arg_f64(vm, base, 3);
    float sw = (float)sdl_arg_f64(vm, base, 4), sh = (float)sdl_arg_f64(vm, base, 5);
    float scale = (float)sdl_arg_f64(vm, base, 6);
    float dx = (float)sdl_arg_f64(vm, base, 7), dy = (float)sdl_arg_f64(vm, base, 8);
    float dw = (float)sdl_arg_f64(vm, base, 9), dh = (float)sdl_arg_f64(vm, base, 10);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (!ren || !tex)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_FRect src = {sx, sy, sw, sh};
    SDL_FRect dst = {dx, dy, dw, dh};
    int use_src = (sw > 0.0f || sh > 0.0f);
    int use_dst = (dw > 0.0f || dh > 0.0f);
    if (SDL_RenderTextureTiled(ren, tex, use_src ? &src : NULL, scale, use_dst ? &dst : NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 26: Display Modes ──────────────────────────────────────── */

/* sdl.get_display_mode(i32 index) -> (i32, i32, f64) — w, h, refresh
 * 2-return limit: return (i32 w, i32 h). Use get_display_refresh for rate. */
static vigil_status_t sdl_fn_get_desktop_display_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int w = 0, h = 0;
    if (idx >= 0 && idx < g_display_count && g_display_ids)
    {
        const SDL_DisplayMode *m = SDL_GetDesktopDisplayMode(g_display_ids[idx]);
        if (m)
        {
            w = m->w;
            h = m->h;
        }
    }
    vigil_status_t st = sdl_push_i32(vm, w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, h, error);
}

static vigil_status_t sdl_fn_get_current_display_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int w = 0, h = 0;
    if (idx >= 0 && idx < g_display_count && g_display_ids)
    {
        const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(g_display_ids[idx]);
        if (m)
        {
            w = m->w;
            h = m->h;
        }
    }
    vigil_status_t st = sdl_push_i32(vm, w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, h, error);
}

static vigil_status_t sdl_fn_get_display_refresh_rate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    float rate = 0.0f;
    if (idx >= 0 && idx < g_display_count && g_display_ids)
    {
        const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(g_display_ids[idx]);
        if (m)
            rate = m->refresh_rate;
    }
    return sdl_push_f64(vm, (double)rate, error);
}

/* win.sync() -> (bool, err) */
static vigil_status_t sdl_window_sync(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SyncWindow(win))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* sdl.get_display_for_window(Window) -> i32 */
static vigil_status_t sdl_fn_get_display_for_window(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (!win)
        return sdl_push_i32(vm, -1, error);
    SDL_DisplayID did = SDL_GetDisplayForWindow(win);
    for (int i = 0; i < g_display_count; i++)
        if (g_display_ids && g_display_ids[i] == did)
            return sdl_push_i32(vm, (int32_t)i, error);
    return sdl_push_i32(vm, -1, error);
}

/* ── Slice 27: Renderer Completions ───────────────────────────────── */

/* ren.get_render_target() -> i64 — returns texture handle or -1 */
static vigil_status_t sdl_renderer_get_render_target(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (!ren)
        return sdl_push_i64(vm, -1, error);
    SDL_Texture *tex = SDL_GetRenderTarget(ren);
    if (!tex)
        return sdl_push_i64(vm, -1, error);
    /* Find the texture in our registry */
    for (int64_t i = 0; i < (int64_t)g_textures.count; i++)
        if (g_textures.items[i] == tex)
            return sdl_push_i64(vm, i, error);
    return sdl_push_i64(vm, -1, error);
}

/* ren.set_draw_color_float(f64 r, g, b, a) -> (bool, err) */
static vigil_status_t sdl_renderer_set_draw_color_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    float r = (float)sdl_arg_f64(vm, base, 1), g = (float)sdl_arg_f64(vm, base, 2);
    float b = (float)sdl_arg_f64(vm, base, 3), a = (float)sdl_arg_f64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_SetRenderDrawColorFloat(ren, r, g, b, a))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.get_logical_presentation() -> (i32, i32) — w, h */
static vigil_status_t sdl_renderer_get_logical_presentation(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int w = 0, h = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderLogicalPresentation(ren, &w, &h, &mode);
    vigil_status_t st = sdl_push_i32(vm, w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, h, error);
}

/* ── Slice 28: Surface Extras ─────────────────────────────────────── */

/* Surface.load_png(string path) -> (Surface, err) */
static vigil_status_t sdl_surface_load_png(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *surf = SDL_LoadPNG(path);
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

/* surf.blit_scaled(dst, sx, sy, sw, sh, dx, dy, dw, dh, mode) -> (bool, err) */
static vigil_status_t sdl_surface_blit_scaled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, SURF_HANDLE);
    int64_t dh = sdl_field_i64(vm, base + 1, SURF_HANDLE);
    int32_t sx = sdl_arg_i32(vm, base, 2), sy = sdl_arg_i32(vm, base, 3);
    int32_t sw = sdl_arg_i32(vm, base, 4), sht = sdl_arg_i32(vm, base, 5);
    int32_t dx = sdl_arg_i32(vm, base, 6), dy = sdl_arg_i32(vm, base, 7);
    int32_t dw = sdl_arg_i32(vm, base, 8), dht = sdl_arg_i32(vm, base, 9);
    int32_t mode = sdl_arg_i32(vm, base, 10);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    SDL_Surface *dst = (SDL_Surface *)SDL_HANDLE_GET(surfaces, dh);
    if (!src || !dst)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect srect = {sx, sy, sw, sht}, drect = {dx, dy, dw, dht};
    int use_src = (sw > 0 || sht > 0), use_dst = (dw > 0 || dht > 0);
    if (SDL_BlitSurfaceScaled(src, use_src ? &srect : NULL, dst, use_dst ? &drect : NULL, (SDL_ScaleMode)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.get_color_key() -> i32 */
static vigil_status_t sdl_surface_get_color_key(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint32 key = 0;
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s)
        SDL_GetSurfaceColorKey(s, &key);
    return sdl_push_i32(vm, (int32_t)key, error);
}

/* ── Slice 29: Joystick (Raw Input) ───────────────────────────────── */

SDL_HANDLE_REGISTRY(joysticks);

enum
{
    JOY_HANDLE = 0,
    JOY_FIELD_COUNT
};

static SDL_JoystickID *g_joystick_ids = NULL;
static int g_joystick_count = 0;

static vigil_status_t sdl_fn_has_joystick(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasJoystick(), error);
}

static vigil_status_t sdl_fn_get_joystick_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_joystick_ids)
    {
        SDL_free(g_joystick_ids);
        g_joystick_ids = NULL;
    }
    g_joystick_ids = SDL_GetJoysticks(&g_joystick_count);
    return sdl_push_i32(vm, (int32_t)g_joystick_count, error);
}

static vigil_status_t sdl_fn_get_joystick_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_joystick_count && g_joystick_ids)
        return sdl_push_i32(vm, (int32_t)g_joystick_ids[idx], error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_is_gamepad_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_IsGamepad((SDL_JoystickID)id), error);
}

/* Joystick.open(i32 instance_id) -> (Joystick, err) */
static vigil_status_t sdl_joystick_open(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int32_t id = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = SDL_OpenJoystick((SDL_JoystickID)id);
    if (!j)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t handle;
    if (SDL_HANDLE_STORE(joysticks, j, &handle) < 0)
    {
        SDL_CloseJoystick(j);
        return sdl_push_nil_and_err(vm, "too many joysticks", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_joystick_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j)
    {
        SDL_CloseJoystick(j);
        SDL_HANDLE_CLEAR(joysticks, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_joystick_get_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_string(vm, j ? SDL_GetJoystickName(j) : "", error);
}

static vigil_status_t sdl_joystick_get_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickType(j) : 0, error);
}

static vigil_status_t sdl_joystick_connected(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_bool(vm, j && SDL_JoystickConnected(j), error);
}

static vigil_status_t sdl_joystick_num_axes(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? SDL_GetNumJoystickAxes(j) : 0, error);
}

static vigil_status_t sdl_joystick_num_buttons(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? SDL_GetNumJoystickButtons(j) : 0, error);
}

static vigil_status_t sdl_joystick_num_hats(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? SDL_GetNumJoystickHats(j) : 0, error);
}

static vigil_status_t sdl_joystick_get_axis(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    int32_t axis = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickAxis(j, axis) : 0, error);
}

static vigil_status_t sdl_joystick_get_button(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    int32_t btn = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_bool(vm, j && SDL_GetJoystickButton(j, btn), error);
}

static vigil_status_t sdl_joystick_get_hat(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    int32_t hat = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickHat(j, hat) : 0, error);
}

static vigil_status_t sdl_joystick_rumble(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    int32_t low = sdl_arg_i32(vm, base, 1), high = sdl_arg_i32(vm, base, 2), dur = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j && SDL_RumbleJoystick(j, (Uint16)low, (Uint16)high, (Uint32)dur))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 30: Window Getters Completion ──────────────────────────── */

static vigil_status_t sdl_window_get_aspect_ratio(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    float mn = 0, mx = 0;
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_GetWindowAspectRatio(win, &mn, &mx);
    vigil_status_t st = sdl_push_f64(vm, (double)mn, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)mx, error);
}

static vigil_status_t sdl_window_get_pixel_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_i32(vm, win ? (int32_t)SDL_GetWindowPixelFormat(win) : 0, error);
}

/* ── Slice 31: Renderer/Texture Remaining ─────────────────────────── */

/* ren.read_pixels(i32 x, y, w, h) -> (Surface, err) */
static vigil_status_t sdl_renderer_read_pixels(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_self_class_index(vm, base);
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), h = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (!ren)
        return sdl_push_nil_and_err(vm, "invalid renderer", SDL_ERR_ARG, error);
    SDL_Rect rect = {x, y, w, h};
    int use_rect = (w > 0 || h > 0);
    SDL_Surface *surf = SDL_RenderReadPixels(ren, use_rect ? &rect : NULL);
    if (!surf)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t sh;
    if (SDL_HANDLE_STORE(surfaces, surf, &sh) < 0)
    {
        SDL_DestroySurface(surf);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    /* Need to find the Surface class index — search classes array */
    (void)ci;
    /* Use a fixed approach: push handle instance with surface class */
    vigil_status_t st = sdl_push_handle_instance(vm, 3U, sh, error); /* Surface is class index 3 */
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* surf.convert(i32 format) -> (Surface, err) */
static vigil_status_t sdl_surface_convert(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_self_class_index(vm, base);
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t fmt = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!src)
        return sdl_push_nil_and_err(vm, "invalid surface", SDL_ERR_ARG, error);
    SDL_Surface *dst = SDL_ConvertSurface(src, (SDL_PixelFormat)fmt);
    if (!dst)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t nh;
    if (SDL_HANDLE_STORE(surfaces, dst, &nh) < 0)
    {
        SDL_DestroySurface(dst);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, nh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* Hat position constants */
SDL_CONST_FN(HAT_CENTERED, SDL_HAT_CENTERED)
SDL_CONST_FN(HAT_UP, SDL_HAT_UP)
SDL_CONST_FN(HAT_RIGHT, SDL_HAT_RIGHT)
SDL_CONST_FN(HAT_DOWN, SDL_HAT_DOWN)
SDL_CONST_FN(HAT_LEFT, SDL_HAT_LEFT)

/* ── Slice 32: Haptic (Simple Rumble) ─────────────────────────────── */

SDL_HANDLE_REGISTRY(haptics);

enum
{
    HAP_HANDLE = 0,
    HAP_FIELD_COUNT
};

static SDL_HapticID *g_haptic_ids = NULL;
static int g_haptic_count = 0;

static vigil_status_t sdl_fn_get_haptic_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_haptic_ids)
    {
        SDL_free(g_haptic_ids);
        g_haptic_ids = NULL;
    }
    g_haptic_ids = SDL_GetHaptics(&g_haptic_count);
    return sdl_push_i32(vm, (int32_t)g_haptic_count, error);
}

static vigil_status_t sdl_fn_is_mouse_haptic(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_IsMouseHaptic(), error);
}

static vigil_status_t sdl_haptic_open(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int32_t idx = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_HapticID hid = (idx >= 0 && idx < g_haptic_count && g_haptic_ids) ? g_haptic_ids[idx] : 0;
    SDL_Haptic *h = hid ? SDL_OpenHaptic(hid) : NULL;
    if (!h)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t handle;
    if (SDL_HANDLE_STORE(haptics, h, &handle) < 0)
    {
        SDL_CloseHaptic(h);
        return sdl_push_nil_and_err(vm, "too many haptics", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_haptic_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp)
    {
        SDL_CloseHaptic(hp);
        SDL_HANDLE_CLEAR(haptics, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_haptic_get_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    return sdl_push_string(vm, hp ? SDL_GetHapticName(hp) : "", error);
}

static vigil_status_t sdl_haptic_rumble_supported(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    return sdl_push_bool(vm, hp && SDL_HapticRumbleSupported(hp), error);
}

static vigil_status_t sdl_haptic_init_rumble(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp && SDL_InitHapticRumble(hp))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_haptic_play_rumble(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    float strength = (float)sdl_arg_f64(vm, base, 1);
    int32_t length = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp && SDL_PlayHapticRumble(hp, strength, (Uint32)length))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_haptic_stop_rumble(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp && SDL_StopHapticRumble(hp))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_haptic_pause(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp && SDL_PauseHaptic(hp))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_haptic_resume(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp && SDL_ResumeHaptic(hp))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 33: Filesystem ─────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_current_directory(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    char *dir = SDL_GetCurrentDirectory();
    vigil_status_t st = sdl_push_string(vm, dir ? dir : "", error);
    SDL_free(dir);
    return st;
}

static vigil_status_t sdl_fn_create_directory(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_CreateDirectory(path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_remove_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_RemovePath(path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_rename_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char old[512];
    sdl_arg_str(vm, base, 0, old, sizeof(old));
    char new_path[512];
    sdl_arg_str(vm, base, 1, new_path, sizeof(new_path));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_RenamePath(old, new_path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_copy_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char old[512];
    sdl_arg_str(vm, base, 0, old, sizeof(old));
    char new_path[512];
    sdl_arg_str(vm, base, 1, new_path, sizeof(new_path));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_CopyFile(old, new_path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* sdl.get_path_type(string path) -> i32 — returns PATHTYPE_* */
static vigil_status_t sdl_fn_get_path_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info))
        return sdl_push_i32(vm, (int32_t)info.type, error);
    return sdl_push_i32(vm, (int32_t)SDL_PATHTYPE_NONE, error);
}

/* sdl.get_path_size(string path) -> i64 */
static vigil_status_t sdl_fn_get_path_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info))
        return sdl_push_i64(vm, (int64_t)info.size, error);
    return sdl_push_i64(vm, 0, error);
}

SDL_CONST_FN(PATHTYPE_NONE, SDL_PATHTYPE_NONE)
SDL_CONST_FN(PATHTYPE_FILE, SDL_PATHTYPE_FILE)
SDL_CONST_FN(PATHTYPE_DIRECTORY, SDL_PATHTYPE_DIRECTORY)

/* ── Slice 34: Remaining Window/Surface ───────────────────────────── */

static vigil_status_t sdl_window_get_borders_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int top = 0, left = 0, bottom = 0, right = 0;
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_GetWindowBordersSize(win, &top, &left, &bottom, &right);
    /* Pack as (i32 top_bottom, i32 left_right) due to 2-return limit */
    vigil_status_t st = sdl_push_i32(vm, (top << 16) | (bottom & 0xFFFF), error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (left << 16) | (right & 0xFFFF), error);
}

static vigil_status_t sdl_window_get_safe_area(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect rect = {0, 0, 0, 0};
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_GetWindowSafeArea(win, &rect);
    vigil_status_t st = sdl_push_i32(vm, rect.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, rect.h, error);
}

static vigil_status_t sdl_surface_set_clip_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), ht = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!s)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    if (w == 0 && ht == 0)
    {
        SDL_SetSurfaceClipRect(s, NULL);
        return sdl_push_bool_ok(vm, error);
    }
    SDL_Rect rect = {x, y, w, ht};
    if (SDL_SetSurfaceClipRect(s, &rect))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Slice 35: Camera ─────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(cameras);

enum
{
    CAM_HANDLE = 0,
    CAM_FIELD_COUNT
};

static SDL_CameraID *g_camera_ids = NULL;
static int g_camera_count = 0;

static vigil_status_t sdl_fn_get_camera_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_camera_ids)
    {
        SDL_free(g_camera_ids);
        g_camera_ids = NULL;
    }
    g_camera_ids = SDL_GetCameras(&g_camera_count);
    return sdl_push_i32(vm, (int32_t)g_camera_count, error);
}

static vigil_status_t sdl_fn_get_cameras(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    SDL_CameraID *ids = NULL;
    int count = 0;
    char *joined = NULL;
    size_t joined_len = 0;
    size_t joined_cap = 0;
    vigil_status_t st = VIGIL_STATUS_OK;

    vigil_vm_stack_pop_n(vm, arg_count);
    ids = SDL_GetCameras(&count);
    if (!ids || count <= 0)
        return sdl_push_string(vm, "", error);

    for (int i = 0; i < count; i++)
    {
        SDL_CameraID id = ids[i];
        const char *name = SDL_GetCameraName(id);
        SDL_CameraPosition position = SDL_GetCameraPosition(id);

        if (joined_len > 0 && sdl_append_bytes(&joined, &joined_len, &joined_cap, "\n", 1U) != 0)
            goto oom;
        if (sdl_append_format(&joined, &joined_len, &joined_cap, "%d,%s,%d", (int)id, name ? name : "",
                              (int)position) != 0)
            goto oom;
    }

    st = sdl_push_string(vm, joined ? joined : "", error);
    SDL_free(ids);
    free(joined);
    return st;

oom:
    SDL_free(ids);
    free(joined);
    vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
    return VIGIL_STATUS_OUT_OF_MEMORY;
}

static vigil_status_t sdl_fn_get_camera_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_camera_count && g_camera_ids)
        return sdl_push_string(vm, SDL_GetCameraName(g_camera_ids[idx]), error);
    return sdl_push_string(vm, "", error);
}

static vigil_status_t sdl_fn_get_current_camera_driver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetCurrentCameraDriver(), error);
}

static vigil_status_t sdl_fn_get_camera_supported_formats(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    SDL_CameraID camera_id = 0;
    SDL_CameraSpec **specs = NULL;
    char *joined = NULL;
    size_t joined_len = 0;
    size_t joined_cap = 0;
    vigil_status_t st = VIGIL_STATUS_OK;
    int count = 0;

    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_camera_count && g_camera_ids)
        camera_id = g_camera_ids[idx];
    if (!camera_id)
        return sdl_push_string(vm, "", error);

    specs = SDL_GetCameraSupportedFormats(camera_id, &count);
    if (!specs || count <= 0)
        return sdl_push_string(vm, "", error);

    for (int i = 0; i < count; i++)
    {
        const SDL_CameraSpec *spec = specs[i];
        if (!spec)
            continue;
        if (joined_len > 0 && sdl_append_bytes(&joined, &joined_len, &joined_cap, "\n", 1U) != 0)
            goto oom;
        if (sdl_append_format(&joined, &joined_len, &joined_cap, "%d,%d,%d,%d,%d,%d", (int)spec->format,
                              (int)spec->colorspace, spec->width, spec->height, spec->framerate_numerator,
                              spec->framerate_denominator) != 0)
            goto oom;
    }

    st = sdl_push_string(vm, joined ? joined : "", error);
    SDL_free(specs);
    free(joined);
    return st;

oom:
    SDL_free(specs);
    free(joined);
    vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
    return VIGIL_STATUS_OUT_OF_MEMORY;
}

/* Camera.open(i32 index, i32 w, i32 h, i32 fps) -> (Camera, err) */
static vigil_status_t sdl_camera_open(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int32_t idx = sdl_arg_i32(vm, base, 1);
    int32_t w = sdl_arg_i32(vm, base, 2);
    int32_t h = sdl_arg_i32(vm, base, 3);
    int32_t fps = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_CameraID cid = (idx >= 0 && idx < g_camera_count && g_camera_ids) ? g_camera_ids[idx] : 0;
    if (!cid)
        return sdl_push_nil_and_err(vm, "invalid camera index", SDL_ERR_ARG, error);
    SDL_CameraSpec spec = {0};
    SDL_CameraSpec *sp = NULL;
    if (w > 0 && h > 0)
    {
        spec.width = w;
        spec.height = h;
        spec.framerate_numerator = fps > 0 ? fps : 30;
        spec.framerate_denominator = 1;
        sp = &spec;
    }
    SDL_Camera *cam = SDL_OpenCamera(cid, sp);
    if (!cam)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t handle;
    if (SDL_HANDLE_STORE(cameras, cam, &handle) < 0)
    {
        SDL_CloseCamera(cam);
        return sdl_push_nil_and_err(vm, "too many cameras", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_camera_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, CAM_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Camera *cam = (SDL_Camera *)SDL_HANDLE_GET(cameras, h);
    if (cam)
    {
        SDL_CloseCamera(cam);
        SDL_HANDLE_CLEAR(cameras, h);
    }
    return VIGIL_STATUS_OK;
}

/* cam.get_permission() -> i32 — -1 denied, 0 pending, 1 approved */
static vigil_status_t sdl_camera_get_permission(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, CAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Camera *cam = (SDL_Camera *)SDL_HANDLE_GET(cameras, h);
    return sdl_push_i32(vm, cam ? (int32_t)SDL_GetCameraPermissionState(cam) : -1, error);
}

/* cam.get_format() -> (i32, i32) — w, h of the camera */
static vigil_status_t sdl_camera_get_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, CAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_CameraSpec spec = {0};
    SDL_Camera *cam = (SDL_Camera *)SDL_HANDLE_GET(cameras, h);
    if (cam)
        SDL_GetCameraFormat(cam, &spec);
    vigil_status_t st = sdl_push_i32(vm, spec.width, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, spec.height, error);
}

static vigil_status_t sdl_camera_get_spec(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, CAM_HANDLE);
    SDL_CameraSpec spec = {0};
    SDL_Camera *cam = NULL;
    vigil_status_t st = VIGIL_STATUS_OK;

    vigil_vm_stack_pop_n(vm, arg_count);
    cam = (SDL_Camera *)SDL_HANDLE_GET(cameras, h);
    if (cam)
        SDL_GetCameraFormat(cam, &spec);

    st = sdl_push_i32(vm, (int32_t)spec.format, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, (int32_t)spec.colorspace, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, spec.width, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, spec.height, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, spec.framerate_numerator, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, spec.framerate_denominator, error);
}

/* cam.acquire_frame() -> (i64, err) — returns surface handle or -1
 * The surface must be released with cam.release_frame(handle). */
static vigil_status_t sdl_camera_acquire_frame(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, CAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Camera *cam = (SDL_Camera *)SDL_HANDLE_GET(cameras, h);
    if (!cam)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid camera", SDL_ERR_ARG, error);
    }
    Uint64 ts = 0;
    SDL_Surface *frame = SDL_AcquireCameraFrame(cam, &ts);
    if (!frame)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_ok(vm, error); /* no frame yet, not an error */
    }
    int64_t sh;
    if (SDL_HANDLE_STORE(surfaces, frame, &sh) < 0)
    {
        SDL_ReleaseCameraFrame(cam, frame);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, sh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* cam.release_frame(i64 surface_handle) */
static vigil_status_t sdl_camera_release_frame(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_field_i64(vm, base, CAM_HANDLE);
    int64_t sh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Camera *cam = (SDL_Camera *)SDL_HANDLE_GET(cameras, ch);
    SDL_Surface *frame = (sh >= 0) ? (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh) : NULL;
    if (cam && frame)
    {
        SDL_ReleaseCameraFrame(cam, frame);
        SDL_HANDLE_CLEAR(surfaces, sh);
    }
    return VIGIL_STATUS_OK;
}

/* ── Slice 36: Remaining Window/Renderer ──────────────────────────── */

/* win.set_mouse_rect(i32 x, y, w, h) -> (bool, err) — zeros to clear */
static vigil_status_t sdl_window_set_mouse_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), ht = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (!win)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    if (w == 0 && ht == 0)
    {
        SDL_SetWindowMouseRect(win, NULL);
        return sdl_push_bool_ok(vm, error);
    }
    SDL_Rect rect = {x, y, w, ht};
    if (SDL_SetWindowMouseRect(win, &rect))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ev.get_description() -> string */
static vigil_status_t sdl_event_get_description(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Event *ev = evt_get(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[256] = {0};
    if (ev)
        SDL_GetEventDescription(ev, buf, sizeof(buf));
    return sdl_push_string(vm, buf, error);
}

/* ── Slice 37: Surface get_clip_rect + stretch ────────────────────── */

static vigil_status_t sdl_surface_get_clip_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect rect = {0, 0, 0, 0};
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s)
        SDL_GetSurfaceClipRect(s, &rect);
    vigil_status_t st = sdl_push_i32(vm, rect.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, rect.h, error);
}

/* surf.stretch(dst, sx, sy, sw, sh, dx, dy, dw, dh, mode) -> (bool, err) */
static vigil_status_t sdl_surface_stretch(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, SURF_HANDLE);
    int64_t dh = sdl_field_i64(vm, base + 1, SURF_HANDLE);
    int32_t sx = sdl_arg_i32(vm, base, 2), sy = sdl_arg_i32(vm, base, 3);
    int32_t sw = sdl_arg_i32(vm, base, 4), sht = sdl_arg_i32(vm, base, 5);
    int32_t dx = sdl_arg_i32(vm, base, 6), dy = sdl_arg_i32(vm, base, 7);
    int32_t dw = sdl_arg_i32(vm, base, 8), dht = sdl_arg_i32(vm, base, 9);
    int32_t mode = sdl_arg_i32(vm, base, 10);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    SDL_Surface *dst = (SDL_Surface *)SDL_HANDLE_GET(surfaces, dh);
    if (!src || !dst)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect srect = {sx, sy, sw, sht}, drect = {dx, dy, dw, dht};
    int use_src = (sw > 0 || sht > 0), use_dst = (dw > 0 || dht > 0);
    if (SDL_StretchSurface(src, use_src ? &srect : NULL, dst, use_dst ? &drect : NULL, (SDL_ScaleMode)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── GPU API ──────────────────────────────────────────────────────── */

#include "vigil/unsafe_buffer.h"

SDL_HANDLE_REGISTRY(gpu_devices);
SDL_HANDLE_REGISTRY(gpu_shaders);
SDL_HANDLE_REGISTRY(gpu_pipelines);
SDL_HANDLE_REGISTRY(gpu_buffers);
SDL_HANDLE_REGISTRY(gpu_xfer_buffers);
SDL_HANDLE_REGISTRY(gpu_textures_gpu);
SDL_HANDLE_REGISTRY(gpu_cmd_buffers);
SDL_HANDLE_REGISTRY(gpu_render_passes);
SDL_HANDLE_REGISTRY(gpu_copy_passes);

enum
{
    GPUDEV_HANDLE = 0,
    GPUDEV_FIELD_COUNT
};

/* sdl.gpu_create_device(i32 shader_formats, bool debug) -> (i64, err) */
static vigil_status_t sdl_fn_gpu_create_device(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    int32_t debug = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = SDL_CreateGPUDevice((SDL_GPUShaderFormat)fmt, debug != 0, NULL);
    if (!dev)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_devices, dev, &h) < 0)
    {
        SDL_DestroyGPUDevice(dev);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many GPU devices", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_destroy_device(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, h);
    if (dev)
    {
        SDL_DestroyGPUDevice(dev);
        SDL_HANDLE_CLEAR(gpu_devices, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_get_driver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, h);
    return sdl_push_string(vm, dev ? SDL_GetGPUDeviceDriver(dev) : "", error);
}

static vigil_status_t sdl_fn_gpu_get_shader_formats(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, h);
    return sdl_push_i32(vm, dev ? (int32_t)SDL_GetGPUShaderFormats(dev) : 0, error);
}

/* sdl.gpu_claim_window(i64 dev, Window win) -> (bool, err) */
static vigil_status_t sdl_fn_gpu_claim_window(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (dev && win && SDL_ClaimWindowForGPUDevice(dev, win))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gpu_release_window(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (dev && win)
        SDL_ReleaseWindowFromGPUDevice(dev, win);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_wait_idle(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    if (dev && SDL_WaitForGPUIdle(dev))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gpu_get_swapchain_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    return sdl_push_i32(vm, (dev && win) ? (int32_t)SDL_GetGPUSwapchainTextureFormat(dev, win) : 0, error);
}

/* sdl.gpu_create_shader(dev, buf_handle, stage, num_samplers, num_storage_tex, num_storage_buf, num_uniforms) -> (i64,
 * err) */
static vigil_status_t sdl_fn_gpu_create_shader(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t buf = sdl_arg_i64(vm, base, 1);
    int32_t stage = sdl_arg_i32(vm, base, 2);
    int32_t n_samp = sdl_arg_i32(vm, base, 3);
    int32_t n_stex = sdl_arg_i32(vm, base, 4);
    int32_t n_sbuf = sdl_arg_i32(vm, base, 5);
    int32_t n_ubuf = sdl_arg_i32(vm, base, 6);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    int32_t code_size = 0;
    void *code = vigil_unsafe_buffer_get(buf, &code_size);
    if (!dev || !code)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid device or buffer", SDL_ERR_ARG, error);
    }
    SDL_GPUShaderFormat fmt = SDL_GetGPUShaderFormats(dev);
    SDL_GPUShaderCreateInfo ci = {0};
    ci.code = (const Uint8 *)code;
    ci.code_size = (size_t)code_size;
    ci.entrypoint = "main";
    ci.format = fmt;
    ci.stage = (SDL_GPUShaderStage)stage;
    ci.num_samplers = (Uint32)n_samp;
    ci.num_storage_textures = (Uint32)n_stex;
    ci.num_storage_buffers = (Uint32)n_sbuf;
    ci.num_uniform_buffers = (Uint32)n_ubuf;
    SDL_GPUShader *sh = SDL_CreateGPUShader(dev, &ci);
    if (!sh)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_shaders, sh, &h) < 0)
    {
        SDL_ReleaseGPUShader(dev, sh);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many shaders", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_release_shader(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0), sh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUShader *shader = (SDL_GPUShader *)SDL_HANDLE_GET(gpu_shaders, sh);
    if (dev && shader)
    {
        SDL_ReleaseGPUShader(dev, shader);
        SDL_HANDLE_CLEAR(gpu_shaders, sh);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_create_graphics_pipeline(dev, vert_shader, frag_shader, primitive_type,
 *   vertex_stride, num_attribs_buf, swapchain_format) -> (i64, err)
 * vertex_stride: bytes per vertex. num_attribs_buf: unsafe buffer with packed attribs
 *   (each attrib: i32 location, i32 format, i32 offset — 12 bytes each) or -1 for none. */
static vigil_status_t sdl_fn_gpu_create_pipeline(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t vsh = sdl_arg_i64(vm, base, 1), fsh = sdl_arg_i64(vm, base, 2);
    int32_t prim = sdl_arg_i32(vm, base, 3);
    int32_t vstride = sdl_arg_i32(vm, base, 4);
    int64_t attrib_buf = sdl_arg_i64(vm, base, 5);
    int32_t sc_fmt = sdl_arg_i32(vm, base, 6);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUShader *vs = (SDL_GPUShader *)SDL_HANDLE_GET(gpu_shaders, vsh);
    SDL_GPUShader *fs = (SDL_GPUShader *)SDL_HANDLE_GET(gpu_shaders, fsh);
    if (!dev || !vs || !fs)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid device/shader", SDL_ERR_ARG, error);
    }

    /* Build vertex attributes from buffer */
    SDL_GPUVertexAttribute attrs[16] = {{0}};
    SDL_GPUVertexBufferDescription vbd = {0};
    Uint32 num_attrs = 0;
    if (attrib_buf >= 0)
    {
        int32_t abuf_size = 0;
        void *abuf = vigil_unsafe_buffer_get(attrib_buf, &abuf_size);
        if (abuf && abuf_size >= 12)
        {
            num_attrs = (Uint32)(abuf_size / 12);
            if (num_attrs > 16)
                num_attrs = 16;
            const int32_t *p = (const int32_t *)abuf;
            for (Uint32 i = 0; i < num_attrs; i++)
            {
                attrs[i].location = (Uint32)p[i * 3];
                attrs[i].format = (SDL_GPUVertexElementFormat)p[i * 3 + 1];
                attrs[i].offset = (Uint32)p[i * 3 + 2];
                attrs[i].buffer_slot = 0;
            }
        }
    }
    vbd.slot = 0;
    vbd.pitch = (Uint32)vstride;
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUColorTargetDescription ctd = {0};
    ctd.format = (SDL_GPUTextureFormat)sc_fmt;

    SDL_GPUGraphicsPipelineCreateInfo ci = {0};
    ci.vertex_shader = vs;
    ci.fragment_shader = fs;
    ci.primitive_type = (SDL_GPUPrimitiveType)prim;
    ci.vertex_input_state.vertex_buffer_descriptions = &vbd;
    ci.vertex_input_state.num_vertex_buffers = (vstride > 0) ? 1 : 0;
    ci.vertex_input_state.vertex_attributes = attrs;
    ci.vertex_input_state.num_vertex_attributes = num_attrs;
    ci.target_info.color_target_descriptions = &ctd;
    ci.target_info.num_color_targets = 1;

    SDL_GPUGraphicsPipeline *pip = SDL_CreateGPUGraphicsPipeline(dev, &ci);
    if (!pip)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_pipelines, pip, &h) < 0)
    {
        SDL_ReleaseGPUGraphicsPipeline(dev, pip);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many pipelines", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_release_pipeline(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0), ph = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUGraphicsPipeline *pip = (SDL_GPUGraphicsPipeline *)SDL_HANDLE_GET(gpu_pipelines, ph);
    if (dev && pip)
    {
        SDL_ReleaseGPUGraphicsPipeline(dev, pip);
        SDL_HANDLE_CLEAR(gpu_pipelines, ph);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_create_buffer(dev, usage_flags, size) -> (i64, err) */
static vigil_status_t sdl_fn_gpu_create_buffer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t usage = sdl_arg_i32(vm, base, 1), size = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    if (!dev)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid device", SDL_ERR_ARG, error);
    }
    SDL_GPUBufferCreateInfo ci = {0};
    ci.usage = (SDL_GPUBufferUsageFlags)usage;
    ci.size = (Uint32)size;
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(dev, &ci);
    if (!buf)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_buffers, buf, &h) < 0)
    {
        SDL_ReleaseGPUBuffer(dev, buf);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many GPU buffers", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_release_buffer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (dev && buf)
    {
        SDL_ReleaseGPUBuffer(dev, buf);
        SDL_HANDLE_CLEAR(gpu_buffers, bh);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_create_transfer_buffer(dev, usage, size) -> (i64, err) */
static vigil_status_t sdl_fn_gpu_create_xfer_buffer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t usage = sdl_arg_i32(vm, base, 1), size = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    if (!dev)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid device", SDL_ERR_ARG, error);
    }
    SDL_GPUTransferBufferCreateInfo ci = {0};
    ci.usage = (SDL_GPUTransferBufferUsage)usage;
    ci.size = (Uint32)size;
    SDL_GPUTransferBuffer *xb = SDL_CreateGPUTransferBuffer(dev, &ci);
    if (!xb)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_xfer_buffers, xb, &h) < 0)
    {
        SDL_ReleaseGPUTransferBuffer(dev, xb);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many xfer buffers", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_release_xfer_buffer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0), xh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUTransferBuffer *xb = (SDL_GPUTransferBuffer *)SDL_HANDLE_GET(gpu_xfer_buffers, xh);
    if (dev && xb)
    {
        SDL_ReleaseGPUTransferBuffer(dev, xb);
        SDL_HANDLE_CLEAR(gpu_xfer_buffers, xh);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_map_xfer_buffer(dev, xfer_handle) -> i64 unsafe buffer handle */
static vigil_status_t sdl_fn_gpu_map_xfer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0), xh = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUTransferBuffer *xb = (SDL_GPUTransferBuffer *)SDL_HANDLE_GET(gpu_xfer_buffers, xh);
    if (!dev || !xb)
        return sdl_push_i64(vm, -1, error);
    void *ptr = SDL_MapGPUTransferBuffer(dev, xb, false);
    if (!ptr)
        return sdl_push_i64(vm, -1, error);
    /* We can't register this in the unsafe buffer registry because SDL owns it.
     * Return the raw pointer as i64 — user writes via unsafe.poke_* at this address. */
    return sdl_push_i64(vm, (int64_t)(intptr_t)ptr, error);
}

static vigil_status_t sdl_fn_gpu_unmap_xfer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0), xh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUTransferBuffer *xb = (SDL_GPUTransferBuffer *)SDL_HANDLE_GET(gpu_xfer_buffers, xh);
    if (dev && xb)
        SDL_UnmapGPUTransferBuffer(dev, xb);
    return VIGIL_STATUS_OK;
}

/* Command buffer + render pass + draw */
static vigil_status_t sdl_fn_gpu_acquire_cmd(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    if (!dev)
        return sdl_push_i64(vm, -1, error);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd)
        return sdl_push_i64(vm, -1, error);
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_cmd_buffers, cmd, &h) < 0)
        return sdl_push_i64(vm, -1, error);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_gpu_submit_cmd(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    if (cmd && SDL_SubmitGPUCommandBuffer(cmd))
    {
        SDL_HANDLE_CLEAR(gpu_cmd_buffers, ch);
        return sdl_push_bool_ok(vm, error);
    }
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gpu_cancel_cmd(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    if (cmd)
    {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_HANDLE_CLEAR(gpu_cmd_buffers, ch);
    }
    return sdl_push_bool_ok(vm, error);
}

/* gpu_acquire_swapchain(cmd, Window) -> (i64 tex_handle, i32 w, i32 h) — 2-return: (i64, err) */
static vigil_status_t sdl_fn_gpu_acquire_swapchain(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (!cmd || !win)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid cmd/window", SDL_ERR_ARG, error);
    }
    SDL_GPUTexture *tex = NULL;
    Uint32 w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, win, &tex, &w, &h) || !tex)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    /* Store the swapchain texture temporarily */
    int64_t th;
    if (SDL_HANDLE_STORE(gpu_textures_gpu, tex, &th) < 0)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many GPU textures", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, th, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* gpu_begin_render_pass(cmd, gpu_tex, clear_r, clear_g, clear_b, clear_a, load_op, store_op) -> i64 pass */
static vigil_status_t sdl_fn_gpu_begin_render_pass(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1);
    float cr = (float)sdl_arg_f64(vm, base, 2), cg = (float)sdl_arg_f64(vm, base, 3);
    float cb = (float)sdl_arg_f64(vm, base, 4), ca = (float)sdl_arg_f64(vm, base, 5);
    int32_t load_op = sdl_arg_i32(vm, base, 6), store_op = sdl_arg_i32(vm, base, 7);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    if (!cmd || !tex)
        return sdl_push_i64(vm, -1, error);
    SDL_GPUColorTargetInfo cti = {0};
    cti.texture = tex;
    cti.clear_color = (SDL_FColor){cr, cg, cb, ca};
    cti.load_op = (SDL_GPULoadOp)load_op;
    cti.store_op = (SDL_GPUStoreOp)store_op;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &cti, 1, NULL);
    if (!pass)
        return sdl_push_i64(vm, -1, error);
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_render_passes, pass, &h) < 0)
        return sdl_push_i64(vm, -1, error);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_gpu_end_render_pass(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    if (pass)
    {
        SDL_EndGPURenderPass(pass);
        SDL_HANDLE_CLEAR(gpu_render_passes, ph);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_pipeline(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0), pih = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUGraphicsPipeline *pip = (SDL_GPUGraphicsPipeline *)SDL_HANDLE_GET(gpu_pipelines, pih);
    if (pass && pip)
        SDL_BindGPUGraphicsPipeline(pass, pip);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_vertex_buffers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    int32_t offset = sdl_arg_i32(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (pass && buf)
    {
        SDL_GPUBufferBinding bb = {buf, (Uint32)offset};
        SDL_BindGPUVertexBuffers(pass, 0, &bb, 1);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_draw_primitives(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    int32_t num_verts = sdl_arg_i32(vm, base, 1), num_inst = sdl_arg_i32(vm, base, 2);
    int32_t first_vert = sdl_arg_i32(vm, base, 3), first_inst = sdl_arg_i32(vm, base, 4);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    if (pass)
        SDL_DrawGPUPrimitives(pass, (Uint32)num_verts, (Uint32)num_inst, (Uint32)first_vert, (Uint32)first_inst);
    return VIGIL_STATUS_OK;
}

/* Copy pass for uploads */
static vigil_status_t sdl_fn_gpu_begin_copy_pass(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    if (!cmd)
        return sdl_push_i64(vm, -1, error);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    if (!cp)
        return sdl_push_i64(vm, -1, error);
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_copy_passes, cp, &h) < 0)
        return sdl_push_i64(vm, -1, error);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_gpu_end_copy_pass(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCopyPass *cp = (SDL_GPUCopyPass *)SDL_HANDLE_GET(gpu_copy_passes, cph);
    if (cp)
    {
        SDL_EndGPUCopyPass(cp);
        SDL_HANDLE_CLEAR(gpu_copy_passes, cph);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_upload_to_buffer(copy_pass, xfer_handle, xfer_offset, gpu_buf, buf_offset, size) */
static vigil_status_t sdl_fn_gpu_upload_to_buffer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0), xh = sdl_arg_i64(vm, base, 1);
    int32_t xoff = sdl_arg_i32(vm, base, 2);
    int64_t bh = sdl_arg_i64(vm, base, 3);
    int32_t boff = sdl_arg_i32(vm, base, 4), sz = sdl_arg_i32(vm, base, 5);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCopyPass *cp = (SDL_GPUCopyPass *)SDL_HANDLE_GET(gpu_copy_passes, cph);
    SDL_GPUTransferBuffer *xb = (SDL_GPUTransferBuffer *)SDL_HANDLE_GET(gpu_xfer_buffers, xh);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (cp && xb && buf)
    {
        SDL_GPUTransferBufferLocation src = {xb, (Uint32)xoff};
        SDL_GPUBufferRegion dst = {buf, (Uint32)boff, (Uint32)sz};
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    }
    return VIGIL_STATUS_OK;
}

/* ── GPU Phase 2 ──────────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(gpu_samplers);

/* gpu_create_gpu_texture(dev, type, format, usage, w, h, layers, levels, samples) -> (i64, err) */
static vigil_status_t sdl_fn_gpu_create_texture(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t type = sdl_arg_i32(vm, base, 1), fmt = sdl_arg_i32(vm, base, 2), usage = sdl_arg_i32(vm, base, 3);
    int32_t w = sdl_arg_i32(vm, base, 4), h = sdl_arg_i32(vm, base, 5);
    int32_t layers = sdl_arg_i32(vm, base, 6), levels = sdl_arg_i32(vm, base, 7), samples = sdl_arg_i32(vm, base, 8);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    if (!dev)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid device", SDL_ERR_ARG, error);
    }
    SDL_GPUTextureCreateInfo ci = {0};
    ci.type = (SDL_GPUTextureType)type;
    ci.format = (SDL_GPUTextureFormat)fmt;
    ci.usage = (SDL_GPUTextureUsageFlags)usage;
    ci.width = (Uint32)w;
    ci.height = (Uint32)h;
    ci.layer_count_or_depth = (Uint32)(layers > 0 ? layers : 1);
    ci.num_levels = (Uint32)(levels > 0 ? levels : 1);
    ci.sample_count = (SDL_GPUSampleCount)samples;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(dev, &ci);
    if (!tex)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t th;
    if (SDL_HANDLE_STORE(gpu_textures_gpu, tex, &th) < 0)
    {
        SDL_ReleaseGPUTexture(dev, tex);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many GPU textures", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, th, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_release_texture(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    if (dev && tex)
    {
        SDL_ReleaseGPUTexture(dev, tex);
        SDL_HANDLE_CLEAR(gpu_textures_gpu, th);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_create_sampler(dev, min_filter, mag_filter, address_mode) -> (i64, err) */
static vigil_status_t sdl_fn_gpu_create_sampler(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t min_f = sdl_arg_i32(vm, base, 1), mag_f = sdl_arg_i32(vm, base, 2), addr = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    if (!dev)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid device", SDL_ERR_ARG, error);
    }
    SDL_GPUSamplerCreateInfo ci = {0};
    ci.min_filter = (SDL_GPUFilter)min_f;
    ci.mag_filter = (SDL_GPUFilter)mag_f;
    ci.address_mode_u = ci.address_mode_v = ci.address_mode_w = (SDL_GPUSamplerAddressMode)addr;
    ci.max_lod = 1000.0f;
    SDL_GPUSampler *s = SDL_CreateGPUSampler(dev, &ci);
    if (!s)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_samplers, s, &h) < 0)
    {
        SDL_ReleaseGPUSampler(dev, s);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many samplers", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_release_sampler(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0), sh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUSampler *s = (SDL_GPUSampler *)SDL_HANDLE_GET(gpu_samplers, sh);
    if (dev && s)
    {
        SDL_ReleaseGPUSampler(dev, s);
        SDL_HANDLE_CLEAR(gpu_samplers, sh);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_bind_index_buffer(pass, buf, offset, element_size) */
static vigil_status_t sdl_fn_gpu_bind_index_buffer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    int32_t offset = sdl_arg_i32(vm, base, 2), elem_sz = sdl_arg_i32(vm, base, 3);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (pass && buf)
    {
        SDL_GPUBufferBinding bb = {buf, (Uint32)offset};
        SDL_BindGPUIndexBuffer(pass, &bb, (SDL_GPUIndexElementSize)elem_sz);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_draw_indexed(pass, num_indices, num_instances, first_index, vertex_offset, first_instance) */
static vigil_status_t sdl_fn_gpu_draw_indexed(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    int32_t ni = sdl_arg_i32(vm, base, 1), ninst = sdl_arg_i32(vm, base, 2);
    int32_t fi = sdl_arg_i32(vm, base, 3), vo = sdl_arg_i32(vm, base, 4), finst = sdl_arg_i32(vm, base, 5);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    if (pass)
        SDL_DrawGPUIndexedPrimitives(pass, (Uint32)ni, (Uint32)ninst, (Uint32)fi, (Sint32)vo, (Uint32)finst);
    return VIGIL_STATUS_OK;
}

/* gpu_bind_fragment_samplers(pass, gpu_tex, sampler) */
static vigil_status_t sdl_fn_gpu_bind_fragment_samplers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1), sh = sdl_arg_i64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    SDL_GPUSampler *samp = (SDL_GPUSampler *)SDL_HANDLE_GET(gpu_samplers, sh);
    if (pass && tex && samp)
    {
        SDL_GPUTextureSamplerBinding tsb = {tex, samp};
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_push_vertex_uniforms(cmd, slot, buf_handle) */
static vigil_status_t sdl_fn_gpu_push_vertex_uniforms(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    int32_t slot = sdl_arg_i32(vm, base, 1);
    int64_t bh = sdl_arg_i64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(bh, &sz);
    if (cmd && data)
        SDL_PushGPUVertexUniformData(cmd, (Uint32)slot, data, (Uint32)sz);
    return VIGIL_STATUS_OK;
}

/* gpu_push_fragment_uniforms(cmd, slot, buf_handle) */
static vigil_status_t sdl_fn_gpu_push_fragment_uniforms(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    int32_t slot = sdl_arg_i32(vm, base, 1);
    int64_t bh = sdl_arg_i64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(bh, &sz);
    if (cmd && data)
        SDL_PushGPUFragmentUniformData(cmd, (Uint32)slot, data, (Uint32)sz);
    return VIGIL_STATUS_OK;
}

/* gpu_set_viewport(pass, x, y, w, h, min_depth, max_depth) */
static vigil_status_t sdl_fn_gpu_set_viewport(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    float x = (float)sdl_arg_f64(vm, base, 1), y = (float)sdl_arg_f64(vm, base, 2);
    float w = (float)sdl_arg_f64(vm, base, 3), h = (float)sdl_arg_f64(vm, base, 4);
    float mind = (float)sdl_arg_f64(vm, base, 5), maxd = (float)sdl_arg_f64(vm, base, 6);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    if (pass)
    {
        SDL_GPUViewport vp = {x, y, w, h, mind, maxd};
        SDL_SetGPUViewport(pass, &vp);
    }
    return VIGIL_STATUS_OK;
}

/* gpu_set_scissor(pass, x, y, w, h) */
static vigil_status_t sdl_fn_gpu_set_scissor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), h = sdl_arg_i32(vm, base, 4);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    if (pass)
    {
        SDL_Rect r = {x, y, w, h};
        SDL_SetGPUScissor(pass, &r);
    }
    return VIGIL_STATUS_OK;
}

/* Debug labels */
static vigil_status_t sdl_fn_gpu_debug_label(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    char text[256];
    sdl_arg_str(vm, base, 1, text, sizeof(text));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    if (cmd)
        SDL_InsertGPUDebugLabel(cmd, text);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_generate_mipmaps(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    if (cmd && tex)
        SDL_GenerateMipmapsForGPUTexture(cmd, tex);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_set_swapchain_params(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    int32_t comp = sdl_arg_i32(vm, base, 2), mode = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (dev && win &&
        SDL_SetGPUSwapchainParameters(dev, win, (SDL_GPUSwapchainComposition)comp, (SDL_GPUPresentMode)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* GPU Phase 2 constants */
SDL_CONST_FN(GPU_TEXTUREUSAGE_SAMPLER, SDL_GPU_TEXTUREUSAGE_SAMPLER)
SDL_CONST_FN(GPU_TEXTUREUSAGE_COLOR_TARGET, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
SDL_CONST_FN(GPU_TEXTURETYPE_2D, SDL_GPU_TEXTURETYPE_2D)
SDL_CONST_FN(GPU_SAMPLECOUNT_1, SDL_GPU_SAMPLECOUNT_1)
SDL_CONST_FN(GPU_SAMPLECOUNT_4, SDL_GPU_SAMPLECOUNT_4)
SDL_CONST_FN(GPU_FILTER_NEAREST, SDL_GPU_FILTER_NEAREST)
SDL_CONST_FN(GPU_FILTER_LINEAR, SDL_GPU_FILTER_LINEAR)
SDL_CONST_FN(GPU_SAMPLERADDRESSMODE_REPEAT, SDL_GPU_SAMPLERADDRESSMODE_REPEAT)
SDL_CONST_FN(GPU_SAMPLERADDRESSMODE_CLAMP, SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE)
SDL_CONST_FN(GPU_INDEXELEMENTSIZE_16, SDL_GPU_INDEXELEMENTSIZE_16BIT)
SDL_CONST_FN(GPU_INDEXELEMENTSIZE_32, SDL_GPU_INDEXELEMENTSIZE_32BIT)
SDL_CONST_FN(GPU_PRESENTMODE_VSYNC, SDL_GPU_PRESENTMODE_VSYNC)
SDL_CONST_FN(GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_IMMEDIATE)
SDL_CONST_FN(GPU_PRESENTMODE_MAILBOX, SDL_GPU_PRESENTMODE_MAILBOX)
SDL_CONST_FN(GPU_SWAPCHAIN_SDR, SDL_GPU_SWAPCHAINCOMPOSITION_SDR)

/* ── GPU Phase 3: Complete ────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(gpu_compute_pipelines);
SDL_HANDLE_REGISTRY(gpu_compute_passes);
SDL_HANDLE_REGISTRY(gpu_fences);

/* Simple query functions */
static vigil_status_t sdl_fn_gpu_num_drivers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetNumGPUDrivers(), error);
}

static vigil_status_t sdl_fn_gpu_get_driver_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetGPUDriver(idx), error);
}

static vigil_status_t sdl_fn_gpu_supports_shader_formats(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_GPUSupportsShaderFormats((SDL_GPUShaderFormat)fmt, NULL), error);
}

static vigil_status_t sdl_fn_gpu_texture_supports_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t fmt = sdl_arg_i32(vm, base, 1);
    int32_t type = sdl_arg_i32(vm, base, 2);
    int32_t usage = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    return sdl_push_bool(vm,
                         dev && SDL_GPUTextureSupportsFormat(dev, (SDL_GPUTextureFormat)fmt, (SDL_GPUTextureType)type,
                                                             (SDL_GPUTextureUsageFlags)usage),
                         error);
}

static vigil_status_t sdl_fn_gpu_texture_supports_sample_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t fmt = sdl_arg_i32(vm, base, 1);
    int32_t sc = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    return sdl_push_bool(
        vm, dev && SDL_GPUTextureSupportsSampleCount(dev, (SDL_GPUTextureFormat)fmt, (SDL_GPUSampleCount)sc), error);
}

static vigil_status_t sdl_fn_gpu_set_allowed_frames(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t n = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    if (dev && SDL_SetGPUAllowedFramesInFlight(dev, (Uint32)n))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* Naming / debug */
static vigil_status_t sdl_fn_gpu_set_buffer_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    char name[256];
    sdl_arg_str(vm, base, 2, name, sizeof(name));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (dev && buf)
        SDL_SetGPUBufferName(dev, buf, name);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_set_texture_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t th = sdl_arg_i64(vm, base, 1);
    char name[256];
    sdl_arg_str(vm, base, 2, name, sizeof(name));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    if (dev && tex)
        SDL_SetGPUTextureName(dev, tex, name);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_push_debug_group(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    if (cmd)
        SDL_PushGPUDebugGroup(cmd, name);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_pop_debug_group(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    if (cmd)
        SDL_PopGPUDebugGroup(cmd);
    return VIGIL_STATUS_OK;
}

/* Render state extras */
static vigil_status_t sdl_fn_gpu_set_blend_constants(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    float r = (float)sdl_arg_f64(vm, base, 1), g = (float)sdl_arg_f64(vm, base, 2), b = (float)sdl_arg_f64(vm, base, 3),
          a = (float)sdl_arg_f64(vm, base, 4);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    if (pass)
    {
        SDL_FColor c = {r, g, b, a};
        SDL_SetGPUBlendConstants(pass, c);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_set_stencil_ref(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    int32_t ref = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    if (pass)
        SDL_SetGPUStencilReference(pass, (Uint8)ref);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_vertex_samplers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    int64_t th = sdl_arg_i64(vm, base, 1);
    int64_t sh = sdl_arg_i64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    SDL_GPUSampler *samp = (SDL_GPUSampler *)SDL_HANDLE_GET(gpu_samplers, sh);
    if (pass && tex && samp)
    {
        SDL_GPUTextureSamplerBinding tsb = {tex, samp};
        SDL_BindGPUVertexSamplers(pass, 0, &tsb, 1);
    }
    return VIGIL_STATUS_OK;
}

/* Indirect draw */
static vigil_status_t sdl_fn_gpu_draw_indirect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t off = sdl_arg_i32(vm, base, 2);
    int32_t cnt = sdl_arg_i32(vm, base, 3);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (pass && buf)
        SDL_DrawGPUPrimitivesIndirect(pass, buf, (Uint32)off, (Uint32)cnt);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_draw_indexed_indirect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t off = sdl_arg_i32(vm, base, 2);
    int32_t cnt = sdl_arg_i32(vm, base, 3);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (pass && buf)
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, buf, (Uint32)off, (Uint32)cnt);
    return VIGIL_STATUS_OK;
}

/* Fences */
static vigil_status_t sdl_fn_gpu_submit_and_fence(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    if (!cmd)
        return sdl_push_i64(vm, -1, error);
    SDL_GPUFence *f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_HANDLE_CLEAR(gpu_cmd_buffers, ch);
    if (!f)
        return sdl_push_i64(vm, -1, error);
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_fences, f, &h) < 0)
        return sdl_push_i64(vm, -1, error);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_gpu_query_fence(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t fh = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUFence *f = (SDL_GPUFence *)SDL_HANDLE_GET(gpu_fences, fh);
    return sdl_push_bool(vm, dev && f && SDL_QueryGPUFence(dev, f), error);
}

static vigil_status_t sdl_fn_gpu_release_fence(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t fh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUFence *f = (SDL_GPUFence *)SDL_HANDLE_GET(gpu_fences, fh);
    if (dev && f)
    {
        SDL_ReleaseGPUFence(dev, f);
        SDL_HANDLE_CLEAR(gpu_fences, fh);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_wait_fences(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t fh = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUFence *f = (SDL_GPUFence *)SDL_HANDLE_GET(gpu_fences, fh);
    if (dev && f && SDL_WaitForGPUFences(dev, true, &f, 1))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* Swapchain extras */
static vigil_status_t sdl_fn_gpu_wait_swapchain(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (dev && win && SDL_WaitForGPUSwapchain(dev, win))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gpu_wait_acquire_swapchain(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (!cmd || !win)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid cmd/window", SDL_ERR_ARG, error);
    }
    SDL_GPUTexture *tex = NULL;
    Uint32 w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &tex, &w, &h) || !tex)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t th;
    if (SDL_HANDLE_STORE(gpu_textures_gpu, tex, &th) < 0)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many GPU textures", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, th, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_window_supports_present(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    int32_t mode = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    return sdl_push_bool(vm, dev && win && SDL_WindowSupportsGPUPresentMode(dev, win, (SDL_GPUPresentMode)mode), error);
}

static vigil_status_t sdl_fn_gpu_window_supports_composition(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t wh = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    int32_t comp = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    return sdl_push_bool(
        vm, dev && win && SDL_WindowSupportsGPUSwapchainComposition(dev, win, (SDL_GPUSwapchainComposition)comp),
        error);
}

/* GPU constants */

/* Compute pipeline */
static vigil_status_t sdl_fn_gpu_create_compute_pipeline(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t buf = sdl_arg_i64(vm, base, 1);
    int32_t n_samp = sdl_arg_i32(vm, base, 2);
    int32_t n_ro_tex = sdl_arg_i32(vm, base, 3);
    int32_t n_ro_buf = sdl_arg_i32(vm, base, 4);
    int32_t n_rw_tex = sdl_arg_i32(vm, base, 5);
    int32_t n_rw_buf = sdl_arg_i32(vm, base, 6);
    int32_t n_ubuf = sdl_arg_i32(vm, base, 7);
    int32_t tx = sdl_arg_i32(vm, base, 8);
    int32_t ty = sdl_arg_i32(vm, base, 9);
    int32_t tz = sdl_arg_i32(vm, base, 10);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    int32_t code_size = 0;
    void *code = vigil_unsafe_buffer_get(buf, &code_size);
    if (!dev || !code)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid device or buffer", SDL_ERR_ARG, error);
    }
    SDL_GPUComputePipelineCreateInfo ci = {0};
    ci.code = (const Uint8 *)code;
    ci.code_size = (size_t)code_size;
    ci.entrypoint = "main";
    ci.format = SDL_GetGPUShaderFormats(dev);
    ci.num_samplers = (Uint32)n_samp;
    ci.num_readonly_storage_textures = (Uint32)n_ro_tex;
    ci.num_readonly_storage_buffers = (Uint32)n_ro_buf;
    ci.num_readwrite_storage_textures = (Uint32)n_rw_tex;
    ci.num_readwrite_storage_buffers = (Uint32)n_rw_buf;
    ci.num_uniform_buffers = (Uint32)n_ubuf;
    ci.threadcount_x = (Uint32)tx;
    ci.threadcount_y = (Uint32)ty;
    ci.threadcount_z = (Uint32)tz;
    SDL_GPUComputePipeline *pip = SDL_CreateGPUComputePipeline(dev, &ci);
    if (!pip)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_compute_pipelines, pip, &h) < 0)
    {
        SDL_ReleaseGPUComputePipeline(dev, pip);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many compute pipelines", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gpu_release_compute_pipeline(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t ph = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, dh);
    SDL_GPUComputePipeline *pip = (SDL_GPUComputePipeline *)SDL_HANDLE_GET(gpu_compute_pipelines, ph);
    if (dev && pip)
    {
        SDL_ReleaseGPUComputePipeline(dev, pip);
        SDL_HANDLE_CLEAR(gpu_compute_pipelines, ph);
    }
    return VIGIL_STATUS_OK;
}

/* Compute pass */
static vigil_status_t sdl_fn_gpu_begin_compute_pass(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    if (!cmd)
        return sdl_push_i64(vm, -1, error);
    SDL_GPUComputePass *cp = SDL_BeginGPUComputePass(cmd, NULL, 0, NULL, 0);
    if (!cp)
        return sdl_push_i64(vm, -1, error);
    int64_t h;
    if (SDL_HANDLE_STORE(gpu_compute_passes, cp, &h) < 0)
        return sdl_push_i64(vm, -1, error);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_gpu_end_compute_pass(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUComputePass *cp = (SDL_GPUComputePass *)SDL_HANDLE_GET(gpu_compute_passes, cph);
    if (cp)
    {
        SDL_EndGPUComputePass(cp);
        SDL_HANDLE_CLEAR(gpu_compute_passes, cph);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_compute_pipeline(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0);
    int64_t pih = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUComputePass *cp = (SDL_GPUComputePass *)SDL_HANDLE_GET(gpu_compute_passes, cph);
    SDL_GPUComputePipeline *pip = (SDL_GPUComputePipeline *)SDL_HANDLE_GET(gpu_compute_pipelines, pih);
    if (cp && pip)
        SDL_BindGPUComputePipeline(cp, pip);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_dispatch_compute(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0);
    int32_t gx = sdl_arg_i32(vm, base, 1);
    int32_t gy = sdl_arg_i32(vm, base, 2);
    int32_t gz = sdl_arg_i32(vm, base, 3);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUComputePass *cp = (SDL_GPUComputePass *)SDL_HANDLE_GET(gpu_compute_passes, cph);
    if (cp)
        SDL_DispatchGPUCompute(cp, (Uint32)gx, (Uint32)gy, (Uint32)gz);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_push_compute_uniforms(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    int32_t slot = sdl_arg_i32(vm, base, 1);
    int64_t bh = sdl_arg_i64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCommandBuffer *cmd = (SDL_GPUCommandBuffer *)SDL_HANDLE_GET(gpu_cmd_buffers, ch);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(bh, &sz);
    if (cmd && data)
        SDL_PushGPUComputeUniformData(cmd, (Uint32)slot, data, (Uint32)sz);
    return VIGIL_STATUS_OK;
}

/* Copy pass extras */
static vigil_status_t sdl_fn_gpu_copy_buffer_to_buffer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0);
    int64_t sh = sdl_arg_i64(vm, base, 1);
    int32_t soff = sdl_arg_i32(vm, base, 2);
    int64_t dh = sdl_arg_i64(vm, base, 3);
    int32_t doff = sdl_arg_i32(vm, base, 4);
    int32_t sz = sdl_arg_i32(vm, base, 5);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCopyPass *cp = (SDL_GPUCopyPass *)SDL_HANDLE_GET(gpu_copy_passes, cph);
    SDL_GPUBuffer *src = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, sh);
    SDL_GPUBuffer *dst = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, dh);
    if (cp && src && dst)
    {
        SDL_GPUBufferLocation sl = {src, (Uint32)soff};
        SDL_GPUBufferLocation dl = {dst, (Uint32)doff};
        SDL_CopyGPUBufferToBuffer(cp, &sl, &dl, (Uint32)sz, false);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_download_from_buffer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t boff = sdl_arg_i32(vm, base, 2);
    int32_t sz = sdl_arg_i32(vm, base, 3);
    int64_t xh = sdl_arg_i64(vm, base, 4);
    int32_t xoff = sdl_arg_i32(vm, base, 5);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCopyPass *cp = (SDL_GPUCopyPass *)SDL_HANDLE_GET(gpu_copy_passes, cph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    SDL_GPUTransferBuffer *xb = (SDL_GPUTransferBuffer *)SDL_HANDLE_GET(gpu_xfer_buffers, xh);
    if (cp && buf && xb)
    {
        SDL_GPUBufferRegion src = {buf, (Uint32)boff, (Uint32)sz};
        SDL_GPUTransferBufferLocation dst = {xb, (Uint32)xoff};
        SDL_DownloadFromGPUBuffer(cp, &src, &dst);
    }
    return VIGIL_STATUS_OK;
}

/* Upload to GPU texture */
static vigil_status_t sdl_fn_gpu_upload_to_texture(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0);
    int64_t xh = sdl_arg_i64(vm, base, 1);
    int32_t xoff = sdl_arg_i32(vm, base, 2);
    int64_t th = sdl_arg_i64(vm, base, 3);
    int32_t w = sdl_arg_i32(vm, base, 4);
    int32_t h = sdl_arg_i32(vm, base, 5);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCopyPass *cp = (SDL_GPUCopyPass *)SDL_HANDLE_GET(gpu_copy_passes, cph);
    SDL_GPUTransferBuffer *xb = (SDL_GPUTransferBuffer *)SDL_HANDLE_GET(gpu_xfer_buffers, xh);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    if (cp && xb && tex)
    {
        SDL_GPUTextureTransferInfo src = {xb, (Uint32)xoff, 0, 0};
        SDL_GPUTextureRegion dst = {tex, 0, 0, 0, 0, 0, (Uint32)w, (Uint32)h, 1};
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
    }
    return VIGIL_STATUS_OK;
}

/* ── SDL stdinc: Math ─────────────────────────────────────────────── */

#define SDL_MATH_F64(name, fn)                                                                                         \
    static vigil_status_t sdl_fn_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                        \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        double v = sdl_arg_f64(vm, base, 0);                                                                           \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        return sdl_push_f64(vm, (double)fn((double)v), error);                                                         \
    }

#define SDL_MATH_F64_F(name, fn)                                                                                       \
    static vigil_status_t sdl_fn_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                        \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        double v = sdl_arg_f64(vm, base, 0);                                                                           \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        return sdl_push_f64(vm, (double)fn((float)v), error);                                                          \
    }

#define SDL_MATH_F64_2(name, fn)                                                                                       \
    static vigil_status_t sdl_fn_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                        \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        double a = sdl_arg_f64(vm, base, 0);                                                                           \
        double b = sdl_arg_f64(vm, base, 1);                                                                           \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        return sdl_push_f64(vm, (double)fn((double)a, (double)b), error);                                              \
    }

#define SDL_MATH_F64_2F(name, fn)                                                                                      \
    static vigil_status_t sdl_fn_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                        \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        double a = sdl_arg_f64(vm, base, 0);                                                                           \
        double b = sdl_arg_f64(vm, base, 1);                                                                           \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        return sdl_push_f64(vm, (double)fn((float)a, (float)b), error);                                                \
    }

/* clang-format off */
SDL_MATH_F64(m_acos, SDL_acos)
SDL_MATH_F64_F(m_acosf, SDL_acosf)
SDL_MATH_F64(m_asin, SDL_asin)
SDL_MATH_F64_F(m_asinf, SDL_asinf)
SDL_MATH_F64(m_atan, SDL_atan)
SDL_MATH_F64_F(m_atanf, SDL_atanf)
SDL_MATH_F64_2(m_atan2, SDL_atan2)
SDL_MATH_F64_2F(m_atan2f, SDL_atan2f)
SDL_MATH_F64(m_ceil, SDL_ceil)
SDL_MATH_F64_F(m_ceilf, SDL_ceilf)
SDL_MATH_F64(m_cos, SDL_cos)
SDL_MATH_F64_F(m_cosf, SDL_cosf)
SDL_MATH_F64(m_exp, SDL_exp)
SDL_MATH_F64_F(m_expf, SDL_expf)
SDL_MATH_F64(m_fabs, SDL_fabs)
SDL_MATH_F64_F(m_fabsf, SDL_fabsf)
SDL_MATH_F64(m_floor, SDL_floor)
SDL_MATH_F64_F(m_floorf, SDL_floorf)
SDL_MATH_F64_2(m_fmod, SDL_fmod)
SDL_MATH_F64_2F(m_fmodf, SDL_fmodf)
SDL_MATH_F64(m_log, SDL_log)
SDL_MATH_F64_F(m_logf, SDL_logf)
SDL_MATH_F64(m_log10, SDL_log10)
SDL_MATH_F64_F(m_log10f, SDL_log10f)
SDL_MATH_F64_2(m_pow, SDL_pow)
SDL_MATH_F64_2F(m_powf, SDL_powf)
SDL_MATH_F64(m_round, SDL_round)
SDL_MATH_F64_F(m_roundf, SDL_roundf)
SDL_MATH_F64(m_sin, SDL_sin)
SDL_MATH_F64_F(m_sinf, SDL_sinf)
SDL_MATH_F64(m_sqrt, SDL_sqrt)
SDL_MATH_F64_F(m_sqrtf, SDL_sqrtf)
SDL_MATH_F64(m_tan, SDL_tan)
SDL_MATH_F64_F(m_tanf, SDL_tanf)
SDL_MATH_F64(m_trunc, SDL_trunc)
SDL_MATH_F64_F(m_truncf, SDL_truncf)
SDL_MATH_F64_2(m_copysign, SDL_copysign)
SDL_MATH_F64_2F(m_copysignf, SDL_copysignf)

#define SDL_CHAR_FN(name, fn)                                                                                          \
    static vigil_status_t sdl_fn_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                        \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        int32_t c = sdl_arg_i32(vm, base, 0);                                                                          \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        return sdl_push_bool(vm, fn(c), error);                                                                        \
    }

SDL_CHAR_FN(c_isalnum, SDL_isalnum)
SDL_CHAR_FN(c_isalpha, SDL_isalpha)
SDL_CHAR_FN(c_isblank, SDL_isblank)
SDL_CHAR_FN(c_iscntrl, SDL_iscntrl)
SDL_CHAR_FN(c_isdigit, SDL_isdigit)
SDL_CHAR_FN(c_isgraph, SDL_isgraph)
SDL_CHAR_FN(c_islower, SDL_islower)
SDL_CHAR_FN(c_isprint, SDL_isprint)
SDL_CHAR_FN(c_ispunct, SDL_ispunct)
SDL_CHAR_FN(c_isspace, SDL_isspace)
SDL_CHAR_FN(c_isupper, SDL_isupper)
SDL_CHAR_FN(c_isxdigit, SDL_isxdigit)
/* clang-format on */

static vigil_status_t sdl_fn_m_scalbn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = sdl_arg_f64(vm, base, 0);
    int n = (int)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, SDL_scalbn(x, n), error);
}

static vigil_status_t sdl_fn_m_lround(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double v = sdl_arg_f64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_lround(v), error);
}

static vigil_status_t sdl_fn_m_lroundf(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double v = sdl_arg_f64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_lroundf((float)v), error);
}

static vigil_status_t sdl_fn_m_isinf(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double v = sdl_arg_f64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_isinf(v), error);
}

static vigil_status_t sdl_fn_m_isnan(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double v = sdl_arg_f64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_isnan(v), error);
}

static vigil_status_t sdl_fn_m_abs(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t v = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_abs(v), error);
}

static vigil_status_t sdl_fn_c_toupper(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t c = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_toupper(c), error);
}

static vigil_status_t sdl_fn_c_tolower(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t c = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_tolower(c), error);
}

/* ── SDL stdinc: String functions ─────────────────────────────────── */

static vigil_status_t sdl_fn_s_strlen(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char buf[4096];
    sdl_arg_str(vm, base, 0, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_strlen(buf), error);
}

static vigil_status_t sdl_fn_s_strcmp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096], b[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    sdl_arg_str(vm, base, 1, b, sizeof(b));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_strcmp(a, b), error);
}

static vigil_status_t sdl_fn_s_strncmp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096], b[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    sdl_arg_str(vm, base, 1, b, sizeof(b));
    int32_t n = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_strncmp(a, b, (size_t)n), error);
}

static vigil_status_t sdl_fn_s_strcasecmp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096], b[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    sdl_arg_str(vm, base, 1, b, sizeof(b));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_strcasecmp(a, b), error);
}

static vigil_status_t sdl_fn_s_strncasecmp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096], b[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    sdl_arg_str(vm, base, 1, b, sizeof(b));
    int32_t n = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_strncasecmp(a, b, (size_t)n), error);
}

static vigil_status_t sdl_fn_s_strstr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096], b[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    sdl_arg_str(vm, base, 1, b, sizeof(b));
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *p = SDL_strstr(a, b);
    return sdl_push_i32(vm, p ? (int32_t)(p - a) : -1, error);
}

static vigil_status_t sdl_fn_s_strchr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    int32_t c = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *p = SDL_strchr(a, c);
    return sdl_push_i32(vm, p ? (int32_t)(p - a) : -1, error);
}

static vigil_status_t sdl_fn_s_strrchr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    int32_t c = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *p = SDL_strrchr(a, c);
    return sdl_push_i32(vm, p ? (int32_t)(p - a) : -1, error);
}

static vigil_status_t sdl_fn_s_strupr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_strupr(a);
    return sdl_push_string(vm, a, error);
}

static vigil_status_t sdl_fn_s_strlwr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_strlwr(a);
    return sdl_push_string(vm, a, error);
}

static vigil_status_t sdl_fn_s_strrev(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_strrev(a);
    return sdl_push_string(vm, a, error);
}

/* Conversion: string -> number */
static vigil_status_t sdl_fn_s_atoi(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_atoi(a), error);
}

static vigil_status_t sdl_fn_s_atof(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, SDL_atof(a), error);
}

static vigil_status_t sdl_fn_s_strtol(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_strtol(a, NULL, radix), error);
}

static vigil_status_t sdl_fn_s_strtoul(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_strtoul(a, NULL, radix), error);
}

static vigil_status_t sdl_fn_s_strtod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, SDL_strtod(a, NULL), error);
}

/* Conversion: number -> string */
static vigil_status_t sdl_fn_s_itoa(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t v = sdl_arg_i32(vm, base, 0);
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[68];
    SDL_itoa(v, buf, radix);
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_s_lltoa(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t v = sdl_arg_i64(vm, base, 0);
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[68];
    SDL_lltoa(v, buf, radix);
    return sdl_push_string(vm, buf, error);
}

/* UTF-8 */
static vigil_status_t sdl_fn_s_utf8strlen(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_utf8strlen(a), error);
}

static vigil_status_t sdl_fn_s_utf8strnlen(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    int32_t n = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_utf8strnlen(a, (size_t)n), error);
}

/* ── SDL stdinc: Random, Hash, Bit, Env ───────────────────────────── */

static vigil_status_t sdl_fn_r_srand(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t seed = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_srand((Uint64)seed);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_r_rand(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t n = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_rand(n), error);
}

static vigil_status_t sdl_fn_r_randf(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, (double)SDL_randf(), error);
}

static vigil_status_t sdl_fn_r_rand_bits(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_rand_bits(), error);
}

static vigil_status_t sdl_fn_r_crc16(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t crc_init = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(bh, &sz);
    if (!data)
        return sdl_push_i32(vm, 0, error);
    return sdl_push_i32(vm, (int32_t)SDL_crc16((Uint16)crc_init, (const Uint8 *)data, (size_t)sz), error);
}

static vigil_status_t sdl_fn_r_crc32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t crc_init = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(bh, &sz);
    if (!data)
        return sdl_push_i32(vm, 0, error);
    return sdl_push_i32(vm, (int32_t)SDL_crc32((Uint32)crc_init, (const Uint8 *)data, (size_t)sz), error);
}

static vigil_status_t sdl_fn_r_murmur3_32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t seed = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(bh, &sz);
    if (!data)
        return sdl_push_i32(vm, 0, error);
    return sdl_push_i32(vm, (int32_t)SDL_murmur3_32(data, (size_t)sz, (Uint32)seed), error);
}

static vigil_status_t sdl_fn_r_msb32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t v = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_MostSignificantBitIndex32((Uint32)v), error);
}

static vigil_status_t sdl_fn_r_has_one_bit32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t v = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasExactlyOneBitSet32((Uint32)v), error);
}

static vigil_status_t sdl_fn_e_getenv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *val = SDL_getenv(name);
    return sdl_push_string(vm, val ? val : "", error);
}

static vigil_status_t sdl_fn_e_setenv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256], val[4096];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    sdl_arg_str(vm, base, 1, val, sizeof(val));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_setenv_unsafe(name, val, 1), error);
}

static vigil_status_t sdl_fn_e_unsetenv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_unsetenv_unsafe(name), error);
}

/* Wide string basics */
static vigil_status_t sdl_fn_s_wcslen(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    vigil_vm_stack_pop_n(vm, arg_count); /* Convert to wchar for length count — approximate with utf8strlen */
    return sdl_push_i32(vm, (int32_t)SDL_utf8strlen(a), error);
}

SDL_CONST_FN(GPU_SHADERFORMAT_SPIRV, SDL_GPU_SHADERFORMAT_SPIRV)
SDL_CONST_FN(GPU_SHADERFORMAT_MSL, SDL_GPU_SHADERFORMAT_MSL)
SDL_CONST_FN(GPU_SHADERFORMAT_METALLIB, SDL_GPU_SHADERFORMAT_METALLIB)
SDL_CONST_FN(GPU_SHADERSTAGE_VERTEX, SDL_GPU_SHADERSTAGE_VERTEX)
SDL_CONST_FN(GPU_SHADERSTAGE_FRAGMENT, SDL_GPU_SHADERSTAGE_FRAGMENT)
SDL_CONST_FN(GPU_PRIMITIVETYPE_TRIANGLELIST, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST)
SDL_CONST_FN(GPU_PRIMITIVETYPE_TRIANGLESTRIP, SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP)
SDL_CONST_FN(GPU_PRIMITIVETYPE_LINELIST, SDL_GPU_PRIMITIVETYPE_LINELIST)
SDL_CONST_FN(GPU_PRIMITIVETYPE_LINESTRIP, SDL_GPU_PRIMITIVETYPE_LINESTRIP)
SDL_CONST_FN(GPU_PRIMITIVETYPE_POINTLIST, SDL_GPU_PRIMITIVETYPE_POINTLIST)
SDL_CONST_FN(GPU_LOADOP_LOAD, SDL_GPU_LOADOP_LOAD)
SDL_CONST_FN(GPU_LOADOP_CLEAR, SDL_GPU_LOADOP_CLEAR)
SDL_CONST_FN(GPU_LOADOP_DONT_CARE, SDL_GPU_LOADOP_DONT_CARE)
SDL_CONST_FN(GPU_STOREOP_STORE, SDL_GPU_STOREOP_STORE)
SDL_CONST_FN(GPU_STOREOP_DONT_CARE, SDL_GPU_STOREOP_DONT_CARE)
SDL_CONST_FN(GPU_BUFFERUSAGE_VERTEX, SDL_GPU_BUFFERUSAGE_VERTEX)
SDL_CONST_FN(GPU_BUFFERUSAGE_INDEX, SDL_GPU_BUFFERUSAGE_INDEX)
SDL_CONST_FN(GPU_XFER_UPLOAD, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD)
SDL_CONST_FN(GPU_XFER_DOWNLOAD, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD)
SDL_CONST_FN(GPU_VERTEXFORMAT_FLOAT2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2)
SDL_CONST_FN(GPU_VERTEXFORMAT_FLOAT3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3)
SDL_CONST_FN(GPU_VERTEXFORMAT_FLOAT4, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4)
SDL_CONST_FN(GPU_VERTEXFORMAT_UBYTE4_NORM, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM)

/* ── Window Complete ──────────────────────────────────────────────── */

static vigil_status_t sdl_fn_create_window_and_renderer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char title[256];
    sdl_arg_str(vm, base, 0, title, sizeof(title));
    int32_t w = sdl_arg_i32(vm, base, 1), h = sdl_arg_i32(vm, base, 2), flags = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (!SDL_CreateWindowAndRenderer(title, w, h, (SDL_WindowFlags)flags, &win, &ren))
        return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
    int64_t wh = -1, rh = -1;
    SDL_HANDLE_STORE(windows, win, &wh);
    SDL_HANDLE_STORE(renderers, ren, &rh);
    /* Return window handle; renderer handle = wh+1 convention won't work.
       Return (i64 win_handle, err). User gets renderer via get_render_window. */
    vigil_status_t st = sdl_push_i64(vm, wh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_create_window_with_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t props = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Window *win = SDL_CreateWindowWithProperties((SDL_PropertiesID)props);
    if (!win)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(windows, win, &handle) < 0)
    {
        SDL_DestroyWindow(win);
        return sdl_push_nil_and_err(vm, "too many windows", SDL_ERR_STATE, error);
    }

    vigil_status_t st = sdl_push_handle_instance(vm, SDL_WINDOW_CLASS_INDEX, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_create_renderer_with_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t props = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_Renderer *ren = SDL_CreateRendererWithProperties((SDL_PropertiesID)props);
    if (!ren)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);

    int64_t handle;
    if (SDL_HANDLE_STORE(renderers, ren, &handle) < 0)
    {
        SDL_DestroyRenderer(ren);
        return sdl_push_nil_and_err(vm, "too many renderers", SDL_ERR_STATE, error);
    }

    vigil_status_t st = sdl_push_handle_instance(vm, SDL_RENDERER_CLASS_INDEX, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_window_set_fullscreen_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    /* Pass NULL to use desktop fullscreen mode */
    if (win && SDL_SetWindowFullscreenMode(win, NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_set_modal(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t modal = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowModal(win, modal != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_set_focusable(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t focusable = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowFocusable(win, focusable != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_show_system_menu(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_ShowWindowSystemMenu(win, x, y))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_set_shape(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    int64_t sh = sdl_field_i64(vm, base + 1, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    SDL_Surface *surf = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    if (win && surf && SDL_SetWindowShape(win, surf))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_get_progress_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_i32(vm, win ? (int32_t)SDL_GetWindowProgressState(win) : 0, error);
}

static vigil_status_t sdl_window_get_progress_value(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_f64(vm, win ? (double)SDL_GetWindowProgressValue(win) : 0.0, error);
}

static vigil_status_t sdl_window_has_surface(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_bool(vm, win && SDL_WindowHasSurface(win), error);
}

static vigil_status_t sdl_window_get_fullscreen_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    const SDL_DisplayMode *mode = NULL;
    SDL_Window *win = NULL;
    vigil_status_t st;
    int32_t width = 0;
    int32_t height = 0;

    vigil_vm_stack_pop_n(vm, arg_count);
    win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    mode = win ? SDL_GetWindowFullscreenMode(win) : NULL;
    if (mode != NULL)
    {
        width = (int32_t)mode->w;
        height = (int32_t)mode->h;
    }

    st = sdl_push_i32(vm, width, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, height, error);
}

static vigil_status_t sdl_window_update_surface(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_UpdateWindowSurface(win))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_update_surface_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    SDL_Rect rect;
    SDL_Window *win;

    rect.x = sdl_arg_i32(vm, base, 1);
    rect.y = sdl_arg_i32(vm, base, 2);
    rect.w = sdl_arg_i32(vm, base, 3);
    rect.h = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);

    win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_UpdateWindowSurfaceRects(win, &rect, 1))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_update_surface_rects(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int64_t rect_buffer = sdl_arg_i64(vm, base, 1);
    int32_t rect_count = sdl_arg_i32(vm, base, 2);
    SDL_Window *win = NULL;
    SDL_Rect *rects = NULL;
    int32_t buffer_size = 0;
    int32_t *raw = NULL;
    vigil_status_t st = VIGIL_STATUS_OK;

    vigil_vm_stack_pop_n(vm, arg_count);
    if (rect_count <= 0)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);

    raw = (int32_t *)vigil_unsafe_buffer_get(rect_buffer, &buffer_size);
    if (!raw || buffer_size < rect_count * (int32_t)(4 * sizeof(int32_t)))
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);

    rects = (SDL_Rect *)malloc((size_t)rect_count * sizeof(SDL_Rect));
    if (!rects)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    for (int32_t i = 0; i < rect_count; i++)
    {
        rects[i].x = raw[i * 4];
        rects[i].y = raw[i * 4 + 1];
        rects[i].w = raw[i * 4 + 2];
        rects[i].h = raw[i * 4 + 3];
    }

    win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_UpdateWindowSurfaceRects(win, rects, rect_count))
        st = sdl_push_bool_ok(vm, error);
    else
        st = sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);

    free(rects);
    return st;
}

static vigil_status_t sdl_window_destroy_surface(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_DestroyWindowSurface(win))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_show_message_box(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t flags = sdl_arg_i32(vm, base, 1);
    SDL_Window *win;
    char title[256];
    char message[4096];
    char buttons[1024];

    sdl_arg_str(vm, base, 2, title, sizeof(title));
    sdl_arg_str(vm, base, 3, message, sizeof(message));
    sdl_arg_str(vm, base, 4, buttons, sizeof(buttons));
    vigil_vm_stack_pop_n(vm, arg_count);

    win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_show_message_box_common(vm, win, flags, title, message, buttons, error);
}

static vigil_status_t sdl_window_set_surface_vsync(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t vsync = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowSurfaceVSync(win, vsync))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_window_get_surface_vsync(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int vsync = 0;
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win)
        SDL_GetWindowSurfaceVSync(win, &vsync);
    return sdl_push_i32(vm, vsync, error);
}

/* Module-level window functions */
static vigil_status_t sdl_fn_get_grabbed_window(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = SDL_GetGrabbedWindow();
    if (!win)
        return sdl_push_i32(vm, -1, error);
    /* Find in registry */
    for (int64_t i = 0; i < (int64_t)g_windows.count; i++)
        if (g_windows.items[i] == win)
            return sdl_push_i32(vm, (int32_t)i, error);
    return sdl_push_i32(vm, -1, error);
}

/* ── Window: remaining functions ──────────────────────────────────── */

/* win.create_popup(i32 x, i32 y, i32 w, i32 h, i32 flags) -> (i64, err) */
static vigil_status_t sdl_window_create_popup(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t ox = sdl_arg_i32(vm, base, 1), oy = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), h = sdl_arg_i32(vm, base, 4);
    int32_t flags = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *parent = (SDL_Window *)SDL_HANDLE_GET(windows, ph);
    if (!parent)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid parent window", SDL_ERR_ARG, error);
    }
    SDL_Window *popup = SDL_CreatePopupWindow(parent, ox, oy, w, h, (SDL_WindowFlags)flags);
    if (!popup)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t wh;
    if (SDL_HANDLE_STORE(windows, popup, &wh) < 0)
    {
        SDL_DestroyWindow(popup);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many windows", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, wh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* win.set_parent(Window parent) -> (bool, err) — pass nil-handle window to unparent */
static vigil_status_t sdl_window_set_parent_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    int64_t ph = sdl_field_i64(vm, base + 1, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    SDL_Window *parent = (SDL_Window *)SDL_HANDLE_GET(windows, ph);
    if (win && SDL_SetWindowParent(win, parent))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* win.set_fill_document(bool fill) -> (bool, err) */
static vigil_status_t sdl_window_set_fill_document(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    int32_t fill = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    if (win && SDL_SetWindowFillDocument(win, fill != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* HitTest callback support */
static vigil_vm_t *g_hit_test_vm = NULL;
static vigil_object_t *g_hit_test_closure = NULL;

static SDL_HitTestResult sdl_hit_test_callback(SDL_Window *win, const SDL_Point *area, void *data)
{
    (void)win;
    (void)data;
    if (!g_hit_test_vm || !g_hit_test_closure)
        return SDL_HITTEST_NORMAL;
    vigil_error_t err = {0};
    vigil_value_t args[2];
    args[0] = vigil_nanbox_encode_i32(area->x);
    args[1] = vigil_nanbox_encode_i32(area->y);
    for (int i = 0; i < 2; i++)
        vigil_vm_stack_push(g_hit_test_vm, &args[i], &err);
    vigil_value_t result = {0};
    vigil_vm_execute_function(g_hit_test_vm, g_hit_test_closure, &result, &err);
    int32_t r = vigil_nanbox_is_int(result) ? (int32_t)vigil_nanbox_decode_int(result) : 0;
    return (SDL_HitTestResult)r;
}

/* win.set_hit_test(fn callback) -> (bool, err)
 * The callback receives (i32 x, i32 y) -> i32 (HITTEST_* constant) */
static vigil_status_t sdl_window_set_hit_test(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_value_t fn_val = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (!win)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);

    /* Release previous closure if any */
    if (g_hit_test_closure)
    {
        vigil_object_release(&g_hit_test_closure);
        g_hit_test_closure = NULL;
    }

    vigil_object_t *fn = (vigil_object_t *)vigil_nanbox_decode_ptr(fn_val);
    if (fn)
    {
        vigil_object_retain(fn);
        g_hit_test_closure = fn;
        g_hit_test_vm = vm;
        if (SDL_SetWindowHitTest(win, sdl_hit_test_callback, NULL))
            return sdl_push_bool_ok(vm, error);
        vigil_object_release(&g_hit_test_closure);
        g_hit_test_closure = NULL;
        g_hit_test_vm = NULL;
        return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
    }
    /* NULL callback clears the hit test */
    SDL_SetWindowHitTest(win, NULL, NULL);
    g_hit_test_vm = NULL;
    return sdl_push_bool_ok(vm, error);
}

/* sdl.time_from_windows(i32 low, i32 high) -> i64 */
static vigil_status_t sdl_fn_time_from_windows(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t low = sdl_arg_i32(vm, base, 0), high = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_TimeFromWindows((Uint32)low, (Uint32)high), error);
}

/* sdl.time_to_windows(i64 ticks) -> (i32, i32) — low, high */
static vigil_status_t sdl_fn_time_to_windows(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ticks = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint32 low = 0, high = 0;
    SDL_TimeToWindows((SDL_Time)ticks, &low, &high);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)low, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)high, error);
}

/* HitTest result constants */
SDL_CONST_FN(HITTEST_NORMAL, SDL_HITTEST_NORMAL)
SDL_CONST_FN(HITTEST_DRAGGABLE, SDL_HITTEST_DRAGGABLE)
SDL_CONST_FN(HITTEST_RESIZE_TOPLEFT, SDL_HITTEST_RESIZE_TOPLEFT)
SDL_CONST_FN(HITTEST_RESIZE_TOP, SDL_HITTEST_RESIZE_TOP)
SDL_CONST_FN(HITTEST_RESIZE_TOPRIGHT, SDL_HITTEST_RESIZE_TOPRIGHT)
SDL_CONST_FN(HITTEST_RESIZE_RIGHT, SDL_HITTEST_RESIZE_RIGHT)
SDL_CONST_FN(HITTEST_RESIZE_BOTTOMRIGHT, SDL_HITTEST_RESIZE_BOTTOMRIGHT)
SDL_CONST_FN(HITTEST_RESIZE_BOTTOM, SDL_HITTEST_RESIZE_BOTTOM)
SDL_CONST_FN(HITTEST_RESIZE_BOTTOMLEFT, SDL_HITTEST_RESIZE_BOTTOMLEFT)
SDL_CONST_FN(HITTEST_RESIZE_LEFT, SDL_HITTEST_RESIZE_LEFT)

/* ── Renderer Complete ────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_num_render_drivers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetNumRenderDrivers(), error);
}

static vigil_status_t sdl_fn_get_render_driver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetRenderDriver(idx), error);
}

/* ren.get_draw_color_float() -> (f64, f64) — r,g packed as doubles (b,a in second) */
static vigil_status_t sdl_renderer_get_draw_color_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    float r = 0, g = 0, b = 0, a = 0;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderDrawColorFloat(ren, &r, &g, &b, &a);
    /* Pack as two f64: first = r (high 32) | g (low 32), but that's lossy.
       Better: return r as f64, caller gets one channel at a time. Or pack RGBA into i64.
       Simplest: return packed i64 with 16-bit-per-channel fixed point. */
    int64_t packed = ((int64_t)(int32_t)(r * 65535.0f) << 48) | ((int64_t)(int32_t)(g * 65535.0f) << 32) |
                     ((int64_t)(int32_t)(b * 65535.0f) << 16) | (int64_t)(int32_t)(a * 65535.0f);
    return sdl_push_i64(vm, packed, error);
}

static vigil_status_t sdl_renderer_get_safe_area(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect rect = {0, 0, 0, 0};
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderSafeArea(ren, &rect);
    vigil_status_t st = sdl_push_i32(vm, rect.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, rect.h, error);
}

static vigil_status_t sdl_renderer_viewport_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    return sdl_push_bool(vm, ren && SDL_RenderViewportSet(ren), error);
}

static vigil_status_t sdl_renderer_set_texture_address_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int32_t u = sdl_arg_i32(vm, base, 1), v = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_SetRenderTextureAddressMode(ren, (SDL_TextureAddressMode)u, (SDL_TextureAddressMode)v))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_get_texture_address_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TextureAddressMode u = SDL_TEXTURE_ADDRESS_AUTO, v = SDL_TEXTURE_ADDRESS_AUTO;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderTextureAddressMode(ren, &u, &v);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)u, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)v, error);
}

static vigil_status_t sdl_renderer_set_default_scale_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int32_t mode = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && SDL_SetDefaultTextureScaleMode(ren, (SDL_ScaleMode)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_get_default_scale_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_ScaleMode mode = SDL_SCALEMODE_NEAREST;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetDefaultTextureScaleMode(ren, &mode);
    return sdl_push_i32(vm, (int32_t)mode, error);
}

static vigil_status_t sdl_renderer_coords_from_window(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    float wx = (float)sdl_arg_f64(vm, base, 1), wy = (float)sdl_arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    float rx = 0, ry = 0;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_RenderCoordinatesFromWindow(ren, wx, wy, &rx, &ry);
    vigil_status_t st = sdl_push_f64(vm, (double)rx, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)ry, error);
}

static vigil_status_t sdl_renderer_coords_to_window(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    float rx = (float)sdl_arg_f64(vm, base, 1), ry = (float)sdl_arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    float wx = 0, wy = 0;
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_RenderCoordinatesToWindow(ren, rx, ry, &wx, &wy);
    vigil_status_t st = sdl_push_f64(vm, (double)wx, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)wy, error);
}

static vigil_status_t sdl_renderer_convert_event_coords(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    SDL_Event *ev = evt_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren && ev && SDL_ConvertEventToRenderCoordinates(ren, ev))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* Texture float mods */
static vigil_status_t sdl_texture_set_color_mod_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    float r = (float)sdl_arg_f64(vm, base, 1), g = (float)sdl_arg_f64(vm, base, 2), b = (float)sdl_arg_f64(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex && SDL_SetTextureColorModFloat(tex, r, g, b))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_texture_set_alpha_mod_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    float a = (float)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex && SDL_SetTextureAlphaModFloat(tex, a))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_texture_get_alpha_mod_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    float a = 1.0f;
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_GetTextureAlphaModFloat(tex, &a);
    return sdl_push_f64(vm, (double)a, error);
}

/* UpdateTexture — uses unsafe buffer for pixel data */
static vigil_status_t sdl_texture_update(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_field_i64(vm, base, TEX_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), h = sdl_arg_i32(vm, base, 4);
    int64_t buf = sdl_arg_i64(vm, base, 5);
    int32_t pitch = sdl_arg_i32(vm, base, 6);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    int32_t sz = 0;
    void *pixels = vigil_unsafe_buffer_get(buf, &sz);
    if (!tex || !pixels)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect rect = {x, y, w, h};
    if (SDL_UpdateTexture(tex, (w > 0 && h > 0) ? &rect : NULL, pixels, pitch))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* LockTexture — returns unsafe buffer handle for pixel data */
static vigil_status_t sdl_texture_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_field_i64(vm, base, TEX_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), h = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (!tex)
        return sdl_push_i64(vm, -1, error);
    void *pixels = NULL;
    int pitch = 0;
    SDL_Rect rect = {x, y, w, h};
    if (!SDL_LockTexture(tex, (w > 0 && h > 0) ? &rect : NULL, &pixels, &pitch))
        return sdl_push_i64(vm, -1, error);
    /* Return raw pointer as i64 — user writes via unsafe.poke_* */
    return sdl_push_i64(vm, (int64_t)(intptr_t)pixels, error);
}

static vigil_status_t sdl_texture_unlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_field_i64(vm, base, TEX_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (tex)
        SDL_UnlockTexture(tex);
    return VIGIL_STATUS_OK;
}

/* RenderPoints — takes unsafe buffer of packed float pairs (x,y) */
static vigil_status_t sdl_renderer_render_points(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t buf = sdl_arg_i64(vm, base, 1);
    int32_t count = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(buf, &sz);
    if (ren && data && SDL_RenderPoints(ren, (const SDL_FPoint *)data, count))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* RenderLines — takes unsafe buffer of packed float pairs */
static vigil_status_t sdl_renderer_render_lines(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t buf = sdl_arg_i64(vm, base, 1);
    int32_t count = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(buf, &sz);
    if (ren && data && SDL_RenderLines(ren, (const SDL_FPoint *)data, count))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* RenderRects — takes unsafe buffer of packed float quads (x,y,w,h) */
static vigil_status_t sdl_renderer_render_rects(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t buf = sdl_arg_i64(vm, base, 1);
    int32_t count = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(buf, &sz);
    if (ren && data && SDL_RenderRects(ren, (const SDL_FRect *)data, count))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_renderer_render_fill_rects(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t buf = sdl_arg_i64(vm, base, 1);
    int32_t count = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(buf, &sz);
    if (ren && data && SDL_RenderFillRects(ren, (const SDL_FRect *)data, count))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* RenderGeometry — vertices in unsafe buffer, optional indices in unsafe buffer */
static vigil_status_t sdl_renderer_render_geometry(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_arg_i64(vm, base, 1); /* texture handle or -1 */
    int64_t vbuf = sdl_arg_i64(vm, base, 2);
    int32_t num_verts = sdl_arg_i32(vm, base, 3);
    int64_t ibuf = sdl_arg_i64(vm, base, 4); /* index buffer handle or -1 */
    int32_t num_indices = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    SDL_Texture *tex = (th >= 0) ? (SDL_Texture *)SDL_HANDLE_GET(textures, th) : NULL;
    int32_t vsz = 0;
    void *verts = vigil_unsafe_buffer_get(vbuf, &vsz);
    int32_t isz = 0;
    const int *indices = NULL;
    if (ibuf >= 0)
        indices = (const int *)vigil_unsafe_buffer_get(ibuf, &isz);
    if (ren && verts && SDL_RenderGeometry(ren, tex, (const SDL_Vertex *)verts, num_verts, indices, num_indices))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* RenderTextureAffine */
static vigil_status_t sdl_renderer_render_texture_affine(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_field_i64(vm, base + 1, TEX_HANDLE);
    float sx = (float)sdl_arg_f64(vm, base, 2), sy = (float)sdl_arg_f64(vm, base, 3);
    float sw = (float)sdl_arg_f64(vm, base, 4), sh = (float)sdl_arg_f64(vm, base, 5);
    float ox = (float)sdl_arg_f64(vm, base, 6), oy = (float)sdl_arg_f64(vm, base, 7);
    float rx = (float)sdl_arg_f64(vm, base, 8), ry = (float)sdl_arg_f64(vm, base, 9);
    float dx = (float)sdl_arg_f64(vm, base, 10), dy = (float)sdl_arg_f64(vm, base, 11);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (!ren || !tex)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_FRect src = {sx, sy, sw, sh};
    SDL_FPoint origin = {ox, oy}, right = {rx, ry}, down = {dx, dy};
    int use_src = (sw > 0.0f || sh > 0.0f);
    if (SDL_RenderTextureAffine(ren, tex, use_src ? &src : NULL, &origin, &right, &down))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* RenderTexture9Grid */
static vigil_status_t sdl_renderer_render_texture_9grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_field_i64(vm, base + 1, TEX_HANDLE);
    float sx = (float)sdl_arg_f64(vm, base, 2), sy = (float)sdl_arg_f64(vm, base, 3);
    float sw = (float)sdl_arg_f64(vm, base, 4), sh = (float)sdl_arg_f64(vm, base, 5);
    float lw = (float)sdl_arg_f64(vm, base, 6), rw = (float)sdl_arg_f64(vm, base, 7);
    float th2 = (float)sdl_arg_f64(vm, base, 8), bh = (float)sdl_arg_f64(vm, base, 9);
    float scale = (float)sdl_arg_f64(vm, base, 10);
    float dx = (float)sdl_arg_f64(vm, base, 11), dy = (float)sdl_arg_f64(vm, base, 12);
    float dw = (float)sdl_arg_f64(vm, base, 13), dh = (float)sdl_arg_f64(vm, base, 14);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (!ren || !tex)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_FRect src = {sx, sy, sw, sh}, dst = {dx, dy, dw, dh};
    if (SDL_RenderTexture9Grid(ren, tex, &src, lw, rw, th2, bh, scale, &dst))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* Address mode constants */
SDL_CONST_FN(TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO)
SDL_CONST_FN(TEXTURE_ADDRESS_CLAMP, SDL_TEXTURE_ADDRESS_CLAMP)
SDL_CONST_FN(TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_WRAP)

/* ── Misc Complete ────────────────────────────────────────────────── */

/* CPU features */
#define SDL_CPU_FN(name, fn)                                                                                           \
    static vigil_status_t sdl_fn_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                        \
    {                                                                                                                  \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        return sdl_push_bool(vm, fn(), error);                                                                         \
    }

/* clang-format off */
SDL_CPU_FN(has_sse, SDL_HasSSE)
SDL_CPU_FN(has_sse2, SDL_HasSSE2)
SDL_CPU_FN(has_sse3, SDL_HasSSE3)
SDL_CPU_FN(has_sse41, SDL_HasSSE41)
SDL_CPU_FN(has_sse42, SDL_HasSSE42)
SDL_CPU_FN(has_avx, SDL_HasAVX)
SDL_CPU_FN(has_avx2, SDL_HasAVX2)
SDL_CPU_FN(has_avx512f, SDL_HasAVX512F)
SDL_CPU_FN(has_neon, SDL_HasNEON)
SDL_CPU_FN(has_mmx, SDL_HasMMX)
SDL_CPU_FN(has_altivec, SDL_HasAltiVec)
SDL_CPU_FN(has_armsimd, SDL_HasARMSIMD)
SDL_CPU_FN(has_lsx, SDL_HasLSX)
SDL_CPU_FN(has_lasx, SDL_HasLASX)
/* clang-format on */

static vigil_status_t sdl_fn_get_cpu_cache_line_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetCPUCacheLineSize(), error);
}

static vigil_status_t sdl_fn_get_num_video_drivers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetNumVideoDrivers(), error);
}

static vigil_status_t sdl_fn_get_video_driver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetVideoDriver(idx), error);
}

static vigil_status_t sdl_fn_get_sandbox(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetSandbox(), error);
}

/* Date/time */
static vigil_status_t sdl_fn_time_to_datetime(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ticks = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_DateTime dt = {0};
    SDL_TimeToDateTime((SDL_Time)ticks, &dt, false);
    /* Pack as string: YYYY-MM-DD HH:MM:SS */
    char buf[32];
    SDL_snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                 dt.second);
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_get_day_of_week(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t y = sdl_arg_i32(vm, base, 0);
    int32_t m = sdl_arg_i32(vm, base, 1);
    int32_t d = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetDayOfWeek(y, m, d), error);
}

static vigil_status_t sdl_fn_get_day_of_year(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t y = sdl_arg_i32(vm, base, 0);
    int32_t m = sdl_arg_i32(vm, base, 1);
    int32_t d = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetDayOfYear(y, m, d), error);
}

static vigil_status_t sdl_fn_get_days_in_month(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t y = sdl_arg_i32(vm, base, 0);
    int32_t m = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetDaysInMonth(y, m), error);
}

/* Primary selection (X11 middle-click paste) */
static vigil_status_t sdl_fn_set_primary_selection_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char text[4096];
    sdl_arg_str(vm, base, 0, text, sizeof(text));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetPrimarySelectionText(text))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_primary_selection_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    char *t = SDL_GetPrimarySelectionText();
    vigil_status_t st = sdl_push_string(vm, t ? t : "", error);
    SDL_free(t);
    return st;
}

static vigil_status_t sdl_fn_has_primary_selection_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasPrimarySelectionText(), error);
}

/* Custom blend mode */
static vigil_status_t sdl_fn_compose_custom_blend_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t sf = sdl_arg_i32(vm, base, 0), df = sdl_arg_i32(vm, base, 1), co = sdl_arg_i32(vm, base, 2);
    int32_t sa = sdl_arg_i32(vm, base, 3), da = sdl_arg_i32(vm, base, 4), ao = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_BlendMode mode = SDL_ComposeCustomBlendMode((SDL_BlendFactor)sf, (SDL_BlendFactor)df, (SDL_BlendOperation)co,
                                                    (SDL_BlendFactor)sa, (SDL_BlendFactor)da, (SDL_BlendOperation)ao);
    return sdl_push_i32(vm, (int32_t)mode, error);
}

/* ── Surface Complete ─────────────────────────────────────────────── */

static vigil_status_t sdl_surface_get_alpha_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint8 a = 255;
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s)
        SDL_GetSurfaceAlphaMod(s, &a);
    return sdl_push_i32(vm, (int32_t)a, error);
}

static vigil_status_t sdl_surface_get_blend_mode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_BlendMode m = SDL_BLENDMODE_NONE;
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s)
        SDL_GetSurfaceBlendMode(s, &m);
    return sdl_push_i32(vm, (int32_t)m, error);
}

static vigil_status_t sdl_surface_get_color_mod(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint8 r = 255, g = 255, b = 255;
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s)
        SDL_GetSurfaceColorMod(s, &r, &g, &b);
    return sdl_push_i32(vm, ((int32_t)r << 16) | ((int32_t)g << 8) | (int32_t)b, error);
}

static vigil_status_t sdl_surface_set_rle(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t en = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_SetSurfaceRLE(s, en != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_surface_has_rle(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    return sdl_push_bool(vm, s && SDL_SurfaceHasRLE(s), error);
}

static vigil_status_t sdl_surface_has_color_key(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    return sdl_push_bool(vm, s && SDL_SurfaceHasColorKey(s), error);
}

static vigil_status_t sdl_surface_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_LockSurface(s))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_surface_unlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s)
        SDL_UnlockSurface(s);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_surface_premultiply_alpha(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_PremultiplySurfaceAlpha(s, true))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Event Complete ───────────────────────────────────────────────── */

static vigil_status_t sdl_fn_has_events(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t min_t = sdl_arg_i32(vm, base, 0);
    int32_t max_t = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasEvents((Uint32)min_t, (Uint32)max_t), error);
}

static vigil_status_t sdl_fn_register_events(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t n = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_RegisterEvents(n), error);
}

static vigil_status_t sdl_fn_gamepad_events_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_GamepadEventsEnabled(), error);
}

static vigil_status_t sdl_fn_set_gamepad_events_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t en = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_SetGamepadEventsEnabled(en != 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_joystick_events_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_JoystickEventsEnabled(), error);
}

static vigil_status_t sdl_fn_set_joystick_events_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t en = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_SetJoystickEventsEnabled(en != 0);
    return VIGIL_STATUS_OK;
}

/* ── Renderer/Surface Final ───────────────────────────────────────── */

static vigil_status_t sdl_renderer_get_clip_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect r = {0, 0, 0, 0};
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderClipRect(ren, &r);
    vigil_status_t st = sdl_push_i32(vm, r.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, r.h, error);
}

static vigil_status_t sdl_renderer_get_logical_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_FRect r = {0, 0, 0, 0};
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    if (ren)
        SDL_GetRenderLogicalPresentationRect(ren, &r);
    vigil_status_t st = sdl_push_f64(vm, (double)r.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, (double)r.h, error);
}

static vigil_status_t sdl_texture_get_color_mod_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    float r = 1, g = 1, b = 1;
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    if (tex)
        SDL_GetTextureColorModFloat(tex, &r, &g, &b);
    /* Pack as i64: 16-bit fixed point per channel */
    int64_t packed = ((int64_t)(int32_t)(r * 65535.0f) << 32) | ((int64_t)(int32_t)(g * 65535.0f) << 16) |
                     (int64_t)(int32_t)(b * 65535.0f);
    return sdl_push_i64(vm, packed, error);
}

/* ren.lock_texture_to_surface(tex, x, y, w, h) -> (i64 surface_handle, err) */
static vigil_status_t sdl_renderer_lock_texture_to_surface(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_arg_i64(vm, base, 0);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), h = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (!tex)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid texture", SDL_ERR_ARG, error);
    }
    SDL_Surface *surf = NULL;
    SDL_Rect rect = {x, y, w, h};
    if (!SDL_LockTextureToSurface(tex, (w > 0 && h > 0) ? &rect : NULL, &surf) || !surf)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    /* Don't store in registry — SDL owns this surface, freed on UnlockTexture */
    int64_t sh;
    if (SDL_HANDLE_STORE(surfaces, surf, &sh) < 0)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, sh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.create_software_renderer(surface) -> (i64 renderer_handle, err) */
static vigil_status_t sdl_fn_create_software_renderer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *surf = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    if (!surf)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid surface", SDL_ERR_ARG, error);
    }
    SDL_Renderer *ren = SDL_CreateSoftwareRenderer(surf);
    if (!ren)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t rh = -1;
    if (SDL_HANDLE_STORE(renderers, ren, &rh) < 0)
    {
        SDL_DestroyRenderer(ren);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many renderers", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, rh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* ren.render_texture_9grid_tiled — like 9grid but with tile scale */
static vigil_status_t sdl_renderer_render_texture_9grid_tiled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_field_i64(vm, base + 1, TEX_HANDLE);
    float sx = (float)sdl_arg_f64(vm, base, 2), sy = (float)sdl_arg_f64(vm, base, 3);
    float sw = (float)sdl_arg_f64(vm, base, 4), sh = (float)sdl_arg_f64(vm, base, 5);
    float lw = (float)sdl_arg_f64(vm, base, 6), rw = (float)sdl_arg_f64(vm, base, 7);
    float th2 = (float)sdl_arg_f64(vm, base, 8), bh = (float)sdl_arg_f64(vm, base, 9);
    float scale = (float)sdl_arg_f64(vm, base, 10);
    float dx = (float)sdl_arg_f64(vm, base, 11), dy = (float)sdl_arg_f64(vm, base, 12);
    float dw = (float)sdl_arg_f64(vm, base, 13), dh = (float)sdl_arg_f64(vm, base, 14);
    float tileScale = (float)sdl_arg_f64(vm, base, 15);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    if (!ren || !tex)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_FRect src = {sx, sy, sw, sh}, dst = {dx, dy, dw, dh};
    if (SDL_RenderTexture9GridTiled(ren, tex, &src, lw, rw, th2, bh, scale, &dst, tileScale))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* tex.update_yuv(x,y,w,h, ybuf,ypitch, ubuf,upitch, vbuf,vpitch) -> (bool,err) */
static vigil_status_t sdl_texture_update_yuv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_field_i64(vm, base, TEX_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2), w = sdl_arg_i32(vm, base, 3),
            h = sdl_arg_i32(vm, base, 4);
    int64_t yb = sdl_arg_i64(vm, base, 5);
    int32_t yp = sdl_arg_i32(vm, base, 6);
    int64_t ub = sdl_arg_i64(vm, base, 7);
    int32_t up = sdl_arg_i32(vm, base, 8);
    int64_t vb = sdl_arg_i64(vm, base, 9);
    int32_t vp = sdl_arg_i32(vm, base, 10);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    int32_t ysz = 0, usz = 0, vsz = 0;
    void *yd = vigil_unsafe_buffer_get(yb, &ysz), *ud = vigil_unsafe_buffer_get(ub, &usz),
         *vd = vigil_unsafe_buffer_get(vb, &vsz);
    if (!tex || !yd || !ud || !vd)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect rect = {x, y, w, h};
    if (SDL_UpdateYUVTexture(tex, (w > 0 && h > 0) ? &rect : NULL, (const Uint8 *)yd, yp, (const Uint8 *)ud, up,
                             (const Uint8 *)vd, vp))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* tex.update_nv(x,y,w,h, ybuf,ypitch, uvbuf,uvpitch) -> (bool,err) */
static vigil_status_t sdl_texture_update_nv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_field_i64(vm, base, TEX_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2), w = sdl_arg_i32(vm, base, 3),
            h = sdl_arg_i32(vm, base, 4);
    int64_t yb = sdl_arg_i64(vm, base, 5);
    int32_t yp = sdl_arg_i32(vm, base, 6);
    int64_t uvb = sdl_arg_i64(vm, base, 7);
    int32_t uvp = sdl_arg_i32(vm, base, 8);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    int32_t ysz = 0, uvsz = 0;
    void *yd = vigil_unsafe_buffer_get(yb, &ysz), *uvd = vigil_unsafe_buffer_get(uvb, &uvsz);
    if (!tex || !yd || !uvd)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect rect = {x, y, w, h};
    if (SDL_UpdateNVTexture(tex, (w > 0 && h > 0) ? &rect : NULL, (const Uint8 *)yd, yp, (const Uint8 *)uvd, uvp))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ren.render_geometry_raw — stride-based vertex data from unsafe buffers */
static vigil_status_t sdl_renderer_render_geometry_raw(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t rh = sdl_field_i64(vm, base, REN_HANDLE);
    int64_t th = sdl_arg_i64(vm, base, 1);   /* texture or -1 */
    int64_t vbuf = sdl_arg_i64(vm, base, 2); /* xy + color + uv interleaved */
    int32_t vstride = sdl_arg_i32(vm, base, 3);
    int32_t num_verts = sdl_arg_i32(vm, base, 4);
    int64_t ibuf = sdl_arg_i64(vm, base, 5); /* index buffer or -1 */
    int32_t num_indices = sdl_arg_i32(vm, base, 6);
    int32_t idx_size = sdl_arg_i32(vm, base, 7); /* 1,2, or 4 bytes per index */
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, rh);
    SDL_Texture *tex = (th >= 0) ? (SDL_Texture *)SDL_HANDLE_GET(textures, th) : NULL;
    int32_t vsz = 0;
    void *vdata = vigil_unsafe_buffer_get(vbuf, &vsz);
    int32_t isz = 0;
    void *idata = (ibuf >= 0) ? vigil_unsafe_buffer_get(ibuf, &isz) : NULL;
    if (!ren || !vdata)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    /* Assume layout: xy(2 floats) at offset 0, color(4 floats) at offset 8, uv(2 floats) at offset 24 */
    const float *xy = (const float *)vdata;
    const SDL_FColor *color = (const SDL_FColor *)((const char *)vdata + 8);
    const float *uv = (const float *)((const char *)vdata + 24);
    if (SDL_RenderGeometryRaw(ren, tex, xy, vstride, color, vstride, uv, vstride, num_verts, idata, num_indices,
                              idx_size))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Surface Final ────────────────────────────────────────────────── */

static vigil_status_t sdl_surface_get_colorspace(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    return sdl_push_i32(vm, s ? (int32_t)SDL_GetSurfaceColorspace(s) : 0, error);
}

static vigil_status_t sdl_surface_set_colorspace(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t cs = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_SetSurfaceColorspace(s, (SDL_Colorspace)cs))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.create_from(buf, w, h, pitch, format) -> (Surface, err) */
static vigil_status_t sdl_surface_create_from(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_static_class_index(vm, base);
    int64_t buf = sdl_arg_i64(vm, base, 1);
    int32_t w = sdl_arg_i32(vm, base, 2), h = sdl_arg_i32(vm, base, 3);
    int32_t pitch = sdl_arg_i32(vm, base, 4), fmt = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *pixels = vigil_unsafe_buffer_get(buf, &sz);
    if (!pixels)
        return sdl_push_nil_and_err(vm, "invalid buffer", SDL_ERR_ARG, error);
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, (SDL_PixelFormat)fmt, pixels, pitch);
    if (!s)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t sh = -1;
    if (SDL_HANDLE_STORE(surfaces, s, &sh) < 0)
    {
        SDL_DestroySurface(s);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, sh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* surf.blit_tiled(dst, sx,sy,sw,sh, dx,dy,dw,dh) -> (bool,err) */
static vigil_status_t sdl_surface_blit_tiled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, SURF_HANDLE);
    int64_t dh = sdl_field_i64(vm, base + 1, SURF_HANDLE);
    int32_t sx = sdl_arg_i32(vm, base, 2), sy = sdl_arg_i32(vm, base, 3), sw = sdl_arg_i32(vm, base, 4),
            sht = sdl_arg_i32(vm, base, 5);
    int32_t dx = sdl_arg_i32(vm, base, 6), dy = sdl_arg_i32(vm, base, 7), dw = sdl_arg_i32(vm, base, 8),
            dht = sdl_arg_i32(vm, base, 9);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    SDL_Surface *dst = (SDL_Surface *)SDL_HANDLE_GET(surfaces, dh);
    if (!src || !dst)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect srect = {sx, sy, sw, sht}, drect = {dx, dy, dw, dht};
    if (SDL_BlitSurfaceTiled(src, (sw > 0 || sht > 0) ? &srect : NULL, dst, (dw > 0 || dht > 0) ? &drect : NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.blit_unchecked(dst, sx,sy,sw,sh, dx,dy) -> (bool,err) */
static vigil_status_t sdl_surface_blit_unchecked(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, SURF_HANDLE);
    int64_t dh = sdl_field_i64(vm, base + 1, SURF_HANDLE);
    int32_t sx = sdl_arg_i32(vm, base, 2), sy = sdl_arg_i32(vm, base, 3), sw = sdl_arg_i32(vm, base, 4),
            sht = sdl_arg_i32(vm, base, 5);
    int32_t dx = sdl_arg_i32(vm, base, 6), dy = sdl_arg_i32(vm, base, 7);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    SDL_Surface *dst = (SDL_Surface *)SDL_HANDLE_GET(surfaces, dh);
    if (!src || !dst)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);
    SDL_Rect srect = {sx, sy, sw, sht}, drect = {dx, dy, 0, 0};
    if (SDL_BlitSurfaceUnchecked(src, &srect, dst, &drect))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.read_pixel_float(x, y) -> (f64, f64) — r,g packed; b,a in second */
static vigil_status_t sdl_surface_read_pixel_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    float r = 0, g = 0, b = 0, a = 0;
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s)
        SDL_ReadSurfacePixelFloat(s, x, y, &r, &g, &b, &a);
    int64_t packed = ((int64_t)(int32_t)(r * 65535.0f) << 48) | ((int64_t)(int32_t)(g * 65535.0f) << 32) |
                     ((int64_t)(int32_t)(b * 65535.0f) << 16) | (int64_t)(int32_t)(a * 65535.0f);
    return sdl_push_i64(vm, packed, error);
}

/* surf.write_pixel_float(x, y, r, g, b, a) -> (bool,err) */
static vigil_status_t sdl_surface_write_pixel_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t x = sdl_arg_i32(vm, base, 1), y = sdl_arg_i32(vm, base, 2);
    float r = (float)sdl_arg_f64(vm, base, 3), g = (float)sdl_arg_f64(vm, base, 4);
    float b = (float)sdl_arg_f64(vm, base, 5), a = (float)sdl_arg_f64(vm, base, 6);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (s && SDL_WriteSurfacePixelFloat(s, x, y, r, g, b, a))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.map_rgb(r, g, b) -> i32 */
static vigil_status_t sdl_surface_map_rgb(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t r = sdl_arg_i32(vm, base, 1), g = sdl_arg_i32(vm, base, 2), b = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!s)
        return sdl_push_i32(vm, 0, error);
    return sdl_push_i32(vm, (int32_t)SDL_MapSurfaceRGB(s, (Uint8)r, (Uint8)g, (Uint8)b), error);
}

/* surf.map_rgba(r, g, b, a) -> i32 */
static vigil_status_t sdl_surface_map_rgba(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t r = sdl_arg_i32(vm, base, 1), g = sdl_arg_i32(vm, base, 2), b = sdl_arg_i32(vm, base, 3),
            a = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!s)
        return sdl_push_i32(vm, 0, error);
    return sdl_push_i32(vm, (int32_t)SDL_MapSurfaceRGBA(s, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a), error);
}

/* surf.fill_rects(buf, count, color) -> (bool,err) — buf = packed i32 quads */
static vigil_status_t sdl_surface_fill_rects(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int64_t buf = sdl_arg_i64(vm, base, 1);
    int32_t count = sdl_arg_i32(vm, base, 2);
    int32_t color = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(buf, &sz);
    if (s && data && SDL_FillSurfaceRects(s, (const SDL_Rect *)data, count, (Uint32)color))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* surf.convert_colorspace(format, colorspace) -> (Surface, err) */
static vigil_status_t sdl_surface_convert_colorspace(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = sdl_self_class_index(vm, base);
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    int32_t fmt = sdl_arg_i32(vm, base, 1), cs = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    if (!src)
        return sdl_push_nil_and_err(vm, "invalid surface", SDL_ERR_ARG, error);
    SDL_PropertiesID props = 0; /* default properties */
    SDL_Surface *dst = SDL_ConvertSurfaceAndColorspace(src, (SDL_PixelFormat)fmt, NULL, (SDL_Colorspace)cs, props);
    if (!dst)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t nh = -1;
    if (SDL_HANDLE_STORE(surfaces, dst, &nh) < 0)
    {
        SDL_DestroySurface(dst);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, ci, nh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* ── SDL IO Stream ────────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(io_streams);

/* sdl.io_from_file(path, mode) -> (i64, err) */
static vigil_status_t sdl_fn_io_from_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512], mode[8];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    sdl_arg_str(vm, base, 1, mode, sizeof(mode));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = SDL_IOFromFile(path, mode);
    if (!io)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(io_streams, io, &h) < 0)
    {
        SDL_CloseIO(io);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many IO streams", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.io_from_mem(buf_handle) -> (i64, err) */
static vigil_status_t sdl_fn_io_from_mem(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *mem = vigil_unsafe_buffer_get(bh, &sz);
    if (!mem)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid buffer", SDL_ERR_ARG, error);
    }
    SDL_IOStream *io = SDL_IOFromMem(mem, (size_t)sz);
    if (!io)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(io_streams, io, &h) < 0)
    {
        SDL_CloseIO(io);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many IO streams", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.io_from_const_mem(buf_handle) -> (i64, err) */
static vigil_status_t sdl_fn_io_from_const_mem(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *mem = vigil_unsafe_buffer_get(bh, &sz);
    if (!mem)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid buffer", SDL_ERR_ARG, error);
    }
    SDL_IOStream *io = SDL_IOFromConstMem(mem, (size_t)sz);
    if (!io)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(io_streams, io, &h) < 0)
    {
        SDL_CloseIO(io);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many IO streams", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.io_from_dynamic_mem() -> (i64, err) */
static vigil_status_t sdl_fn_io_from_dynamic_mem(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = SDL_IOFromDynamicMem();
    if (!io)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(io_streams, io, &h) < 0)
    {
        SDL_CloseIO(io);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many IO streams", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.io_close(handle) -> (bool, err) */
static vigil_status_t sdl_fn_io_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    if (io && SDL_CloseIO(io))
    {
        SDL_HANDLE_CLEAR(io_streams, h);
        return sdl_push_bool_ok(vm, error);
    }
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* sdl.io_size(handle) -> i64 */
static vigil_status_t sdl_fn_io_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    return sdl_push_i64(vm, io ? SDL_GetIOSize(io) : -1, error);
}

/* sdl.io_seek(handle, offset, whence) -> i64 */
static vigil_status_t sdl_fn_io_seek(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int64_t offset = sdl_arg_i64(vm, base, 1);
    int32_t whence = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    return sdl_push_i64(vm, io ? SDL_SeekIO(io, offset, (SDL_IOWhence)whence) : -1, error);
}

/* sdl.io_tell(handle) -> i64 */
static vigil_status_t sdl_fn_io_tell(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    return sdl_push_i64(vm, io ? SDL_TellIO(io) : -1, error);
}

/* sdl.io_read(handle, buf_handle, size) -> i32 bytes read */
static vigil_status_t sdl_fn_io_read(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t size = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    int32_t bsz = 0;
    void *buf = vigil_unsafe_buffer_get(bh, &bsz);
    if (!io || !buf)
        return sdl_push_i32(vm, 0, error);
    int32_t to_read = (size > 0 && size <= bsz) ? size : bsz;
    return sdl_push_i32(vm, (int32_t)SDL_ReadIO(io, buf, (size_t)to_read), error);
}

/* sdl.io_write(handle, buf_handle, size) -> i32 bytes written */
static vigil_status_t sdl_fn_io_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t size = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    int32_t bsz = 0;
    void *buf = vigil_unsafe_buffer_get(bh, &bsz);
    if (!io || !buf)
        return sdl_push_i32(vm, 0, error);
    int32_t to_write = (size > 0 && size <= bsz) ? size : bsz;
    return sdl_push_i32(vm, (int32_t)SDL_WriteIO(io, buf, (size_t)to_write), error);
}

/* sdl.io_flush(handle) -> (bool, err) */
static vigil_status_t sdl_fn_io_flush(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    if (io && SDL_FlushIO(io))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* sdl.io_status(handle) -> i32 */
static vigil_status_t sdl_fn_io_status(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    return sdl_push_i32(vm, io ? (int32_t)SDL_GetIOStatus(io) : (int32_t)SDL_IO_STATUS_ERROR, error);
}

/* sdl.io_load_file(handle) -> (i64 buf_handle, err) — reads entire stream */
static vigil_status_t sdl_fn_io_load_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    if (!io)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid IO stream", SDL_ERR_ARG, error);
    }
    size_t datasize = 0;
    void *data = SDL_LoadFile_IO(io, &datasize, false);
    if (!data)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t bh = vigil_unsafe_buffer_register(data, (int32_t)datasize);
    if (bh < 0)
    {
        SDL_free(data);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many buffers", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, bh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.io_save_file(handle, buf_handle) -> (bool, err) */
static vigil_status_t sdl_fn_io_save_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    int32_t sz = 0;
    void *data = vigil_unsafe_buffer_get(bh, &sz);
    if (io && data && SDL_SaveFile_IO(io, data, (size_t)sz, false))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* IO stream constants */
SDL_CONST_FN(IO_SEEK_SET, SDL_IO_SEEK_SET)
SDL_CONST_FN(IO_SEEK_CUR, SDL_IO_SEEK_CUR)
SDL_CONST_FN(IO_SEEK_END, SDL_IO_SEEK_END)
SDL_CONST_FN(IO_STATUS_READY, SDL_IO_STATUS_READY)
SDL_CONST_FN(IO_STATUS_ERROR, SDL_IO_STATUS_ERROR)
SDL_CONST_FN(IO_STATUS_EOF, SDL_IO_STATUS_EOF)

/* Now bind the _IO variants of load functions */

/* sdl.load_bmp_io(io_handle) -> (Surface, err) */
static vigil_status_t sdl_fn_load_bmp_io(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    if (!io)
        return sdl_push_nil_and_err(vm, "invalid IO stream", SDL_ERR_ARG, error);
    SDL_Surface *surf = SDL_LoadBMP_IO(io, false);
    if (!surf)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t sh = -1;
    if (SDL_HANDLE_STORE(surfaces, surf, &sh) < 0)
    {
        SDL_DestroySurface(surf);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, 3U, sh, error); /* Surface class index */
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.load_surface_io(io_handle) -> (Surface, err) */
static vigil_status_t sdl_fn_load_surface_io(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    if (!io)
        return sdl_push_nil_and_err(vm, "invalid IO stream", SDL_ERR_ARG, error);
    SDL_Surface *surf = SDL_LoadSurface_IO(io, false);
    if (!surf)
        return sdl_push_nil_and_sdl_err(vm, SDL_ERR_IO, error);
    int64_t sh = -1;
    if (SDL_HANDLE_STORE(surfaces, surf, &sh) < 0)
    {
        SDL_DestroySurface(surf);
        return sdl_push_nil_and_err(vm, "too many surfaces", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_handle_instance(vm, 3U, sh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* sdl.save_bmp_io(surface, io_handle) -> (bool, err) */
static vigil_status_t sdl_fn_save_bmp_io(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_field_i64(vm, base, SURF_HANDLE);
    int64_t ih = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *surf = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, ih);
    if (surf && io && SDL_SaveBMP_IO(surf, io, false))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* sdl.load_wav_io(io_handle) -> (i64 wav_handle, err) */
static vigil_status_t sdl_fn_load_wav_io(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);
    if (!io)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid IO stream", SDL_ERR_ARG, error);
    }
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
    if (!SDL_LoadWAV_IO(io, false, &spec, &buf, &len))
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

/* ── Gamepad Complete ─────────────────────────────────────────────── */

static vigil_status_t sdl_fn_add_gamepad_mapping(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char mapping[4096];
    sdl_arg_str(vm, base, 0, mapping, sizeof(mapping));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_AddGamepadMapping(mapping), error);
}

static vigil_status_t sdl_fn_add_gamepad_mappings_from_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_AddGamepadMappingsFromFile(path), error);
}

static vigil_status_t sdl_fn_reload_gamepad_mappings(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_ReloadGamepadMappings())
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_gamepad_axis_from_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char s[64];
    sdl_arg_str(vm, base, 0, s, sizeof(s));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGamepadAxisFromString(s), error);
}

static vigil_status_t sdl_fn_get_gamepad_button_from_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char s[64];
    sdl_arg_str(vm, base, 0, s, sizeof(s));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGamepadButtonFromString(s), error);
}

static vigil_status_t sdl_fn_get_gamepad_string_for_axis(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t a = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetGamepadStringForAxis((SDL_GamepadAxis)a), error);
}

static vigil_status_t sdl_fn_get_gamepad_string_for_button(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t b = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetGamepadStringForButton((SDL_GamepadButton)b), error);
}

static vigil_status_t sdl_fn_get_gamepad_string_for_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t t = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetGamepadStringForType((SDL_GamepadType)t), error);
}

static vigil_status_t sdl_fn_get_gamepad_type_from_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char s[64];
    sdl_arg_str(vm, base, 0, s, sizeof(s));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGamepadTypeFromString(s), error);
}

static vigil_status_t sdl_fn_get_gamepad_name_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetGamepadNameForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_gamepad_type_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGamepadTypeForID((SDL_JoystickID)id), error);
}

/* Gamepad instance methods */
static vigil_status_t sdl_gamepad_get_mapping(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    char *m = gp ? SDL_GetGamepadMapping(gp) : NULL;
    vigil_status_t st = sdl_push_string(vm, m ? m : "", error);
    SDL_free(m);
    return st;
}

static vigil_status_t sdl_gamepad_get_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_string(vm, gp ? SDL_GetGamepadPath(gp) : "", error);
}

static vigil_status_t sdl_gamepad_get_serial(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_string(vm, gp ? SDL_GetGamepadSerial(gp) : "", error);
}

static vigil_status_t sdl_gamepad_get_vendor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetGamepadVendor(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_get_product(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetGamepadProduct(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_get_product_version(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetGamepadProductVersion(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_get_firmware_version(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetGamepadFirmwareVersion(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_get_player_index(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? SDL_GetGamepadPlayerIndex(gp) : -1, error);
}

static vigil_status_t sdl_gamepad_set_player_index(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t idx = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    if (gp && SDL_SetGamepadPlayerIndex(gp, idx))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_gamepad_get_steam_handle(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i64(vm, gp ? (int64_t)SDL_GetGamepadSteamHandle(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_get_connection_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetGamepadConnectionState(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_get_real_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetRealGamepadType(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_get_num_touchpads(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? SDL_GetNumGamepadTouchpads(gp) : 0, error);
}

static vigil_status_t sdl_gamepad_has_sensor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t type = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_bool(vm, gp && SDL_GamepadHasSensor(gp, (SDL_SensorType)type), error);
}

static vigil_status_t sdl_gamepad_set_sensor_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t type = sdl_arg_i32(vm, base, 1);
    int32_t en = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    if (gp && SDL_SetGamepadSensorEnabled(gp, (SDL_SensorType)type, en != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_gamepad_sensor_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t type = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_bool(vm, gp && SDL_GamepadSensorEnabled(gp, (SDL_SensorType)type), error);
}

static vigil_status_t sdl_gamepad_get_button_label(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, GP_HANDLE);
    int32_t btn = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *gp = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, h);
    return sdl_push_i32(vm, gp ? (int32_t)SDL_GetGamepadButtonLabel(gp, (SDL_GamepadButton)btn) : 0, error);
}

/* ── Joystick Complete ────────────────────────────────────────────── */

/* Module-level joystick functions */
static vigil_status_t sdl_fn_update_joysticks(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_UpdateJoysticks();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_lock_joysticks(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_LockJoysticks();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_unlock_joysticks(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_UnlockJoysticks();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_joystick_name_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetJoystickNameForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_joystick_path_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetJoystickPathForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_joystick_type_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetJoystickTypeForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_joystick_vendor_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetJoystickVendorForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_joystick_product_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetJoystickProductForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_joystick_product_version_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetJoystickProductVersionForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_joystick_player_index_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetJoystickPlayerIndexForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_is_joystick_virtual(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_IsJoystickVirtual((SDL_JoystickID)id), error);
}

/* Joystick instance methods */
static vigil_status_t sdl_joystick_get_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickID(j) : 0, error);
}

static vigil_status_t sdl_joystick_get_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_string(vm, j ? SDL_GetJoystickPath(j) : "", error);
}

static vigil_status_t sdl_joystick_get_serial(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_string(vm, j ? SDL_GetJoystickSerial(j) : "", error);
}

static vigil_status_t sdl_joystick_get_vendor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickVendor(j) : 0, error);
}

static vigil_status_t sdl_joystick_get_product(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickProduct(j) : 0, error);
}

static vigil_status_t sdl_joystick_get_product_version(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickProductVersion(j) : 0, error);
}

static vigil_status_t sdl_joystick_get_firmware_version(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickFirmwareVersion(j) : 0, error);
}

static vigil_status_t sdl_joystick_get_player_index(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? SDL_GetJoystickPlayerIndex(j) : -1, error);
}

static vigil_status_t sdl_joystick_set_player_index(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    int32_t idx = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j && SDL_SetJoystickPlayerIndex(j, idx))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_joystick_get_power_info(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    int percent = -1;
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j)
        SDL_GetJoystickPowerInfo(j, &percent);
    return sdl_push_i32(vm, percent, error);
}

static vigil_status_t sdl_joystick_get_connection_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickConnectionState(j) : 0, error);
}

static vigil_status_t sdl_joystick_num_balls(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_i32(vm, j ? SDL_GetNumJoystickBalls(j) : 0, error);
}

static vigil_status_t sdl_joystick_get_ball(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    int32_t ball = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int dx = 0, dy = 0;
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j)
        SDL_GetJoystickBall(j, ball, &dx, &dy);
    vigil_status_t st = sdl_push_i32(vm, dx, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, dy, error);
}

static vigil_status_t sdl_joystick_set_led(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    int32_t r = sdl_arg_i32(vm, base, 1), g = sdl_arg_i32(vm, base, 2), b = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j && SDL_SetJoystickLED(j, (Uint8)r, (Uint8)g, (Uint8)b))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_joystick_rumble_triggers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    int32_t left = sdl_arg_i32(vm, base, 1), right = sdl_arg_i32(vm, base, 2), dur = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j && SDL_RumbleJoystickTriggers(j, (Uint16)left, (Uint16)right, (Uint32)dur))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_joystick_is_haptic(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, JOY_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    return sdl_push_bool(vm, j && SDL_IsJoystickHaptic(j), error);
}

/* Virtual joystick */
static vigil_status_t sdl_fn_attach_virtual_joystick(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t type = sdl_arg_i32(vm, base, 0), naxes = sdl_arg_i32(vm, base, 1);
    int32_t nbuttons = sdl_arg_i32(vm, base, 2), nhats = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.type = (Uint16)type;
    desc.naxes = (Uint16)naxes;
    desc.nbuttons = (Uint16)nbuttons;
    desc.nhats = (Uint16)nhats;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    return sdl_push_i32(vm, (int32_t)id, error);
}

static vigil_status_t sdl_fn_detach_virtual_joystick(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_DetachVirtualJoystick((SDL_JoystickID)id))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_set_joystick_virtual_axis(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int32_t axis = sdl_arg_i32(vm, base, 1);
    int32_t val = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j && SDL_SetJoystickVirtualAxis(j, axis, (Sint16)val))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_set_joystick_virtual_button(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int32_t btn = sdl_arg_i32(vm, base, 1);
    int32_t val = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j && SDL_SetJoystickVirtualButton(j, btn, val != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_set_joystick_virtual_hat(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int32_t hat = sdl_arg_i32(vm, base, 1);
    int32_t val = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, h);
    if (j && SDL_SetJoystickVirtualHat(j, hat, (Uint8)val))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Audio Complete ───────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_num_audio_drivers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetNumAudioDrivers(), error);
}

static vigil_status_t sdl_fn_get_audio_driver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetAudioDriver(idx), error);
}

static vigil_status_t sdl_fn_get_audio_format_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetAudioFormatName((SDL_AudioFormat)fmt), error);
}

/* sdl.open_audio_device(i32 device_id, i32 format, i32 channels, i32 freq) -> (i32, err) */
static vigil_status_t sdl_fn_open_audio_device(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t devid = sdl_arg_i32(vm, base, 0);
    int32_t fmt = sdl_arg_i32(vm, base, 1), ch = sdl_arg_i32(vm, base, 2), freq = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioSpec spec = {0};
    spec.format = (SDL_AudioFormat)fmt;
    spec.channels = ch;
    spec.freq = freq;
    SDL_AudioDeviceID id = SDL_OpenAudioDevice((SDL_AudioDeviceID)devid, (fmt > 0) ? &spec : NULL);
    if (id == 0)
    {
        vigil_status_t st = sdl_push_i32(vm, 0, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    vigil_status_t st = sdl_push_i32(vm, (int32_t)id, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_close_audio_device(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_CloseAudioDevice((SDL_AudioDeviceID)id);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_pause_audio_device(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_PauseAudioDevice((SDL_AudioDeviceID)id))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_resume_audio_device(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_ResumeAudioDevice((SDL_AudioDeviceID)id))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_audio_device_paused(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_AudioDevicePaused((SDL_AudioDeviceID)id), error);
}

static vigil_status_t sdl_fn_get_audio_device_gain(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, (double)SDL_GetAudioDeviceGain((SDL_AudioDeviceID)id), error);
}

static vigil_status_t sdl_fn_set_audio_device_gain(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    float gain = (float)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetAudioDeviceGain((SDL_AudioDeviceID)id, gain))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_is_audio_device_physical(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_IsAudioDevicePhysical((SDL_AudioDeviceID)id), error);
}

static vigil_status_t sdl_fn_is_audio_device_playback(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_IsAudioDevicePlayback((SDL_AudioDeviceID)id), error);
}

static vigil_status_t sdl_fn_get_audio_recording_device_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    int count = 0;
    SDL_AudioDeviceID *devs = SDL_GetAudioRecordingDevices(&count);
    SDL_free(devs);
    return sdl_push_i32(vm, count, error);
}

/* AudioStream methods */
static vigil_status_t sdl_audio_stream_get_device(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    return sdl_push_i32(vm, s ? (int32_t)SDL_GetAudioStreamDevice(s) : 0, error);
}

static vigil_status_t sdl_audio_stream_paused(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    return sdl_push_bool(vm, s && SDL_AudioStreamDevicePaused(s), error);
}

static vigil_status_t sdl_audio_stream_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s && SDL_LockAudioStream(s))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_audio_stream_unlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s)
        SDL_UnlockAudioStream(s);
    return VIGIL_STATUS_OK;
}

/* stream.bind(device_id) / stream.unbind() */
static vigil_status_t sdl_audio_stream_bind(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    int32_t devid = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s && SDL_BindAudioStream((SDL_AudioDeviceID)devid, s))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_audio_stream_unbind(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    if (s)
        SDL_UnbindAudioStream(s);
    return VIGIL_STATUS_OK;
}

/* stream.get_data(buf, size) -> i32 bytes read */
static vigil_status_t sdl_audio_stream_get_data(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, ASTREAM_HANDLE);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t size = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, h);
    int32_t bsz = 0;
    void *buf = vigil_unsafe_buffer_get(bh, &bsz);
    if (!s || !buf)
        return sdl_push_i32(vm, 0, error);
    int32_t to_read = (size > 0 && size <= bsz) ? size : bsz;
    return sdl_push_i32(vm, SDL_GetAudioStreamData(s, buf, to_read), error);
}

/* ── Sensor Complete ──────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(sensors);

static SDL_SensorID *g_sensor_ids = NULL;
static int g_sensor_count = 0;

static vigil_status_t sdl_fn_get_sensor_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_sensor_ids)
    {
        SDL_free(g_sensor_ids);
        g_sensor_ids = NULL;
    }
    g_sensor_ids = SDL_GetSensors(&g_sensor_count);
    return sdl_push_i32(vm, g_sensor_count, error);
}

static vigil_status_t sdl_fn_get_sensor_name_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_sensor_count && g_sensor_ids)
        return sdl_push_string(vm, SDL_GetSensorNameForID(g_sensor_ids[idx]), error);
    return sdl_push_string(vm, "", error);
}

static vigil_status_t sdl_fn_get_sensor_type_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_sensor_count && g_sensor_ids)
        return sdl_push_i32(vm, (int32_t)SDL_GetSensorTypeForID(g_sensor_ids[idx]), error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_get_sensor_nonportable_type_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_sensor_count && g_sensor_ids)
        return sdl_push_i32(vm, SDL_GetSensorNonPortableTypeForID(g_sensor_ids[idx]), error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_update_sensors(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_UpdateSensors();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_open_sensor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_SensorID sid = (idx >= 0 && idx < g_sensor_count && g_sensor_ids) ? g_sensor_ids[idx] : 0;
    if (!sid)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid sensor index", SDL_ERR_ARG, error);
    }
    SDL_Sensor *s = SDL_OpenSensor(sid);
    if (!s)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(sensors, s, &h) < 0)
    {
        SDL_CloseSensor(s);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many sensors", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_close_sensor(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Sensor *s = (SDL_Sensor *)SDL_HANDLE_GET(sensors, h);
    if (s)
    {
        SDL_CloseSensor(s);
        SDL_HANDLE_CLEAR(sensors, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_sensor_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Sensor *s = (SDL_Sensor *)SDL_HANDLE_GET(sensors, h);
    return sdl_push_string(vm, s ? SDL_GetSensorName(s) : "", error);
}

static vigil_status_t sdl_fn_get_sensor_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Sensor *s = (SDL_Sensor *)SDL_HANDLE_GET(sensors, h);
    return sdl_push_i32(vm, s ? (int32_t)SDL_GetSensorType(s) : 0, error);
}

static vigil_status_t sdl_fn_get_sensor_nonportable_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Sensor *s = (SDL_Sensor *)SDL_HANDLE_GET(sensors, h);
    return sdl_push_i32(vm, s ? SDL_GetSensorNonPortableType(s) : 0, error);
}

/* Sensor data constants */
SDL_CONST_FN(SENSOR_ACCEL, SDL_SENSOR_ACCEL)
SDL_CONST_FN(SENSOR_GYRO, SDL_SENSOR_GYRO)
SDL_CONST_FN(SENSOR_ACCEL_L, SDL_SENSOR_ACCEL_L)
SDL_CONST_FN(SENSOR_GYRO_L, SDL_SENSOR_GYRO_L)
SDL_CONST_FN(SENSOR_ACCEL_R, SDL_SENSOR_ACCEL_R)
SDL_CONST_FN(SENSOR_GYRO_R, SDL_SENSOR_GYRO_R)

/* ── Keyboard Complete ────────────────────────────────────────────── */

static vigil_status_t sdl_fn_clear_composition(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (win)
        SDL_ClearComposition(win);
    return sdl_push_bool_ok(vm, error);
}

static vigil_status_t sdl_fn_has_screen_keyboard_support(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasScreenKeyboardSupport(), error);
}

static vigil_status_t sdl_fn_screen_keyboard_shown(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    return sdl_push_bool(vm, win && SDL_ScreenKeyboardShown(win), error);
}

static vigil_status_t sdl_fn_reset_keyboard(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_ResetKeyboard();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_set_mod_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t mod = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_SetModState((SDL_Keymod)mod);
    return VIGIL_STATUS_OK;
}

/* ── Touch Complete ───────────────────────────────────────────────── */

static SDL_TouchID *g_touch_ids = NULL;
static int g_touch_count = 0;

static vigil_status_t sdl_fn_get_touch_device_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_touch_ids)
    {
        SDL_free(g_touch_ids);
        g_touch_ids = NULL;
    }
    g_touch_ids = SDL_GetTouchDevices(&g_touch_count);
    return sdl_push_i32(vm, g_touch_count, error);
}

static vigil_status_t sdl_fn_get_touch_device_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_touch_count && g_touch_ids)
        return sdl_push_string(vm, SDL_GetTouchDeviceName(g_touch_ids[idx]), error);
    return sdl_push_string(vm, "", error);
}

static vigil_status_t sdl_fn_get_touch_device_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_touch_count && g_touch_ids)
        return sdl_push_i32(vm, (int32_t)SDL_GetTouchDeviceType(g_touch_ids[idx]), error);
    return sdl_push_i32(vm, 0, error);
}

/* ── Cursor Complete ──────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_mouse_focus(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = SDL_GetMouseFocus();
    if (!win)
        return sdl_push_i32(vm, -1, error);
    for (int64_t i = 0; i < (int64_t)g_windows.count; i++)
        if (g_windows.items[i] == win)
            return sdl_push_i32(vm, (int32_t)i, error);
    return sdl_push_i32(vm, -1, error);
}

/* ── Display Complete ─────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_current_display_orientation(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_display_count && g_display_ids)
        return sdl_push_i32(vm, (int32_t)SDL_GetCurrentDisplayOrientation(g_display_ids[idx]), error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_get_natural_display_orientation(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_display_count && g_display_ids)
        return sdl_push_i32(vm, (int32_t)SDL_GetNaturalDisplayOrientation(g_display_ids[idx]), error);
    return sdl_push_i32(vm, 0, error);
}

/* ── Camera Complete ──────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_num_camera_drivers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetNumCameraDrivers(), error);
}

static vigil_status_t sdl_fn_get_camera_driver(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetCameraDriver(idx), error);
}

/* ── GPU Final ────────────────────────────────────────────────────── */

/* Compute storage bindings */
static vigil_status_t sdl_fn_gpu_bind_compute_storage_buffers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUComputePass *cp = (SDL_GPUComputePass *)SDL_HANDLE_GET(gpu_compute_passes, cph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (cp && buf)
        SDL_BindGPUComputeStorageBuffers(cp, 0, &buf, 1);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_compute_storage_textures(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUComputePass *cp = (SDL_GPUComputePass *)SDL_HANDLE_GET(gpu_compute_passes, cph);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    if (cp && tex)
        SDL_BindGPUComputeStorageTextures(cp, 0, &tex, 1);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_compute_samplers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1), sh = sdl_arg_i64(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUComputePass *cp = (SDL_GPUComputePass *)SDL_HANDLE_GET(gpu_compute_passes, cph);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    SDL_GPUSampler *samp = (SDL_GPUSampler *)SDL_HANDLE_GET(gpu_samplers, sh);
    if (cp && tex && samp)
    {
        SDL_GPUTextureSamplerBinding tsb = {tex, samp};
        SDL_BindGPUComputeSamplers(cp, 0, &tsb, 1);
    }
    return VIGIL_STATUS_OK;
}

/* Fragment/vertex storage bindings */
static vigil_status_t sdl_fn_gpu_bind_fragment_storage_buffers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (pass && buf)
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &buf, 1);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_fragment_storage_textures(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    if (pass && tex)
        SDL_BindGPUFragmentStorageTextures(pass, 0, &tex, 1);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_vertex_storage_buffers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (pass && buf)
        SDL_BindGPUVertexStorageBuffers(pass, 0, &buf, 1);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gpu_bind_vertex_storage_textures(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPURenderPass *pass = (SDL_GPURenderPass *)SDL_HANDLE_GET(gpu_render_passes, ph);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    if (pass && tex)
        SDL_BindGPUVertexStorageTextures(pass, 0, &tex, 1);
    return VIGIL_STATUS_OK;
}

/* Indirect compute dispatch */
static vigil_status_t sdl_fn_gpu_dispatch_compute_indirect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    int32_t offset = sdl_arg_i32(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUComputePass *cp = (SDL_GPUComputePass *)SDL_HANDLE_GET(gpu_compute_passes, cph);
    SDL_GPUBuffer *buf = (SDL_GPUBuffer *)SDL_HANDLE_GET(gpu_buffers, bh);
    if (cp && buf)
        SDL_DispatchGPUComputeIndirect(cp, buf, (Uint32)offset);
    return VIGIL_STATUS_OK;
}

/* Texture format queries */
static vigil_status_t sdl_fn_gpu_texture_format_texel_block_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GPUTextureFormatTexelBlockSize((SDL_GPUTextureFormat)fmt), error);
}

static vigil_status_t sdl_fn_gpu_texture_format_from_pixel(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGPUTextureFormatFromPixelFormat((SDL_PixelFormat)fmt), error);
}

static vigil_status_t sdl_fn_gpu_pixel_format_from_texture(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetPixelFormatFromGPUTextureFormat((SDL_GPUTextureFormat)fmt), error);
}

/* GPU texture copy */
static vigil_status_t sdl_fn_gpu_copy_texture_to_texture(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0);
    int64_t src_th = sdl_arg_i64(vm, base, 1), dst_th = sdl_arg_i64(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3), h = sdl_arg_i32(vm, base, 4), d = sdl_arg_i32(vm, base, 5);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCopyPass *cp = (SDL_GPUCopyPass *)SDL_HANDLE_GET(gpu_copy_passes, cph);
    SDL_GPUTexture *src = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, src_th);
    SDL_GPUTexture *dst = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, dst_th);
    if (cp && src && dst)
    {
        SDL_GPUTextureLocation sl = {src, 0, 0, 0, 0, 0};
        SDL_GPUTextureLocation dl = {dst, 0, 0, 0, 0, 0};
        SDL_CopyGPUTextureToTexture(cp, &sl, &dl, (Uint32)w, (Uint32)h, (Uint32)(d > 0 ? d : 1), false);
    }
    return VIGIL_STATUS_OK;
}

/* GPU texture download */
static vigil_status_t sdl_fn_gpu_download_from_texture(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cph = sdl_arg_i64(vm, base, 0), th = sdl_arg_i64(vm, base, 1);
    int32_t w = sdl_arg_i32(vm, base, 2), h = sdl_arg_i32(vm, base, 3);
    int64_t xh = sdl_arg_i64(vm, base, 4);
    int32_t xoff = sdl_arg_i32(vm, base, 5);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUCopyPass *cp = (SDL_GPUCopyPass *)SDL_HANDLE_GET(gpu_copy_passes, cph);
    SDL_GPUTexture *tex = (SDL_GPUTexture *)SDL_HANDLE_GET(gpu_textures_gpu, th);
    SDL_GPUTransferBuffer *xb = (SDL_GPUTransferBuffer *)SDL_HANDLE_GET(gpu_xfer_buffers, xh);
    if (cp && tex && xb)
    {
        SDL_GPUTextureRegion src = {tex, 0, 0, 0, 0, 0, (Uint32)w, (Uint32)h, 1};
        SDL_GPUTextureTransferInfo dst = {xb, (Uint32)xoff, 0, 0};
        SDL_DownloadFromGPUTexture(cp, &src, &dst);
    }
    return VIGIL_STATUS_OK;
}

/* ── Haptic Complete ──────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_haptic_name_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (idx >= 0 && idx < g_haptic_count && g_haptic_ids)
        return sdl_push_string(vm, SDL_GetHapticNameForID(g_haptic_ids[idx]), error);
    return sdl_push_string(vm, "", error);
}

static vigil_status_t sdl_fn_open_haptic_from_mouse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *h = SDL_OpenHapticFromMouse();
    if (!h)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t handle = -1;
    if (SDL_HANDLE_STORE(haptics, h, &handle) < 0)
    {
        SDL_CloseHaptic(h);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many haptics", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* Haptic instance methods */
static vigil_status_t sdl_haptic_get_features(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    return sdl_push_i32(vm, hp ? (int32_t)SDL_GetHapticFeatures(hp) : 0, error);
}

static vigil_status_t sdl_haptic_get_max_effects(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    return sdl_push_i32(vm, hp ? SDL_GetMaxHapticEffects(hp) : 0, error);
}

static vigil_status_t sdl_haptic_get_max_effects_playing(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    return sdl_push_i32(vm, hp ? SDL_GetMaxHapticEffectsPlaying(hp) : 0, error);
}

static vigil_status_t sdl_haptic_get_num_axes(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    return sdl_push_i32(vm, hp ? SDL_GetNumHapticAxes(hp) : 0, error);
}

static vigil_status_t sdl_haptic_set_gain(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    int32_t gain = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp && SDL_SetHapticGain(hp, gain))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_haptic_set_autocenter(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    int32_t ac = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp && SDL_SetHapticAutocenter(hp, ac))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_haptic_stop_effects(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, HAP_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *hp = (SDL_Haptic *)SDL_HANDLE_GET(haptics, h);
    if (hp && SDL_StopHapticEffects(hp))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── stdinc batch 2: remaining portable functions ─────────────────── */

/* Math extras */
static vigil_status_t sdl_fn_m_isinff(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double v = sdl_arg_f64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_isinff((float)v), error);
}

static vigil_status_t sdl_fn_m_isnanf(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double v = sdl_arg_f64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_isnanf((float)v), error);
}

static vigil_status_t sdl_fn_m_scalbnf(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = sdl_arg_f64(vm, base, 0);
    int n = (int)sdl_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, (double)SDL_scalbnf((float)x, n), error);
}

/* String extras */
static vigil_status_t sdl_fn_s_strnlen(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    int32_t n = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_strnlen(a, (size_t)n), error);
}

static vigil_status_t sdl_fn_s_strlcpy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char src[4096];
    sdl_arg_str(vm, base, 0, src, sizeof(src));
    int32_t maxlen = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    char dst[4096] = {0};
    SDL_strlcpy(dst, src, (size_t)(maxlen > 0 && maxlen < 4096 ? maxlen : 4095));
    return sdl_push_string(vm, dst, error);
}

static vigil_status_t sdl_fn_s_strlcat(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char dst[4096];
    sdl_arg_str(vm, base, 0, dst, sizeof(dst));
    char src[4096];
    sdl_arg_str(vm, base, 1, src, sizeof(src));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_strlcat(dst, src, sizeof(dst));
    return sdl_push_string(vm, dst, error);
}

static vigil_status_t sdl_fn_s_strpbrk(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096], b[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    sdl_arg_str(vm, base, 1, b, sizeof(b));
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *p = SDL_strpbrk(a, b);
    return sdl_push_i32(vm, p ? (int32_t)(p - a) : -1, error);
}

static vigil_status_t sdl_fn_s_strcasestr(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[4096], b[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    sdl_arg_str(vm, base, 1, b, sizeof(b));
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *p = SDL_strcasestr(a, b);
    return sdl_push_i32(vm, p ? (int32_t)(p - a) : -1, error);
}

static vigil_status_t sdl_fn_s_strtoll(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_strtoll(a, NULL, radix), error);
}

static vigil_status_t sdl_fn_s_strtoull(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char a[256];
    sdl_arg_str(vm, base, 0, a, sizeof(a));
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_strtoull(a, NULL, radix), error);
}

static vigil_status_t sdl_fn_s_uitoa(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t v = sdl_arg_i32(vm, base, 0);
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[68];
    SDL_uitoa((unsigned int)v, buf, radix);
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_s_ltoa(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t v = sdl_arg_i64(vm, base, 0);
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[68];
    SDL_ltoa((long)v, buf, radix);
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_s_ulltoa(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t v = sdl_arg_i64(vm, base, 0);
    int32_t radix = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[68];
    SDL_ulltoa((unsigned long long)v, buf, radix);
    return sdl_push_string(vm, buf, error);
}

/* Pixel format */
static vigil_status_t sdl_fn_get_pixel_format_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetPixelFormatName((SDL_PixelFormat)fmt), error);
}

/* Memory info */
static vigil_status_t sdl_fn_get_num_allocations(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetNumAllocations(), error);
}

static vigil_status_t sdl_fn_get_simd_alignment(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetSIMDAlignment(), error);
}

static vigil_status_t sdl_fn_get_system_page_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetSystemPageSize(), error);
}

/* Error */
static vigil_status_t sdl_fn_clear_error(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_ClearError();
    return VIGIL_STATUS_OK;
}

/* Byte swap */
static vigil_status_t sdl_fn_swap16(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t v = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_Swap16((Uint16)v), error);
}

static vigil_status_t sdl_fn_swap32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t v = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_Swap32((Uint32)v), error);
}

static vigil_status_t sdl_fn_swap64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t v = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_Swap64((Uint64)v), error);
}

/* Hints */
static vigil_status_t sdl_fn_set_hint(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256], val[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    sdl_arg_str(vm, base, 1, val, sizeof(val));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_SetHint(name, val), error);
}

static vigil_status_t sdl_fn_get_hint(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetHint(name), error);
}

static vigil_status_t sdl_fn_get_hint_boolean(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    int32_t def = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_GetHintBoolean(name, def != 0), error);
}

static vigil_status_t sdl_fn_reset_hint(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_ResetHint(name), error);
}

static vigil_status_t sdl_fn_reset_hints(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_ResetHints();
    return VIGIL_STATUS_OK;
}

/* Logging */
static vigil_status_t sdl_fn_log_info(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t cat = sdl_arg_i32(vm, base, 0);
    char msg[4096];
    sdl_arg_str(vm, base, 1, msg, sizeof(msg));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_LogInfo(cat, "%s", msg);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_log_debug(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t cat = sdl_arg_i32(vm, base, 0);
    char msg[4096];
    sdl_arg_str(vm, base, 1, msg, sizeof(msg));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_LogDebug(cat, "%s", msg);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_log_verbose(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t cat = sdl_arg_i32(vm, base, 0);
    char msg[4096];
    sdl_arg_str(vm, base, 1, msg, sizeof(msg));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_LogVerbose(cat, "%s", msg);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_log_critical(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t cat = sdl_arg_i32(vm, base, 0);
    char msg[4096];
    sdl_arg_str(vm, base, 1, msg, sizeof(msg));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_LogCritical(cat, "%s", msg);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_set_log_priority(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t cat = sdl_arg_i32(vm, base, 0);
    int32_t pri = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_SetLogPriority(cat, (SDL_LogPriority)pri);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_log_priority(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t cat = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetLogPriority(cat), error);
}

static vigil_status_t sdl_fn_reset_log_priorities(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_ResetLogPriorities();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_set_log_priorities(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t pri = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_SetLogPriorities((SDL_LogPriority)pri);
    return VIGIL_STATUS_OK;
}

/* ── Properties ───────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_create_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_PropertiesID id = SDL_CreateProperties();
    return sdl_push_i32(vm, (int32_t)id, error);
}

static vigil_status_t sdl_fn_destroy_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_DestroyProperties((SDL_PropertiesID)id);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_global_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGlobalProperties(), error);
}

static vigil_status_t sdl_fn_lock_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_LockProperties((SDL_PropertiesID)id))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_unlock_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_UnlockProperties((SDL_PropertiesID)id);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_copy_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t src = sdl_arg_i32(vm, base, 0);
    int32_t dst = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_CopyProperties((SDL_PropertiesID)src, (SDL_PropertiesID)dst))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_has_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasProperty((SDL_PropertiesID)id, name), error);
}

static vigil_status_t sdl_fn_get_property_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetPropertyType((SDL_PropertiesID)id, name), error);
}

static vigil_status_t sdl_fn_clear_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_ClearProperty((SDL_PropertiesID)id, name))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_set_string_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    char val[4096];
    sdl_arg_str(vm, base, 2, val, sizeof(val));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetStringProperty((SDL_PropertiesID)id, name, val))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_string_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    char def[256];
    sdl_arg_str(vm, base, 2, def, sizeof(def));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetStringProperty((SDL_PropertiesID)id, name, def), error);
}

static vigil_status_t sdl_fn_set_number_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    int64_t val = sdl_arg_i64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetNumberProperty((SDL_PropertiesID)id, name, val))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_number_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    int64_t def = sdl_arg_i64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, SDL_GetNumberProperty((SDL_PropertiesID)id, name, def), error);
}

static vigil_status_t sdl_fn_set_float_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    float val = (float)sdl_arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetFloatProperty((SDL_PropertiesID)id, name, val))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_float_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    float def = (float)sdl_arg_f64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_f64(vm, (double)SDL_GetFloatProperty((SDL_PropertiesID)id, name, def), error);
}

static vigil_status_t sdl_fn_set_boolean_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    int32_t val = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetBooleanProperty((SDL_PropertiesID)id, name, val != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_boolean_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    int32_t def = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_GetBooleanProperty((SDL_PropertiesID)id, name, def != 0), error);
}

/* Get*Properties for each subsystem */
static vigil_status_t sdl_fn_get_window_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, WIN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, h);
    return sdl_push_i32(vm, win ? (int32_t)SDL_GetWindowProperties(win) : 0, error);
}

static vigil_status_t sdl_fn_get_renderer_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, REN_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Renderer *ren = (SDL_Renderer *)SDL_HANDLE_GET(renderers, h);
    return sdl_push_i32(vm, ren ? (int32_t)SDL_GetRendererProperties(ren) : 0, error);
}

static vigil_status_t sdl_fn_get_texture_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, TEX_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *tex = (SDL_Texture *)SDL_HANDLE_GET(textures, h);
    return sdl_push_i32(vm, tex ? (int32_t)SDL_GetTextureProperties(tex) : 0, error);
}

static vigil_status_t sdl_fn_get_surface_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_field_i64(vm, base, SURF_HANDLE);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, h);
    return sdl_push_i32(vm, s ? (int32_t)SDL_GetSurfaceProperties(s) : 0, error);
}

static vigil_status_t sdl_fn_get_app_metadata_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_string(vm, SDL_GetAppMetadataProperty(name), error);
}

static vigil_status_t sdl_fn_set_app_metadata_property(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    char val[4096];
    sdl_arg_str(vm, base, 1, val, sizeof(val));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetAppMetadataProperty(name, val))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── IO Read/Write helpers (endian-aware) ─────────────────────────── */

#define SDL_IO_READ_FN(name, sdl_fn, type, push_fn)                                                                    \
    static vigil_status_t sdl_fn_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                        \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        int64_t h = sdl_arg_i64(vm, base, 0);                                                                          \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);                                              \
        type val = 0;                                                                                                  \
        if (io)                                                                                                        \
            sdl_fn(io, &val);                                                                                          \
        return push_fn(vm, val, error);                                                                                \
    }

#define SDL_IO_WRITE_FN(name, sdl_fn, arg_fn)                                                                          \
    static vigil_status_t sdl_fn_##name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)                        \
    {                                                                                                                  \
        size_t base = vigil_vm_stack_depth(vm) - arg_count;                                                            \
        int64_t h = sdl_arg_i64(vm, base, 0);                                                                          \
        vigil_vm_stack_pop_n(vm, arg_count);                                                                           \
        SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h);                                              \
        (void)error;                                                                                                   \
        return VIGIL_STATUS_OK;                                                                                        \
    }

/* clang-format off */
static vigil_status_t sdl_fn_io_read_u8(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); Uint8 val = 0; if (io) SDL_ReadU8(io, &val); return sdl_push_i32(vm, (int32_t)val, error); }

static vigil_status_t sdl_fn_io_read_s16le(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); Sint16 val = 0; if (io) SDL_ReadS16LE(io, &val); return sdl_push_i32(vm, (int32_t)val, error); }

static vigil_status_t sdl_fn_io_read_s16be(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); Sint16 val = 0; if (io) SDL_ReadS16BE(io, &val); return sdl_push_i32(vm, (int32_t)val, error); }

static vigil_status_t sdl_fn_io_read_s32le(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); Sint32 val = 0; if (io) SDL_ReadS32LE(io, &val); return sdl_push_i32(vm, (int32_t)val, error); }

static vigil_status_t sdl_fn_io_read_s32be(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); Sint32 val = 0; if (io) SDL_ReadS32BE(io, &val); return sdl_push_i32(vm, (int32_t)val, error); }

static vigil_status_t sdl_fn_io_read_s64le(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); Sint64 val = 0; if (io) SDL_ReadS64LE(io, &val); return sdl_push_i64(vm, (int64_t)val, error); }

static vigil_status_t sdl_fn_io_read_s64be(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); Sint64 val = 0; if (io) SDL_ReadS64BE(io, &val); return sdl_push_i64(vm, (int64_t)val, error); }

static vigil_status_t sdl_fn_io_write_u8(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); int32_t v = sdl_arg_i32(vm, base, 1); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); if (io && SDL_WriteU8(io, (Uint8)v)) return sdl_push_bool_ok(vm, error); return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error); }

static vigil_status_t sdl_fn_io_write_s16le(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); int32_t v = sdl_arg_i32(vm, base, 1); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); if (io && SDL_WriteS16LE(io, (Sint16)v)) return sdl_push_bool_ok(vm, error); return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error); }

static vigil_status_t sdl_fn_io_write_s32le(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); int32_t v = sdl_arg_i32(vm, base, 1); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); if (io && SDL_WriteS32LE(io, (Sint32)v)) return sdl_push_bool_ok(vm, error); return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error); }

static vigil_status_t sdl_fn_io_write_s64le(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); int64_t v = sdl_arg_i64(vm, base, 1); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); if (io && SDL_WriteS64LE(io, (Sint64)v)) return sdl_push_bool_ok(vm, error); return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error); }

static vigil_status_t sdl_fn_io_write_s16be(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); int32_t v = sdl_arg_i32(vm, base, 1); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); if (io && SDL_WriteS16BE(io, (Sint16)v)) return sdl_push_bool_ok(vm, error); return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error); }

static vigil_status_t sdl_fn_io_write_s32be(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); int32_t v = sdl_arg_i32(vm, base, 1); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); if (io && SDL_WriteS32BE(io, (Sint32)v)) return sdl_push_bool_ok(vm, error); return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error); }

static vigil_status_t sdl_fn_io_write_s64be(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{ size_t base = vigil_vm_stack_depth(vm) - arg_count; int64_t h = sdl_arg_i64(vm, base, 0); int64_t v = sdl_arg_i64(vm, base, 1); vigil_vm_stack_pop_n(vm, arg_count); SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, h); if (io && SDL_WriteS64BE(io, (Sint64)v)) return sdl_push_bool_ok(vm, error); return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error); }
/* clang-format on */

/* ── Misc batch: timers, memory, environment, process ─────────────── */

/* Timers (callback-based — store closure like HitTest) */
static vigil_vm_t *g_timer_vm = NULL;
static vigil_object_t *g_timer_closures[8] = {0};

static Uint32 sdl_timer_callback(void *userdata, SDL_TimerID timerID, Uint32 interval)
{
    (void)timerID;
    int slot = (int)(intptr_t)userdata;
    if (!g_timer_vm || slot < 0 || slot >= 8 || !g_timer_closures[slot])
        return 0;
    vigil_error_t err = {0};
    vigil_value_t arg = vigil_nanbox_encode_i32((int32_t)interval);
    vigil_vm_stack_push(g_timer_vm, &arg, &err);
    vigil_value_t result = {0};
    vigil_vm_execute_function(g_timer_vm, g_timer_closures[slot], &result, &err);
    return vigil_nanbox_is_int(result) ? (Uint32)vigil_nanbox_decode_int(result) : 0;
}

static vigil_status_t sdl_fn_add_timer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t interval = sdl_arg_i32(vm, base, 0);
    vigil_value_t fn_val = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_object_t *fn = (vigil_object_t *)vigil_nanbox_decode_ptr(fn_val);
    if (!fn)
        return sdl_push_i32(vm, 0, error);
    int slot = -1;
    for (int i = 0; i < 8; i++)
    {
        if (!g_timer_closures[i])
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return sdl_push_i32(vm, 0, error);
    vigil_object_retain(fn);
    g_timer_closures[slot] = fn;
    g_timer_vm = vm;
    SDL_TimerID id = SDL_AddTimer((Uint32)interval, sdl_timer_callback, (void *)(intptr_t)slot);
    if (id == 0)
    {
        vigil_object_release(&g_timer_closures[slot]);
        g_timer_closures[slot] = NULL;
    }
    return sdl_push_i32(vm, (int32_t)id, error);
}

static vigil_status_t sdl_fn_remove_timer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_RemoveTimer((SDL_TimerID)id), error);
}

/* Memory — operate on unsafe buffers */
static vigil_status_t sdl_fn_mem_alloc(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t size = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, vigil_unsafe_buffer_alloc(size), error);
}

static vigil_status_t sdl_fn_mem_free(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_unsafe_buffer_free(h);
    return VIGIL_STATUS_OK;
}

/* Environment */
static vigil_status_t sdl_fn_getenv_unsafe(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *val = SDL_getenv_unsafe(name);
    return sdl_push_string(vm, val ? val : "", error);
}

/* Process */
static vigil_status_t sdl_fn_get_current_thread_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i64(vm, (int64_t)SDL_GetCurrentThreadID(), error);
}

static vigil_status_t sdl_fn_is_main_thread(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_IsMainThread(), error);
}

/* DateTimeToTime */
static vigil_status_t sdl_fn_datetime_to_time(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t y = sdl_arg_i32(vm, base, 0), m = sdl_arg_i32(vm, base, 1), d = sdl_arg_i32(vm, base, 2);
    int32_t hr = sdl_arg_i32(vm, base, 3), mn = sdl_arg_i32(vm, base, 4), sec = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_DateTime dt = {0};
    dt.year = y;
    dt.month = m;
    dt.day = d;
    dt.hour = hr;
    dt.minute = mn;
    dt.second = sec;
    SDL_Time ticks = 0;
    SDL_DateTimeToTime(&dt, &ticks);
    return sdl_push_i64(vm, (int64_t)ticks, error);
}

/* Misc remaining */
static vigil_status_t sdl_fn_get_preferred_locales_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    int count = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    SDL_free(locales);
    return sdl_push_i32(vm, count, error);
}

static vigil_status_t sdl_fn_glob_directory(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512], pattern[256];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    sdl_arg_str(vm, base, 1, pattern, sizeof(pattern));
    vigil_vm_stack_pop_n(vm, arg_count);
    int count = 0;
    char **results = SDL_GlobDirectory(path, pattern[0] ? pattern : NULL, 0, &count);
    /* Return count — user can query individual results later */
    /* For now just return count; full array support would need iteration */
    SDL_free(results);
    return sdl_push_i32(vm, count, error);
}

/* ── Rect Math ────────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_point_in_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t px = sdl_arg_i32(vm, base, 0), py = sdl_arg_i32(vm, base, 1);
    int32_t rx = sdl_arg_i32(vm, base, 2), ry = sdl_arg_i32(vm, base, 3), rw = sdl_arg_i32(vm, base, 4),
            rh = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Point p = {px, py};
    SDL_Rect r = {rx, ry, rw, rh};
    return sdl_push_bool(vm, SDL_PointInRect(&p, &r), error);
}

static vigil_status_t sdl_fn_rect_empty(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t x = sdl_arg_i32(vm, base, 0), y = sdl_arg_i32(vm, base, 1), w = sdl_arg_i32(vm, base, 2),
            h = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect r = {x, y, w, h};
    return sdl_push_bool(vm, SDL_RectEmpty(&r), error);
}

static vigil_status_t sdl_fn_rects_equal(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Rect a = {sdl_arg_i32(vm, base, 0), sdl_arg_i32(vm, base, 1), sdl_arg_i32(vm, base, 2),
                  sdl_arg_i32(vm, base, 3)};
    SDL_Rect b = {sdl_arg_i32(vm, base, 4), sdl_arg_i32(vm, base, 5), sdl_arg_i32(vm, base, 6),
                  sdl_arg_i32(vm, base, 7)};
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_RectsEqual(&a, &b), error);
}

static vigil_status_t sdl_fn_has_rect_intersection(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Rect a = {sdl_arg_i32(vm, base, 0), sdl_arg_i32(vm, base, 1), sdl_arg_i32(vm, base, 2),
                  sdl_arg_i32(vm, base, 3)};
    SDL_Rect b = {sdl_arg_i32(vm, base, 4), sdl_arg_i32(vm, base, 5), sdl_arg_i32(vm, base, 6),
                  sdl_arg_i32(vm, base, 7)};
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasRectIntersection(&a, &b), error);
}

/* get_rect_intersection(ax,ay,aw,ah, bx,by,bw,bh) -> (i32 w, i32 h) of intersection */
static vigil_status_t sdl_fn_get_rect_intersection(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Rect a = {sdl_arg_i32(vm, base, 0), sdl_arg_i32(vm, base, 1), sdl_arg_i32(vm, base, 2),
                  sdl_arg_i32(vm, base, 3)};
    SDL_Rect b = {sdl_arg_i32(vm, base, 4), sdl_arg_i32(vm, base, 5), sdl_arg_i32(vm, base, 6),
                  sdl_arg_i32(vm, base, 7)};
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect result = {0};
    SDL_GetRectIntersection(&a, &b, &result);
    vigil_status_t st = sdl_push_i32(vm, result.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, result.h, error);
}

/* get_rect_union(ax,ay,aw,ah, bx,by,bw,bh) -> (i32 w, i32 h) of union */
static vigil_status_t sdl_fn_get_rect_union(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Rect a = {sdl_arg_i32(vm, base, 0), sdl_arg_i32(vm, base, 1), sdl_arg_i32(vm, base, 2),
                  sdl_arg_i32(vm, base, 3)};
    SDL_Rect b = {sdl_arg_i32(vm, base, 4), sdl_arg_i32(vm, base, 5), sdl_arg_i32(vm, base, 6),
                  sdl_arg_i32(vm, base, 7)};
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Rect result = {0};
    SDL_GetRectUnion(&a, &b, &result);
    vigil_status_t st = sdl_push_i32(vm, result.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, result.h, error);
}

/* Float variants */
static vigil_status_t sdl_fn_point_in_rect_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    float px = (float)sdl_arg_f64(vm, base, 0), py = (float)sdl_arg_f64(vm, base, 1);
    float rx = (float)sdl_arg_f64(vm, base, 2), ry = (float)sdl_arg_f64(vm, base, 3),
          rw = (float)sdl_arg_f64(vm, base, 4), rh = (float)sdl_arg_f64(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_FPoint p = {px, py};
    SDL_FRect r = {rx, ry, rw, rh};
    return sdl_push_bool(vm, SDL_PointInRectFloat(&p, &r), error);
}

static vigil_status_t sdl_fn_rect_empty_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_FRect r = {(float)sdl_arg_f64(vm, base, 0), (float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2),
                   (float)sdl_arg_f64(vm, base, 3)};
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_RectEmptyFloat(&r), error);
}

static vigil_status_t sdl_fn_has_rect_intersection_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_FRect a = {(float)sdl_arg_f64(vm, base, 0), (float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2),
                   (float)sdl_arg_f64(vm, base, 3)};
    SDL_FRect b = {(float)sdl_arg_f64(vm, base, 4), (float)sdl_arg_f64(vm, base, 5), (float)sdl_arg_f64(vm, base, 6),
                   (float)sdl_arg_f64(vm, base, 7)};
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasRectIntersectionFloat(&a, &b), error);
}

/* ── Async IO ─────────────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(async_ios);
SDL_HANDLE_REGISTRY(async_io_queues);

static vigil_status_t sdl_fn_async_io_from_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512], mode[8];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    sdl_arg_str(vm, base, 1, mode, sizeof(mode));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIO *aio = SDL_AsyncIOFromFile(path, mode);
    if (!aio)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(async_ios, aio, &h) < 0)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many async IOs", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_async_io_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIO *aio = (SDL_AsyncIO *)SDL_HANDLE_GET(async_ios, h);
    return sdl_push_i64(vm, aio ? SDL_GetAsyncIOSize(aio) : -1, error);
}

static vigil_status_t sdl_fn_create_async_io_queue(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIOQueue *q = SDL_CreateAsyncIOQueue();
    if (!q)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(async_io_queues, q, &h) < 0)
    {
        SDL_DestroyAsyncIOQueue(q);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many queues", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_destroy_async_io_queue(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIOQueue *q = (SDL_AsyncIOQueue *)SDL_HANDLE_GET(async_io_queues, h);
    if (q)
    {
        SDL_DestroyAsyncIOQueue(q);
        SDL_HANDLE_CLEAR(async_io_queues, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_signal_async_io_queue(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIOQueue *q = (SDL_AsyncIOQueue *)SDL_HANDLE_GET(async_io_queues, h);
    if (q)
        SDL_SignalAsyncIOQueue(q);
    return VIGIL_STATUS_OK;
}

/* async_io_read(aio, buf, offset, size, queue) -> (bool, err) */
static vigil_status_t sdl_fn_async_io_read(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ah = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    int64_t offset = sdl_arg_i64(vm, base, 2);
    int32_t size = sdl_arg_i32(vm, base, 3);
    int64_t qh = sdl_arg_i64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIO *aio = (SDL_AsyncIO *)SDL_HANDLE_GET(async_ios, ah);
    int32_t bsz = 0;
    void *buf = vigil_unsafe_buffer_get(bh, &bsz);
    SDL_AsyncIOQueue *q = (SDL_AsyncIOQueue *)SDL_HANDLE_GET(async_io_queues, qh);
    if (aio && buf && q && SDL_ReadAsyncIO(aio, buf, (Uint64)offset, (Uint64)size, q, NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_async_io_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ah = sdl_arg_i64(vm, base, 0), bh = sdl_arg_i64(vm, base, 1);
    int64_t offset = sdl_arg_i64(vm, base, 2);
    int32_t size = sdl_arg_i32(vm, base, 3);
    int64_t qh = sdl_arg_i64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIO *aio = (SDL_AsyncIO *)SDL_HANDLE_GET(async_ios, ah);
    int32_t bsz = 0;
    void *buf = vigil_unsafe_buffer_get(bh, &bsz);
    SDL_AsyncIOQueue *q = (SDL_AsyncIOQueue *)SDL_HANDLE_GET(async_io_queues, qh);
    if (aio && buf && q && SDL_WriteAsyncIO(aio, buf, (Uint64)offset, (Uint64)size, q, NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_close_async_io(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ah = sdl_arg_i64(vm, base, 0);
    int32_t flush = sdl_arg_i32(vm, base, 1);
    int64_t qh = sdl_arg_i64(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIO *aio = (SDL_AsyncIO *)SDL_HANDLE_GET(async_ios, ah);
    SDL_AsyncIOQueue *q = (SDL_AsyncIOQueue *)SDL_HANDLE_GET(async_io_queues, qh);
    if (aio && SDL_CloseAsyncIO(aio, flush != 0, q, NULL))
    {
        SDL_HANDLE_CLEAR(async_ios, ah);
        return sdl_push_bool_ok(vm, error);
    }
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* get_async_io_result(queue) -> i32 (result code, 0=nothing, 1=complete, -1=error) */
static vigil_status_t sdl_fn_get_async_io_result(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t qh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIOQueue *q = (SDL_AsyncIOQueue *)SDL_HANDLE_GET(async_io_queues, qh);
    if (!q)
        return sdl_push_i32(vm, 0, error);
    SDL_AsyncIOOutcome outcome = {0};
    if (SDL_GetAsyncIOResult(q, &outcome))
        return sdl_push_i32(vm, (int32_t)outcome.result, error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_wait_async_io_result(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t qh = sdl_arg_i64(vm, base, 0);
    int32_t timeout = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AsyncIOQueue *q = (SDL_AsyncIOQueue *)SDL_HANDLE_GET(async_io_queues, qh);
    if (!q)
        return sdl_push_i32(vm, 0, error);
    SDL_AsyncIOOutcome outcome = {0};
    if (SDL_WaitAsyncIOResult(q, &outcome, timeout))
        return sdl_push_i32(vm, (int32_t)outcome.result, error);
    return sdl_push_i32(vm, 0, error);
}

/* ── SDL Threading Primitives ─────────────────────────────────────── */

SDL_HANDLE_REGISTRY(mutexes);
SDL_HANDLE_REGISTRY(rwlocks);
SDL_HANDLE_REGISTRY(semaphores);
SDL_HANDLE_REGISTRY(conditions);

/* Mutex */
static vigil_status_t sdl_fn_create_mutex(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Mutex *m = SDL_CreateMutex();
    if (!m)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(mutexes, m, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_destroy_mutex(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Mutex *m = (SDL_Mutex *)SDL_HANDLE_GET(mutexes, h);
    if (m)
    {
        SDL_DestroyMutex(m);
        SDL_HANDLE_CLEAR(mutexes, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_lock_mutex(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Mutex *m = (SDL_Mutex *)SDL_HANDLE_GET(mutexes, h);
    if (m)
        SDL_LockMutex(m);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_try_lock_mutex(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Mutex *m = (SDL_Mutex *)SDL_HANDLE_GET(mutexes, h);
    return sdl_push_bool(vm, m && SDL_TryLockMutex(m), error);
}

static vigil_status_t sdl_fn_unlock_mutex(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Mutex *m = (SDL_Mutex *)SDL_HANDLE_GET(mutexes, h);
    if (m)
        SDL_UnlockMutex(m);
    return VIGIL_STATUS_OK;
}

/* RWLock */
static vigil_status_t sdl_fn_create_rwlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_RWLock *rw = SDL_CreateRWLock();
    if (!rw)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(rwlocks, rw, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_destroy_rwlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_RWLock *rw = (SDL_RWLock *)SDL_HANDLE_GET(rwlocks, h);
    if (rw)
    {
        SDL_DestroyRWLock(rw);
        SDL_HANDLE_CLEAR(rwlocks, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_lock_rwlock_read(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_RWLock *rw = (SDL_RWLock *)SDL_HANDLE_GET(rwlocks, h);
    if (rw)
        SDL_LockRWLockForReading(rw);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_lock_rwlock_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_RWLock *rw = (SDL_RWLock *)SDL_HANDLE_GET(rwlocks, h);
    if (rw)
        SDL_LockRWLockForWriting(rw);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_try_lock_rwlock_read(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_RWLock *rw = (SDL_RWLock *)SDL_HANDLE_GET(rwlocks, h);
    return sdl_push_bool(vm, rw && SDL_TryLockRWLockForReading(rw), error);
}

static vigil_status_t sdl_fn_try_lock_rwlock_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_RWLock *rw = (SDL_RWLock *)SDL_HANDLE_GET(rwlocks, h);
    return sdl_push_bool(vm, rw && SDL_TryLockRWLockForWriting(rw), error);
}

static vigil_status_t sdl_fn_unlock_rwlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_RWLock *rw = (SDL_RWLock *)SDL_HANDLE_GET(rwlocks, h);
    if (rw)
        SDL_UnlockRWLock(rw);
    return VIGIL_STATUS_OK;
}

/* Semaphore */
static vigil_status_t sdl_fn_create_semaphore(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t val = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Semaphore *s = SDL_CreateSemaphore((Uint32)val);
    if (!s)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(semaphores, s, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_destroy_semaphore(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Semaphore *s = (SDL_Semaphore *)SDL_HANDLE_GET(semaphores, h);
    if (s)
    {
        SDL_DestroySemaphore(s);
        SDL_HANDLE_CLEAR(semaphores, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_wait_semaphore(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Semaphore *s = (SDL_Semaphore *)SDL_HANDLE_GET(semaphores, h);
    if (s)
        SDL_WaitSemaphore(s);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_try_wait_semaphore(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Semaphore *s = (SDL_Semaphore *)SDL_HANDLE_GET(semaphores, h);
    return sdl_push_bool(vm, s && SDL_TryWaitSemaphore(s), error);
}

static vigil_status_t sdl_fn_wait_semaphore_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int32_t ms = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Semaphore *s = (SDL_Semaphore *)SDL_HANDLE_GET(semaphores, h);
    return sdl_push_bool(vm, s && SDL_WaitSemaphoreTimeout(s, (Sint32)ms), error);
}

static vigil_status_t sdl_fn_signal_semaphore(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Semaphore *s = (SDL_Semaphore *)SDL_HANDLE_GET(semaphores, h);
    if (s)
        SDL_SignalSemaphore(s);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_semaphore_value(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Semaphore *s = (SDL_Semaphore *)SDL_HANDLE_GET(semaphores, h);
    return sdl_push_i32(vm, s ? (int32_t)SDL_GetSemaphoreValue(s) : 0, error);
}

/* Condition */
static vigil_status_t sdl_fn_create_condition(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Condition *c = SDL_CreateCondition();
    if (!c)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(conditions, c, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_destroy_condition(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Condition *c = (SDL_Condition *)SDL_HANDLE_GET(conditions, h);
    if (c)
    {
        SDL_DestroyCondition(c);
        SDL_HANDLE_CLEAR(conditions, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_signal_condition(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Condition *c = (SDL_Condition *)SDL_HANDLE_GET(conditions, h);
    if (c)
        SDL_SignalCondition(c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_broadcast_condition(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Condition *c = (SDL_Condition *)SDL_HANDLE_GET(conditions, h);
    if (c)
        SDL_BroadcastCondition(c);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_wait_condition(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    int64_t mh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Condition *c = (SDL_Condition *)SDL_HANDLE_GET(conditions, ch);
    SDL_Mutex *m = (SDL_Mutex *)SDL_HANDLE_GET(mutexes, mh);
    if (c && m)
        SDL_WaitCondition(c, m);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_wait_condition_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    int64_t mh = sdl_arg_i64(vm, base, 1);
    int32_t ms = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Condition *c = (SDL_Condition *)SDL_HANDLE_GET(conditions, ch);
    SDL_Mutex *m = (SDL_Mutex *)SDL_HANDLE_GET(mutexes, mh);
    return sdl_push_bool(vm, c && m && SDL_WaitConditionTimeout(c, m, (Sint32)ms), error);
}

/* Atomics */
static vigil_status_t sdl_fn_set_atomic_int(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t val = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    if (p && sz >= (int32_t)sizeof(SDL_AtomicInt))
        return sdl_push_i32(vm, SDL_SetAtomicInt((SDL_AtomicInt *)p, val), error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_get_atomic_int(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    if (p && sz >= (int32_t)sizeof(SDL_AtomicInt))
        return sdl_push_i32(vm, SDL_GetAtomicInt((SDL_AtomicInt *)p), error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_add_atomic_int(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t val = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    if (p && sz >= (int32_t)sizeof(SDL_AtomicInt))
        return sdl_push_i32(vm, SDL_AddAtomicInt((SDL_AtomicInt *)p, val), error);
    return sdl_push_i32(vm, 0, error);
}

/* Thread priority */
static vigil_status_t sdl_fn_set_current_thread_priority(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t pri = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetCurrentThreadPriority((SDL_ThreadPriority)pri))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Storage ──────────────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(storages);

static vigil_status_t sdl_fn_open_title_storage(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char override[512];
    sdl_arg_str(vm, base, 0, override, sizeof(override));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = SDL_OpenTitleStorage(override[0] ? override : NULL, 0);
    if (!s)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(storages, s, &h) < 0)
    {
        SDL_CloseStorage(s);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many storages", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_open_user_storage(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char org[256], app[256];
    sdl_arg_str(vm, base, 0, org, sizeof(org));
    sdl_arg_str(vm, base, 1, app, sizeof(app));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = SDL_OpenUserStorage(org, app, 0);
    if (!s)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(storages, s, &h) < 0)
    {
        SDL_CloseStorage(s);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many storages", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_open_file_storage(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = SDL_OpenFileStorage(path);
    if (!s)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(storages, s, &h) < 0)
    {
        SDL_CloseStorage(s);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many storages", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_close_storage(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    if (s && SDL_CloseStorage(s))
    {
        SDL_HANDLE_CLEAR(storages, h);
        return sdl_push_bool_ok(vm, error);
    }
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_storage_ready(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    return sdl_push_bool(vm, s && SDL_StorageReady(s), error);
}

static vigil_status_t sdl_fn_get_storage_file_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint64 len = 0;
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    if (s)
        SDL_GetStorageFileSize(s, path, &len);
    return sdl_push_i64(vm, (int64_t)len, error);
}

static vigil_status_t sdl_fn_read_storage_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    int64_t bh = sdl_arg_i64(vm, base, 2);
    int32_t len = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    int32_t bsz = 0;
    void *buf = vigil_unsafe_buffer_get(bh, &bsz);
    if (s && buf && SDL_ReadStorageFile(s, path, buf, (Uint64)(len > 0 ? len : bsz)))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_write_storage_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    int64_t bh = sdl_arg_i64(vm, base, 2);
    int32_t len = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    int32_t bsz = 0;
    void *buf = vigil_unsafe_buffer_get(bh, &bsz);
    if (s && buf && SDL_WriteStorageFile(s, path, buf, (Uint64)(len > 0 ? len : bsz)))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_create_storage_directory(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    if (s && SDL_CreateStorageDirectory(s, path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_remove_storage_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    if (s && SDL_RemoveStoragePath(s, path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_rename_storage_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char old[512];
    sdl_arg_str(vm, base, 1, old, sizeof(old));
    char new_path[512];
    sdl_arg_str(vm, base, 2, new_path, sizeof(new_path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    if (s && SDL_RenameStoragePath(s, old, new_path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_copy_storage_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char old[512];
    sdl_arg_str(vm, base, 1, old, sizeof(old));
    char new_path[512];
    sdl_arg_str(vm, base, 2, new_path, sizeof(new_path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    if (s && SDL_CopyStorageFile(s, old, new_path))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_storage_path_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char path[512];
    sdl_arg_str(vm, base, 1, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_PathInfo info = {0};
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    if (s)
        SDL_GetStoragePathInfo(s, path, &info);
    return sdl_push_i32(vm, (int32_t)info.type, error);
}

static vigil_status_t sdl_fn_get_storage_space_remaining(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Storage *s = (SDL_Storage *)SDL_HANDLE_GET(storages, h);
    return sdl_push_i64(vm, s ? (int64_t)SDL_GetStorageSpaceRemaining(s) : 0, error);
}

/* ── Process ──────────────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(processes);

static vigil_status_t sdl_fn_create_process(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char cmd[512];
    sdl_arg_str(vm, base, 0, cmd, sizeof(cmd));
    int32_t pipe_stdio = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *args[] = {cmd, NULL};
    SDL_Process *p = SDL_CreateProcess(args, pipe_stdio != 0);
    if (!p)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    if (SDL_HANDLE_STORE(processes, p, &h) < 0)
    {
        SDL_DestroyProcess(p);
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many processes", SDL_ERR_STATE, error);
    }
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_destroy_process(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Process *p = (SDL_Process *)SDL_HANDLE_GET(processes, h);
    if (p)
    {
        SDL_DestroyProcess(p);
        SDL_HANDLE_CLEAR(processes, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_kill_process(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int32_t force = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Process *p = (SDL_Process *)SDL_HANDLE_GET(processes, h);
    if (p && SDL_KillProcess(p, force != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_wait_process(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int32_t block = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int exitcode = -1;
    SDL_Process *p = (SDL_Process *)SDL_HANDLE_GET(processes, h);
    if (p)
        SDL_WaitProcess(p, block != 0, &exitcode);
    return sdl_push_i32(vm, exitcode, error);
}

static vigil_status_t sdl_fn_read_process(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Process *p = (SDL_Process *)SDL_HANDLE_GET(processes, h);
    if (!p)
        return sdl_push_string(vm, "", error);
    size_t datasize = 0;
    int exitcode = 0;
    void *data = SDL_ReadProcess(p, &datasize, &exitcode);
    if (!data)
        return sdl_push_string(vm, "", error);
    /* Register as unsafe buffer */
    int64_t bh = vigil_unsafe_buffer_register(data, (int32_t)datasize);
    return sdl_push_i64(vm, bh, error);
}

/* ── Palette ──────────────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(palettes);

static vigil_status_t sdl_fn_create_palette(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t ncolors = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Palette *p = SDL_CreatePalette(ncolors);
    if (!p)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(palettes, p, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_destroy_palette(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Palette *p = (SDL_Palette *)SDL_HANDLE_GET(palettes, h);
    if (p)
    {
        SDL_DestroyPalette(p);
        SDL_HANDLE_CLEAR(palettes, h);
    }
    return VIGIL_STATUS_OK;
}

/* set_palette_colors(palette, buf_handle, first, count) -> bool */
static vigil_status_t sdl_fn_set_palette_colors(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t first = sdl_arg_i32(vm, base, 2);
    int32_t count = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Palette *p = (SDL_Palette *)SDL_HANDLE_GET(palettes, ph);
    int32_t bsz = 0;
    void *colors = vigil_unsafe_buffer_get(bh, &bsz);
    if (p && colors && SDL_SetPaletteColors(p, (const SDL_Color *)colors, first, count))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Tray ─────────────────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(trays);
SDL_HANDLE_REGISTRY(tray_menus);
SDL_HANDLE_REGISTRY(tray_entries);

static vigil_status_t sdl_fn_create_tray(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char tooltip[256];
    sdl_arg_str(vm, base, 0, tooltip, sizeof(tooltip));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Tray *t = SDL_CreateTray(NULL, tooltip[0] ? tooltip : NULL);
    if (!t)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    SDL_HANDLE_STORE(trays, t, &h);
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_destroy_tray(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Tray *t = (SDL_Tray *)SDL_HANDLE_GET(trays, h);
    if (t)
    {
        SDL_DestroyTray(t);
        SDL_HANDLE_CLEAR(trays, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_set_tray_tooltip(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char tip[256];
    sdl_arg_str(vm, base, 1, tip, sizeof(tip));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Tray *t = (SDL_Tray *)SDL_HANDLE_GET(trays, h);
    if (t)
        SDL_SetTrayTooltip(t, tip);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_create_tray_menu(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Tray *t = (SDL_Tray *)SDL_HANDLE_GET(trays, th);
    if (!t)
        return sdl_push_i64(vm, -1, error);
    SDL_TrayMenu *m = SDL_CreateTrayMenu(t);
    if (!m)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(tray_menus, m, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_insert_tray_entry(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t mh = sdl_arg_i64(vm, base, 0);
    int32_t pos = sdl_arg_i32(vm, base, 1);
    char label[256];
    sdl_arg_str(vm, base, 2, label, sizeof(label));
    int32_t flags = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayMenu *m = (SDL_TrayMenu *)SDL_HANDLE_GET(tray_menus, mh);
    if (!m)
        return sdl_push_i64(vm, -1, error);
    SDL_TrayEntry *e = SDL_InsertTrayEntryAt(m, pos, label[0] ? label : NULL, (SDL_TrayEntryFlags)flags);
    if (!e)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(tray_entries, e, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_remove_tray_entry(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, h);
    if (e)
    {
        SDL_RemoveTrayEntry(e);
        SDL_HANDLE_CLEAR(tray_entries, h);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_set_tray_entry_label(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    char label[256];
    sdl_arg_str(vm, base, 1, label, sizeof(label));
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, h);
    if (e)
        SDL_SetTrayEntryLabel(e, label);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_tray_entry_label(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, h);
    return sdl_push_string(vm, e ? SDL_GetTrayEntryLabel(e) : "", error);
}

static vigil_status_t sdl_fn_set_tray_entry_checked(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int32_t checked = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, h);
    if (e)
        SDL_SetTrayEntryChecked(e, checked != 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_tray_entry_checked(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, h);
    return sdl_push_bool(vm, e && SDL_GetTrayEntryChecked(e), error);
}

static vigil_status_t sdl_fn_set_tray_entry_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    int32_t en = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, h);
    if (e)
        SDL_SetTrayEntryEnabled(e, en != 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_tray_entry_enabled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, h);
    return sdl_push_bool(vm, e && SDL_GetTrayEntryEnabled(e), error);
}

static vigil_status_t sdl_fn_update_trays(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_UpdateTrays();
    return VIGIL_STATUS_OK;
}

/* ── OpenGL ───────────────────────────────────────────────────────── */

SDL_HANDLE_REGISTRY(gl_contexts);

static vigil_status_t sdl_fn_gl_load_library(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_GL_LoadLibrary(path[0] ? path : NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gl_unload_library(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GL_UnloadLibrary();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gl_extension_supported(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char ext[256];
    sdl_arg_str(vm, base, 0, ext, sizeof(ext));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_GL_ExtensionSupported(ext), error);
}

static vigil_status_t sdl_fn_gl_reset_attributes(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GL_ResetAttributes();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_gl_set_attribute(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t attr = sdl_arg_i32(vm, base, 0);
    int32_t val = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_GL_SetAttribute((SDL_GLAttr)attr, val))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gl_get_attribute(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t attr = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int val = 0;
    SDL_GL_GetAttribute((SDL_GLAttr)attr, &val);
    return sdl_push_i32(vm, val, error);
}

static vigil_status_t sdl_fn_gl_create_context(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (!w)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "invalid window", SDL_ERR_STATE, error);
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(w);
    if (!ctx)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    SDL_HANDLE_STORE(gl_contexts, ctx, &h);
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_gl_destroy_context(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GLContext ctx = (SDL_GLContext)SDL_HANDLE_GET(gl_contexts, h);
    if (ctx && SDL_GL_DestroyContext(ctx))
    {
        SDL_HANDLE_CLEAR(gl_contexts, h);
        return sdl_push_bool_ok(vm, error);
    }
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gl_make_current(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    int64_t ch = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    SDL_GLContext ctx = (SDL_GLContext)SDL_HANDLE_GET(gl_contexts, ch);
    if (w && SDL_GL_MakeCurrent(w, ctx))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gl_set_swap_interval(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t interval = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_GL_SetSwapInterval(interval))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gl_get_swap_interval(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    int interval = 0;
    SDL_GL_GetSwapInterval(&interval);
    return sdl_push_i32(vm, interval, error);
}

static vigil_status_t sdl_fn_gl_swap_window(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (w && SDL_GL_SwapWindow(w))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Vulkan ───────────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_vulkan_load_library(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_Vulkan_LoadLibrary(path[0] ? path : NULL))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_vulkan_unload_library(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Vulkan_UnloadLibrary();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_vulkan_get_instance_extensions(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint32 count = 0;
    const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!exts || count == 0)
        return sdl_push_string(vm, "", error);
    /* Join with comma */
    char buf[2048] = {0};
    size_t off = 0;
    for (Uint32 i = 0; i < count && off < sizeof(buf) - 1; i++)
    {
        if (i > 0 && off < sizeof(buf) - 1)
            buf[off++] = ',';
        size_t len = strlen(exts[i]);
        if (off + len >= sizeof(buf))
            break;
        memcpy(buf + off, exts[i], len);
        off += len;
    }
    return sdl_push_string(vm, buf, error);
}

/* ── Metal ────────────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_metal_create_view(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    if (!w)
        return sdl_push_i64(vm, 0, error);
    SDL_MetalView v = SDL_Metal_CreateView(w);
    return sdl_push_i64(vm, (int64_t)(uintptr_t)v, error);
}

static vigil_status_t sdl_fn_metal_destroy_view(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t v = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (v)
        SDL_Metal_DestroyView((SDL_MetalView)(uintptr_t)v);
    return VIGIL_STATUS_OK;
}

/* ── Remaining Rect Math ──────────────────────────────────────────── */

/* rect_to_frect(x,y,w,h) -> (f64,f64,f64,f64) */
static vigil_status_t sdl_fn_rect_to_frect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Rect r = {sdl_arg_i32(vm, base, 0), sdl_arg_i32(vm, base, 1), sdl_arg_i32(vm, base, 2),
                  sdl_arg_i32(vm, base, 3)};
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_FRect fr;
    SDL_RectToFRect(&r, &fr);
    vigil_status_t st;
    st = sdl_push_f64(vm, fr.x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, fr.y, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, fr.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, fr.h, error);
}

static vigil_status_t sdl_fn_rects_equal_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_FRect a = {(float)sdl_arg_f64(vm, base, 0), (float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2),
                   (float)sdl_arg_f64(vm, base, 3)};
    SDL_FRect b = {(float)sdl_arg_f64(vm, base, 4), (float)sdl_arg_f64(vm, base, 5), (float)sdl_arg_f64(vm, base, 6),
                   (float)sdl_arg_f64(vm, base, 7)};
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_RectsEqualFloat(&a, &b), error);
}

static vigil_status_t sdl_fn_rects_equal_epsilon(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_FRect a = {(float)sdl_arg_f64(vm, base, 0), (float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2),
                   (float)sdl_arg_f64(vm, base, 3)};
    SDL_FRect b = {(float)sdl_arg_f64(vm, base, 4), (float)sdl_arg_f64(vm, base, 5), (float)sdl_arg_f64(vm, base, 6),
                   (float)sdl_arg_f64(vm, base, 7)};
    float eps = (float)sdl_arg_f64(vm, base, 8);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_RectsEqualEpsilon(&a, &b, eps), error);
}

static vigil_status_t sdl_fn_get_rect_intersection_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_FRect a = {(float)sdl_arg_f64(vm, base, 0), (float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2),
                   (float)sdl_arg_f64(vm, base, 3)};
    SDL_FRect b = {(float)sdl_arg_f64(vm, base, 4), (float)sdl_arg_f64(vm, base, 5), (float)sdl_arg_f64(vm, base, 6),
                   (float)sdl_arg_f64(vm, base, 7)};
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_FRect result = {0};
    SDL_GetRectIntersectionFloat(&a, &b, &result);
    vigil_status_t st = sdl_push_f64(vm, result.x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, result.y, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, result.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, result.h, error);
}

static vigil_status_t sdl_fn_get_rect_union_float(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_FRect a = {(float)sdl_arg_f64(vm, base, 0), (float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2),
                   (float)sdl_arg_f64(vm, base, 3)};
    SDL_FRect b = {(float)sdl_arg_f64(vm, base, 4), (float)sdl_arg_f64(vm, base, 5), (float)sdl_arg_f64(vm, base, 6),
                   (float)sdl_arg_f64(vm, base, 7)};
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_FRect result = {0};
    SDL_GetRectUnionFloat(&a, &b, &result);
    vigil_status_t st = sdl_push_f64(vm, result.x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, result.y, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, result.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, result.h, error);
}

/* get_rect_and_line_intersection(rx,ry,rw,rh, x1,y1,x2,y2) -> (bool, i32 x1, i32 y1, i32 x2, i32 y2) */
static vigil_status_t sdl_fn_get_rect_and_line_intersection(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Rect r = {sdl_arg_i32(vm, base, 0), sdl_arg_i32(vm, base, 1), sdl_arg_i32(vm, base, 2),
                  sdl_arg_i32(vm, base, 3)};
    int x1 = sdl_arg_i32(vm, base, 4), y1 = sdl_arg_i32(vm, base, 5), x2 = sdl_arg_i32(vm, base, 6),
        y2 = sdl_arg_i32(vm, base, 7);
    vigil_vm_stack_pop_n(vm, arg_count);
    bool hit = SDL_GetRectAndLineIntersection(&r, &x1, &y1, &x2, &y2);
    vigil_status_t st;
    st = sdl_push_bool(vm, hit, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, x1, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, y1, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, x2, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, y2, error);
}

static vigil_status_t sdl_fn_get_rect_and_line_intersection_float(vigil_vm_t *vm, size_t arg_count,
                                                                  vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_FRect r = {(float)sdl_arg_f64(vm, base, 0), (float)sdl_arg_f64(vm, base, 1), (float)sdl_arg_f64(vm, base, 2),
                   (float)sdl_arg_f64(vm, base, 3)};
    float x1 = (float)sdl_arg_f64(vm, base, 4), y1 = (float)sdl_arg_f64(vm, base, 5),
          x2 = (float)sdl_arg_f64(vm, base, 6), y2 = (float)sdl_arg_f64(vm, base, 7);
    vigil_vm_stack_pop_n(vm, arg_count);
    bool hit = SDL_GetRectAndLineIntersectionFloat(&r, &x1, &y1, &x2, &y2);
    vigil_status_t st;
    st = sdl_push_bool(vm, hit, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, x1, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, y1, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, x2, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, y2, error);
}

/* ── Spinlock ─────────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_lock_spinlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    if (p && sz >= (int32_t)sizeof(SDL_SpinLock))
        SDL_LockSpinlock((SDL_SpinLock *)p);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_try_lock_spinlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    return sdl_push_bool(vm, p && sz >= (int32_t)sizeof(SDL_SpinLock) && SDL_TryLockSpinlock((SDL_SpinLock *)p), error);
}

static vigil_status_t sdl_fn_unlock_spinlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    if (p && sz >= (int32_t)sizeof(SDL_SpinLock))
        SDL_UnlockSpinlock((SDL_SpinLock *)p);
    return VIGIL_STATUS_OK;
}

/* ── More Atomics ─────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_compare_and_swap_atomic_int(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t oldval = sdl_arg_i32(vm, base, 1);
    int32_t newval = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    return sdl_push_bool(vm,
                         p && sz >= (int32_t)sizeof(SDL_AtomicInt) &&
                             SDL_CompareAndSwapAtomicInt((SDL_AtomicInt *)p, oldval, newval),
                         error);
}

static vigil_status_t sdl_fn_set_atomic_u32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t val = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    if (p && sz >= (int32_t)sizeof(SDL_AtomicU32))
        return sdl_push_i32(vm, (int32_t)SDL_SetAtomicU32((SDL_AtomicU32 *)p, (Uint32)val), error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_get_atomic_u32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    if (p && sz >= (int32_t)sizeof(SDL_AtomicU32))
        return sdl_push_i32(vm, (int32_t)SDL_GetAtomicU32((SDL_AtomicU32 *)p), error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_add_atomic_u32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t val = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    if (p && sz >= (int32_t)sizeof(SDL_AtomicU32))
        return sdl_push_i32(vm, (int32_t)SDL_AddAtomicU32((SDL_AtomicU32 *)p, val), error);
    return sdl_push_i32(vm, 0, error);
}

static vigil_status_t sdl_fn_compare_and_swap_atomic_u32(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t bh = sdl_arg_i64(vm, base, 0);
    int32_t oldval = sdl_arg_i32(vm, base, 1);
    int32_t newval = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t sz = 0;
    void *p = vigil_unsafe_buffer_get(bh, &sz);
    return sdl_push_bool(vm,
                         p && sz >= (int32_t)sizeof(SDL_AtomicU32) &&
                             SDL_CompareAndSwapAtomicU32((SDL_AtomicU32 *)p, (Uint32)oldval, (Uint32)newval),
                         error);
}

/* ── Misc Utility ─────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_silence_value_for_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetSilenceValueForFormat((SDL_AudioFormat)fmt), error);
}

static vigil_status_t sdl_fn_get_pixel_format_for_masks(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t bpp = sdl_arg_i32(vm, base, 0);
    int32_t rm = sdl_arg_i32(vm, base, 1);
    int32_t gm = sdl_arg_i32(vm, base, 2);
    int32_t bm = sdl_arg_i32(vm, base, 3);
    int32_t am = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetPixelFormatForMasks(bpp, (Uint32)rm, (Uint32)gm, (Uint32)bm, (Uint32)am),
                        error);
}

static vigil_status_t sdl_fn_modf(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    double x = sdl_arg_f64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    double intpart = 0;
    double frac = SDL_modf(x, &intpart);
    vigil_status_t st = sdl_push_f64(vm, frac, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, intpart, error);
}

static vigil_status_t sdl_fn_load_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t datasize = 0;
    void *data = SDL_LoadFile(path, &datasize);
    if (!data)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        st = sdl_push_i64(vm, 0, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t bh = vigil_unsafe_buffer_register(data, (int32_t)datasize);
    vigil_status_t st = sdl_push_i64(vm, bh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i64(vm, (int64_t)datasize, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_save_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char path[512];
    sdl_arg_str(vm, base, 0, path, sizeof(path));
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t len = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t bsz = 0;
    void *buf = vigil_unsafe_buffer_get(bh, &bsz);
    if (buf && SDL_SaveFile(path, buf, (size_t)(len > 0 ? len : bsz)))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_clear_clipboard_data(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_ClearClipboardData())
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_has_clipboard_data(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char mime[256];
    sdl_arg_str(vm, base, 0, mime, sizeof(mime));
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_HasClipboardData(mime), error);
}

static vigil_status_t sdl_fn_get_clipboard_data(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char mime[256];
    size_t size = 0;
    void *data = NULL;
    uint8_t *copy = NULL;
    int64_t buffer_handle = -1;
    vigil_status_t st = VIGIL_STATUS_OK;

    sdl_arg_str(vm, base, 0, mime, sizeof(mime));
    vigil_vm_stack_pop_n(vm, arg_count);

    data = SDL_GetClipboardData(mime, &size);
    if (!data)
    {
        st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        st = sdl_push_i64(vm, 0, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }

    if (size > 0)
    {
        copy = (uint8_t *)malloc(size);
        if (!copy)
        {
            SDL_free(data);
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        memcpy(copy, data, size);
    }
    SDL_free(data);

    if (size == 0)
    {
        st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        st = sdl_push_i64(vm, 0, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_ok(vm, error);
    }

    buffer_handle = vigil_unsafe_buffer_register(copy, (int32_t)size);
    if (buffer_handle < 0)
    {
        free(copy);
        st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        st = sdl_push_i64(vm, 0, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many buffers", SDL_ERR_STATE, error);
    }

    st = sdl_push_i64(vm, buffer_handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i64(vm, (int64_t)size, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_set_clipboard_data(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char mime[256];
    int64_t buffer_handle = sdl_arg_i64(vm, base, 1);
    int32_t length = sdl_arg_i32(vm, base, 2);
    int32_t buffer_size = 0;
    void *source = NULL;
    sdl_clipboard_payload_t *payload = NULL;
    const char *mime_types[1];

    sdl_arg_str(vm, base, 0, mime, sizeof(mime));
    vigil_vm_stack_pop_n(vm, arg_count);

    source = vigil_unsafe_buffer_get(buffer_handle, &buffer_size);
    if (!source)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);

    if (length <= 0)
        length = buffer_size;
    if (length < 0 || length > buffer_size)
        return sdl_push_bool_sdl_err(vm, SDL_ERR_ARG, error);

    payload = (sdl_clipboard_payload_t *)calloc(1U, sizeof(*payload));
    if (!payload)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    payload->mime_type = (char *)malloc(strlen(mime) + 1U);
    payload->data = (uint8_t *)malloc((size_t)length);
    if (!payload->mime_type || (!payload->data && length > 0))
    {
        sdl_clipboard_cleanup(payload);
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memcpy(payload->mime_type, mime, strlen(mime) + 1U);
    if (length > 0)
        memcpy(payload->data, source, (size_t)length);
    payload->size = (size_t)length;

    mime_types[0] = payload->mime_type;
    if (SDL_SetClipboardData(sdl_clipboard_data_callback, sdl_clipboard_cleanup, payload, mime_types, 1U))
        return sdl_push_bool_ok(vm, error);

    sdl_clipboard_cleanup(payload);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_set_hint_with_priority(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    char val[256];
    sdl_arg_str(vm, base, 1, val, sizeof(val));
    int32_t pri = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_bool(vm, SDL_SetHintWithPriority(name, val, (SDL_HintPriority)pri), error);
}

static vigil_status_t sdl_fn_set_log_priority_prefix(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t pri = sdl_arg_i32(vm, base, 0);
    char prefix[128];
    sdl_arg_str(vm, base, 1, prefix, sizeof(prefix));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetLogPriorityPrefix((SDL_LogPriority)pri, prefix))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_set_scancode_name(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t sc = sdl_arg_i32(vm, base, 0);
    char name[128];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetScancodeName((SDL_Scancode)sc, name))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_display_for_point(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Point p = {sdl_arg_i32(vm, base, 0), sdl_arg_i32(vm, base, 1)};
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetDisplayForPoint(&p), error);
}

static vigil_status_t sdl_fn_get_display_for_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    SDL_Rect r = {sdl_arg_i32(vm, base, 0), sdl_arg_i32(vm, base, 1), sdl_arg_i32(vm, base, 2),
                  sdl_arg_i32(vm, base, 3)};
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetDisplayForRect(&r), error);
}

static vigil_status_t sdl_fn_get_date_time_locale_preferences(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_DateFormat df = 0;
    SDL_TimeFormat tf = 0;
    SDL_GetDateTimeLocalePreferences(&df, &tf);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)df, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)tf, error);
}

/* Tray extras */
static vigil_status_t sdl_fn_create_tray_submenu(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, eh);
    if (!e)
        return sdl_push_i64(vm, -1, error);
    SDL_TrayMenu *m = SDL_CreateTraySubmenu(e);
    if (!m)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(tray_menus, m, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_tray_menu(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Tray *t = (SDL_Tray *)SDL_HANDLE_GET(trays, th);
    if (!t)
        return sdl_push_i64(vm, -1, error);
    SDL_TrayMenu *m = SDL_GetTrayMenu(t);
    if (!m)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(tray_menus, m, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_tray_submenu(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, eh);
    if (!e)
        return sdl_push_i64(vm, -1, error);
    SDL_TrayMenu *m = SDL_GetTraySubmenu(e);
    if (!m)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(tray_menus, m, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_click_tray_entry(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eh = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, eh);
    if (e)
        SDL_ClickTrayEntry(e);
    return VIGIL_STATUS_OK;
}

/* Haptic effects */
static vigil_status_t sdl_fn_run_haptic_effect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t hh = sdl_arg_i64(vm, base, 0);
    int32_t eid = sdl_arg_i32(vm, base, 1);
    int32_t iters = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *h = (SDL_Haptic *)SDL_HANDLE_GET(haptics, hh);
    if (h && SDL_RunHapticEffect(h, eid, (Uint32)iters))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_stop_haptic_effect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t hh = sdl_arg_i64(vm, base, 0);
    int32_t eid = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *h = (SDL_Haptic *)SDL_HANDLE_GET(haptics, hh);
    if (h && SDL_StopHapticEffect(h, eid))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_destroy_haptic_effect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t hh = sdl_arg_i64(vm, base, 0);
    int32_t eid = sdl_arg_i32(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *h = (SDL_Haptic *)SDL_HANDLE_GET(haptics, hh);
    if (h)
        SDL_DestroyHapticEffect(h, eid);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_haptic_effect_status(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t hh = sdl_arg_i64(vm, base, 0);
    int32_t eid = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *h = (SDL_Haptic *)SDL_HANDLE_GET(haptics, hh);
    return sdl_push_bool(vm, h && SDL_GetHapticEffectStatus(h, eid), error);
}

/* Convert pixels between formats using unsafe buffers */
static vigil_status_t sdl_fn_convert_pixels(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t w = sdl_arg_i32(vm, base, 0), h = sdl_arg_i32(vm, base, 1);
    int32_t src_fmt = sdl_arg_i32(vm, base, 2);
    int64_t src_bh = sdl_arg_i64(vm, base, 3);
    int32_t src_pitch = sdl_arg_i32(vm, base, 4);
    int32_t dst_fmt = sdl_arg_i32(vm, base, 5);
    int64_t dst_bh = sdl_arg_i64(vm, base, 6);
    int32_t dst_pitch = sdl_arg_i32(vm, base, 7);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t ssz = 0, dsz = 0;
    void *src = vigil_unsafe_buffer_get(src_bh, &ssz);
    void *dst = vigil_unsafe_buffer_get(dst_bh, &dsz);
    if (src && dst &&
        SDL_ConvertPixels(w, h, (SDL_PixelFormat)src_fmt, src, src_pitch, (SDL_PixelFormat)dst_fmt, dst, dst_pitch))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Gamepad ID queries ───────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_gamepad_from_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = SDL_GetGamepadFromID((SDL_JoystickID)id);
    if (!g)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(gamepads, g, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_gamepad_from_player_index(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = SDL_GetGamepadFromPlayerIndex(idx);
    if (!g)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(gamepads, g, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_gamepad_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, gh);
    return sdl_push_i32(vm, g ? (int32_t)SDL_GetGamepadProperties(g) : 0, error);
}

static vigil_status_t sdl_fn_get_gamepad_path_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *p = SDL_GetGamepadPathForID((SDL_JoystickID)id);
    return sdl_push_string(vm, p ? p : "", error);
}

static vigil_status_t sdl_fn_get_gamepad_player_index_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, SDL_GetGamepadPlayerIndexForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_gamepad_vendor_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGamepadVendorForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_gamepad_product_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGamepadProductForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_gamepad_product_version_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGamepadProductVersionForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_real_gamepad_type_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetRealGamepadTypeForID((SDL_JoystickID)id), error);
}

static vigil_status_t sdl_fn_get_gamepad_mapping_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    char *m = SDL_GetGamepadMappingForID((SDL_JoystickID)id);
    vigil_status_t st = sdl_push_string(vm, m ? m : "", error);
    SDL_free(m);
    return st;
}

static vigil_status_t sdl_fn_set_gamepad_mapping(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    char mapping[1024];
    sdl_arg_str(vm, base, 1, mapping, sizeof(mapping));
    vigil_vm_stack_pop_n(vm, arg_count);
    if (SDL_SetGamepadMapping((SDL_JoystickID)id, mapping))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_gamepad_button_label_for_type(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t type = sdl_arg_i32(vm, base, 0);
    int32_t btn = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetGamepadButtonLabelForType((SDL_GamepadType)type, (SDL_GamepadButton)btn),
                        error);
}

static vigil_status_t sdl_fn_get_gamepad_sensor_data_rate(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    int32_t type = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, gh);
    return sdl_push_f64(vm, g ? SDL_GetGamepadSensorDataRate(g, (SDL_SensorType)type) : 0.0f, error);
}

static vigil_status_t sdl_fn_get_num_gamepad_touchpad_fingers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    int32_t tp = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, gh);
    return sdl_push_i32(vm, g ? SDL_GetNumGamepadTouchpadFingers(g, tp) : 0, error);
}

static vigil_status_t sdl_fn_get_gamepad_touchpad_finger(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    int32_t tp = sdl_arg_i32(vm, base, 1);
    int32_t finger = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, gh);
    bool down = false;
    float x = 0, y = 0, pressure = 0;
    if (g)
        SDL_GetGamepadTouchpadFinger(g, tp, finger, &down, &x, &y, &pressure);
    vigil_status_t st = sdl_push_bool(vm, down, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, y, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, pressure, error);
}

static vigil_status_t sdl_fn_get_gamepad_apple_sf_symbols_name_for_button(vigil_vm_t *vm, size_t arg_count,
                                                                          vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    int32_t btn = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, gh);
    const char *n = g ? SDL_GetGamepadAppleSFSymbolsNameForButton(g, (SDL_GamepadButton)btn) : NULL;
    return sdl_push_string(vm, n ? n : "", error);
}

static vigil_status_t sdl_fn_get_gamepad_apple_sf_symbols_name_for_axis(vigil_vm_t *vm, size_t arg_count,
                                                                        vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    int32_t axis = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, gh);
    const char *n = g ? SDL_GetGamepadAppleSFSymbolsNameForAxis(g, (SDL_GamepadAxis)axis) : NULL;
    return sdl_push_string(vm, n ? n : "", error);
}

/* ── Joystick ID queries ──────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_joystick_from_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = SDL_GetJoystickFromID((SDL_JoystickID)id);
    if (!j)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(joysticks, j, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_joystick_from_player_index(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t idx = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = SDL_GetJoystickFromPlayerIndex(idx);
    if (!j)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(joysticks, j, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_joystick_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t jh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, jh);
    return sdl_push_i32(vm, j ? (int32_t)SDL_GetJoystickProperties(j) : 0, error);
}

static vigil_status_t sdl_fn_get_joystick_guid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t jh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, jh);
    char buf[64] = {0};
    if (j)
    {
        SDL_GUID g = SDL_GetJoystickGUID(j);
        SDL_GUIDToString(g, buf, sizeof(buf));
    }
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_get_joystick_guid_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[64] = {0};
    SDL_GUID g = SDL_GetJoystickGUIDForID((SDL_JoystickID)id);
    SDL_GUIDToString(g, buf, sizeof(buf));
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_get_joystick_guid_info(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char guid_text[64];
    sdl_arg_str(vm, base, 0, guid_text, sizeof(guid_text));
    vigil_vm_stack_pop_n(vm, arg_count);

    Uint16 vendor = 0;
    Uint16 product = 0;
    Uint16 version = 0;
    Uint16 crc16 = 0;
    SDL_GUID guid = SDL_StringToGUID(guid_text);
    SDL_GetJoystickGUIDInfo(guid, &vendor, &product, &version, &crc16);

    vigil_status_t st = sdl_push_i32(vm, (int32_t)vendor, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, (int32_t)product, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, (int32_t)version, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)crc16, error);
}

static vigil_status_t sdl_fn_get_joystick_axis_initial_state(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t jh = sdl_arg_i64(vm, base, 0);
    int32_t axis = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, jh);
    Sint16 state = 0;
    bool has = j && SDL_GetJoystickAxisInitialState(j, axis, &state);
    vigil_status_t st = sdl_push_bool(vm, has, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, state, error);
}

/* ── Haptic ID queries ────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_haptic_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t hh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *h = (SDL_Haptic *)SDL_HANDLE_GET(haptics, hh);
    return sdl_push_i32(vm, h ? (int32_t)SDL_GetHapticID(h) : 0, error);
}

static vigil_status_t sdl_fn_get_haptic_from_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Haptic *h = SDL_GetHapticFromID((SDL_HapticID)id);
    if (!h)
        return sdl_push_i64(vm, -1, error);
    int64_t hh = -1;
    SDL_HANDLE_STORE(haptics, h, &hh);
    return sdl_push_i64(vm, hh, error);
}

static vigil_status_t sdl_fn_open_haptic_from_joystick(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t jh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, jh);
    if (!j)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    SDL_Haptic *h = SDL_OpenHapticFromJoystick(j);
    if (!h)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t hh = -1;
    SDL_HANDLE_STORE(haptics, h, &hh);
    vigil_status_t st = sdl_push_i64(vm, hh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* ── Sensor queries ───────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_sensor_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Sensor *s = (SDL_Sensor *)SDL_HANDLE_GET(sensors, sh);
    return sdl_push_i32(vm, s ? (int32_t)SDL_GetSensorID(s) : 0, error);
}

static vigil_status_t sdl_fn_get_sensor_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Sensor *s = (SDL_Sensor *)SDL_HANDLE_GET(sensors, sh);
    return sdl_push_i32(vm, s ? (int32_t)SDL_GetSensorProperties(s) : 0, error);
}

static vigil_status_t sdl_fn_get_sensor_from_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Sensor *s = SDL_GetSensorFromID((SDL_SensorID)id);
    if (!s)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(sensors, s, &h);
    return sdl_push_i64(vm, h, error);
}

/* ── Camera queries ───────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_camera_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Camera *c = (SDL_Camera *)SDL_HANDLE_GET(cameras, ch);
    return sdl_push_i32(vm, c ? (int32_t)SDL_GetCameraID(c) : 0, error);
}

static vigil_status_t sdl_fn_get_camera_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Camera *c = (SDL_Camera *)SDL_HANDLE_GET(cameras, ch);
    return sdl_push_i32(vm, c ? (int32_t)SDL_GetCameraProperties(c) : 0, error);
}

static vigil_status_t sdl_fn_get_camera_position(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetCameraPosition((SDL_CameraID)id), error);
}

/* ── Window/Renderer queries ──────────────────────────────────────── */

static vigil_status_t sdl_fn_get_window_from_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = SDL_GetWindowFromID((SDL_WindowID)id);
    if (!w)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(windows, w, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_window_parent(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    SDL_Window *p = w ? SDL_GetWindowParent(w) : NULL;
    if (!p)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(windows, p, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_window_icc_profile(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    size_t sz = 0;
    void *data = w ? SDL_GetWindowICCProfile(w, &sz) : NULL;
    if (!data)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_i64(vm, 0, error);
    }
    int64_t bh = vigil_unsafe_buffer_register(data, (int32_t)sz);
    vigil_status_t st = sdl_push_i64(vm, bh, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i64(vm, (int64_t)sz, error);
}

static vigil_status_t sdl_fn_get_display_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(vm, (int32_t)SDL_GetDisplayProperties((SDL_DisplayID)id), error);
}

static vigil_status_t sdl_fn_get_keyboard_focus(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = SDL_GetKeyboardFocus();
    if (!w)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(windows, w, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_keyboard_name_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *n = SDL_GetKeyboardNameForID((SDL_KeyboardID)id);
    return sdl_push_string(vm, n ? n : "", error);
}

static vigil_status_t sdl_fn_get_mouse_name_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *n = SDL_GetMouseNameForID((SDL_MouseID)id);
    return sdl_push_string(vm, n ? n : "", error);
}

static vigil_status_t sdl_fn_get_io_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ih = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_IOStream *io = (SDL_IOStream *)SDL_HANDLE_GET(io_streams, ih);
    return sdl_push_i32(vm, io ? (int32_t)SDL_GetIOProperties(io) : 0, error);
}

static vigil_status_t sdl_fn_get_gpu_device_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GPUDevice *d = (SDL_GPUDevice *)SDL_HANDLE_GET(gpu_devices, gh);
    return sdl_push_i32(vm, d ? (int32_t)SDL_GetGPUDeviceProperties(d) : 0, error);
}

static vigil_status_t sdl_fn_get_audio_stream_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ah = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, ah);
    return sdl_push_i32(vm, s ? (int32_t)SDL_GetAudioStreamProperties(s) : 0, error);
}

/* ── Audio format queries ─────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_audio_device_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t devid = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioSpec spec = {0};
    int sample_frames = 0;
    SDL_GetAudioDeviceFormat((SDL_AudioDeviceID)devid, &spec, &sample_frames);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)spec.format, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, spec.channels, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, spec.freq, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, sample_frames, error);
}

static vigil_status_t sdl_fn_get_audio_stream_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ah = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, ah);
    SDL_AudioSpec src = {0}, dst = {0};
    if (s)
        SDL_GetAudioStreamFormat(s, &src, &dst);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)src.format, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, src.channels, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, src.freq, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, (int32_t)dst.format, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, dst.channels, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, dst.freq, error);
}

static vigil_status_t sdl_fn_set_audio_stream_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ah = sdl_arg_i64(vm, base, 0);
    int32_t sf = sdl_arg_i32(vm, base, 1);
    int32_t sc = sdl_arg_i32(vm, base, 2);
    int32_t sr = sdl_arg_i32(vm, base, 3);
    int32_t df = sdl_arg_i32(vm, base, 4);
    int32_t dc = sdl_arg_i32(vm, base, 5);
    int32_t dr = sdl_arg_i32(vm, base, 6);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioStream *s = (SDL_AudioStream *)SDL_HANDLE_GET(audio_streams, ah);
    SDL_AudioSpec src = {(SDL_AudioFormat)sf, sc, sr};
    SDL_AudioSpec dst = {(SDL_AudioFormat)df, dc, dr};
    if (s && SDL_SetAudioStreamFormat(s, &src, &dst))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Misc remaining ───────────────────────────────────────────────── */

static vigil_status_t sdl_fn_guid_to_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char in[64];
    sdl_arg_str(vm, base, 0, in, sizeof(in));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GUID g = SDL_StringToGUID(in);
    char out[64] = {0};
    SDL_GUIDToString(g, out, sizeof(out));
    return sdl_push_string(vm, out, error);
}

static vigil_status_t sdl_fn_string_to_guid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char in[64];
    sdl_arg_str(vm, base, 0, in, sizeof(in));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GUID g = SDL_StringToGUID(in);
    char out[64] = {0};
    SDL_GUIDToString(g, out, sizeof(out));
    return sdl_push_string(vm, out, error);
}

static vigil_status_t sdl_fn_modff(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    float x = (float)sdl_arg_f64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    float intpart = 0;
    float frac = SDL_modff(x, &intpart);
    vigil_status_t st = sdl_push_f64(vm, frac, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, intpart, error);
}

static vigil_status_t sdl_fn_get_masks_for_pixel_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    int bpp = 0;
    Uint32 rm = 0, gm = 0, bm = 0, am = 0;
    SDL_GetMasksForPixelFormat((SDL_PixelFormat)fmt, &bpp, &rm, &gm, &bm, &am);
    vigil_status_t st = sdl_push_i32(vm, bpp, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, (int32_t)rm, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, (int32_t)gm, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, (int32_t)bm, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)am, error);
}

static vigil_status_t sdl_fn_surface_has_alternate_images(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    return sdl_push_bool(vm, s && SDL_SurfaceHasAlternateImages(s), error);
}

static vigil_status_t sdl_fn_remove_surface_alternate_images(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_arg_i64(vm, base, 0);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    if (s)
        SDL_RemoveSurfaceAlternateImages(s);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_cleanup_tls(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_CleanupTLS();
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_set_environment_variable(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    char val[512];
    sdl_arg_str(vm, base, 1, val, sizeof(val));
    int32_t overwrite = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = SDL_GetEnvironment();
    if (env && SDL_SetEnvironmentVariable(env, name, val, overwrite != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_unset_environment_variable(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = SDL_GetEnvironment();
    if (env && SDL_UnsetEnvironmentVariable(env, name))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_environment_variable(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char name[256];
    sdl_arg_str(vm, base, 0, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = SDL_GetEnvironment();
    const char *v = env ? SDL_GetEnvironmentVariable(env, name) : NULL;
    return sdl_push_string(vm, v ? v : "", error);
}

static vigil_status_t sdl_fn_create_environment(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t populated = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = SDL_CreateEnvironment(populated != 0);
    int64_t handle = -1;
    vigil_status_t st;

    if (!env)
    {
        st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }

    if (SDL_HANDLE_STORE(environments, env, &handle) < 0)
    {
        SDL_DestroyEnvironment(env);
        st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_err(vm, "too many environments", SDL_ERR_STATE, error);
    }

    st = sdl_push_i64(vm, handle, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

static vigil_status_t sdl_fn_destroy_environment(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = (SDL_Environment *)SDL_HANDLE_GET(environments, handle);
    if (env)
    {
        SDL_DestroyEnvironment(env);
        SDL_HANDLE_CLEAR(environments, handle);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_environment_variables(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = (SDL_Environment *)SDL_HANDLE_GET(environments, handle);
    char **items = env ? SDL_GetEnvironmentVariables(env) : NULL;
    vigil_status_t st;
    int count = 0;

    if (!items)
        return sdl_push_string(vm, "", error);

    while (items[count] != NULL)
        count++;
    st = sdl_push_joined_lines(vm, items, count, error);
    SDL_free(items);
    return st;
}

static vigil_status_t sdl_fn_get_environment_variable_from(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = sdl_arg_i64(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = (SDL_Environment *)SDL_HANDLE_GET(environments, handle);
    const char *value = env ? SDL_GetEnvironmentVariable(env, name) : NULL;
    return sdl_push_string(vm, value ? value : "", error);
}

static vigil_status_t sdl_fn_set_environment_variable_in(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = sdl_arg_i64(vm, base, 0);
    char name[256];
    char value[512];
    int32_t overwrite = sdl_arg_i32(vm, base, 3);
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    sdl_arg_str(vm, base, 2, value, sizeof(value));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = (SDL_Environment *)SDL_HANDLE_GET(environments, handle);
    if (env && SDL_SetEnvironmentVariable(env, name, value, overwrite != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_unset_environment_variable_in(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = sdl_arg_i64(vm, base, 0);
    char name[256];
    sdl_arg_str(vm, base, 1, name, sizeof(name));
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Environment *env = (SDL_Environment *)SDL_HANDLE_GET(environments, handle);
    if (env && SDL_UnsetEnvironmentVariable(env, name))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_gl_get_current_window(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = SDL_GL_GetCurrentWindow();
    if (!w)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(windows, w, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_gl_get_current_context(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    if (!ctx)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(gl_contexts, ctx, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_metal_get_layer(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t v = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *layer = v ? SDL_Metal_GetLayer((SDL_MetalView)(uintptr_t)v) : NULL;
    return sdl_push_i64(vm, (int64_t)(uintptr_t)layer, error);
}

static vigil_status_t sdl_fn_get_process_properties(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ph = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Process *p = (SDL_Process *)SDL_HANDLE_GET(processes, ph);
    return sdl_push_i32(vm, p ? (int32_t)SDL_GetProcessProperties(p) : 0, error);
}

static vigil_status_t sdl_fn_get_clipboard_mime_types(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t count = 0;
    char **types = SDL_GetClipboardMimeTypes(&count);
    char *joined = NULL;
    size_t joined_len = 0;
    size_t joined_cap = 0;
    vigil_status_t st = VIGIL_STATUS_OK;

    if (!types || count == 0)
        return sdl_push_string(vm, "", error);

    for (size_t i = 0; i < count; i++)
    {
        const char *type = types[i] ? types[i] : "";
        if (i > 0 && sdl_append_bytes(&joined, &joined_len, &joined_cap, ",", 1U) != 0)
            goto oom;
        if (sdl_append_cstr(&joined, &joined_len, &joined_cap, type) != 0)
            goto oom;
    }

    st = sdl_push_string(vm, joined ? joined : "", error);
    SDL_free(types);
    free(joined);
    return st;

oom:
    SDL_free(types);
    free(joined);
    vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
    return VIGIL_STATUS_OUT_OF_MEMORY;
}

static vigil_status_t sdl_fn_set_tray_icon(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_arg_i64(vm, base, 0);
    int64_t sh = sdl_arg_i64(vm, base, 1);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Tray *t = (SDL_Tray *)SDL_HANDLE_GET(trays, th);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    if (t)
        SDL_SetTrayIcon(t, s);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_tray_entry_parent(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t eh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayEntry *e = (SDL_TrayEntry *)SDL_HANDLE_GET(tray_entries, eh);
    if (!e)
        return sdl_push_i64(vm, -1, error);
    SDL_TrayMenu *m = SDL_GetTrayEntryParent(e);
    if (!m)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(tray_menus, m, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_tray_menu_parent_entry(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t mh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayMenu *m = (SDL_TrayMenu *)SDL_HANDLE_GET(tray_menus, mh);
    if (!m)
        return sdl_push_i64(vm, -1, error);
    SDL_TrayEntry *e = SDL_GetTrayMenuParentEntry(m);
    if (!e)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(tray_entries, e, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_tray_menu_parent_tray(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t mh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_TrayMenu *m = (SDL_TrayMenu *)SDL_HANDLE_GET(tray_menus, mh);
    if (!m)
        return sdl_push_i64(vm, -1, error);
    SDL_Tray *t = SDL_GetTrayMenuParentTray(m);
    if (!t)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(trays, t, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_set_surface_palette(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_arg_i64(vm, base, 0);
    int64_t ph = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    SDL_Palette *p = (SDL_Palette *)SDL_HANDLE_GET(palettes, ph);
    if (s && p && SDL_SetSurfacePalette(s, p))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_set_texture_palette(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t th = sdl_arg_i64(vm, base, 0);
    int64_t ph = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Texture *t = (SDL_Texture *)SDL_HANDLE_GET(textures, th);
    SDL_Palette *p = (SDL_Palette *)SDL_HANDLE_GET(palettes, ph);
    if (t && p && SDL_SetTexturePalette(t, p))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_create_surface_palette(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *s = (SDL_Surface *)SDL_HANDLE_GET(surfaces, sh);
    SDL_Palette *p = s ? SDL_CreateSurfacePalette(s) : NULL;
    if (!p)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(palettes, p, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_gamepad_joystick(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, gh);
    SDL_Joystick *j = g ? SDL_GetGamepadJoystick(g) : NULL;
    if (!j)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(joysticks, j, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_gamepad_guid_for_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t id = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[64] = {0};
    SDL_GUID g = SDL_GetGamepadGUIDForID((SDL_JoystickID)id);
    SDL_GUIDToString(g, buf, sizeof(buf));
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_get_gamepad_mapping_for_guid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char guid_text[64];
    sdl_arg_str(vm, base, 0, guid_text, sizeof(guid_text));
    vigil_vm_stack_pop_n(vm, arg_count);

    SDL_GUID guid = SDL_StringToGUID(guid_text);
    char *mapping = SDL_GetGamepadMappingForGUID(guid);
    vigil_status_t st = sdl_push_string(vm, mapping ? mapping : "", error);
    SDL_free(mapping);
    return st;
}

static vigil_status_t sdl_fn_get_gamepad_mappings(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    int count = 0;
    char **mappings = SDL_GetGamepadMappings(&count);
    if (!mappings)
        return sdl_push_string(vm, "", error);
    if (count <= 0)
    {
        SDL_free(mappings);
        return sdl_push_string(vm, "", error);
    }
    vigil_status_t st = sdl_push_joined_lines(vm, mappings, count, error);
    SDL_free(mappings);
    return st;
}

static vigil_status_t sdl_gamepad_get_bindings(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = sdl_field_i64(vm, base, GP_HANDLE);
    SDL_Gamepad *gamepad = NULL;
    SDL_GamepadBinding **bindings = NULL;
    char *joined = NULL;
    size_t joined_len = 0;
    size_t joined_cap = 0;
    vigil_status_t st = VIGIL_STATUS_OK;
    int count = 0;

    vigil_vm_stack_pop_n(vm, arg_count);
    gamepad = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, handle);
    if (!gamepad)
        return sdl_push_string(vm, "", error);

    bindings = SDL_GetGamepadBindings(gamepad, &count);
    if (!bindings || count <= 0)
        return sdl_push_string(vm, "", error);

    for (int i = 0; i < count; i++)
    {
        const SDL_GamepadBinding *binding = bindings[i];
        if (!binding)
            continue;

        if (joined_len > 0 && sdl_append_bytes(&joined, &joined_len, &joined_cap, "\n", 1U) != 0)
            goto oom;

        switch (binding->input_type)
        {
        case SDL_GAMEPAD_BINDTYPE_BUTTON:
            if (sdl_append_format(&joined, &joined_len, &joined_cap, "input=button:%d ", binding->input.button) != 0)
                goto oom;
            break;
        case SDL_GAMEPAD_BINDTYPE_AXIS:
            if (sdl_append_format(&joined, &joined_len, &joined_cap, "input=axis:%d[%d:%d] ", binding->input.axis.axis,
                                  binding->input.axis.axis_min, binding->input.axis.axis_max) != 0)
                goto oom;
            break;
        case SDL_GAMEPAD_BINDTYPE_HAT:
            if (sdl_append_format(&joined, &joined_len, &joined_cap, "input=hat:%d:%d ", binding->input.hat.hat,
                                  binding->input.hat.hat_mask) != 0)
                goto oom;
            break;
        default:
            if (sdl_append_cstr(&joined, &joined_len, &joined_cap, "input=none ") != 0)
                goto oom;
            break;
        }

        switch (binding->output_type)
        {
        case SDL_GAMEPAD_BINDTYPE_BUTTON: {
            const char *button_name = SDL_GetGamepadStringForButton(binding->output.button);
            if (sdl_append_format(&joined, &joined_len, &joined_cap, "output=button:%s",
                                  button_name ? button_name : "unknown") != 0)
                goto oom;
            break;
        }
        case SDL_GAMEPAD_BINDTYPE_AXIS: {
            const char *axis_name = SDL_GetGamepadStringForAxis(binding->output.axis.axis);
            if (sdl_append_format(&joined, &joined_len, &joined_cap, "output=axis:%s[%d:%d]",
                                  axis_name ? axis_name : "unknown", binding->output.axis.axis_min,
                                  binding->output.axis.axis_max) != 0)
                goto oom;
            break;
        }
        default:
            if (sdl_append_cstr(&joined, &joined_len, &joined_cap, "output=none") != 0)
                goto oom;
            break;
        }
    }

    st = sdl_push_string(vm, joined ? joined : "", error);
    SDL_free(bindings);
    free(joined);
    return st;

oom:
    SDL_free(bindings);
    free(joined);
    vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
    return VIGIL_STATUS_OUT_OF_MEMORY;
}

static vigil_status_t sdl_gamepad_get_guid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = sdl_field_i64(vm, base, GP_HANDLE);
    SDL_Gamepad *gamepad = NULL;
    SDL_JoystickID id = 0;
    char buf[64] = {0};

    vigil_vm_stack_pop_n(vm, arg_count);
    gamepad = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, handle);
    if (!gamepad)
        return sdl_push_string(vm, "", error);

    id = SDL_GetGamepadID(gamepad);
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), buf, sizeof(buf));
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_calculate_gpu_texture_format_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    int32_t w = sdl_arg_i32(vm, base, 1);
    int32_t h = sdl_arg_i32(vm, base, 2);
    int32_t d = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    return sdl_push_i32(
        vm, (int32_t)SDL_CalculateGPUTextureFormatSize((SDL_GPUTextureFormat)fmt, (Uint32)w, (Uint32)h, (Uint32)d),
        error);
}

/* ── CreateAudioStream (flattened specs) ──────────────────────────── */

static vigil_status_t sdl_fn_create_audio_stream(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t sf = sdl_arg_i32(vm, base, 0), sc = sdl_arg_i32(vm, base, 1), sr = sdl_arg_i32(vm, base, 2);
    int32_t df = sdl_arg_i32(vm, base, 3), dc = sdl_arg_i32(vm, base, 4), dr = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_AudioSpec src = {(SDL_AudioFormat)sf, sc, sr}, dst = {(SDL_AudioFormat)df, dc, dr};
    SDL_AudioStream *s = SDL_CreateAudioStream(&src, &dst);
    if (!s)
    {
        vigil_status_t st = sdl_push_i64(vm, -1, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_sdl_err(vm, SDL_ERR_IO, error);
    }
    int64_t h = -1;
    SDL_HANDLE_STORE(audio_streams, s, &h);
    vigil_status_t st = sdl_push_i64(vm, h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_ok(vm, error);
}

/* ── MixAudio (unsafe buffers) ────────────────────────────────────── */

static vigil_status_t sdl_fn_mix_audio(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dst_bh = sdl_arg_i64(vm, base, 0), src_bh = sdl_arg_i64(vm, base, 1);
    int32_t fmt = sdl_arg_i32(vm, base, 2);
    int32_t len = sdl_arg_i32(vm, base, 3);
    double vol = sdl_arg_f64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t dsz = 0, ssz = 0;
    void *dst = vigil_unsafe_buffer_get(dst_bh, &dsz);
    void *src = vigil_unsafe_buffer_get(src_bh, &ssz);
    if (dst && src && SDL_MixAudio((Uint8 *)dst, (const Uint8 *)src, (SDL_AudioFormat)fmt, (Uint32)len, (float)vol))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── PremultiplyAlpha ─────────────────────────────────────────────── */

static vigil_status_t sdl_fn_premultiply_alpha(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t w = sdl_arg_i32(vm, base, 0), h = sdl_arg_i32(vm, base, 1);
    int32_t sf = sdl_arg_i32(vm, base, 2);
    int64_t sbh = sdl_arg_i64(vm, base, 3);
    int32_t sp = sdl_arg_i32(vm, base, 4);
    int32_t df = sdl_arg_i32(vm, base, 5);
    int64_t dbh = sdl_arg_i64(vm, base, 6);
    int32_t dp = sdl_arg_i32(vm, base, 7);
    int32_t linear = sdl_arg_i32(vm, base, 8);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t ssz = 0, dsz = 0;
    void *src = vigil_unsafe_buffer_get(sbh, &ssz);
    void *dst = vigil_unsafe_buffer_get(dbh, &dsz);
    if (src && dst &&
        SDL_PremultiplyAlpha(w, h, (SDL_PixelFormat)sf, src, sp, (SDL_PixelFormat)df, dst, dp, linear != 0))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── MapRGB / MapRGBA / GetRGB / GetRGBA ──────────────────────────── */

static vigil_status_t sdl_fn_map_rgb(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    int32_t r = sdl_arg_i32(vm, base, 1);
    int32_t g = sdl_arg_i32(vm, base, 2);
    int32_t b = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    const SDL_PixelFormatDetails *d = SDL_GetPixelFormatDetails((SDL_PixelFormat)fmt);
    return sdl_push_i32(vm, d ? (int32_t)SDL_MapRGB(d, NULL, (Uint8)r, (Uint8)g, (Uint8)b) : 0, error);
}

static vigil_status_t sdl_fn_map_rgba(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t fmt = sdl_arg_i32(vm, base, 0);
    int32_t r = sdl_arg_i32(vm, base, 1);
    int32_t g = sdl_arg_i32(vm, base, 2);
    int32_t b = sdl_arg_i32(vm, base, 3);
    int32_t a = sdl_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    const SDL_PixelFormatDetails *d = SDL_GetPixelFormatDetails((SDL_PixelFormat)fmt);
    return sdl_push_i32(vm, d ? (int32_t)SDL_MapRGBA(d, NULL, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a) : 0, error);
}

static vigil_status_t sdl_fn_get_rgb(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t pixel = sdl_arg_i32(vm, base, 0);
    int32_t fmt = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint8 r = 0, g = 0, b = 0;
    const SDL_PixelFormatDetails *d = SDL_GetPixelFormatDetails((SDL_PixelFormat)fmt);
    if (d)
        SDL_GetRGB((Uint32)pixel, d, NULL, &r, &g, &b);
    vigil_status_t st = sdl_push_i32(vm, r, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, g, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, b, error);
}

static vigil_status_t sdl_fn_get_rgba(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t pixel = sdl_arg_i32(vm, base, 0);
    int32_t fmt = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    const SDL_PixelFormatDetails *d = SDL_GetPixelFormatDetails((SDL_PixelFormat)fmt);
    if (d)
        SDL_GetRGBA((Uint32)pixel, d, NULL, &r, &g, &b, &a);
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

/* ── Sensor/Gamepad sensor data ───────────────────────────────────── */

static vigil_status_t sdl_fn_get_sensor_data(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t sh = sdl_arg_i64(vm, base, 0);
    int32_t num = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Sensor *s = (SDL_Sensor *)SDL_HANDLE_GET(sensors, sh);
    float data[16] = {0};
    if (num > 16)
        num = 16;
    if (s)
        SDL_GetSensorData(s, data, num);
    vigil_status_t st;
    for (int i = 0; i < num; i++)
    {
        st = sdl_push_f64(vm, data[i], error);
        if (st != VIGIL_STATUS_OK)
            return st;
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_get_gamepad_sensor_data(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t gh = sdl_arg_i64(vm, base, 0);
    int32_t type = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Gamepad *g = (SDL_Gamepad *)SDL_HANDLE_GET(gamepads, gh);
    float data[3] = {0};
    if (g)
        SDL_GetGamepadSensorData(g, (SDL_SensorType)type, data, 3);
    vigil_status_t st = sdl_push_f64(vm, data[0], error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_f64(vm, data[1], error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_f64(vm, data[2], error);
}

/* ── Text input area ──────────────────────────────────────────────── */

static vigil_status_t sdl_fn_set_text_input_area(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    int32_t x = sdl_arg_i32(vm, base, 1);
    int32_t y = sdl_arg_i32(vm, base, 2);
    int32_t w = sdl_arg_i32(vm, base, 3);
    int32_t h = sdl_arg_i32(vm, base, 4);
    int32_t cursor = sdl_arg_i32(vm, base, 5);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    SDL_Rect r = {x, y, w, h};
    if (win && SDL_SetTextInputArea(win, &r, cursor))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_get_text_input_area(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *win = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    SDL_Rect r = {0};
    int cursor = 0;
    if (win)
        SDL_GetTextInputArea(win, &r, &cursor);
    vigil_status_t st = sdl_push_i32(vm, r.x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, r.y, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, r.w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, r.h, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, cursor, error);
}

/* ── Joystick virtual input ───────────────────────────────────────── */

static vigil_status_t sdl_fn_set_joystick_virtual_ball(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t jh = sdl_arg_i64(vm, base, 0);
    int32_t ball = sdl_arg_i32(vm, base, 1);
    int32_t xrel = sdl_arg_i32(vm, base, 2);
    int32_t yrel = sdl_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, jh);
    if (j && SDL_SetJoystickVirtualBall(j, ball, (Sint16)xrel, (Sint16)yrel))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_set_joystick_virtual_touchpad(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t jh = sdl_arg_i64(vm, base, 0);
    int32_t tp = sdl_arg_i32(vm, base, 1);
    int32_t finger = sdl_arg_i32(vm, base, 2);
    int32_t down = sdl_arg_i32(vm, base, 3);
    double x = sdl_arg_f64(vm, base, 4);
    double y = sdl_arg_f64(vm, base, 5);
    double pressure = sdl_arg_f64(vm, base, 6);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Joystick *j = (SDL_Joystick *)SDL_HANDLE_GET(joysticks, jh);
    if (j && SDL_SetJoystickVirtualTouchpad(j, tp, finger, down != 0, (float)x, (float)y, (float)pressure))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── Window surface ───────────────────────────────────────────────── */

static vigil_status_t sdl_fn_get_window_surface(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    SDL_Surface *s = w ? SDL_GetWindowSurface(w) : NULL;
    if (!s)
        return sdl_push_i64(vm, -1, error);
    int64_t h = -1;
    SDL_HANDLE_STORE(surfaces, s, &h);
    return sdl_push_i64(vm, h, error);
}

static vigil_status_t sdl_fn_get_window_mouse_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t wh = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Window *w = (SDL_Window *)SDL_HANDLE_GET(windows, wh);
    const SDL_Rect *r = w ? SDL_GetWindowMouseRect(w) : NULL;
    if (!r)
    {
        vigil_status_t st = sdl_push_i32(vm, 0, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        st = sdl_push_i32(vm, 0, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        st = sdl_push_i32(vm, 0, error);
        if (st != VIGIL_STATUS_OK)
            return st;
        return sdl_push_i32(vm, 0, error);
    }
    vigil_status_t st = sdl_push_i32(vm, r->x, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, r->y, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = sdl_push_i32(vm, r->w, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, r->h, error);
}

/* ── Surface blits ────────────────────────────────────────────────── */

static vigil_status_t sdl_fn_blit_surface_unchecked_scaled(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t src_h = sdl_arg_i64(vm, base, 0);
    int32_t sx = sdl_arg_i32(vm, base, 1), sy = sdl_arg_i32(vm, base, 2), sw = sdl_arg_i32(vm, base, 3),
            sh = sdl_arg_i32(vm, base, 4);
    int64_t dst_h = sdl_arg_i64(vm, base, 5);
    int32_t dx = sdl_arg_i32(vm, base, 6), dy = sdl_arg_i32(vm, base, 7), dw = sdl_arg_i32(vm, base, 8),
            dh = sdl_arg_i32(vm, base, 9);
    int32_t mode = sdl_arg_i32(vm, base, 10);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, src_h);
    SDL_Surface *dst = (SDL_Surface *)SDL_HANDLE_GET(surfaces, dst_h);
    SDL_Rect sr = {sx, sy, sw, sh}, dr = {dx, dy, dw, dh};
    if (src && dst && SDL_BlitSurfaceUncheckedScaled(src, &sr, dst, &dr, (SDL_ScaleMode)mode))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_blit_surface_tiled_with_scale(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t src_h = sdl_arg_i64(vm, base, 0);
    int32_t sx = sdl_arg_i32(vm, base, 1), sy = sdl_arg_i32(vm, base, 2), sw = sdl_arg_i32(vm, base, 3),
            sh = sdl_arg_i32(vm, base, 4);
    double scale = sdl_arg_f64(vm, base, 5);
    int32_t mode = sdl_arg_i32(vm, base, 6);
    int64_t dst_h = sdl_arg_i64(vm, base, 7);
    int32_t dx = sdl_arg_i32(vm, base, 8), dy = sdl_arg_i32(vm, base, 9), dw = sdl_arg_i32(vm, base, 10),
            dh = sdl_arg_i32(vm, base, 11);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, src_h);
    SDL_Surface *dst = (SDL_Surface *)SDL_HANDLE_GET(surfaces, dst_h);
    SDL_Rect sr = {sx, sy, sw, sh}, dr = {dx, dy, dw, dh};
    if (src && dst && SDL_BlitSurfaceTiledWithScale(src, &sr, (float)scale, (SDL_ScaleMode)mode, dst, &dr))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

static vigil_status_t sdl_fn_blit_surface_9grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t src_h = sdl_arg_i64(vm, base, 0);
    int32_t sx = sdl_arg_i32(vm, base, 1), sy = sdl_arg_i32(vm, base, 2), sw = sdl_arg_i32(vm, base, 3),
            sh = sdl_arg_i32(vm, base, 4);
    int32_t lw = sdl_arg_i32(vm, base, 5), rw = sdl_arg_i32(vm, base, 6), th = sdl_arg_i32(vm, base, 7),
            bh = sdl_arg_i32(vm, base, 8);
    double scale = sdl_arg_f64(vm, base, 9);
    int32_t mode = sdl_arg_i32(vm, base, 10);
    int64_t dst_h = sdl_arg_i64(vm, base, 11);
    int32_t dx = sdl_arg_i32(vm, base, 12), dy = sdl_arg_i32(vm, base, 13), dw = sdl_arg_i32(vm, base, 14),
            dh = sdl_arg_i32(vm, base, 15);
    vigil_vm_stack_pop_n(vm, arg_count);
    SDL_Surface *src = (SDL_Surface *)SDL_HANDLE_GET(surfaces, src_h);
    SDL_Surface *dst = (SDL_Surface *)SDL_HANDLE_GET(surfaces, dst_h);
    SDL_Rect sr = {sx, sy, sw, sh}, dr = {dx, dy, dw, dh};
    if (src && dst && SDL_BlitSurface9Grid(src, &sr, lw, rw, th, bh, (float)scale, (SDL_ScaleMode)mode, dst, &dr))
        return sdl_push_bool_ok(vm, error);
    return sdl_push_bool_sdl_err(vm, SDL_ERR_IO, error);
}

/* ── C stdlib wrappers ────────────────────────────────────────────── */

/* Buffer ops via unsafe buffers */
static vigil_status_t sdl_fn_memcpy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t sh = sdl_arg_i64(vm, base, 1);
    int32_t len = sdl_arg_i32(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t dsz = 0, ssz = 0;
    void *d = vigil_unsafe_buffer_get(dh, &dsz);
    void *s = vigil_unsafe_buffer_get(sh, &ssz);
    if (d && s && len <= dsz && len <= ssz)
        SDL_memcpy(d, s, (size_t)len);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_memmove(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int64_t sh = sdl_arg_i64(vm, base, 1);
    int32_t len = sdl_arg_i32(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t dsz = 0, ssz = 0;
    void *d = vigil_unsafe_buffer_get(dh, &dsz);
    void *s = vigil_unsafe_buffer_get(sh, &ssz);
    if (d && s && len <= dsz && len <= ssz)
        SDL_memmove(d, s, (size_t)len);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_memset(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t c = sdl_arg_i32(vm, base, 1);
    int32_t len = sdl_arg_i32(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t dsz = 0;
    void *d = vigil_unsafe_buffer_get(dh, &dsz);
    if (d && len <= dsz)
        SDL_memset(d, c, (size_t)len);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_memset4(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t dh = sdl_arg_i64(vm, base, 0);
    int32_t val = sdl_arg_i32(vm, base, 1);
    int32_t dwords = sdl_arg_i32(vm, base, 2);
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t dsz = 0;
    void *d = vigil_unsafe_buffer_get(dh, &dsz);
    if (d && (int32_t)(dwords * 4) <= dsz)
        SDL_memset4(d, (Uint32)val, (size_t)dwords);
    return VIGIL_STATUS_OK;
}

static vigil_status_t sdl_fn_memcmp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ah = sdl_arg_i64(vm, base, 0);
    int64_t bh = sdl_arg_i64(vm, base, 1);
    int32_t len = sdl_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    int32_t asz = 0, bsz = 0;
    void *a = vigil_unsafe_buffer_get(ah, &asz);
    void *b = vigil_unsafe_buffer_get(bh, &bsz);
    if (a && b && len <= asz && len <= bsz)
        return sdl_push_i32(vm, SDL_memcmp(a, b, (size_t)len), error);
    return sdl_push_i32(vm, -1, error);
}

/* UTF-8 */
static vigil_status_t sdl_fn_ucs4_to_utf8(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t cp = sdl_arg_i32(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[8] = {0};
    SDL_UCS4ToUTF8((Uint32)cp, buf);
    return sdl_push_string(vm, buf, error);
}

static vigil_status_t sdl_fn_step_utf8(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char str[512];
    sdl_arg_str(vm, base, 0, str, sizeof(str));
    int32_t pos = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    const char *p = str + pos;
    size_t slen = strlen(str) - (size_t)pos;
    Uint32 cp = SDL_StepUTF8(&p, &slen);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)cp, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i32(vm, (int32_t)(p - str), error);
}

/* iconv_string */
static vigil_status_t sdl_fn_iconv_string(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char tocode[64];
    sdl_arg_str(vm, base, 0, tocode, sizeof(tocode));
    char fromcode[64];
    sdl_arg_str(vm, base, 1, fromcode, sizeof(fromcode));
    char inbuf[1024];
    sdl_arg_str(vm, base, 2, inbuf, sizeof(inbuf));
    vigil_vm_stack_pop_n(vm, arg_count);
    char *out = SDL_iconv_string(tocode, fromcode, inbuf, strlen(inbuf) + 1);
    vigil_status_t st = sdl_push_string(vm, out ? out : "", error);
    SDL_free(out);
    return st;
}

/* Seeded random */
static vigil_status_t sdl_fn_rand_r(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t state = sdl_arg_i64(vm, base, 0);
    int32_t n = sdl_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint64 s = (Uint64)state;
    Sint32 r = SDL_rand_r(&s, n);
    vigil_status_t st = sdl_push_i32(vm, r, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i64(vm, (int64_t)s, error);
}

static vigil_status_t sdl_fn_randf_r(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t state = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint64 s = (Uint64)state;
    float r = SDL_randf_r(&s);
    vigil_status_t st = sdl_push_f64(vm, r, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i64(vm, (int64_t)s, error);
}

static vigil_status_t sdl_fn_rand_bits_r(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t state = sdl_arg_i64(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    Uint64 s = (Uint64)state;
    Uint32 r = SDL_rand_bits_r(&s);
    vigil_status_t st = sdl_push_i32(vm, (int32_t)r, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_i64(vm, (int64_t)s, error);
}

/* Overflow checks */
static vigil_status_t sdl_fn_size_add_check_overflow(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t a = sdl_arg_i64(vm, base, 0);
    int64_t b = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t result = 0;
    bool ok = SDL_size_add_check_overflow((size_t)a, (size_t)b, &result);
    vigil_status_t st = sdl_push_i64(vm, (int64_t)result, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_bool(vm, ok, error);
}

static vigil_status_t sdl_fn_size_mul_check_overflow(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t a = sdl_arg_i64(vm, base, 0);
    int64_t b = sdl_arg_i64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t result = 0;
    bool ok = SDL_size_mul_check_overflow((size_t)a, (size_t)b, &result);
    vigil_status_t st = sdl_push_i64(vm, (int64_t)result, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return sdl_push_bool(vm, ok, error);
}
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
#define SDL_FN_BOOL_ERR(n, nl, fn, pc, pt)                                                                            \
    {                                                                                                                 \
        n, nl, fn, pc, pt, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL             \
    }

/* Function returning a single value. */
#define SDL_FN(n, nl, fn, pc, pt, rt)                                                                                 \
    {                                                                                                                 \
        n, nl, fn, pc, pt, rt, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL                                  \
    }

/* Void function. */
#define SDL_FN_VOID(n, nl, fn, pc, pt)                                                                                \
    {                                                                                                                 \
        n, nl, fn, pc, pt, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL                    \
    }

/* clang-format on */

static const char *const sdl_properties_param_names[] = {"properties"};
static const char *const sdl_guid_param_names[] = {"guid"};
static const char *const sdl_environment_handle_param_names[] = {"environment"};
static const char *const sdl_environment_name_param_names[] = {"environment", "name"};
static const char *const sdl_environment_set_param_names[] = {"environment", "name", "value", "overwrite"};
static const char *const sdl_environment_create_param_names[] = {"populated"};
static const char *const sdl_message_box_param_names[] = {"flags", "title", "message", "buttons"};
static const char *const sdl_window_rect_param_names[] = {"x", "y", "w", "h"};
static const char *const sdl_window_rects_param_names[] = {"rects", "count"};
static const char *const sdl_clipboard_mime_param_names[] = {"mime_type"};
static const char *const sdl_clipboard_set_param_names[] = {"mime_type", "buffer", "size"};
static const char *const sdl_camera_index_param_names[] = {"camera_index"};
static const vigil_native_symbol_doc_t sdl_doc_create_window_with_properties = {
    "Create a window from an SDL property bag.",
    "Pass a properties ID configured with SDL window creation keys such as title, size, and flags.",
    "i32 props = sdl.create_properties()\n"
    "bool ok, err e = sdl.set_string_property(props, \"SDL.window.create.title\", \"Demo\")",
};

static const vigil_native_symbol_doc_t sdl_doc_create_renderer_with_properties = {
    "Create a renderer from an SDL property bag.",
    "Pass a properties ID configured with SDL renderer creation keys such as the target window and driver name.",
    NULL,
};

static const vigil_native_symbol_doc_t sdl_doc_get_gamepad_mapping_for_guid = {
    "Return the SDL mapping string for a gamepad GUID.",
    "Use a GUID string returned by SDL helper functions such as get_gamepad_guid_for_id or get_joystick_guid_for_id.",
    "string mapping = sdl.get_gamepad_mapping_for_guid(guid)",
};

static const vigil_native_symbol_doc_t sdl_doc_get_gamepad_mappings = {
    "Return all known gamepad mappings as a newline-separated string.",
    "Each line is one SDL gamepad mapping string. Empty output means no mappings are currently registered.",
    "string mappings = sdl.get_gamepad_mappings()",
};

static const vigil_native_symbol_doc_t sdl_doc_get_joystick_guid_info = {
    "Decode vendor, product, version, and CRC16 values from a joystick GUID string.",
    "Returns four i32 values in order: vendor, product, version, crc16.",
    "i32 vendor, i32 product, i32 version, i32 crc = sdl.get_joystick_guid_info(guid)",
};

static const vigil_native_symbol_doc_t sdl_doc_create_environment = {
    "Create an SDL environment handle for isolated variable editing.",
    "Pass 1 to clone the current process environment or 0 to start with an empty environment.",
    "i64 env, err e = sdl.create_environment(1)",
};

static const vigil_native_symbol_doc_t sdl_doc_get_environment_variables = {
    "List environment variables from an SDL environment handle.",
    "Returns newline-separated NAME=value pairs.",
    "string vars = sdl.get_environment_variables(env)",
};

static const vigil_native_symbol_doc_t sdl_doc_get_environment_variable_from = {
    "Read one variable from an SDL environment handle.",
    "Returns an empty string when the variable is missing.",
    "string home = sdl.get_environment_variable_from(env, \"HOME\")",
};

static const vigil_native_symbol_doc_t sdl_doc_set_environment_variable_in = {
    "Set one variable in an SDL environment handle.",
    "Set overwrite to 0 to keep an existing value unchanged.",
    "bool ok, err e = sdl.set_environment_variable_in(env, \"DEMO\", \"1\", 1)",
};

static const vigil_native_symbol_doc_t sdl_doc_show_message_box = {
    "Show a message box with custom buttons and return the selected button index.",
    "Pass button labels as a newline-separated string such as "
    "\"Yes\\nNo\\nCancel\". The return value is the zero-based button index.",
    "i32 button, err e = sdl.show_message_box(sdl.MESSAGEBOX_INFORMATION(), \"Question\", \"Continue?\", \"Yes\\nNo\")",
};

static const vigil_native_symbol_doc_t sdl_doc_window_get_fullscreen_mode = {
    "Return the exclusive fullscreen mode size for the window.",
    "Returns 0, 0 when the window is using borderless desktop fullscreen or no explicit fullscreen mode.",
    "i32 w, i32 h = win.get_fullscreen_mode()",
};

static const vigil_native_symbol_doc_t sdl_doc_window_update_surface_rect = {
    "Update one rectangle of the window surface.",
    "Use this after drawing to a window surface when only one region needs to be presented.",
    "bool ok, err e = win.update_surface_rect(0, 0, 64, 64)",
};

static const vigil_native_symbol_doc_t sdl_doc_get_camera_supported_formats = {
    "List camera formats supported by one enumerated camera device.",
    "Each line is format,colorspace,width,height,fps_num,fps_den using SDL numeric constants. "
    "Pass the same camera index used by Camera.open.",
    "string specs = sdl.get_camera_supported_formats(0)",
};

static const vigil_native_symbol_doc_t sdl_doc_get_cameras = {
    "List enumerated camera devices in a compact newline-separated form.",
    "Each line is instance_id,name,position using SDL camera position constants.",
    "string cameras = sdl.get_cameras()",
};

static const vigil_native_symbol_doc_t sdl_doc_camera_get_spec = {
    "Return the active camera format specification.",
    "Returns six i32 values in order: pixel format, colorspace, width, height, fps numerator, fps denominator.",
    "i32 fmt, i32 cs, i32 w, i32 h, i32 fps_num, i32 fps_den = cam.get_spec()",
};

static const vigil_native_symbol_doc_t sdl_doc_gamepad_get_bindings = {
    "Describe the current SDL gamepad bindings for one opened controller.",
    "Each line summarizes one SDL_GamepadBinding using a practical textual form for debugging and tooling.",
    "string bindings = pad.get_bindings()",
};

static const vigil_native_symbol_doc_t sdl_doc_get_gamepads = {
    "List connected gamepads in a compact newline-separated form.",
    "Each line is instance_id,name,type,player_index,path using SDL gamepad type constants.",
    "string pads = sdl.get_gamepads()",
};

static const vigil_native_symbol_doc_t sdl_doc_gamepad_get_guid = {
    "Return the SDL GUID string for an opened gamepad.",
    "This is equivalent to looking up the opened gamepad's instance ID and converting its GUID to text.",
    "string guid = pad.get_guid()",
};

static const vigil_native_symbol_doc_t sdl_doc_gamepad_get_power_info = {
    "Return the power state and battery percentage for an opened gamepad.",
    "Returns two i32 values in order: SDL power state and percentage, with -1 when the percentage is unavailable.",
    "i32 state, i32 percent = pad.get_power_info()",
};

static const vigil_native_symbol_doc_t sdl_doc_window_update_surface_rects = {
    "Update multiple rectangles of the window surface from an unsafe buffer.",
    "The buffer must contain count rectangles packed as repeating i32 x, y, w, h quads.",
    "bool ok, err e = win.update_surface_rects(rects, 2)",
};

static const vigil_native_symbol_doc_t sdl_doc_get_clipboard_data = {
    "Read clipboard data for one MIME type into an unsafe buffer.",
    "Returns a buffer handle, the byte size, and err. Free the buffer with unsafe.free when done.",
    "i64 buf, i64 size, err e = sdl.get_clipboard_data(\"text/plain\")",
};

static const vigil_native_symbol_doc_t sdl_doc_set_clipboard_data = {
    "Publish one clipboard payload from an unsafe buffer under a MIME type.",
    "Pass size <= 0 to publish the whole buffer. The data is copied before SDL takes ownership.",
    "bool ok, err e = sdl.set_clipboard_data(\"text/plain\", buf, size)",
};

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
    {"get_mouse_state", 15U, sdl_fn_get_mouse_state, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    SDL_FN("get_mouse_buttons", 17U, sdl_fn_get_mouse_buttons, 0U, NULL, VIGIL_TYPE_I32),
    {"get_global_mouse_state", 22U, sdl_fn_get_global_mouse_state, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
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
    {"load_wav", 8U, sdl_fn_load_wav, 1U, p_str, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
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
    {"get_gamepads", 12U, sdl_fn_get_gamepads, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, &sdl_doc_get_gamepads},
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
    {"show_message_box", 16U, sdl_fn_show_message_box, 4U, p_i32_str_str_str, VIGIL_TYPE_I32, 2U, rt_i32_err, 0, NULL,
     NULL, 0U, sdl_message_box_param_names, NULL, NULL, &sdl_doc_show_message_box},
    SDL_FN_BOOL_ERR("open_url", 8U, sdl_fn_open_url, 1U, p_str),
    SDL_FN("get_base_path", 13U, sdl_fn_get_base_path, 0U, NULL, VIGIL_TYPE_STRING),
    {"get_pref_path", 13U, sdl_fn_get_pref_path, 2U, p_str_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    SDL_FN("get_num_cpu_cores", 17U, sdl_fn_get_num_cpu_cores, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_system_ram", 14U, sdl_fn_get_system_ram, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN_VOID("log", 3U, sdl_fn_log, 1U, p_str),
    SDL_FN_VOID("log_error", 9U, sdl_fn_log_error, 1U, p_str),
    SDL_FN_VOID("log_warn", 8U, sdl_fn_log_warn, 1U, p_str),
    /* MessageBox flag constants */
    SDL_CONST_ENTRY("MESSAGEBOX_ERROR", MESSAGEBOX_ERROR),
    SDL_CONST_ENTRY("MESSAGEBOX_WARNING", MESSAGEBOX_WARNING),
    SDL_CONST_ENTRY("MESSAGEBOX_INFORMATION", MESSAGEBOX_INFORMATION),
    SDL_CONST_ENTRY("MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT", MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT),
    SDL_CONST_ENTRY("MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT", MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT),
    SDL_CONST_ENTRY("MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT", MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT),
    SDL_CONST_ENTRY("MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT", MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT),
    /* Extended display / power / screensaver (slice 13) */
    SDL_FN("get_primary_display", 19U, sdl_fn_get_primary_display, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_display_content_scale", 25U, sdl_fn_get_display_content_scale, 1U, p_i32, VIGIL_TYPE_F64),
    {"get_display_usable_bounds", 25U, sdl_fn_get_display_usable_bounds, 1U, p_i32, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_power_info", 14U, sdl_fn_get_power_info, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    SDL_FN("screen_saver_enabled", 20U, sdl_fn_screen_saver_enabled, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("enable_screen_saver", 19U, sdl_fn_enable_screen_saver, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("disable_screen_saver", 20U, sdl_fn_disable_screen_saver, 0U, NULL, VIGIL_TYPE_BOOL),
    /* Flash operation constants */
    SDL_CONST_ENTRY("FLASH_CANCEL", FLASH_CANCEL),
    SDL_CONST_ENTRY("FLASH_BRIEFLY", FLASH_BRIEFLY),
    SDL_CONST_ENTRY("FLASH_UNTIL_FOCUSED", FLASH_UNTIL_FOCUSED),
    /* Audio extras (slice 14) */
    SDL_FN("get_audio_playback_count", 24U, sdl_fn_get_audio_playback_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_audio_device_name", 21U, sdl_fn_get_audio_device_name, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_current_audio_driver", 24U, sdl_fn_get_current_audio_driver, 0U, NULL, VIGIL_TYPE_STRING),
    /* Cursor (slice 15) */
    {"create_system_cursor", 20U, sdl_fn_create_system_cursor, 1U, p_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    {"create_color_cursor", 19U, sdl_fn_create_color_cursor, 3U, p_obj_i32_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("set_cursor", 10U, sdl_fn_set_cursor, 1U, p_i64),
    SDL_FN_VOID("destroy_cursor", 14U, sdl_fn_destroy_cursor, 1U, p_i64),
    SDL_FN_BOOL_ERR("capture_mouse", 13U, sdl_fn_capture_mouse, 1U, p_i32),
    {"get_relative_mouse_state", 24U, sdl_fn_get_relative_mouse_state, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("warp_mouse_global", 17U, sdl_fn_warp_mouse_global, 2U, p_f64_f64),
    /* System cursor constants */
    SDL_CONST_ENTRY("CURSOR_DEFAULT", CURSOR_DEFAULT),
    SDL_CONST_ENTRY("CURSOR_TEXT", CURSOR_TEXT),
    SDL_CONST_ENTRY("CURSOR_WAIT", CURSOR_WAIT),
    SDL_CONST_ENTRY("CURSOR_CROSSHAIR", CURSOR_CROSSHAIR),
    SDL_CONST_ENTRY("CURSOR_PROGRESS", CURSOR_PROGRESS),
    SDL_CONST_ENTRY("CURSOR_NWSE_RESIZE", CURSOR_NWSE_RESIZE),
    SDL_CONST_ENTRY("CURSOR_NESW_RESIZE", CURSOR_NESW_RESIZE),
    SDL_CONST_ENTRY("CURSOR_EW_RESIZE", CURSOR_EW_RESIZE),
    SDL_CONST_ENTRY("CURSOR_NS_RESIZE", CURSOR_NS_RESIZE),
    SDL_CONST_ENTRY("CURSOR_MOVE", CURSOR_MOVE),
    SDL_CONST_ENTRY("CURSOR_NOT_ALLOWED", CURSOR_NOT_ALLOWED),
    SDL_CONST_ENTRY("CURSOR_POINTER", CURSOR_POINTER),
    /* Events (slice 16) */
    SDL_FN_VOID("pump_events", 11U, sdl_fn_pump_events, 0U, NULL),
    SDL_FN("has_event", 9U, sdl_fn_has_event, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("flush_event", 11U, sdl_fn_flush_event, 1U, p_i32),
    SDL_FN_VOID("flush_events", 12U, sdl_fn_flush_events, 2U, p_i32_i32),
    SDL_FN("event_enabled", 13U, sdl_fn_event_enabled, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("set_event_enabled", 17U, sdl_fn_set_event_enabled, 2U, p_i32_i32),
    /* Scale mode constants (slice 18) */
    SDL_CONST_ENTRY("SCALEMODE_NEAREST", SCALEMODE_NEAREST),
    SDL_CONST_ENTRY("SCALEMODE_LINEAR", SCALEMODE_LINEAR),
    /* System info (slice 19) */
    SDL_FN("get_current_time", 16U, sdl_fn_get_current_time, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN("get_user_folder", 15U, sdl_fn_get_user_folder, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_system_theme", 16U, sdl_fn_get_system_theme, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("is_tablet", 9U, sdl_fn_is_tablet, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("is_tv", 5U, sdl_fn_is_tv, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN_BOOL_ERR("set_app_metadata", 16U, sdl_fn_set_app_metadata, 3U, p_str_str_str),
    SDL_FN("get_current_video_driver", 24U, sdl_fn_get_current_video_driver, 0U, NULL, VIGIL_TYPE_STRING),
    /* System theme constants */
    SDL_CONST_ENTRY("SYSTEM_THEME_UNKNOWN", SYSTEM_THEME_UNKNOWN),
    SDL_CONST_ENTRY("SYSTEM_THEME_LIGHT", SYSTEM_THEME_LIGHT),
    SDL_CONST_ENTRY("SYSTEM_THEME_DARK", SYSTEM_THEME_DARK),
    /* User folder constants */
    SDL_CONST_ENTRY("FOLDER_HOME", FOLDER_HOME),
    SDL_CONST_ENTRY("FOLDER_DESKTOP", FOLDER_DESKTOP),
    SDL_CONST_ENTRY("FOLDER_DOCUMENTS", FOLDER_DOCUMENTS),
    SDL_CONST_ENTRY("FOLDER_DOWNLOADS", FOLDER_DOWNLOADS),
    SDL_CONST_ENTRY("FOLDER_MUSIC", FOLDER_MUSIC),
    SDL_CONST_ENTRY("FOLDER_PICTURES", FOLDER_PICTURES),
    SDL_CONST_ENTRY("FOLDER_SAVEDGAMES", FOLDER_SAVEDGAMES),
    SDL_CONST_ENTRY("FOLDER_SCREENSHOTS", FOLDER_SCREENSHOTS),
    SDL_CONST_ENTRY("FOLDER_VIDEOS", FOLDER_VIDEOS),
    /* Gamepad extras (slice 22) */
    SDL_FN_VOID("update_gamepads", 15U, sdl_fn_update_gamepads, 0U, NULL),
    /* Progress state constants (slice 21) */
    SDL_CONST_ENTRY("PROGRESS_NONE", PROGRESS_NONE),
    SDL_CONST_ENTRY("PROGRESS_INDETERMINATE", PROGRESS_INDETERMINATE),
    SDL_CONST_ENTRY("PROGRESS_NORMAL", PROGRESS_NORMAL),
    SDL_CONST_ENTRY("PROGRESS_PAUSED", PROGRESS_PAUSED),
    SDL_CONST_ENTRY("PROGRESS_ERROR", PROGRESS_ERROR),
    /* Keyboard/scancode lookups (slice 24) */
    SDL_FN("get_key_from_scancode", 21U, sdl_fn_get_key_from_scancode, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_scancode_from_key", 21U, sdl_fn_get_scancode_from_key, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_key_from_name", 17U, sdl_fn_get_key_from_name, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("get_scancode_from_name", 21U, sdl_fn_get_scancode_from_name, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("has_keyboard", 12U, sdl_fn_has_keyboard, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_mouse", 9U, sdl_fn_has_mouse, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN_BOOL_ERR("start_text_input", 16U, sdl_fn_start_text_input, 1U, p_obj),
    SDL_FN_BOOL_ERR("stop_text_input", 15U, sdl_fn_stop_text_input, 1U, p_obj),
    SDL_FN("text_input_active", 17U, sdl_fn_text_input_active, 1U, p_obj, VIGIL_TYPE_BOOL),
    /* Display modes (slice 26) */
    {"get_desktop_display_mode", 24U, sdl_fn_get_desktop_display_mode, 1U, p_i32, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_current_display_mode", 24U, sdl_fn_get_current_display_mode, 1U, p_i32, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("get_display_refresh_rate", 24U, sdl_fn_get_display_refresh_rate, 1U, p_i32, VIGIL_TYPE_F64),
    SDL_FN("get_display_for_window", 22U, sdl_fn_get_display_for_window, 1U, p_obj, VIGIL_TYPE_I32),
    /* Joystick (slice 29) */
    SDL_FN("has_joystick", 12U, sdl_fn_has_joystick, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("get_joystick_count", 18U, sdl_fn_get_joystick_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_joystick_id", 15U, sdl_fn_get_joystick_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("is_gamepad_id", 13U, sdl_fn_is_gamepad_id, 1U, p_i32, VIGIL_TYPE_BOOL),
    /* Hat constants */
    SDL_CONST_ENTRY("HAT_CENTERED", HAT_CENTERED),
    SDL_CONST_ENTRY("HAT_UP", HAT_UP),
    SDL_CONST_ENTRY("HAT_RIGHT", HAT_RIGHT),
    SDL_CONST_ENTRY("HAT_DOWN", HAT_DOWN),
    SDL_CONST_ENTRY("HAT_LEFT", HAT_LEFT),
    /* Haptic (slice 32) */
    SDL_FN("get_haptic_count", 15U, sdl_fn_get_haptic_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("is_mouse_haptic", 15U, sdl_fn_is_mouse_haptic, 0U, NULL, VIGIL_TYPE_BOOL),
    /* Filesystem (slice 33) */
    SDL_FN("get_current_directory", 21U, sdl_fn_get_current_directory, 0U, NULL, VIGIL_TYPE_STRING),
    SDL_FN_BOOL_ERR("create_directory", 16U, sdl_fn_create_directory, 1U, p_str),
    SDL_FN_BOOL_ERR("remove_path", 11U, sdl_fn_remove_path, 1U, p_str),
    SDL_FN_BOOL_ERR("rename_path", 11U, sdl_fn_rename_path, 2U, p_str_str),
    SDL_FN_BOOL_ERR("copy_file", 9U, sdl_fn_copy_file, 2U, p_str_str),
    SDL_FN("get_path_type", 13U, sdl_fn_get_path_type, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("get_path_size", 13U, sdl_fn_get_path_size, 1U, p_str, VIGIL_TYPE_I64),
    SDL_CONST_ENTRY("PATHTYPE_NONE", PATHTYPE_NONE),
    SDL_CONST_ENTRY("PATHTYPE_FILE", PATHTYPE_FILE),
    SDL_CONST_ENTRY("PATHTYPE_DIRECTORY", PATHTYPE_DIRECTORY),
    /* Camera (slice 35) */
    SDL_FN("get_camera_count", 16U, sdl_fn_get_camera_count, 0U, NULL, VIGIL_TYPE_I32),
    {"get_cameras", 11U, sdl_fn_get_cameras, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &sdl_doc_get_cameras},
    SDL_FN("get_camera_name", 15U, sdl_fn_get_camera_name, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_current_camera_driver", 25U, sdl_fn_get_current_camera_driver, 0U, NULL, VIGIL_TYPE_STRING),
    {"get_camera_supported_formats", 28U, sdl_fn_get_camera_supported_formats, 1U, p_i32, VIGIL_TYPE_STRING, 1U, NULL,
     0, NULL, NULL, 0U, sdl_camera_index_param_names, NULL, NULL, &sdl_doc_get_camera_supported_formats},
    /* Window complete */
    {"create_window_with_properties", 29U, sdl_fn_create_window_with_properties, 1U, p_i32, VIGIL_TYPE_OBJECT, 2U,
     rt_obj_err, 0, NULL, NULL, 0U, sdl_properties_param_names, NULL, "Window", &sdl_doc_create_window_with_properties},
    {"create_window_and_renderer", 26U, sdl_fn_create_window_and_renderer, 4U, p_str_i32_i32_i32, VIGIL_TYPE_I64, 2U,
     rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("get_grabbed_window", 18U, sdl_fn_get_grabbed_window, 0U, NULL, VIGIL_TYPE_I32),
    /* Renderer complete - module functions */
    {"create_renderer_with_properties", 31U, sdl_fn_create_renderer_with_properties, 1U, p_i32, VIGIL_TYPE_OBJECT, 2U,
     rt_obj_err, 0, NULL, NULL, 0U, sdl_properties_param_names, NULL, "Renderer",
     &sdl_doc_create_renderer_with_properties},
    SDL_FN("get_num_render_drivers", 21U, sdl_fn_get_num_render_drivers, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_render_driver", 17U, sdl_fn_get_render_driver, 1U, p_i32, VIGIL_TYPE_STRING),
    /* Renderer/Surface final - module functions */
    {"create_software_renderer", 24U, sdl_fn_create_software_renderer, 1U, p_obj, VIGIL_TYPE_I64, 2U, rt_i64_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"lock_texture_to_surface", 23U, sdl_renderer_lock_texture_to_surface, 5U, p_i64_i32_i32_i32_i32, VIGIL_TYPE_I64,
     2U, rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* IO Stream */
    {"io_from_file", 12U, sdl_fn_io_from_file, 2U, p_str_str, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    {"io_from_mem", 11U, sdl_fn_io_from_mem, 1U, p_i64, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"io_from_const_mem", 16U, sdl_fn_io_from_const_mem, 1U, p_i64, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"io_from_dynamic_mem", 18U, sdl_fn_io_from_dynamic_mem, 0U, NULL, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("io_close", 8U, sdl_fn_io_close, 1U, p_i64),
    SDL_FN("io_size", 7U, sdl_fn_io_size, 1U, p_i64, VIGIL_TYPE_I64),
    {"io_seek", 7U, sdl_fn_io_seek, 3U, p_i64_i64_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    SDL_FN("io_tell", 7U, sdl_fn_io_tell, 1U, p_i64, VIGIL_TYPE_I64),
    {"io_read", 7U, sdl_fn_io_read, 3U, p_i64_i64_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"io_write", 8U, sdl_fn_io_write, 3U, p_i64_i64_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    SDL_FN_BOOL_ERR("io_flush", 8U, sdl_fn_io_flush, 1U, p_i64),
    SDL_FN("io_status", 9U, sdl_fn_io_status, 1U, p_i64, VIGIL_TYPE_I32),
    {"io_load_file", 12U, sdl_fn_io_load_file, 1U, p_i64, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    SDL_FN_BOOL_ERR("io_save_file", 12U, sdl_fn_io_save_file, 2U, p_i64_i64),
    {"load_bmp_io", 11U, sdl_fn_load_bmp_io, 1U, p_i64, VIGIL_TYPE_OBJECT, 2U, rt_obj_err, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    {"load_surface_io", 15U, sdl_fn_load_surface_io, 1U, p_i64, VIGIL_TYPE_OBJECT, 2U, rt_obj_err, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"save_bmp_io", 11U, sdl_fn_save_bmp_io, 2U, p_obj_i64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    {"load_wav_io", 11U, sdl_fn_load_wav_io, 1U, p_i64, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    /* Gamepad complete - module functions */
    SDL_FN("add_gamepad_mapping", 19U, sdl_fn_add_gamepad_mapping, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("add_gamepad_mappings_from_file", 30U, sdl_fn_add_gamepad_mappings_from_file, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN_BOOL_ERR("reload_gamepad_mappings", 22U, sdl_fn_reload_gamepad_mappings, 0U, NULL),
    SDL_FN("get_gamepad_axis_from_string", 28U, sdl_fn_get_gamepad_axis_from_string, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_button_from_string", 30U, sdl_fn_get_gamepad_button_from_string, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_string_for_axis", 27U, sdl_fn_get_gamepad_string_for_axis, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_gamepad_string_for_button", 29U, sdl_fn_get_gamepad_string_for_button, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_gamepad_string_for_type", 27U, sdl_fn_get_gamepad_string_for_type, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_gamepad_type_from_string", 28U, sdl_fn_get_gamepad_type_from_string, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_name_for_id", 22U, sdl_fn_get_gamepad_name_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_gamepad_type_for_id", 22U, sdl_fn_get_gamepad_type_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    {"get_gamepad_mapping_for_guid", 28U, sdl_fn_get_gamepad_mapping_for_guid, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL,
     0, NULL, NULL, 0U, sdl_guid_param_names, NULL, NULL, &sdl_doc_get_gamepad_mapping_for_guid},
    {"get_gamepad_mappings", 20U, sdl_fn_get_gamepad_mappings, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, &sdl_doc_get_gamepad_mappings},
    /* Joystick complete - module */
    SDL_FN_VOID("update_joysticks", 17U, sdl_fn_update_joysticks, 0U, NULL),
    SDL_FN_VOID("lock_joysticks", 15U, sdl_fn_lock_joysticks, 0U, NULL),
    SDL_FN_VOID("unlock_joysticks", 17U, sdl_fn_unlock_joysticks, 0U, NULL),
    SDL_FN("get_joystick_name_for_id", 23U, sdl_fn_get_joystick_name_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_joystick_path_for_id", 23U, sdl_fn_get_joystick_path_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_joystick_type_for_id", 23U, sdl_fn_get_joystick_type_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_joystick_vendor_for_id", 25U, sdl_fn_get_joystick_vendor_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_joystick_product_for_id", 26U, sdl_fn_get_joystick_product_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_joystick_product_version_for_id", 34U, sdl_fn_get_joystick_product_version_for_id, 1U, p_i32,
           VIGIL_TYPE_I32),
    SDL_FN("get_joystick_player_index_for_id", 31U, sdl_fn_get_joystick_player_index_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("is_joystick_virtual", 19U, sdl_fn_is_joystick_virtual, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("attach_virtual_joystick", 23U, sdl_fn_attach_virtual_joystick, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_I32),
    SDL_FN_BOOL_ERR("detach_virtual_joystick", 22U, sdl_fn_detach_virtual_joystick, 1U, p_i32),
    {"set_joystick_virtual_axis", 25U, sdl_fn_set_joystick_virtual_axis, 3U, p_i64_i32_i32, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"set_joystick_virtual_button", 27U, sdl_fn_set_joystick_virtual_button, 3U, p_i64_i32_i32, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"set_joystick_virtual_hat", 24U, sdl_fn_set_joystick_virtual_hat, 3U, p_i64_i32_i32, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Audio complete - module */
    SDL_FN("get_num_audio_drivers", 21U, sdl_fn_get_num_audio_drivers, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_audio_driver", 16U, sdl_fn_get_audio_driver, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_audio_format_name", 21U, sdl_fn_get_audio_format_name, 1U, p_i32, VIGIL_TYPE_STRING),
    {"open_audio_device", 17U, sdl_fn_open_audio_device, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_I32, 2U, rt_i32_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("close_audio_device", 18U, sdl_fn_close_audio_device, 1U, p_i32),
    SDL_FN_BOOL_ERR("pause_audio_device", 18U, sdl_fn_pause_audio_device, 1U, p_i32),
    SDL_FN_BOOL_ERR("resume_audio_device", 19U, sdl_fn_resume_audio_device, 1U, p_i32),
    SDL_FN("audio_device_paused", 19U, sdl_fn_audio_device_paused, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("get_audio_device_gain", 21U, sdl_fn_get_audio_device_gain, 1U, p_i32, VIGIL_TYPE_F64),
    {"set_audio_device_gain", 21U, sdl_fn_set_audio_device_gain, 2U, p_i32_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("is_audio_device_physical", 24U, sdl_fn_is_audio_device_physical, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("is_audio_device_playback", 24U, sdl_fn_is_audio_device_playback, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("get_audio_recording_device_count", 32U, sdl_fn_get_audio_recording_device_count, 0U, NULL, VIGIL_TYPE_I32),
    /* Sensor complete */
    SDL_FN("get_sensor_count", 16U, sdl_fn_get_sensor_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_sensor_name_for_id", 22U, sdl_fn_get_sensor_name_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_sensor_type_for_id", 22U, sdl_fn_get_sensor_type_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_sensor_nonportable_type_for_id", 34U, sdl_fn_get_sensor_nonportable_type_for_id, 1U, p_i32,
           VIGIL_TYPE_I32),
    SDL_FN_VOID("update_sensors", 14U, sdl_fn_update_sensors, 0U, NULL),
    {"open_sensor", 11U, sdl_fn_open_sensor, 1U, p_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    SDL_FN_VOID("close_sensor", 12U, sdl_fn_close_sensor, 1U, p_i64),
    SDL_FN("get_sensor_name", 15U, sdl_fn_get_sensor_name, 1U, p_i64, VIGIL_TYPE_STRING),
    SDL_FN("get_sensor_type", 15U, sdl_fn_get_sensor_type, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_sensor_nonportable_type", 27U, sdl_fn_get_sensor_nonportable_type, 1U, p_i64, VIGIL_TYPE_I32),
    /* Sensor type constants */
    SDL_CONST_ENTRY("SENSOR_ACCEL", SENSOR_ACCEL),
    SDL_CONST_ENTRY("SENSOR_GYRO", SENSOR_GYRO),
    SDL_CONST_ENTRY("SENSOR_ACCEL_L", SENSOR_ACCEL_L),
    SDL_CONST_ENTRY("SENSOR_GYRO_L", SENSOR_GYRO_L),
    SDL_CONST_ENTRY("SENSOR_ACCEL_R", SENSOR_ACCEL_R),
    SDL_CONST_ENTRY("SENSOR_GYRO_R", SENSOR_GYRO_R),
    /* Keyboard complete */
    SDL_FN_BOOL_ERR("clear_composition", 17U, sdl_fn_clear_composition, 1U, p_obj),
    SDL_FN("has_screen_keyboard_support", 27U, sdl_fn_has_screen_keyboard_support, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("screen_keyboard_shown", 21U, sdl_fn_screen_keyboard_shown, 1U, p_obj, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("reset_keyboard", 14U, sdl_fn_reset_keyboard, 0U, NULL),
    SDL_FN_VOID("set_mod_state", 13U, sdl_fn_set_mod_state, 1U, p_i32),
    /* Touch complete */
    SDL_FN("get_touch_device_count", 22U, sdl_fn_get_touch_device_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_touch_device_name", 21U, sdl_fn_get_touch_device_name, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_touch_device_type", 21U, sdl_fn_get_touch_device_type, 1U, p_i32, VIGIL_TYPE_I32),
    /* Cursor complete */
    SDL_FN("get_mouse_focus", 15U, sdl_fn_get_mouse_focus, 0U, NULL, VIGIL_TYPE_I32),
    /* Display complete */
    SDL_FN("get_current_display_orientation", 31U, sdl_fn_get_current_display_orientation, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_natural_display_orientation", 31U, sdl_fn_get_natural_display_orientation, 1U, p_i32, VIGIL_TYPE_I32),
    /* Camera complete */
    SDL_FN("get_num_camera_drivers", 21U, sdl_fn_get_num_camera_drivers, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_camera_driver", 16U, sdl_fn_get_camera_driver, 1U, p_i32, VIGIL_TYPE_STRING),
    /* GPU final */
    SDL_FN_VOID("gpu_bind_compute_storage_buffers", 32U, sdl_fn_gpu_bind_compute_storage_buffers, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_bind_compute_storage_textures", 33U, sdl_fn_gpu_bind_compute_storage_textures, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_bind_compute_samplers", 25U, sdl_fn_gpu_bind_compute_samplers, 3U, p_i64_i64_i64),
    SDL_FN_VOID("gpu_bind_fragment_storage_buffers", 33U, sdl_fn_gpu_bind_fragment_storage_buffers, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_bind_fragment_storage_textures", 34U, sdl_fn_gpu_bind_fragment_storage_textures, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_bind_vertex_storage_buffers", 31U, sdl_fn_gpu_bind_vertex_storage_buffers, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_bind_vertex_storage_textures", 32U, sdl_fn_gpu_bind_vertex_storage_textures, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_dispatch_compute_indirect", 29U, sdl_fn_gpu_dispatch_compute_indirect, 3U, p_i64_i64_i32),
    SDL_FN("gpu_texture_format_texel_block_size", 36U, sdl_fn_gpu_texture_format_texel_block_size, 1U, p_i32,
           VIGIL_TYPE_I32),
    SDL_FN("gpu_texture_format_from_pixel", 30U, sdl_fn_gpu_texture_format_from_pixel, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("gpu_pixel_format_from_texture", 30U, sdl_fn_gpu_pixel_format_from_texture, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN_VOID("gpu_copy_texture_to_texture", 27U, sdl_fn_gpu_copy_texture_to_texture, 6U, p_i64_i64_i64_i32_i32_i32),
    {"gpu_download_from_texture", 24U, sdl_fn_gpu_download_from_texture, 6U, p_i64_i64_i32_i32_i64_i32, VIGIL_TYPE_VOID,
     0U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Haptic complete - module */
    SDL_FN("get_haptic_name_for_id", 22U, sdl_fn_get_haptic_name_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    {"open_haptic_from_mouse", 22U, sdl_fn_open_haptic_from_mouse, 0U, NULL, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    /* stdinc batch 2 */
    SDL_FN("m_isinff", 8U, sdl_fn_m_isinff, 1U, p_f64, VIGIL_TYPE_BOOL),
    SDL_FN("m_isnanf", 8U, sdl_fn_m_isnanf, 1U, p_f64, VIGIL_TYPE_BOOL),
    SDL_FN("m_scalbnf", 9U, sdl_fn_m_scalbnf, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("s_strnlen", 9U, sdl_fn_s_strnlen, 2U, p_str_i32, VIGIL_TYPE_I32),
    SDL_FN("s_strlcpy", 9U, sdl_fn_s_strlcpy, 2U, p_str_i32, VIGIL_TYPE_STRING),
    SDL_FN("s_strlcat", 9U, sdl_fn_s_strlcat, 2U, p_str_str, VIGIL_TYPE_STRING),
    SDL_FN("s_strpbrk", 9U, sdl_fn_s_strpbrk, 2U, p_str_str, VIGIL_TYPE_I32),
    SDL_FN("s_strcasestr", 12U, sdl_fn_s_strcasestr, 2U, p_str_str, VIGIL_TYPE_I32),
    SDL_FN("s_strtoll", 9U, sdl_fn_s_strtoll, 2U, p_str_i32, VIGIL_TYPE_I64),
    SDL_FN("s_strtoull", 10U, sdl_fn_s_strtoull, 2U, p_str_i32, VIGIL_TYPE_I64),
    SDL_FN("s_uitoa", 7U, sdl_fn_s_uitoa, 2U, p_i32_i32, VIGIL_TYPE_STRING),
    SDL_FN("s_ltoa", 6U, sdl_fn_s_ltoa, 2U, p_i64_i32, VIGIL_TYPE_STRING),
    SDL_FN("s_ulltoa", 8U, sdl_fn_s_ulltoa, 2U, p_i64_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_pixel_format_name", 21U, sdl_fn_get_pixel_format_name, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_num_allocations", 19U, sdl_fn_get_num_allocations, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_simd_alignment", 19U, sdl_fn_get_simd_alignment, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_system_page_size", 20U, sdl_fn_get_system_page_size, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN_VOID("clear_error", 11U, sdl_fn_clear_error, 0U, NULL),
    SDL_FN("swap16", 6U, sdl_fn_swap16, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("swap32", 6U, sdl_fn_swap32, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("swap64", 6U, sdl_fn_swap64, 1U, p_i64, VIGIL_TYPE_I64),
    /* Hints */
    SDL_FN("set_hint", 8U, sdl_fn_set_hint, 2U, p_str_str, VIGIL_TYPE_BOOL),
    SDL_FN("get_hint", 8U, sdl_fn_get_hint, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN("get_hint_boolean", 16U, sdl_fn_get_hint_boolean, 2U, p_str_i32, VIGIL_TYPE_BOOL),
    SDL_FN("reset_hint", 10U, sdl_fn_reset_hint, 1U, p_str, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("reset_hints", 11U, sdl_fn_reset_hints, 0U, NULL),
    /* Logging */
    SDL_FN_VOID("log_info", 8U, sdl_fn_log_info, 2U, p_i32_str),
    SDL_FN_VOID("log_debug", 9U, sdl_fn_log_debug, 2U, p_i32_str),
    SDL_FN_VOID("log_verbose", 11U, sdl_fn_log_verbose, 2U, p_i32_str),
    SDL_FN_VOID("log_critical", 12U, sdl_fn_log_critical, 2U, p_i32_str),
    SDL_FN_VOID("set_log_priority", 16U, sdl_fn_set_log_priority, 2U, p_i32_i32),
    SDL_FN("get_log_priority", 16U, sdl_fn_get_log_priority, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN_VOID("reset_log_priorities", 20U, sdl_fn_reset_log_priorities, 0U, NULL),
    SDL_FN_VOID("set_log_priorities", 18U, sdl_fn_set_log_priorities, 1U, p_i32),
    /* Properties */
    SDL_FN("create_properties", 17U, sdl_fn_create_properties, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN_VOID("destroy_properties", 18U, sdl_fn_destroy_properties, 1U, p_i32),
    SDL_FN("get_global_properties", 21U, sdl_fn_get_global_properties, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN_BOOL_ERR("lock_properties", 15U, sdl_fn_lock_properties, 1U, p_i32),
    SDL_FN_VOID("unlock_properties", 17U, sdl_fn_unlock_properties, 1U, p_i32),
    SDL_FN_BOOL_ERR("copy_properties", 15U, sdl_fn_copy_properties, 2U, p_i32_i32),
    SDL_FN("has_property", 12U, sdl_fn_has_property, 2U, p_i32_str, VIGIL_TYPE_BOOL),
    SDL_FN("get_property_type", 17U, sdl_fn_get_property_type, 2U, p_i32_str, VIGIL_TYPE_I32),
    SDL_FN_BOOL_ERR("clear_property", 14U, sdl_fn_clear_property, 2U, p_i32_str),
    {"set_string_property", 19U, sdl_fn_set_string_property, 3U, p_i32_str_str, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_string_property", 19U, sdl_fn_get_string_property, 3U, p_i32_str_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"set_number_property", 19U, sdl_fn_set_number_property, 3U, p_i32_str_i64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_number_property", 19U, sdl_fn_get_number_property, 3U, p_i32_str_i64, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    {"set_float_property", 18U, sdl_fn_set_float_property, 3U, p_i32_str_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_float_property", 18U, sdl_fn_get_float_property, 3U, p_i32_str_f64, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    {"set_boolean_property", 20U, sdl_fn_set_boolean_property, 3U, p_i32_str_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_boolean_property", 20U, sdl_fn_get_boolean_property, 3U, p_i32_str_i32, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("get_window_properties", 21U, sdl_fn_get_window_properties, 1U, p_obj, VIGIL_TYPE_I32),
    SDL_FN("get_renderer_properties", 23U, sdl_fn_get_renderer_properties, 1U, p_obj, VIGIL_TYPE_I32),
    SDL_FN("get_texture_properties", 22U, sdl_fn_get_texture_properties, 1U, p_obj, VIGIL_TYPE_I32),
    SDL_FN("get_surface_properties", 22U, sdl_fn_get_surface_properties, 1U, p_obj, VIGIL_TYPE_I32),
    SDL_FN("get_app_metadata_property", 25U, sdl_fn_get_app_metadata_property, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN_BOOL_ERR("set_app_metadata_property", 25U, sdl_fn_set_app_metadata_property, 2U, p_str_str),
    /* IO read/write helpers */
    SDL_FN("io_read_u8", 10U, sdl_fn_io_read_u8, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("io_read_s16le", 13U, sdl_fn_io_read_s16le, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("io_read_s16be", 13U, sdl_fn_io_read_s16be, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("io_read_s32le", 13U, sdl_fn_io_read_s32le, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("io_read_s32be", 13U, sdl_fn_io_read_s32be, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("io_read_s64le", 13U, sdl_fn_io_read_s64le, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN("io_read_s64be", 13U, sdl_fn_io_read_s64be, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN_BOOL_ERR("io_write_u8", 11U, sdl_fn_io_write_u8, 2U, p_i64_i32),
    SDL_FN_BOOL_ERR("io_write_s16le", 14U, sdl_fn_io_write_s16le, 2U, p_i64_i32),
    SDL_FN_BOOL_ERR("io_write_s32le", 14U, sdl_fn_io_write_s32le, 2U, p_i64_i32),
    SDL_FN_BOOL_ERR("io_write_s64le", 14U, sdl_fn_io_write_s64le, 2U, p_i64_i64),
    SDL_FN_BOOL_ERR("io_write_s16be", 14U, sdl_fn_io_write_s16be, 2U, p_i64_i32),
    SDL_FN_BOOL_ERR("io_write_s32be", 14U, sdl_fn_io_write_s32be, 2U, p_i64_i32),
    SDL_FN_BOOL_ERR("io_write_s64be", 14U, sdl_fn_io_write_s64be, 2U, p_i64_i64),
    /* Misc batch */
    {"add_timer", 9U, sdl_fn_add_timer, 2U, p_i32_obj, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    SDL_FN("remove_timer", 12U, sdl_fn_remove_timer, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("mem_alloc", 9U, sdl_fn_mem_alloc, 1U, p_i32, VIGIL_TYPE_I64),
    SDL_FN_VOID("mem_free", 8U, sdl_fn_mem_free, 1U, p_i64),
    SDL_FN("getenv_unsafe", 13U, sdl_fn_getenv_unsafe, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN("get_current_thread_id", 21U, sdl_fn_get_current_thread_id, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN("is_main_thread", 14U, sdl_fn_is_main_thread, 0U, NULL, VIGIL_TYPE_BOOL),
    {"datetime_to_time", 16U, sdl_fn_datetime_to_time, 6U, p_i32_i32_i32_i32_i32_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("get_preferred_locales_count", 27U, sdl_fn_get_preferred_locales_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("glob_directory", 14U, sdl_fn_glob_directory, 2U, p_str_str, VIGIL_TYPE_I32),
    /* Rect math */
    {"point_in_rect", 13U, sdl_fn_point_in_rect, 6U, p_i32_i32_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("rect_empty", 10U, sdl_fn_rect_empty, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_BOOL),
    {"rects_equal", 11U, sdl_fn_rects_equal, 8U, p_i32x8, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"has_rect_intersection", 21U, sdl_fn_has_rect_intersection, 8U, p_i32x8, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    {"get_rect_intersection", 21U, sdl_fn_get_rect_intersection, 8U, p_i32x8, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_rect_union", 14U, sdl_fn_get_rect_union, 8U, p_i32x8, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    {"point_in_rect_float", 19U, sdl_fn_point_in_rect_float, 6U, p_f64x6, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    SDL_FN("rect_empty_float", 16U, sdl_fn_rect_empty_float, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_BOOL),
    {"has_rect_intersection_float", 27U, sdl_fn_has_rect_intersection_float, 8U, p_f64x8, VIGIL_TYPE_BOOL, 1U, NULL, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Async IO */
    {"async_io_from_file", 18U, sdl_fn_async_io_from_file, 2U, p_str_str, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    SDL_FN("async_io_size", 13U, sdl_fn_async_io_size, 1U, p_i64, VIGIL_TYPE_I64),
    {"create_async_io_queue", 21U, sdl_fn_create_async_io_queue, 0U, NULL, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("destroy_async_io_queue", 22U, sdl_fn_destroy_async_io_queue, 1U, p_i64),
    SDL_FN_VOID("signal_async_io_queue", 21U, sdl_fn_signal_async_io_queue, 1U, p_i64),
    {"async_io_read", 13U, sdl_fn_async_io_read, 5U, p_i64_i64_i64_i32_i64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"async_io_write", 14U, sdl_fn_async_io_write, 5U, p_i64_i64_i64_i32_i64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"close_async_io", 14U, sdl_fn_close_async_io, 3U, p_i64_i32_i64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    SDL_FN("get_async_io_result", 19U, sdl_fn_get_async_io_result, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("wait_async_io_result", 20U, sdl_fn_wait_async_io_result, 2U, p_i64_i32, VIGIL_TYPE_I32),
    /* Threading */
    SDL_FN("create_mutex", 12U, sdl_fn_create_mutex, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN_VOID("destroy_mutex", 13U, sdl_fn_destroy_mutex, 1U, p_i64),
    SDL_FN_VOID("lock_mutex", 10U, sdl_fn_lock_mutex, 1U, p_i64),
    SDL_FN("try_lock_mutex", 14U, sdl_fn_try_lock_mutex, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("unlock_mutex", 12U, sdl_fn_unlock_mutex, 1U, p_i64),
    SDL_FN("create_rwlock", 13U, sdl_fn_create_rwlock, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN_VOID("destroy_rwlock", 14U, sdl_fn_destroy_rwlock, 1U, p_i64),
    SDL_FN_VOID("lock_rwlock_read", 15U, sdl_fn_lock_rwlock_read, 1U, p_i64),
    SDL_FN_VOID("lock_rwlock_write", 16U, sdl_fn_lock_rwlock_write, 1U, p_i64),
    SDL_FN("try_lock_rwlock_read", 19U, sdl_fn_try_lock_rwlock_read, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN("try_lock_rwlock_write", 20U, sdl_fn_try_lock_rwlock_write, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("unlock_rwlock", 13U, sdl_fn_unlock_rwlock, 1U, p_i64),
    SDL_FN("create_semaphore", 16U, sdl_fn_create_semaphore, 1U, p_i32, VIGIL_TYPE_I64),
    SDL_FN_VOID("destroy_semaphore", 17U, sdl_fn_destroy_semaphore, 1U, p_i64),
    SDL_FN_VOID("wait_semaphore", 14U, sdl_fn_wait_semaphore, 1U, p_i64),
    SDL_FN("try_wait_semaphore", 18U, sdl_fn_try_wait_semaphore, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN("wait_semaphore_timeout", 22U, sdl_fn_wait_semaphore_timeout, 2U, p_i64_i32, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("signal_semaphore", 16U, sdl_fn_signal_semaphore, 1U, p_i64),
    SDL_FN("get_semaphore_value", 19U, sdl_fn_get_semaphore_value, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("create_condition", 16U, sdl_fn_create_condition, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN_VOID("destroy_condition", 17U, sdl_fn_destroy_condition, 1U, p_i64),
    SDL_FN_VOID("signal_condition", 16U, sdl_fn_signal_condition, 1U, p_i64),
    SDL_FN_VOID("broadcast_condition", 19U, sdl_fn_broadcast_condition, 1U, p_i64),
    SDL_FN_VOID("wait_condition", 14U, sdl_fn_wait_condition, 2U, p_i64_i64),
    {"wait_condition_timeout", 22U, sdl_fn_wait_condition_timeout, 3U, p_i64_i64_i32, VIGIL_TYPE_BOOL, 1U, NULL, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("set_atomic_int", 14U, sdl_fn_set_atomic_int, 2U, p_i64_i32, VIGIL_TYPE_I32),
    SDL_FN("get_atomic_int", 14U, sdl_fn_get_atomic_int, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("add_atomic_int", 14U, sdl_fn_add_atomic_int, 2U, p_i64_i32, VIGIL_TYPE_I32),
    SDL_FN_BOOL_ERR("set_current_thread_priority", 27U, sdl_fn_set_current_thread_priority, 1U, p_i32),
    /* Storage */
    {"open_title_storage", 18U, sdl_fn_open_title_storage, 1U, p_str, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"open_user_storage", 17U, sdl_fn_open_user_storage, 2U, p_str_str, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    {"open_file_storage", 17U, sdl_fn_open_file_storage, 1U, p_str, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("close_storage", 13U, sdl_fn_close_storage, 1U, p_i64),
    SDL_FN("storage_ready", 13U, sdl_fn_storage_ready, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN("get_storage_file_size", 21U, sdl_fn_get_storage_file_size, 2U, p_i64_str, VIGIL_TYPE_I64),
    {"read_storage_file", 17U, sdl_fn_read_storage_file, 4U, p_i64_str_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"write_storage_file", 18U, sdl_fn_write_storage_file, 4U, p_i64_str_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("create_storage_directory", 23U, sdl_fn_create_storage_directory, 2U, p_i64_str),
    SDL_FN_BOOL_ERR("remove_storage_path", 19U, sdl_fn_remove_storage_path, 2U, p_i64_str),
    {"rename_storage_path", 19U, sdl_fn_rename_storage_path, 3U, p_i64_str_str, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"copy_storage_file", 17U, sdl_fn_copy_storage_file, 3U, p_i64_str_str, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("get_storage_path_type", 21U, sdl_fn_get_storage_path_type, 2U, p_i64_str, VIGIL_TYPE_I32),
    SDL_FN("get_storage_space_remaining", 27U, sdl_fn_get_storage_space_remaining, 1U, p_i64, VIGIL_TYPE_I64),
    /* Process */
    {"create_process", 14U, sdl_fn_create_process, 2U, p_str_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    SDL_FN_VOID("destroy_process", 15U, sdl_fn_destroy_process, 1U, p_i64),
    SDL_FN_BOOL_ERR("kill_process", 12U, sdl_fn_kill_process, 2U, p_i64_i32),
    SDL_FN("wait_process", 12U, sdl_fn_wait_process, 2U, p_i64_i32, VIGIL_TYPE_I32),
    SDL_FN("read_process", 12U, sdl_fn_read_process, 1U, p_i64, VIGIL_TYPE_I64),
    /* Palette */
    SDL_FN("create_palette", 14U, sdl_fn_create_palette, 1U, p_i32, VIGIL_TYPE_I64),
    SDL_FN_VOID("destroy_palette", 15U, sdl_fn_destroy_palette, 1U, p_i64),
    {"set_palette_colors", 18U, sdl_fn_set_palette_colors, 4U, p_i64_i64_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Tray */
    {"create_tray", 11U, sdl_fn_create_tray, 1U, p_str, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    SDL_FN_VOID("destroy_tray", 12U, sdl_fn_destroy_tray, 1U, p_i64),
    SDL_FN_VOID("set_tray_tooltip", 16U, sdl_fn_set_tray_tooltip, 2U, p_i64_str),
    SDL_FN("create_tray_menu", 16U, sdl_fn_create_tray_menu, 1U, p_i64, VIGIL_TYPE_I64),
    {"insert_tray_entry", 17U, sdl_fn_insert_tray_entry, 4U, p_i64_i32_str_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("remove_tray_entry", 17U, sdl_fn_remove_tray_entry, 1U, p_i64),
    SDL_FN_VOID("set_tray_entry_label", 20U, sdl_fn_set_tray_entry_label, 2U, p_i64_str),
    SDL_FN("get_tray_entry_label", 20U, sdl_fn_get_tray_entry_label, 1U, p_i64, VIGIL_TYPE_STRING),
    SDL_FN_VOID("set_tray_entry_checked", 22U, sdl_fn_set_tray_entry_checked, 2U, p_i64_i32),
    SDL_FN("get_tray_entry_checked", 22U, sdl_fn_get_tray_entry_checked, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("set_tray_entry_enabled", 22U, sdl_fn_set_tray_entry_enabled, 2U, p_i64_i32),
    SDL_FN("get_tray_entry_enabled", 22U, sdl_fn_get_tray_entry_enabled, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("update_trays", 12U, sdl_fn_update_trays, 0U, NULL),
    /* OpenGL */
    SDL_FN_BOOL_ERR("gl_load_library", 15U, sdl_fn_gl_load_library, 1U, p_str),
    SDL_FN_VOID("gl_unload_library", 17U, sdl_fn_gl_unload_library, 0U, NULL),
    SDL_FN("gl_extension_supported", 22U, sdl_fn_gl_extension_supported, 1U, p_str, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("gl_reset_attributes", 19U, sdl_fn_gl_reset_attributes, 0U, NULL),
    SDL_FN_BOOL_ERR("gl_set_attribute", 16U, sdl_fn_gl_set_attribute, 2U, p_i32_i32),
    SDL_FN("gl_get_attribute", 16U, sdl_fn_gl_get_attribute, 1U, p_i32, VIGIL_TYPE_I32),
    {"gl_create_context", 17U, sdl_fn_gl_create_context, 1U, p_i64, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("gl_destroy_context", 18U, sdl_fn_gl_destroy_context, 1U, p_i64),
    SDL_FN_BOOL_ERR("gl_make_current", 15U, sdl_fn_gl_make_current, 2U, p_i64_i64),
    SDL_FN_BOOL_ERR("gl_set_swap_interval", 20U, sdl_fn_gl_set_swap_interval, 1U, p_i32),
    SDL_FN("gl_get_swap_interval", 20U, sdl_fn_gl_get_swap_interval, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN_BOOL_ERR("gl_swap_window", 14U, sdl_fn_gl_swap_window, 1U, p_i64),
    /* Vulkan */
    SDL_FN_BOOL_ERR("vulkan_load_library", 19U, sdl_fn_vulkan_load_library, 1U, p_str),
    SDL_FN_VOID("vulkan_unload_library", 21U, sdl_fn_vulkan_unload_library, 0U, NULL),
    SDL_FN("vulkan_get_instance_extensions", 30U, sdl_fn_vulkan_get_instance_extensions, 0U, NULL, VIGIL_TYPE_STRING),
    /* Metal */
    SDL_FN("metal_create_view", 17U, sdl_fn_metal_create_view, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN_VOID("metal_destroy_view", 18U, sdl_fn_metal_destroy_view, 1U, p_i64),
    /* Remaining rect math */
    {"rect_to_frect", 13U, sdl_fn_rect_to_frect, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_F64, 4U, rt_f64x4, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"rects_equal_float", 17U, sdl_fn_rects_equal_float, 8U, p_f64x8, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"rects_equal_epsilon", 19U, sdl_fn_rects_equal_epsilon, 9U, p_f64x9, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"get_rect_intersection_float", 27U, sdl_fn_get_rect_intersection_float, 8U, p_f64x8, VIGIL_TYPE_F64, 4U, rt_f64x4,
     0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_rect_union_float", 20U, sdl_fn_get_rect_union_float, 8U, p_f64x8, VIGIL_TYPE_F64, 4U, rt_f64x4, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    {"get_rect_and_line_intersection", 30U, sdl_fn_get_rect_and_line_intersection, 8U, p_i32x8, VIGIL_TYPE_BOOL, 5U,
     rt_bool_i32x4, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_rect_and_line_intersection_float", 36U, sdl_fn_get_rect_and_line_intersection_float, 8U, p_f64x8,
     VIGIL_TYPE_BOOL, 5U, rt_bool_f64x4, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Spinlock */
    SDL_FN_VOID("lock_spinlock", 13U, sdl_fn_lock_spinlock, 1U, p_i64),
    SDL_FN("try_lock_spinlock", 17U, sdl_fn_try_lock_spinlock, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("unlock_spinlock", 15U, sdl_fn_unlock_spinlock, 1U, p_i64),
    /* More atomics */
    {"compare_and_swap_atomic_int", 27U, sdl_fn_compare_and_swap_atomic_int, 3U, p_i64_i32_i32, VIGIL_TYPE_BOOL, 1U,
     NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("set_atomic_u32", 14U, sdl_fn_set_atomic_u32, 2U, p_i64_i32, VIGIL_TYPE_I32),
    SDL_FN("get_atomic_u32", 14U, sdl_fn_get_atomic_u32, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("add_atomic_u32", 14U, sdl_fn_add_atomic_u32, 2U, p_i64_i32, VIGIL_TYPE_I32),
    {"compare_and_swap_atomic_u32", 27U, sdl_fn_compare_and_swap_atomic_u32, 3U, p_i64_i32_i32, VIGIL_TYPE_BOOL, 1U,
     NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Misc utility */
    SDL_FN("get_silence_value_for_format", 29U, sdl_fn_get_silence_value_for_format, 1U, p_i32, VIGIL_TYPE_I32),
    {"get_pixel_format_for_masks", 26U, sdl_fn_get_pixel_format_for_masks, 5U, p_i32_i32_i32_i32_i32, VIGIL_TYPE_I32,
     1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"modf", 4U, sdl_fn_modf, 1U, p_f64, VIGIL_TYPE_F64, 2U, rt_f64_f64, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"load_file", 9U, sdl_fn_load_file, 1U, p_str, VIGIL_TYPE_I64, 3U, rt_i64_i64_err, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"save_file", 9U, sdl_fn_save_file, 3U, p_str_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    {"create_environment", 18U, sdl_fn_create_environment, 1U, p_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL, 0U,
     sdl_environment_create_param_names, NULL, NULL, &sdl_doc_create_environment},
    SDL_FN_VOID("destroy_environment", 19U, sdl_fn_destroy_environment, 1U, p_i64),
    {"get_environment_variables", 25U, sdl_fn_get_environment_variables, 1U, p_i64, VIGIL_TYPE_STRING, 1U, NULL, 0,
     NULL, NULL, 0U, sdl_environment_handle_param_names, NULL, NULL, &sdl_doc_get_environment_variables},
    {"get_environment_variable_from", 29U, sdl_fn_get_environment_variable_from, 2U, p_i64_str, VIGIL_TYPE_STRING, 1U,
     NULL, 0, NULL, NULL, 0U, sdl_environment_name_param_names, NULL, NULL, &sdl_doc_get_environment_variable_from},
    {"set_environment_variable_in", 27U, sdl_fn_set_environment_variable_in, 4U, p_i64_str_str_i32, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, sdl_environment_set_param_names, NULL, NULL, &sdl_doc_set_environment_variable_in},
    {"unset_environment_variable_in", 29U, sdl_fn_unset_environment_variable_in, 2U, p_i64_str, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, sdl_environment_name_param_names, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("clear_clipboard_data", 20U, sdl_fn_clear_clipboard_data, 0U, NULL),
    SDL_FN("has_clipboard_data", 18U, sdl_fn_has_clipboard_data, 1U, p_str, VIGIL_TYPE_BOOL),
    {"get_clipboard_data", 18U, sdl_fn_get_clipboard_data, 1U, p_str, VIGIL_TYPE_I64, 3U, rt_i64_i64_err, 0, NULL, NULL,
     0U, sdl_clipboard_mime_param_names, NULL, NULL, &sdl_doc_get_clipboard_data},
    {"set_clipboard_data", 18U, sdl_fn_set_clipboard_data, 3U, p_str_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, sdl_clipboard_set_param_names, NULL, NULL, &sdl_doc_set_clipboard_data},
    {"set_hint_with_priority", 22U, sdl_fn_set_hint_with_priority, 3U, p_str_str_i32, VIGIL_TYPE_BOOL, 1U, NULL, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("set_log_priority_prefix", 23U, sdl_fn_set_log_priority_prefix, 2U, p_i32_str),
    SDL_FN_BOOL_ERR("set_scancode_name", 17U, sdl_fn_set_scancode_name, 2U, p_i32_str),
    SDL_FN("get_display_for_point", 21U, sdl_fn_get_display_for_point, 2U, p_i32_i32, VIGIL_TYPE_I32),
    SDL_FN("get_display_for_rect", 20U, sdl_fn_get_display_for_rect, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_I32),
    {"get_date_time_locale_preferences", 31U, sdl_fn_get_date_time_locale_preferences, 0U, NULL, VIGIL_TYPE_I32, 2U,
     rt_i32_i32, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Tray extras */
    SDL_FN("create_tray_submenu", 19U, sdl_fn_create_tray_submenu, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN("get_tray_menu", 13U, sdl_fn_get_tray_menu, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN("get_tray_submenu", 16U, sdl_fn_get_tray_submenu, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN_VOID("click_tray_entry", 16U, sdl_fn_click_tray_entry, 1U, p_i64),
    /* Haptic effects */
    {"run_haptic_effect", 17U, sdl_fn_run_haptic_effect, 3U, p_i64_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("stop_haptic_effect", 18U, sdl_fn_stop_haptic_effect, 2U, p_i64_i32),
    SDL_FN_VOID("destroy_haptic_effect", 21U, sdl_fn_destroy_haptic_effect, 2U, p_i64_i32),
    SDL_FN("get_haptic_effect_status", 24U, sdl_fn_get_haptic_effect_status, 2U, p_i64_i32, VIGIL_TYPE_BOOL),
    /* Convert pixels */
    {"convert_pixels", 14U, sdl_fn_convert_pixels, 8U, p_convert_pixels, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    /* Gamepad ID queries */
    SDL_FN("get_gamepad_from_id", 19U, sdl_fn_get_gamepad_from_id, 1U, p_i32, VIGIL_TYPE_I64),
    SDL_FN("get_gamepad_from_player_index", 29U, sdl_fn_get_gamepad_from_player_index, 1U, p_i32, VIGIL_TYPE_I64),
    SDL_FN("get_gamepad_id", 14U, sdl_fn_get_gamepad_id, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_properties", 22U, sdl_fn_get_gamepad_properties, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_path_for_id", 23U, sdl_fn_get_gamepad_path_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_gamepad_player_index_for_id", 30U, sdl_fn_get_gamepad_player_index_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_vendor_for_id", 25U, sdl_fn_get_gamepad_vendor_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_product_for_id", 26U, sdl_fn_get_gamepad_product_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_product_version_for_id", 34U, sdl_fn_get_gamepad_product_version_for_id, 1U, p_i32,
           VIGIL_TYPE_I32),
    SDL_FN("get_real_gamepad_type_for_id", 28U, sdl_fn_get_real_gamepad_type_for_id, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_mapping_for_id", 26U, sdl_fn_get_gamepad_mapping_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN_BOOL_ERR("set_gamepad_mapping", 19U, sdl_fn_set_gamepad_mapping, 2U, p_i32_str),
    SDL_FN("get_gamepad_button_label_for_type", 33U, sdl_fn_get_gamepad_button_label_for_type, 2U, p_i32_i32,
           VIGIL_TYPE_I32),
    SDL_FN("get_gamepad_sensor_data_rate", 28U, sdl_fn_get_gamepad_sensor_data_rate, 2U, p_i64_i32, VIGIL_TYPE_F64),
    SDL_FN("get_num_gamepad_touchpad_fingers", 31U, sdl_fn_get_num_gamepad_touchpad_fingers, 2U, p_i64_i32,
           VIGIL_TYPE_I32),
    {"get_gamepad_touchpad_finger", 27U, sdl_fn_get_gamepad_touchpad_finger, 3U, p_i64_i32_i32, VIGIL_TYPE_BOOL, 4U,
     rt_bool_f64x3, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("get_gamepad_apple_sf_symbols_name_for_button", 45U, sdl_fn_get_gamepad_apple_sf_symbols_name_for_button, 2U,
           p_i64_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_gamepad_apple_sf_symbols_name_for_axis", 43U, sdl_fn_get_gamepad_apple_sf_symbols_name_for_axis, 2U,
           p_i64_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_gamepad_joystick", 20U, sdl_fn_get_gamepad_joystick, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN("get_gamepad_guid_for_id", 23U, sdl_fn_get_gamepad_guid_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    /* Joystick ID queries */
    SDL_FN("get_joystick_from_id", 20U, sdl_fn_get_joystick_from_id, 1U, p_i32, VIGIL_TYPE_I64),
    SDL_FN("get_joystick_from_player_index", 30U, sdl_fn_get_joystick_from_player_index, 1U, p_i32, VIGIL_TYPE_I64),
    SDL_FN("get_joystick_properties", 23U, sdl_fn_get_joystick_properties, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_joystick_guid", 18U, sdl_fn_get_joystick_guid, 1U, p_i64, VIGIL_TYPE_STRING),
    SDL_FN("get_joystick_guid_for_id", 23U, sdl_fn_get_joystick_guid_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    {"get_joystick_guid_info", 22U, sdl_fn_get_joystick_guid_info, 1U, p_str, VIGIL_TYPE_I32, 4U, rt_i32x4, 0, NULL,
     NULL, 0U, sdl_guid_param_names, NULL, NULL, &sdl_doc_get_joystick_guid_info},
    {"get_joystick_axis_initial_state", 30U, sdl_fn_get_joystick_axis_initial_state, 2U, p_i64_i32, VIGIL_TYPE_BOOL, 2U,
     rt_bool_i32, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Haptic */
    SDL_FN("get_haptic_id", 13U, sdl_fn_get_haptic_id, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_haptic_from_id", 18U, sdl_fn_get_haptic_from_id, 1U, p_i32, VIGIL_TYPE_I64),
    {"open_haptic_from_joystick", 25U, sdl_fn_open_haptic_from_joystick, 1U, p_i64, VIGIL_TYPE_I64, 2U, rt_i64_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Sensor */
    SDL_FN("get_sensor_id", 13U, sdl_fn_get_sensor_id, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_sensor_properties", 21U, sdl_fn_get_sensor_properties, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_sensor_from_id", 18U, sdl_fn_get_sensor_from_id, 1U, p_i32, VIGIL_TYPE_I64),
    /* Camera */
    SDL_FN("get_camera_id", 13U, sdl_fn_get_camera_id, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_camera_properties", 21U, sdl_fn_get_camera_properties, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_camera_position", 19U, sdl_fn_get_camera_position, 1U, p_i32, VIGIL_TYPE_I32),
    /* Window/Renderer */
    SDL_FN("get_window_from_id", 18U, sdl_fn_get_window_from_id, 1U, p_i32, VIGIL_TYPE_I64),
    SDL_FN("get_window_parent", 17U, sdl_fn_get_window_parent, 1U, p_i64, VIGIL_TYPE_I64),
    {"get_window_icc_profile", 22U, sdl_fn_get_window_icc_profile, 1U, p_i64, VIGIL_TYPE_I64, 2U, rt_i64_i64, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("get_display_properties", 22U, sdl_fn_get_display_properties, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("get_keyboard_focus", 18U, sdl_fn_get_keyboard_focus, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN("get_keyboard_name_for_id", 24U, sdl_fn_get_keyboard_name_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_mouse_name_for_id", 21U, sdl_fn_get_mouse_name_for_id, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_io_properties", 17U, sdl_fn_get_io_properties, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_gpu_device_properties", 25U, sdl_fn_get_gpu_device_properties, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_audio_stream_properties", 27U, sdl_fn_get_audio_stream_properties, 1U, p_i64, VIGIL_TYPE_I32),
    /* Audio format */
    {"get_audio_device_format", 23U, sdl_fn_get_audio_device_format, 1U, p_i32, VIGIL_TYPE_I32, 4U, rt_i32x4, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_audio_stream_format", 23U, sdl_fn_get_audio_stream_format, 1U, p_i64, VIGIL_TYPE_I32, 6U, rt_i32x6, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"set_audio_stream_format", 23U, sdl_fn_set_audio_stream_format, 7U, p_i64_i32x6, VIGIL_TYPE_BOOL, 2U, rt_bool_err,
     0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Misc */
    SDL_FN("guid_to_string", 14U, sdl_fn_guid_to_string, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN("string_to_guid", 14U, sdl_fn_string_to_guid, 1U, p_str, VIGIL_TYPE_STRING),
    {"modff", 5U, sdl_fn_modff, 1U, p_f64, VIGIL_TYPE_F64, 2U, rt_f64_f64, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_masks_for_pixel_format", 26U, sdl_fn_get_masks_for_pixel_format, 1U, p_i32, VIGIL_TYPE_I32, 5U, rt_i32x5, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN("surface_has_alternate_images", 28U, sdl_fn_surface_has_alternate_images, 1U, p_i64, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("remove_surface_alternate_images", 31U, sdl_fn_remove_surface_alternate_images, 1U, p_i64),
    SDL_FN_VOID("cleanup_tls", 11U, sdl_fn_cleanup_tls, 0U, NULL),
    {"set_environment_variable", 24U, sdl_fn_set_environment_variable, 3U, p_str_str_i32, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("unset_environment_variable", 25U, sdl_fn_unset_environment_variable, 1U, p_str),
    SDL_FN("get_environment_variable", 24U, sdl_fn_get_environment_variable, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN("gl_get_current_window", 21U, sdl_fn_gl_get_current_window, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN("gl_get_current_context", 22U, sdl_fn_gl_get_current_context, 0U, NULL, VIGIL_TYPE_I64),
    SDL_FN("metal_get_layer", 15U, sdl_fn_metal_get_layer, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN("get_process_properties", 22U, sdl_fn_get_process_properties, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN("get_clipboard_mime_types", 24U, sdl_fn_get_clipboard_mime_types, 0U, NULL, VIGIL_TYPE_STRING),
    SDL_FN_VOID("set_tray_icon", 13U, sdl_fn_set_tray_icon, 2U, p_i64_i64),
    SDL_FN("get_tray_entry_parent", 21U, sdl_fn_get_tray_entry_parent, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN("get_tray_menu_parent_entry", 25U, sdl_fn_get_tray_menu_parent_entry, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN("get_tray_menu_parent_tray", 25U, sdl_fn_get_tray_menu_parent_tray, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN_BOOL_ERR("set_surface_palette", 19U, sdl_fn_set_surface_palette, 2U, p_i64_i64),
    SDL_FN_BOOL_ERR("set_texture_palette", 19U, sdl_fn_set_texture_palette, 2U, p_i64_i64),
    SDL_FN("create_surface_palette", 22U, sdl_fn_create_surface_palette, 1U, p_i64, VIGIL_TYPE_I64),
    {"calculate_gpu_texture_format_size", 33U, sdl_fn_calculate_gpu_texture_format_size, 4U, p_i32_i32_i32_i32,
     VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Audio stream creation */
    {"create_audio_stream", 19U, sdl_fn_create_audio_stream, 6U, p_i32x6, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    /* Mix audio */
    {"mix_audio", 9U, sdl_fn_mix_audio, 5U, p_i64_i64_i32_i32_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    /* Premultiply alpha */
    {"premultiply_alpha", 17U, sdl_fn_premultiply_alpha, 9U, p_premultiply, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    /* Pixel color mapping */
    SDL_FN("map_rgb", 7U, sdl_fn_map_rgb, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_I32),
    {"map_rgba", 8U, sdl_fn_map_rgba, 5U, p_i32_i32_i32_i32_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    {"get_rgb", 7U, sdl_fn_get_rgb, 2U, p_i32_i32, VIGIL_TYPE_I32, 3U, rt_i32x3, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"get_rgba", 8U, sdl_fn_get_rgba, 2U, p_i32_i32, VIGIL_TYPE_I32, 4U, rt_i32x4, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    /* Sensor data */
    {"get_sensor_data", 15U, sdl_fn_get_sensor_data, 2U, p_i64_i32, VIGIL_TYPE_F64, 3U, rt_f64x3, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"get_gamepad_sensor_data", 23U, sdl_fn_get_gamepad_sensor_data, 2U, p_i64_i32, VIGIL_TYPE_F64, 3U, rt_f64x3, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Text input area */
    {"set_text_input_area", 19U, sdl_fn_set_text_input_area, 6U, p_i64_i32_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"get_text_input_area", 19U, sdl_fn_get_text_input_area, 1U, p_i64, VIGIL_TYPE_I32, 5U, rt_i32x5, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    /* Joystick virtual */
    {"set_joystick_virtual_ball", 25U, sdl_fn_set_joystick_virtual_ball, 4U, p_i64_i32_i32_i32, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"set_joystick_virtual_touchpad", 29U, sdl_fn_set_joystick_virtual_touchpad, 7U, p_vtouch, VIGIL_TYPE_BOOL, 2U,
     rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Window surface */
    SDL_FN("get_window_surface", 18U, sdl_fn_get_window_surface, 1U, p_i64, VIGIL_TYPE_I64),
    {"get_window_mouse_rect", 21U, sdl_fn_get_window_mouse_rect, 1U, p_i64, VIGIL_TYPE_I32, 4U, rt_i32x4, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    /* Surface blits */
    {"blit_surface_unchecked_scaled", 28U, sdl_fn_blit_surface_unchecked_scaled, 11U, p_blit_uscaled, VIGIL_TYPE_BOOL,
     2U, rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"blit_surface_tiled_with_scale", 29U, sdl_fn_blit_surface_tiled_with_scale, 12U, p_blit_tscaled, VIGIL_TYPE_BOOL,
     2U, rt_bool_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"blit_surface_9grid", 18U, sdl_fn_blit_surface_9grid, 16U, p_blit_9grid, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    /* C stdlib wrappers — buffer ops */
    SDL_FN_VOID("memcpy", 6U, sdl_fn_memcpy, 3U, p_i64_i64_i32),
    SDL_FN_VOID("memmove", 7U, sdl_fn_memmove, 3U, p_i64_i64_i32),
    SDL_FN_VOID("memset", 6U, sdl_fn_memset, 3U, p_i64_i32_i32),
    SDL_FN_VOID("memset4", 7U, sdl_fn_memset4, 3U, p_i64_i32_i32),
    SDL_FN("memcmp", 6U, sdl_fn_memcmp, 3U, p_i64_i64_i32, VIGIL_TYPE_I32),
    /* UTF-8 */
    SDL_FN("ucs4_to_utf8", 12U, sdl_fn_ucs4_to_utf8, 1U, p_i32, VIGIL_TYPE_STRING),
    {"step_utf8", 9U, sdl_fn_step_utf8, 2U, p_str_i32, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    /* iconv */
    {"iconv_string", 12U, sdl_fn_iconv_string, 3U, p_str_str_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    /* Seeded random */
    {"rand_r", 6U, sdl_fn_rand_r, 2U, p_i64_i32, VIGIL_TYPE_I32, 2U, rt_i32_i64, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"randf_r", 7U, sdl_fn_randf_r, 1U, p_i64, VIGIL_TYPE_F64, 2U, rt_f64_i64, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"rand_bits_r", 11U, sdl_fn_rand_bits_r, 1U, p_i64, VIGIL_TYPE_I32, 2U, rt_i32_i64, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    /* Overflow checks */
    {"size_add_check_overflow", 23U, sdl_fn_size_add_check_overflow, 2U, p_i64_i64, VIGIL_TYPE_I64, 2U, rt_i64_bool, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"size_mul_check_overflow", 23U, sdl_fn_size_mul_check_overflow, 2U, p_i64_i64, VIGIL_TYPE_I64, 2U, rt_i64_bool, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* IO constants */
    SDL_CONST_ENTRY("IO_SEEK_SET", IO_SEEK_SET),
    SDL_CONST_ENTRY("IO_SEEK_CUR", IO_SEEK_CUR),
    SDL_CONST_ENTRY("IO_SEEK_END", IO_SEEK_END),
    SDL_CONST_ENTRY("IO_STATUS_READY", IO_STATUS_READY),
    SDL_CONST_ENTRY("IO_STATUS_ERROR", IO_STATUS_ERROR),
    SDL_CONST_ENTRY("IO_STATUS_EOF", IO_STATUS_EOF),
    /* Misc complete */
    SDL_FN("has_sse", 7U, sdl_fn_has_sse, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_sse2", 8U, sdl_fn_has_sse2, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_sse3", 8U, sdl_fn_has_sse3, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_sse41", 9U, sdl_fn_has_sse41, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_sse42", 9U, sdl_fn_has_sse42, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_avx", 7U, sdl_fn_has_avx, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_avx2", 8U, sdl_fn_has_avx2, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_avx512f", 11U, sdl_fn_has_avx512f, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_neon", 8U, sdl_fn_has_neon, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_mmx", 7U, sdl_fn_has_mmx, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_altivec", 11U, sdl_fn_has_altivec, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_armsimd", 11U, sdl_fn_has_armsimd, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_lsx", 7U, sdl_fn_has_lsx, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("has_lasx", 8U, sdl_fn_has_lasx, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN("get_cpu_cache_line_size", 23U, sdl_fn_get_cpu_cache_line_size, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_num_video_drivers", 21U, sdl_fn_get_num_video_drivers, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_video_driver", 16U, sdl_fn_get_video_driver, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("get_sandbox", 11U, sdl_fn_get_sandbox, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("time_to_datetime", 16U, sdl_fn_time_to_datetime, 1U, p_i64, VIGIL_TYPE_STRING),
    {"get_day_of_week", 15U, sdl_fn_get_day_of_week, 3U, p_i32_i32_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"get_day_of_year", 15U, sdl_fn_get_day_of_year, 3U, p_i32_i32_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    SDL_FN("get_days_in_month", 17U, sdl_fn_get_days_in_month, 2U, p_i32_i32, VIGIL_TYPE_I32),
    SDL_FN_BOOL_ERR("set_primary_selection_text", 25U, sdl_fn_set_primary_selection_text, 1U, p_str),
    SDL_FN("get_primary_selection_text", 25U, sdl_fn_get_primary_selection_text, 0U, NULL, VIGIL_TYPE_STRING),
    SDL_FN("has_primary_selection_text", 25U, sdl_fn_has_primary_selection_text, 0U, NULL, VIGIL_TYPE_BOOL),
    {"compose_custom_blend_mode", 25U, sdl_fn_compose_custom_blend_mode, 6U, p_i32_i32_i32_i32_i32_i32, VIGIL_TYPE_I32,
     1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    /* Event complete */
    SDL_FN("has_events", 10U, sdl_fn_has_events, 2U, p_i32_i32, VIGIL_TYPE_BOOL),
    SDL_FN("register_events", 15U, sdl_fn_register_events, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("gamepad_events_enabled", 22U, sdl_fn_gamepad_events_enabled, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("set_gamepad_events_enabled", 26U, sdl_fn_set_gamepad_events_enabled, 1U, p_i32),
    SDL_FN("joystick_events_enabled", 23U, sdl_fn_joystick_events_enabled, 0U, NULL, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("set_joystick_events_enabled", 27U, sdl_fn_set_joystick_events_enabled, 1U, p_i32),
    /* Address mode constants */
    SDL_CONST_ENTRY("TEXTURE_ADDRESS_AUTO", TEXTURE_ADDRESS_AUTO),
    SDL_CONST_ENTRY("TEXTURE_ADDRESS_CLAMP", TEXTURE_ADDRESS_CLAMP),
    SDL_CONST_ENTRY("TEXTURE_ADDRESS_WRAP", TEXTURE_ADDRESS_WRAP),
    /* Window remaining - module functions */
    {"time_from_windows", 18U, sdl_fn_time_from_windows, 2U, p_i32_i32, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"time_to_windows", 16U, sdl_fn_time_to_windows, 1U, p_i64, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    /* HitTest constants */
    SDL_CONST_ENTRY("HITTEST_NORMAL", HITTEST_NORMAL),
    SDL_CONST_ENTRY("HITTEST_DRAGGABLE", HITTEST_DRAGGABLE),
    SDL_CONST_ENTRY("HITTEST_RESIZE_TOPLEFT", HITTEST_RESIZE_TOPLEFT),
    SDL_CONST_ENTRY("HITTEST_RESIZE_TOP", HITTEST_RESIZE_TOP),
    SDL_CONST_ENTRY("HITTEST_RESIZE_TOPRIGHT", HITTEST_RESIZE_TOPRIGHT),
    SDL_CONST_ENTRY("HITTEST_RESIZE_RIGHT", HITTEST_RESIZE_RIGHT),
    SDL_CONST_ENTRY("HITTEST_RESIZE_BOTTOMRIGHT", HITTEST_RESIZE_BOTTOMRIGHT),
    SDL_CONST_ENTRY("HITTEST_RESIZE_BOTTOM", HITTEST_RESIZE_BOTTOM),
    SDL_CONST_ENTRY("HITTEST_RESIZE_BOTTOMLEFT", HITTEST_RESIZE_BOTTOMLEFT),
    SDL_CONST_ENTRY("HITTEST_RESIZE_LEFT", HITTEST_RESIZE_LEFT),
    /* GPU API */
    {"gpu_create_device", 17U, sdl_fn_gpu_create_device, 2U, p_i32_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, NULL,
     0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_destroy_device", 17U, sdl_fn_gpu_destroy_device, 1U, p_i64),
    SDL_FN("gpu_get_driver", 14U, sdl_fn_gpu_get_driver, 1U, p_i64, VIGIL_TYPE_STRING),
    SDL_FN("gpu_get_shader_formats", 22U, sdl_fn_gpu_get_shader_formats, 1U, p_i64, VIGIL_TYPE_I32),
    SDL_FN_BOOL_ERR("gpu_claim_window", 16U, sdl_fn_gpu_claim_window, 2U, p_i64_obj),
    SDL_FN_VOID("gpu_release_window", 18U, sdl_fn_gpu_release_window, 2U, p_i64_obj),
    SDL_FN_BOOL_ERR("gpu_wait_idle", 13U, sdl_fn_gpu_wait_idle, 1U, p_i64),
    SDL_FN("gpu_get_swapchain_format", 24U, sdl_fn_gpu_get_swapchain_format, 2U, p_i64_obj, VIGIL_TYPE_I32),
    {"gpu_create_shader", 17U, sdl_fn_gpu_create_shader, 7U, p_i64_i64_i32_i32_i32_i32_i32, VIGIL_TYPE_I64, 2U,
     rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_release_shader", 17U, sdl_fn_gpu_release_shader, 2U, p_i64_i64),
    {"gpu_create_pipeline", 19U, sdl_fn_gpu_create_pipeline, 7U, p_i64_i64_i32_i32_i32_i32_i32, VIGIL_TYPE_I64, 2U,
     rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_release_pipeline", 19U, sdl_fn_gpu_release_pipeline, 2U, p_i64_i64),
    {"gpu_create_buffer", 17U, sdl_fn_gpu_create_buffer, 3U, p_i64_i32_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_release_buffer", 17U, sdl_fn_gpu_release_buffer, 2U, p_i64_i64),
    {"gpu_create_xfer_buffer", 22U, sdl_fn_gpu_create_xfer_buffer, 3U, p_i64_i32_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_release_xfer_buffer", 22U, sdl_fn_gpu_release_xfer_buffer, 2U, p_i64_i64),
    SDL_FN("gpu_map_xfer", 12U, sdl_fn_gpu_map_xfer, 2U, p_i64_i64, VIGIL_TYPE_I64),
    SDL_FN_VOID("gpu_unmap_xfer", 14U, sdl_fn_gpu_unmap_xfer, 2U, p_i64_i64),
    SDL_FN("gpu_acquire_cmd", 15U, sdl_fn_gpu_acquire_cmd, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN_BOOL_ERR("gpu_submit_cmd", 14U, sdl_fn_gpu_submit_cmd, 1U, p_i64),
    SDL_FN_BOOL_ERR("gpu_cancel_cmd", 14U, sdl_fn_gpu_cancel_cmd, 1U, p_i64),
    {"gpu_acquire_swapchain", 21U, sdl_fn_gpu_acquire_swapchain, 2U, p_i64_obj, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL,
     NULL, 0U, NULL, NULL, NULL, NULL},
    {"gpu_begin_render_pass", 21U, sdl_fn_gpu_begin_render_pass, 8U, p_i64_i64_f64_f64_f64_f64_i32_i32, VIGIL_TYPE_I64,
     1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_end_render_pass", 19U, sdl_fn_gpu_end_render_pass, 1U, p_i64),
    SDL_FN_VOID("gpu_bind_pipeline", 17U, sdl_fn_gpu_bind_pipeline, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_bind_vertex_buffers", 22U, sdl_fn_gpu_bind_vertex_buffers, 3U, p_i64_i64_i32),
    SDL_FN_VOID("gpu_draw_primitives", 19U, sdl_fn_gpu_draw_primitives, 5U, p_i64_i32_i32_i32_i32),
    SDL_FN("gpu_begin_copy_pass", 19U, sdl_fn_gpu_begin_copy_pass, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN_VOID("gpu_end_copy_pass", 17U, sdl_fn_gpu_end_copy_pass, 1U, p_i64),
    SDL_FN_VOID("gpu_upload_to_buffer", 20U, sdl_fn_gpu_upload_to_buffer, 6U, p_i64_i64_i32_i64_i32_i32),
    /* GPU constants */
    SDL_CONST_ENTRY("GPU_SHADERFORMAT_SPIRV", GPU_SHADERFORMAT_SPIRV),
    SDL_CONST_ENTRY("GPU_SHADERFORMAT_MSL", GPU_SHADERFORMAT_MSL),
    SDL_CONST_ENTRY("GPU_SHADERFORMAT_METALLIB", GPU_SHADERFORMAT_METALLIB),
    SDL_CONST_ENTRY("GPU_SHADERSTAGE_VERTEX", GPU_SHADERSTAGE_VERTEX),
    SDL_CONST_ENTRY("GPU_SHADERSTAGE_FRAGMENT", GPU_SHADERSTAGE_FRAGMENT),
    SDL_CONST_ENTRY("GPU_PRIMITIVETYPE_TRIANGLELIST", GPU_PRIMITIVETYPE_TRIANGLELIST),
    SDL_CONST_ENTRY("GPU_PRIMITIVETYPE_TRIANGLESTRIP", GPU_PRIMITIVETYPE_TRIANGLESTRIP),
    SDL_CONST_ENTRY("GPU_PRIMITIVETYPE_LINELIST", GPU_PRIMITIVETYPE_LINELIST),
    SDL_CONST_ENTRY("GPU_PRIMITIVETYPE_LINESTRIP", GPU_PRIMITIVETYPE_LINESTRIP),
    SDL_CONST_ENTRY("GPU_PRIMITIVETYPE_POINTLIST", GPU_PRIMITIVETYPE_POINTLIST),
    SDL_CONST_ENTRY("GPU_LOADOP_LOAD", GPU_LOADOP_LOAD),
    SDL_CONST_ENTRY("GPU_LOADOP_CLEAR", GPU_LOADOP_CLEAR),
    SDL_CONST_ENTRY("GPU_LOADOP_DONT_CARE", GPU_LOADOP_DONT_CARE),
    SDL_CONST_ENTRY("GPU_STOREOP_STORE", GPU_STOREOP_STORE),
    SDL_CONST_ENTRY("GPU_STOREOP_DONT_CARE", GPU_STOREOP_DONT_CARE),
    SDL_CONST_ENTRY("GPU_BUFFERUSAGE_VERTEX", GPU_BUFFERUSAGE_VERTEX),
    SDL_CONST_ENTRY("GPU_BUFFERUSAGE_INDEX", GPU_BUFFERUSAGE_INDEX),
    SDL_CONST_ENTRY("GPU_XFER_UPLOAD", GPU_XFER_UPLOAD),
    SDL_CONST_ENTRY("GPU_XFER_DOWNLOAD", GPU_XFER_DOWNLOAD),
    SDL_CONST_ENTRY("GPU_VERTEXFORMAT_FLOAT2", GPU_VERTEXFORMAT_FLOAT2),
    SDL_CONST_ENTRY("GPU_VERTEXFORMAT_FLOAT3", GPU_VERTEXFORMAT_FLOAT3),
    SDL_CONST_ENTRY("GPU_VERTEXFORMAT_FLOAT4", GPU_VERTEXFORMAT_FLOAT4),
    SDL_CONST_ENTRY("GPU_VERTEXFORMAT_UBYTE4_NORM", GPU_VERTEXFORMAT_UBYTE4_NORM),
    /* GPU Phase 2 */
    {"gpu_create_texture", 18U, sdl_fn_gpu_create_texture, 9U, p_i64_i32_i32_i32_i32_i32_i32_i32_i32, VIGIL_TYPE_I64,
     2U, rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_release_texture", 18U, sdl_fn_gpu_release_texture, 2U, p_i64_i64),
    {"gpu_create_sampler", 18U, sdl_fn_gpu_create_sampler, 4U, p_i64_i32_i32_i32, VIGIL_TYPE_I64, 2U, rt_i64_err, 0,
     NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_release_sampler", 18U, sdl_fn_gpu_release_sampler, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_bind_index_buffer", 20U, sdl_fn_gpu_bind_index_buffer, 4U, p_i64_i64_i32_i32),
    SDL_FN_VOID("gpu_draw_indexed", 16U, sdl_fn_gpu_draw_indexed, 6U, p_i64_i32_i32_i32_i32_i32),
    SDL_FN_VOID("gpu_bind_fragment_samplers", 25U, sdl_fn_gpu_bind_fragment_samplers, 3U, p_i64_i64_i64),
    SDL_FN_VOID("gpu_push_vertex_uniforms", 24U, sdl_fn_gpu_push_vertex_uniforms, 3U, p_i64_i32_i64),
    SDL_FN_VOID("gpu_push_fragment_uniforms", 26U, sdl_fn_gpu_push_fragment_uniforms, 3U, p_i64_i32_i64),
    SDL_FN_VOID("gpu_set_viewport", 16U, sdl_fn_gpu_set_viewport, 7U, p_i64_f64_f64_f64_f64_f64_f64),
    SDL_FN_VOID("gpu_set_scissor", 15U, sdl_fn_gpu_set_scissor, 5U, p_i64_i32_i32_i32_i32),
    SDL_FN_VOID("gpu_debug_label", 15U, sdl_fn_gpu_debug_label, 2U, p_i64_str),
    SDL_FN_VOID("gpu_generate_mipmaps", 20U, sdl_fn_gpu_generate_mipmaps, 2U, p_i64_i64),
    SDL_FN_BOOL_ERR("gpu_set_swapchain_params", 23U, sdl_fn_gpu_set_swapchain_params, 4U, p_i64_obj_i32_i32),
    /* stdinc: math */
    SDL_FN("m_acos", 6U, sdl_fn_m_acos, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_acosf", 7U, sdl_fn_m_acosf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_asin", 6U, sdl_fn_m_asin, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_asinf", 7U, sdl_fn_m_asinf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_atan", 6U, sdl_fn_m_atan, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_atanf", 7U, sdl_fn_m_atanf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_atan2", 7U, sdl_fn_m_atan2, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_atan2f", 8U, sdl_fn_m_atan2f, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_ceil", 6U, sdl_fn_m_ceil, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_ceilf", 7U, sdl_fn_m_ceilf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_cos", 5U, sdl_fn_m_cos, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_cosf", 6U, sdl_fn_m_cosf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_exp", 5U, sdl_fn_m_exp, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_expf", 6U, sdl_fn_m_expf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_fabs", 6U, sdl_fn_m_fabs, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_fabsf", 7U, sdl_fn_m_fabsf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_floor", 7U, sdl_fn_m_floor, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_floorf", 8U, sdl_fn_m_floorf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_fmod", 6U, sdl_fn_m_fmod, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_fmodf", 7U, sdl_fn_m_fmodf, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_log", 5U, sdl_fn_m_log, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_logf", 6U, sdl_fn_m_logf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_log10", 7U, sdl_fn_m_log10, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_log10f", 8U, sdl_fn_m_log10f, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_pow", 5U, sdl_fn_m_pow, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_powf", 6U, sdl_fn_m_powf, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_round", 7U, sdl_fn_m_round, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_roundf", 8U, sdl_fn_m_roundf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_sin", 5U, sdl_fn_m_sin, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_sinf", 6U, sdl_fn_m_sinf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_sqrt", 6U, sdl_fn_m_sqrt, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_sqrtf", 7U, sdl_fn_m_sqrtf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_tan", 5U, sdl_fn_m_tan, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_tanf", 6U, sdl_fn_m_tanf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_trunc", 7U, sdl_fn_m_trunc, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_truncf", 8U, sdl_fn_m_truncf, 1U, p_f64, VIGIL_TYPE_F64),
    SDL_FN("m_copysign", 10U, sdl_fn_m_copysign, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_copysignf", 11U, sdl_fn_m_copysignf, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_scalbn", 8U, sdl_fn_m_scalbn, 2U, p_f64_f64, VIGIL_TYPE_F64),
    SDL_FN("m_lround", 8U, sdl_fn_m_lround, 1U, p_f64, VIGIL_TYPE_I64),
    SDL_FN("m_lroundf", 9U, sdl_fn_m_lroundf, 1U, p_f64, VIGIL_TYPE_I64),
    SDL_FN("m_isinf", 7U, sdl_fn_m_isinf, 1U, p_f64, VIGIL_TYPE_BOOL),
    SDL_FN("m_isnan", 7U, sdl_fn_m_isnan, 1U, p_f64, VIGIL_TYPE_BOOL),
    SDL_FN("m_abs", 5U, sdl_fn_m_abs, 1U, p_i32, VIGIL_TYPE_I32),
    /* stdinc: char */
    SDL_FN("c_isalnum", 9U, sdl_fn_c_isalnum, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_isalpha", 9U, sdl_fn_c_isalpha, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_isblank", 9U, sdl_fn_c_isblank, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_iscntrl", 9U, sdl_fn_c_iscntrl, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_isdigit", 9U, sdl_fn_c_isdigit, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_isgraph", 9U, sdl_fn_c_isgraph, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_islower", 9U, sdl_fn_c_islower, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_isprint", 9U, sdl_fn_c_isprint, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_ispunct", 9U, sdl_fn_c_ispunct, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_isspace", 9U, sdl_fn_c_isspace, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_isupper", 9U, sdl_fn_c_isupper, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_isxdigit", 10U, sdl_fn_c_isxdigit, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("c_toupper", 9U, sdl_fn_c_toupper, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("c_tolower", 9U, sdl_fn_c_tolower, 1U, p_i32, VIGIL_TYPE_I32),
    /* stdinc: string */
    SDL_FN("s_strlen", 8U, sdl_fn_s_strlen, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("s_strcmp", 8U, sdl_fn_s_strcmp, 2U, p_str_str, VIGIL_TYPE_I32),
    {"s_strncmp", 9U, sdl_fn_s_strncmp, 3U, p_str_str_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    SDL_FN("s_strcasecmp", 12U, sdl_fn_s_strcasecmp, 2U, p_str_str, VIGIL_TYPE_I32),
    {"s_strncasecmp", 13U, sdl_fn_s_strncasecmp, 3U, p_str_str_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    SDL_FN("s_strstr", 8U, sdl_fn_s_strstr, 2U, p_str_str, VIGIL_TYPE_I32),
    SDL_FN("s_strchr", 8U, sdl_fn_s_strchr, 2U, p_str_i32, VIGIL_TYPE_I32),
    SDL_FN("s_strrchr", 9U, sdl_fn_s_strrchr, 2U, p_str_i32, VIGIL_TYPE_I32),
    SDL_FN("s_strupr", 8U, sdl_fn_s_strupr, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN("s_strlwr", 8U, sdl_fn_s_strlwr, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN("s_strrev", 8U, sdl_fn_s_strrev, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN("s_atoi", 6U, sdl_fn_s_atoi, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("s_atof", 6U, sdl_fn_s_atof, 1U, p_str, VIGIL_TYPE_F64),
    SDL_FN("s_strtol", 8U, sdl_fn_s_strtol, 2U, p_str_i32, VIGIL_TYPE_I64),
    SDL_FN("s_strtoul", 9U, sdl_fn_s_strtoul, 2U, p_str_i32, VIGIL_TYPE_I64),
    SDL_FN("s_strtod", 8U, sdl_fn_s_strtod, 1U, p_str, VIGIL_TYPE_F64),
    SDL_FN("s_itoa", 6U, sdl_fn_s_itoa, 2U, p_i32_i32, VIGIL_TYPE_STRING),
    SDL_FN("s_lltoa", 7U, sdl_fn_s_lltoa, 2U, p_i64_i32, VIGIL_TYPE_STRING),
    SDL_FN("s_utf8strlen", 12U, sdl_fn_s_utf8strlen, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("s_utf8strnlen", 13U, sdl_fn_s_utf8strnlen, 2U, p_str_i32, VIGIL_TYPE_I32),
    /* stdinc: random/hash/bit/env */
    SDL_FN_VOID("r_srand", 7U, sdl_fn_r_srand, 1U, p_i64),
    SDL_FN("r_rand", 6U, sdl_fn_r_rand, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("r_randf", 7U, sdl_fn_r_randf, 0U, NULL, VIGIL_TYPE_F64),
    SDL_FN("r_rand_bits", 11U, sdl_fn_r_rand_bits, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("r_crc16", 7U, sdl_fn_r_crc16, 2U, p_i64_i32, VIGIL_TYPE_I32),
    SDL_FN("r_crc32", 7U, sdl_fn_r_crc32, 2U, p_i64_i32, VIGIL_TYPE_I32),
    SDL_FN("r_murmur3_32", 12U, sdl_fn_r_murmur3_32, 2U, p_i64_i32, VIGIL_TYPE_I32),
    SDL_FN("r_msb32", 7U, sdl_fn_r_msb32, 1U, p_i32, VIGIL_TYPE_I32),
    SDL_FN("r_has_one_bit32", 15U, sdl_fn_r_has_one_bit32, 1U, p_i32, VIGIL_TYPE_BOOL),
    SDL_FN("e_getenv", 8U, sdl_fn_e_getenv, 1U, p_str, VIGIL_TYPE_STRING),
    SDL_FN("e_setenv", 8U, sdl_fn_e_setenv, 2U, p_str_str, VIGIL_TYPE_I32),
    SDL_FN("e_unsetenv", 10U, sdl_fn_e_unsetenv, 1U, p_str, VIGIL_TYPE_I32),
    SDL_FN("s_wcslen", 8U, sdl_fn_s_wcslen, 1U, p_str, VIGIL_TYPE_I32),
    /* GPU Phase 2 constants */
    SDL_CONST_ENTRY("GPU_TEXTUREUSAGE_SAMPLER", GPU_TEXTUREUSAGE_SAMPLER),
    SDL_CONST_ENTRY("GPU_TEXTUREUSAGE_COLOR_TARGET", GPU_TEXTUREUSAGE_COLOR_TARGET),
    SDL_CONST_ENTRY("GPU_TEXTURETYPE_2D", GPU_TEXTURETYPE_2D),
    SDL_CONST_ENTRY("GPU_SAMPLECOUNT_1", GPU_SAMPLECOUNT_1),
    SDL_CONST_ENTRY("GPU_SAMPLECOUNT_4", GPU_SAMPLECOUNT_4),
    SDL_CONST_ENTRY("GPU_FILTER_NEAREST", GPU_FILTER_NEAREST),
    SDL_CONST_ENTRY("GPU_FILTER_LINEAR", GPU_FILTER_LINEAR),
    SDL_CONST_ENTRY("GPU_SAMPLERADDRESSMODE_REPEAT", GPU_SAMPLERADDRESSMODE_REPEAT),
    SDL_CONST_ENTRY("GPU_SAMPLERADDRESSMODE_CLAMP", GPU_SAMPLERADDRESSMODE_CLAMP),
    SDL_CONST_ENTRY("GPU_INDEXELEMENTSIZE_16", GPU_INDEXELEMENTSIZE_16),
    SDL_CONST_ENTRY("GPU_INDEXELEMENTSIZE_32", GPU_INDEXELEMENTSIZE_32),
    SDL_CONST_ENTRY("GPU_PRESENTMODE_VSYNC", GPU_PRESENTMODE_VSYNC),
    SDL_CONST_ENTRY("GPU_PRESENTMODE_IMMEDIATE", GPU_PRESENTMODE_IMMEDIATE),
    SDL_CONST_ENTRY("GPU_PRESENTMODE_MAILBOX", GPU_PRESENTMODE_MAILBOX),
    SDL_CONST_ENTRY("GPU_SWAPCHAIN_SDR", GPU_SWAPCHAIN_SDR),
    /* GPU Phase 3 */
    SDL_FN("gpu_num_drivers", 15U, sdl_fn_gpu_num_drivers, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("gpu_get_driver_name", 19U, sdl_fn_gpu_get_driver_name, 1U, p_i32, VIGIL_TYPE_STRING),
    SDL_FN("gpu_supports_shader_formats", 27U, sdl_fn_gpu_supports_shader_formats, 1U, p_i32, VIGIL_TYPE_BOOL),
    {"gpu_texture_supports_format", 27U, sdl_fn_gpu_texture_supports_format, 4U, p_i64_i32_i32_i32, VIGIL_TYPE_BOOL, 1U,
     NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"gpu_texture_supports_sample_count", 33U, sdl_fn_gpu_texture_supports_sample_count, 3U, p_i64_i32_i32,
     VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_BOOL_ERR("gpu_set_allowed_frames", 22U, sdl_fn_gpu_set_allowed_frames, 2U, p_i64_i32),
    SDL_FN_VOID("gpu_set_buffer_name", 19U, sdl_fn_gpu_set_buffer_name, 3U, p_i64_i64_str),
    SDL_FN_VOID("gpu_set_texture_name", 20U, sdl_fn_gpu_set_texture_name, 3U, p_i64_i64_str),
    SDL_FN_VOID("gpu_push_debug_group", 20U, sdl_fn_gpu_push_debug_group, 2U, p_i64_str),
    SDL_FN_VOID("gpu_pop_debug_group", 19U, sdl_fn_gpu_pop_debug_group, 1U, p_i64),
    SDL_FN_VOID("gpu_set_blend_constants", 23U, sdl_fn_gpu_set_blend_constants, 5U, p_i64_f64_f64_f64_f64),
    SDL_FN_VOID("gpu_set_stencil_ref", 19U, sdl_fn_gpu_set_stencil_ref, 2U, p_i64_i32),
    SDL_FN_VOID("gpu_bind_vertex_samplers", 24U, sdl_fn_gpu_bind_vertex_samplers, 3U, p_i64_i64_i64),
    SDL_FN_VOID("gpu_draw_indirect", 17U, sdl_fn_gpu_draw_indirect, 4U, p_i64_i64_i32_i32),
    SDL_FN_VOID("gpu_draw_indexed_indirect", 24U, sdl_fn_gpu_draw_indexed_indirect, 4U, p_i64_i64_i32_i32),
    SDL_FN("gpu_submit_and_fence", 20U, sdl_fn_gpu_submit_and_fence, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN("gpu_query_fence", 15U, sdl_fn_gpu_query_fence, 2U, p_i64_i64, VIGIL_TYPE_BOOL),
    SDL_FN_VOID("gpu_release_fence", 17U, sdl_fn_gpu_release_fence, 2U, p_i64_i64),
    SDL_FN_BOOL_ERR("gpu_wait_fences", 14U, sdl_fn_gpu_wait_fences, 2U, p_i64_i64),
    SDL_FN_BOOL_ERR("gpu_wait_swapchain", 18U, sdl_fn_gpu_wait_swapchain, 2U, p_i64_obj),
    {"gpu_wait_acquire_swapchain", 26U, sdl_fn_gpu_wait_acquire_swapchain, 2U, p_i64_obj, VIGIL_TYPE_I64, 2U,
     rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"gpu_window_supports_present", 27U, sdl_fn_gpu_window_supports_present, 3U, p_i64_obj_i32, VIGIL_TYPE_BOOL, 1U,
     NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"gpu_window_supports_composition", 31U, sdl_fn_gpu_window_supports_composition, 3U, p_i64_obj_i32, VIGIL_TYPE_BOOL,
     1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    {"gpu_create_compute_pipeline", 27U, sdl_fn_gpu_create_compute_pipeline, 11U, p_i64_i64_i32x9, VIGIL_TYPE_I64, 2U,
     rt_i64_err, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL},
    SDL_FN_VOID("gpu_release_compute_pipeline", 27U, sdl_fn_gpu_release_compute_pipeline, 2U, p_i64_i64),
    SDL_FN("gpu_begin_compute_pass", 22U, sdl_fn_gpu_begin_compute_pass, 1U, p_i64, VIGIL_TYPE_I64),
    SDL_FN_VOID("gpu_end_compute_pass", 20U, sdl_fn_gpu_end_compute_pass, 1U, p_i64),
    SDL_FN_VOID("gpu_bind_compute_pipeline", 24U, sdl_fn_gpu_bind_compute_pipeline, 2U, p_i64_i64),
    SDL_FN_VOID("gpu_dispatch_compute", 20U, sdl_fn_gpu_dispatch_compute, 4U, p_i64_i32_i32_i32),
    SDL_FN_VOID("gpu_push_compute_uniforms", 25U, sdl_fn_gpu_push_compute_uniforms, 3U, p_i64_i32_i64),
    SDL_FN_VOID("gpu_copy_buffer_to_buffer", 24U, sdl_fn_gpu_copy_buffer_to_buffer, 6U, p_i64_i64_i32_i64_i32_i32),
    SDL_FN_VOID("gpu_download_from_buffer", 23U, sdl_fn_gpu_download_from_buffer, 6U, p_i64_i64_i32_i32_i64_i32),
    SDL_FN_VOID("gpu_upload_to_texture", 21U, sdl_fn_gpu_upload_to_texture, 6U, p_i64_i64_i32_i64_i32_i32),
    /* Display info (slice 11) */
    SDL_FN("get_display_count", 17U, sdl_fn_get_display_count, 0U, NULL, VIGIL_TYPE_I32),
    SDL_FN("get_display_name", 16U, sdl_fn_get_display_name, 1U, p_i32, VIGIL_TYPE_STRING),
    {"get_display_bounds", 18U, sdl_fn_get_display_bounds, 1U, p_i32, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
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
    /* Slice 13: window extras */
    SDL_METHOD("flash", 5U, sdl_window_flash, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_icon", 8U, sdl_window_set_icon, 1U, p_obj, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_opacity", 11U, sdl_window_set_opacity, 1U, p_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_opacity", 11U, sdl_window_get_opacity, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("set_min_size", 12U, sdl_window_set_min_size, 2U, p_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_min_size", 12U, sdl_window_get_min_size, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("set_max_size", 12U, sdl_window_set_max_size, 2U, p_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_max_size", 12U, sdl_window_get_max_size, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("set_always_on_top", 17U, sdl_window_set_always_on_top, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_mouse_grab", 14U, sdl_window_set_mouse_grab, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_keyboard_grab", 17U, sdl_window_set_keyboard_grab, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_size_in_pixels", 18U, sdl_window_get_size_in_pixels, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("get_display_scale", 17U, sdl_window_get_display_scale, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("set_relative_mouse", 18U, sdl_window_set_relative_mouse, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Slice 21: window getters */
    SDL_METHOD("get_pixel_density", 17U, sdl_window_get_pixel_density, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("get_mouse_grab", 14U, sdl_window_get_mouse_grab, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("get_keyboard_grab", 17U, sdl_window_get_keyboard_grab, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("get_relative_mouse", 18U, sdl_window_get_relative_mouse, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("set_progress", 12U, sdl_window_set_progress, 2U, p_i32_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_aspect_ratio", 16U, sdl_window_set_aspect_ratio, 2U, p_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Slice 26: display/window */
    SDL_METHOD("sync", 4U, sdl_window_sync, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Slice 30: window getters completion */
    SDL_METHOD("get_aspect_ratio", 16U, sdl_window_get_aspect_ratio, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64),
    SDL_METHOD("get_pixel_format", 16U, sdl_window_get_pixel_format, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    /* Window complete methods */
    SDL_METHOD("set_fullscreen_mode", 19U, sdl_window_set_fullscreen_mode, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_modal", 9U, sdl_window_set_modal, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_focusable", 13U, sdl_window_set_focusable, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("show_system_menu", 16U, sdl_window_show_system_menu, 2U, p_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_shape", 9U, sdl_window_set_shape, 1U, p_obj, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_progress_state", 18U, sdl_window_get_progress_state, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_progress_value", 18U, sdl_window_get_progress_value, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    {"get_fullscreen_mode", 19U, sdl_window_get_fullscreen_mode, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL, 0U,
     0, NULL, NULL, NULL, &sdl_doc_window_get_fullscreen_mode},
    SDL_METHOD("has_surface", 11U, sdl_window_has_surface, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("update_surface", 14U, sdl_window_update_surface, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    {"update_surface_rect", 19U, sdl_window_update_surface_rect, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err,
     0, NULL, 0U, 0, sdl_window_rect_param_names, NULL, NULL, &sdl_doc_window_update_surface_rect},
    {"update_surface_rects", 20U, sdl_window_update_surface_rects, 2U, p_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0,
     NULL, 0U, 0, sdl_window_rects_param_names, NULL, NULL, &sdl_doc_window_update_surface_rects},
    SDL_METHOD("destroy_surface", 15U, sdl_window_destroy_surface, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    {"show_message_box", 16U, sdl_window_show_message_box, 4U, p_i32_str_str_str, VIGIL_TYPE_I32, 2U, rt_i32_err, 0,
     NULL, 0U, 0, sdl_message_box_param_names, NULL, NULL, &sdl_doc_show_message_box},
    SDL_METHOD("set_surface_vsync", 17U, sdl_window_set_surface_vsync, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_surface_vsync", 17U, sdl_window_get_surface_vsync, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    /* Window remaining - methods */
    SDL_METHOD("create_popup", 12U, sdl_window_create_popup, 5U, p_i32_i32_i32_i32_i32, VIGIL_TYPE_I64, 2U, rt_i64_err),
    SDL_METHOD("set_parent", 10U, sdl_window_set_parent_fn, 1U, p_obj, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_fill_document", 17U, sdl_window_set_fill_document, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_hit_test", 12U, sdl_window_set_hit_test, 1U, p_obj, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Slice 34: window extras */
    SDL_METHOD("get_borders_size", 16U, sdl_window_get_borders_size, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("get_safe_area", 13U, sdl_window_get_safe_area, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    /* Slice 36: window mouse rect */
    SDL_METHOD("set_mouse_rect", 14U, sdl_window_set_mouse_rect, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
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
    /* Slice 12: drawing extras */
    SDL_METHOD("render_debug_text", 17U, sdl_renderer_render_debug_text, 3U, p_f64_f64_str, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_viewport", 12U, sdl_renderer_set_viewport, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_draw_blend_mode", 19U, sdl_renderer_set_draw_blend_mode, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_draw_blend_mode", 19U, sdl_renderer_get_draw_blend_mode, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("set_color_scale", 15U, sdl_renderer_set_color_scale, 1U, p_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_color_scale", 15U, sdl_renderer_get_color_scale, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("flush", 5U, sdl_renderer_flush, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_output_size", 15U, sdl_renderer_get_output_size, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("get_current_output_size", 23U, sdl_renderer_get_current_output_size, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("get_name", 8U, sdl_renderer_get_name, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    /* Slice 17: renderer getters */
    SDL_METHOD("get_vsync", 9U, sdl_renderer_get_vsync, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("clip_enabled", 12U, sdl_renderer_clip_enabled, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("get_viewport", 12U, sdl_renderer_get_viewport, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    /* Renderer complete - methods */
    SDL_METHOD("get_draw_color_float", 20U, sdl_renderer_get_draw_color_float, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL),
    SDL_METHOD("get_safe_area", 13U, sdl_renderer_get_safe_area, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("viewport_set", 12U, sdl_renderer_viewport_set, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("set_texture_address_mode", 24U, sdl_renderer_set_texture_address_mode, 2U, p_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_texture_address_mode", 24U, sdl_renderer_get_texture_address_mode, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("set_default_scale_mode", 22U, sdl_renderer_set_default_scale_mode, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_default_scale_mode", 22U, sdl_renderer_get_default_scale_mode, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("coords_from_window", 18U, sdl_renderer_coords_from_window, 2U, p_f64_f64, VIGIL_TYPE_F64, 2U, rt_f64_f64),
    SDL_METHOD("coords_to_window", 16U, sdl_renderer_coords_to_window, 2U, p_f64_f64, VIGIL_TYPE_F64, 2U, rt_f64_f64),
    SDL_METHOD("convert_event_coords", 20U, sdl_renderer_convert_event_coords, 1U, p_obj, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("render_points", 13U, sdl_renderer_render_points, 2U, p_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("render_lines", 12U, sdl_renderer_render_lines, 2U, p_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("render_rects", 12U, sdl_renderer_render_rects, 2U, p_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("render_fill_rects", 17U, sdl_renderer_render_fill_rects, 2U, p_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    {"render_geometry", 15U, sdl_renderer_render_geometry, 5U, p_i64_i64_i32_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    {"render_texture_affine", 22U, sdl_renderer_render_texture_affine, 11U, p_obj_f64x11, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    {"render_texture_9grid", 21U, sdl_renderer_render_texture_9grid, 14U, p_obj_f64x13, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    /* Renderer final methods */
    SDL_METHOD("get_clip_rect", 13U, sdl_renderer_get_clip_rect, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("get_logical_rect", 16U, sdl_renderer_get_logical_rect, 0U, NULL, VIGIL_TYPE_F64, 2U, rt_f64_f64),
    {"render_texture_9grid_tiled", 26U, sdl_renderer_render_texture_9grid_tiled, 15U, p_obj_f64x14, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    {"render_geometry_raw", 20U, sdl_renderer_render_geometry_raw, 7U, p_i64_i64_i32_i32_i64_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    /* Slice 25: tiled rendering */
    SDL_METHOD("render_texture_tiled", 21U, sdl_renderer_render_texture_tiled, 10U, p_obj_f64x9, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Slice 27: renderer completions */
    SDL_METHOD("get_render_target", 17U, sdl_renderer_get_render_target, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL),
    SDL_METHOD("set_draw_color_float", 20U, sdl_renderer_set_draw_color_float, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_logical_presentation", 24U, sdl_renderer_get_logical_presentation, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    /* Slice 31: read_pixels */
    SDL_METHOD("read_pixels", 11U, sdl_renderer_read_pixels, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
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
    /* Slice 36: event description */
    SDL_METHOD("get_description", 15U, sdl_event_get_description, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
};

/* ── Surface class descriptor ────────────────────────────────────── */

static const vigil_native_class_field_t sdl_surface_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_surface_methods[] = {
    SDL_STATIC("load", 4U, sdl_surface_load, 1U, p_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_STATIC("load_bmp", 8U, sdl_surface_load_bmp, 1U, p_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("destroy", 7U, sdl_surface_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    /* Slice 20: surface ops */
    SDL_METHOD("clear", 5U, sdl_surface_clear, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("fill_rect", 9U, sdl_surface_fill_rect, 5U, p_i32_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("flip", 4U, sdl_surface_flip, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_color_mod", 13U, sdl_surface_set_color_mod, 3U, p_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_alpha_mod", 13U, sdl_surface_set_alpha_mod, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_blend_mode", 14U, sdl_surface_set_blend_mode, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_color_key", 13U, sdl_surface_set_color_key, 2U, p_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("blit", 4U, sdl_surface_blit, 7U, p_obj_i32x6, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Slice 23: advanced surface */
    SDL_STATIC("create", 6U, sdl_surface_create, 3U, p_i32_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("duplicate", 9U, sdl_surface_duplicate, 0U, NULL, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("scale", 5U, sdl_surface_scale, 3U, p_i32_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("rotate", 6U, sdl_surface_rotate, 1U, p_f64, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("save_bmp", 8U, sdl_surface_save_bmp, 1U, p_str, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("save_png", 8U, sdl_surface_save_png, 1U, p_str, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("read_pixel", 10U, sdl_surface_read_pixel, 2U, p_i32_i32, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("write_pixel", 11U, sdl_surface_write_pixel, 6U, p_i32_i32_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Slice 28: surface extras */
    SDL_STATIC("load_png", 8U, sdl_surface_load_png, 1U, p_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("blit_scaled", 11U, sdl_surface_blit_scaled, 10U, p_obj_i32x9, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_color_key", 13U, sdl_surface_get_color_key, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    /* Slice 31: convert */
    SDL_METHOD("convert", 7U, sdl_surface_convert, 1U, p_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    /* Slice 34: surface clip rect */
    SDL_METHOD("set_clip_rect", 13U, sdl_surface_set_clip_rect, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Surface complete */
    SDL_METHOD("get_alpha_mod", 13U, sdl_surface_get_alpha_mod, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_blend_mode", 14U, sdl_surface_get_blend_mode, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_color_mod", 13U, sdl_surface_get_color_mod, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("set_rle", 7U, sdl_surface_set_rle, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("has_rle", 7U, sdl_surface_has_rle, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("has_color_key", 13U, sdl_surface_has_color_key, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("lock", 4U, sdl_surface_lock, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("unlock", 6U, sdl_surface_unlock, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("premultiply_alpha", 17U, sdl_surface_premultiply_alpha, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Surface final methods */
    SDL_METHOD("get_colorspace", 14U, sdl_surface_get_colorspace, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("set_colorspace", 14U, sdl_surface_set_colorspace, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_STATIC("create_from", 11U, sdl_surface_create_from, 5U, p_i64_i32_i32_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("blit_tiled", 10U, sdl_surface_blit_tiled, 9U, p_obj_i32x8, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("blit_unchecked", 14U, sdl_surface_blit_unchecked, 7U, p_obj_i32x6, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("read_pixel_float", 16U, sdl_surface_read_pixel_float, 2U, p_i32_i32, VIGIL_TYPE_I64, 1U, NULL),
    {"write_pixel_float", 17U, sdl_surface_write_pixel_float, 6U, p_i32_i32_f64_f64_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    SDL_METHOD("map_rgb", 7U, sdl_surface_map_rgb, 3U, p_i32_i32_i32, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("map_rgba", 8U, sdl_surface_map_rgba, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_I32, 1U, NULL),
    {"fill_rects", 10U, sdl_surface_fill_rects, 3U, p_i64_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    SDL_METHOD("convert_colorspace", 18U, sdl_surface_convert_colorspace, 2U, p_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    /* Slice 37: surface extras */
    SDL_METHOD("get_clip_rect", 13U, sdl_surface_get_clip_rect, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("stretch", 7U, sdl_surface_stretch, 10U, p_obj_i32x9, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
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
    /* Slice 18: texture extras */
    SDL_METHOD("get_color_mod", 13U, sdl_texture_get_color_mod, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_alpha_mod", 13U, sdl_texture_get_alpha_mod, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_blend_mode", 14U, sdl_texture_get_blend_mode, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("set_scale_mode", 14U, sdl_texture_set_scale_mode, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_scale_mode", 14U, sdl_texture_get_scale_mode, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    /* Texture complete - methods */
    SDL_METHOD("set_color_mod_float", 19U, sdl_texture_set_color_mod_float, 3U, p_f64_f64_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_alpha_mod_float", 19U, sdl_texture_set_alpha_mod_float, 1U, p_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_alpha_mod_float", 19U, sdl_texture_get_alpha_mod_float, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    {"update", 6U, sdl_texture_update, 6U, p_i32_i32_i32_i32_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    SDL_METHOD("lock", 4U, sdl_texture_lock, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_I64, 1U, NULL),
    SDL_METHOD("unlock", 6U, sdl_texture_unlock, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    /* Texture final methods */
    SDL_METHOD("get_color_mod_float", 19U, sdl_texture_get_color_mod_float, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL),
    {"update_yuv", 10U, sdl_texture_update_yuv, 10U, p_i32x4_i64_i32_i64_i32_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
    {"update_nv", 9U, sdl_texture_update_nv, 8U, p_i32x4_i64_i32_i64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
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
    /* Slice 14: audio extras */
    SDL_METHOD("set_gain", 8U, sdl_audio_stream_set_gain, 1U, p_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_gain", 8U, sdl_audio_stream_get_gain, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("set_freq_ratio", 14U, sdl_audio_stream_set_freq_ratio, 1U, p_f64, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_freq_ratio", 14U, sdl_audio_stream_get_freq_ratio, 0U, NULL, VIGIL_TYPE_F64, 1U, NULL),
    SDL_METHOD("get_available", 13U, sdl_audio_stream_get_available, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("flush", 5U, sdl_audio_stream_flush, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("clear", 5U, sdl_audio_stream_clear, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Audio complete - stream methods */
    SDL_METHOD("get_device", 10U, sdl_audio_stream_get_device, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("paused", 6U, sdl_audio_stream_paused, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("lock", 4U, sdl_audio_stream_lock, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("unlock", 6U, sdl_audio_stream_unlock, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("bind", 4U, sdl_audio_stream_bind, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("unbind", 6U, sdl_audio_stream_unbind, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    {"get_data", 8U, sdl_audio_stream_get_data, 2U, p_i64_i32, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, 0U, 0U, NULL, NULL, NULL, NULL},
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
    /* Slice 22: gamepad extras */
    SDL_METHOD("connected", 9U, sdl_gamepad_connected, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("get_type", 8U, sdl_gamepad_get_type, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_power_percent", 17U, sdl_gamepad_get_power_percent, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    {"get_power_info", 14U, sdl_gamepad_get_power_info, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32, 0, NULL, 0U, 0, NULL,
     NULL, NULL, &sdl_doc_gamepad_get_power_info},
    SDL_METHOD("set_led", 7U, sdl_gamepad_set_led, 3U, p_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("rumble_triggers", 15U, sdl_gamepad_rumble_triggers, 3U, p_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("has_axis", 8U, sdl_gamepad_has_axis, 1U, p_i32, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("has_button", 10U, sdl_gamepad_has_button, 1U, p_i32, VIGIL_TYPE_BOOL, 1U, NULL),
    /* Gamepad complete - methods */
    SDL_METHOD("get_mapping", 11U, sdl_gamepad_get_mapping, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("get_path", 8U, sdl_gamepad_get_path, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("get_serial", 10U, sdl_gamepad_get_serial, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("get_vendor", 10U, sdl_gamepad_get_vendor, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_product", 11U, sdl_gamepad_get_product, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_product_version", 19U, sdl_gamepad_get_product_version, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_firmware_version", 20U, sdl_gamepad_get_firmware_version, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_player_index", 16U, sdl_gamepad_get_player_index, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("set_player_index", 16U, sdl_gamepad_set_player_index, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    {"get_guid", 8U, sdl_gamepad_get_guid, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &sdl_doc_gamepad_get_guid},
    SDL_METHOD("get_steam_handle", 16U, sdl_gamepad_get_steam_handle, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL),
    SDL_METHOD("get_connection_state", 20U, sdl_gamepad_get_connection_state, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_real_type", 13U, sdl_gamepad_get_real_type, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_num_touchpads", 17U, sdl_gamepad_get_num_touchpads, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("has_sensor", 10U, sdl_gamepad_has_sensor, 1U, p_i32, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("set_sensor_enabled", 18U, sdl_gamepad_set_sensor_enabled, 2U, p_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("sensor_enabled", 14U, sdl_gamepad_sensor_enabled, 1U, p_i32, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("get_button_label", 16U, sdl_gamepad_get_button_label, 1U, p_i32, VIGIL_TYPE_I32, 1U, NULL),
    {"get_bindings", 12U, sdl_gamepad_get_bindings, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL,
     NULL, &sdl_doc_gamepad_get_bindings},
};

/* ── Joystick class descriptor ───────────────────────────────────── */

static const vigil_native_class_field_t sdl_joystick_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_joystick_methods[] = {
    SDL_STATIC("open", 4U, sdl_joystick_open, 1U, p_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("close", 5U, sdl_joystick_close, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("get_name", 8U, sdl_joystick_get_name, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("get_type", 8U, sdl_joystick_get_type, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("connected", 9U, sdl_joystick_connected, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("num_axes", 8U, sdl_joystick_num_axes, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("num_buttons", 11U, sdl_joystick_num_buttons, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("num_hats", 8U, sdl_joystick_num_hats, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_axis", 8U, sdl_joystick_get_axis, 1U, p_i32, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_button", 10U, sdl_joystick_get_button, 1U, p_i32, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("get_hat", 7U, sdl_joystick_get_hat, 1U, p_i32, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("rumble", 6U, sdl_joystick_rumble, 3U, p_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Joystick complete - methods */
    SDL_METHOD("get_id", 6U, sdl_joystick_get_id, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_path", 8U, sdl_joystick_get_path, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("get_serial", 10U, sdl_joystick_get_serial, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("get_vendor", 10U, sdl_joystick_get_vendor, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_product", 11U, sdl_joystick_get_product, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_product_version", 19U, sdl_joystick_get_product_version, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_firmware_version", 20U, sdl_joystick_get_firmware_version, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_player_index", 16U, sdl_joystick_get_player_index, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("set_player_index", 16U, sdl_joystick_set_player_index, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("get_power_info", 14U, sdl_joystick_get_power_info, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_connection_state", 20U, sdl_joystick_get_connection_state, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("num_balls", 9U, sdl_joystick_num_balls, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_ball", 8U, sdl_joystick_get_ball, 1U, p_i32, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    SDL_METHOD("set_led", 7U, sdl_joystick_set_led, 3U, p_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("rumble_triggers", 15U, sdl_joystick_rumble_triggers, 3U, p_i32_i32_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("is_haptic", 9U, sdl_joystick_is_haptic, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
};

/* ── Haptic class descriptor ─────────────────────────────────────── */

static const vigil_native_class_field_t sdl_haptic_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_haptic_methods[] = {
    SDL_STATIC("open", 4U, sdl_haptic_open, 1U, p_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("close", 5U, sdl_haptic_close, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("get_name", 8U, sdl_haptic_get_name, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL),
    SDL_METHOD("rumble_supported", 16U, sdl_haptic_rumble_supported, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL),
    SDL_METHOD("init_rumble", 11U, sdl_haptic_init_rumble, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("play_rumble", 11U, sdl_haptic_play_rumble, 2U, p_f64_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("stop_rumble", 11U, sdl_haptic_stop_rumble, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    /* Haptic complete - methods */
    SDL_METHOD("get_features", 12U, sdl_haptic_get_features, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_max_effects", 15U, sdl_haptic_get_max_effects, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_max_effects_playing", 23U, sdl_haptic_get_max_effects_playing, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_num_axes", 12U, sdl_haptic_get_num_axes, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("set_gain", 8U, sdl_haptic_set_gain, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("set_autocenter", 14U, sdl_haptic_set_autocenter, 1U, p_i32, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("stop_effects", 12U, sdl_haptic_stop_effects, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("pause", 5U, sdl_haptic_pause, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
    SDL_METHOD("resume", 6U, sdl_haptic_resume, 0U, NULL, VIGIL_TYPE_BOOL, 2U, rt_bool_err),
};

/* ── Camera class descriptor ─────────────────────────────────────── */

static const vigil_native_class_field_t sdl_camera_fields[] = {
    SDL_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t sdl_camera_methods[] = {
    SDL_STATIC("open", 4U, sdl_camera_open, 4U, p_i32_i32_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    SDL_METHOD("close", 5U, sdl_camera_close, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    SDL_METHOD("get_permission", 14U, sdl_camera_get_permission, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL),
    SDL_METHOD("get_format", 10U, sdl_camera_get_format, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    {"get_spec", 8U, sdl_camera_get_spec, 0U, NULL, VIGIL_TYPE_I32, 6U, rt_i32x6, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &sdl_doc_camera_get_spec},
    {"acquire_frame", 13U, sdl_camera_acquire_frame, 0U, NULL, VIGIL_TYPE_I64, 2U, rt_i64_err, 0, NULL, 0U, 0, NULL,
     NULL, NULL, NULL},
    SDL_METHOD("release_frame", 13U, sdl_camera_release_frame, 1U, p_i64, VIGIL_TYPE_VOID, 0U, NULL),
};

static const vigil_native_class_t sdl_classes[] = {
    {"Window", 6U, sdl_window_fields, WIN_FIELD_COUNT, sdl_window_methods,
     sizeof(sdl_window_methods) / sizeof(sdl_window_methods[0]), NULL, NULL},
    {"Renderer", 8U, sdl_renderer_fields, REN_FIELD_COUNT, sdl_renderer_methods,
     sizeof(sdl_renderer_methods) / sizeof(sdl_renderer_methods[0]), NULL, NULL},
    {"Event", 5U, sdl_event_fields, EVT_FIELD_COUNT, sdl_event_methods,
     sizeof(sdl_event_methods) / sizeof(sdl_event_methods[0]), NULL, NULL},
    {"Surface", 7U, sdl_surface_fields, SURF_FIELD_COUNT, sdl_surface_methods,
     sizeof(sdl_surface_methods) / sizeof(sdl_surface_methods[0]), NULL, NULL},
    {"Texture", 7U, sdl_texture_fields, TEX_FIELD_COUNT, sdl_texture_methods,
     sizeof(sdl_texture_methods) / sizeof(sdl_texture_methods[0]), NULL, NULL},
    {"AudioStream", 11U, sdl_audio_stream_fields, ASTREAM_FIELD_COUNT, sdl_audio_stream_methods,
     sizeof(sdl_audio_stream_methods) / sizeof(sdl_audio_stream_methods[0]), NULL, NULL},
    {"Gamepad", 7U, sdl_gamepad_fields, GP_FIELD_COUNT, sdl_gamepad_methods,
     sizeof(sdl_gamepad_methods) / sizeof(sdl_gamepad_methods[0]), NULL, NULL},
    {"Joystick", 8U, sdl_joystick_fields, JOY_FIELD_COUNT, sdl_joystick_methods,
     sizeof(sdl_joystick_methods) / sizeof(sdl_joystick_methods[0]), NULL, NULL},
    {"Haptic", 6U, sdl_haptic_fields, HAP_FIELD_COUNT, sdl_haptic_methods,
     sizeof(sdl_haptic_methods) / sizeof(sdl_haptic_methods[0]), NULL, NULL},
    {"Camera", 6U, sdl_camera_fields, CAM_FIELD_COUNT, sdl_camera_methods,
     sizeof(sdl_camera_methods) / sizeof(sdl_camera_methods[0]), NULL, NULL},
};
/* clang-format on */

#define SDL_CLASS_COUNT (sizeof(sdl_classes) / sizeof(sdl_classes[0]))

/* ── Module export ───────────────────────────────────────────────── */

VIGIL_API const vigil_native_module_t vigil_plugin_sdl = {
    "sdl", 3U, sdl_functions, SDL_FUNCTION_COUNT, sdl_classes, SDL_CLASS_COUNT, NULL};
