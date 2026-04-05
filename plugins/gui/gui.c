/* gui.c — Vigil gui plugin: cross-platform Tk-style widget toolkit.
 *
 * Phase 1: App, Window, Label, Button + grid layout + on_click + message_box.
 * See: https://github.com/bluesentinelsec/vigil/issues/399
 */
#include <stdint.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

#include "gui_backend.h"

/* ── Backend selection ───────────────────────────────────────────── */

const gui_backend_t *gui_backend_select(void)
{
#if defined(__APPLE__)
    return &gui_backend_cocoa;
#elif defined(_WIN32)
    return &gui_backend_win32;
#elif defined(__linux__)
    return &gui_backend_gtk;
#else
#ifdef VIGIL_GUI_SDL_BACKEND
    return &gui_backend_sdl;
#else
    return &gui_backend_stub;
#endif
#endif
}

/* SDL fallback — tried if the native backend's init() fails. */
static const gui_backend_t *gui_backend_fallback(void)
{
#ifdef VIGIL_GUI_SDL_BACKEND
    return &gui_backend_sdl;
#else
    return NULL;
#endif
}

static const gui_backend_t *g_backend = NULL;

/* ── Handle registry (same pattern as SDL plugin) ────────────────── */

#define GUI_HANDLE_MAX 256

typedef struct
{
    void *items[GUI_HANDLE_MAX];
    int32_t count;
} gui_handle_registry_t;

static int gui_handle_store(gui_handle_registry_t *r, void *ptr, int64_t *out)
{
    for (int32_t i = 0; i < r->count; i++)
    {
        if (r->items[i] == NULL)
        {
            r->items[i] = ptr;
            *out = (int64_t)i;
            return 0;
        }
    }
    if (r->count >= GUI_HANDLE_MAX)
        return -1;
    r->items[r->count] = ptr;
    *out = (int64_t)r->count++;
    return 0;
}

static void *gui_handle_get(gui_handle_registry_t *r, int64_t h)
{
    if (h < 0 || h >= (int64_t)r->count)
        return NULL;
    return r->items[h];
}

static void gui_handle_clear(gui_handle_registry_t *r, int64_t h)
{
    if (h >= 0 && h < (int64_t)r->count)
        r->items[h] = NULL;
}

static gui_handle_registry_t g_windows = {{0}, 0};
static gui_handle_registry_t g_labels = {{0}, 0};
static gui_handle_registry_t g_buttons = {{0}, 0};
static gui_handle_registry_t g_entries = {{0}, 0};
static gui_handle_registry_t g_checkboxes = {{0}, 0};
static gui_handle_registry_t g_sliders = {{0}, 0};
static gui_handle_registry_t g_selects = {{0}, 0};
static gui_handle_registry_t g_texts = {{0}, 0};
static gui_handle_registry_t g_radios = {{0}, 0};
static gui_handle_registry_t g_spinboxes = {{0}, 0};
static gui_handle_registry_t g_frames = {{0}, 0};
static gui_handle_registry_t g_listboxes = {{0}, 0};
static gui_handle_registry_t g_menubars = {{0}, 0};
static gui_handle_registry_t g_submenus = {{0}, 0};
static gui_handle_registry_t g_paneds = {{0}, 0};
static gui_handle_registry_t g_canvases = {{0}, 0};

/* ── Stack helpers ───────────────────────────────────────────────── */

static int32_t gui_arg_i32(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base + idx));
}

static double gui_arg_f64(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + idx));
}

static const char *gui_arg_str(vigil_vm_t *vm, size_t base, size_t idx, char *buf, size_t bufsz)
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

/* Extract the handle (i64 field 0) from self instance at stack slot `base`. */
static int64_t gui_self_handle(vigil_vm_t *vm, size_t base)
{
    vigil_value_t val = vigil_vm_stack_get(vm, base);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(val);
    vigil_value_t field;
    vigil_instance_object_get_field(obj, 0, &field);
    int64_t result = vigil_nanbox_decode_int(field);
    vigil_value_release(&field);
    return result;
}

/* Extract handle from an instance object at stack slot base+idx. */
static int64_t gui_arg_handle(vigil_vm_t *vm, size_t base, size_t idx)
{
    return gui_self_handle(vm, base + idx);
}

static vigil_status_t gui_push_i32(vigil_vm_t *vm, int32_t val, vigil_error_t *error)
{
    vigil_value_t v = vigil_nanbox_encode_i32(val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t gui_push_handle_instance(vigil_vm_t *vm, size_t class_index, int64_t handle, vigil_error_t *error)
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

static vigil_status_t gui_push_ok_err(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t gui_push_fail_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    st = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return st;
}

/* Class indexes — must match gui_classes[] order. */
enum
{
    GUI_APP_CLASS_INDEX = 0U,
    GUI_WINDOW_CLASS_INDEX = 1U,
    GUI_LABEL_CLASS_INDEX = 2U,
    GUI_BUTTON_CLASS_INDEX = 3U,
    GUI_ENTRY_CLASS_INDEX = 4U,
    GUI_CHECKBOX_CLASS_INDEX = 5U,
    GUI_SLIDER_CLASS_INDEX = 6U,
    GUI_SELECT_CLASS_INDEX = 7U,
    GUI_TEXT_CLASS_INDEX = 8U,
    GUI_RADIO_CLASS_INDEX = 9U,
    GUI_SPINBOX_CLASS_INDEX = 10U,
    GUI_FRAME_CLASS_INDEX = 11U,
    GUI_LISTBOX_CLASS_INDEX = 12U,
    GUI_MENU_CLASS_INDEX = 13U,
    GUI_PANED_CLASS_INDEX = 14U,
    GUI_CANVAS_CLASS_INDEX = 15U,
};

/* ── Callback bridge ─────────────────────────────────────────────── */
/* Store Vigil closure objects and invoke them from the backend. */

#define GUI_MAX_CLOSURES 256

typedef struct
{
    vigil_vm_t *vm;
    vigil_object_t *closure;
} gui_closure_t;

static gui_closure_t g_closures[GUI_MAX_CLOSURES];
static int g_closure_count = 0;

static void gui_closure_invoke(void *user_data)
{
    gui_closure_t *c = (gui_closure_t *)user_data;
    if (!c || !c->vm || !c->closure)
        return;
    vigil_error_t err;
    memset(&err, 0, sizeof(err));
    vigil_value_t result;
    vigil_value_init_nil(&result);
    vigil_vm_execute_function(c->vm, c->closure, &result, &err);
}

static gui_closure_t *gui_closure_store(vigil_vm_t *vm, vigil_value_t closure_val)
{
    if (g_closure_count >= GUI_MAX_CLOSURES)
        return NULL;
    /* Extract the object pointer from the value. */
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(closure_val);
    if (!obj)
        return NULL;
    gui_closure_t *c = &g_closures[g_closure_count++];
    c->vm = vm;
    c->closure = obj;
    vigil_object_retain(obj);
    return c;
}

/* ── gui.App ─────────────────────────────────────────────────────── */

static vigil_status_t gui_app_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    /* arg 0 is implicit class ref from VM; explicit args start at 1. */
    char buf[256];
    const char *name = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);

    g_backend = gui_backend_select();
    if (!g_backend || !g_backend->init(name))
    {
        /* Try SDL fallback if native backend failed. */
        const gui_backend_t *fb = gui_backend_fallback();
        if (fb && fb != g_backend && fb->init(name))
        {
            g_backend = fb;
        }
        else
        {
            g_backend = NULL;
            gui_push_handle_instance(vm, GUI_APP_CLASS_INDEX, -1, error);
            return gui_push_fail_err(vm, "gui: no supported backend available on this platform", error);
        }
    }

    gui_push_handle_instance(vm, GUI_APP_CLASS_INDEX, 0, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_app_main_loop(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_backend)
        g_backend->main_loop();
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_app_quit(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_backend)
        g_backend->quit();
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_app_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (g_backend)
        g_backend->shutdown();
    g_backend = NULL;
    return VIGIL_STATUS_OK;
}

/* ── gui.Window ──────────────────────────────────────────────────── */

static vigil_status_t gui_window_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    /* arg 0 is implicit class ref; explicit args: app(1), title(2), w(3), h(4) */
    char buf[256];
    const char *title = gui_arg_str(vm, base, 2, buf, sizeof(buf));
    int w = gui_arg_i32(vm, base, 3);
    int h = gui_arg_i32(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (!g_backend)
    {
        gui_push_handle_instance(vm, GUI_WINDOW_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: backend not initialized", error);
    }

    void *native = g_backend->window_create(title, w, h);
    if (!native)
    {
        gui_push_handle_instance(vm, GUI_WINDOW_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: failed to create window", error);
    }

    int64_t handle;
    gui_handle_store(&g_windows, native, &handle);
    gui_push_handle_instance(vm, GUI_WINDOW_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_window_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_windows, h);
    if (native && g_backend)
        g_backend->window_destroy(native);
    gui_handle_clear(&g_windows, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_window_set_title(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *title = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_windows, h);
    if (native && g_backend)
        g_backend->window_set_title(native, title);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_window_get_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    int w = 0, ht = 0;
    void *native = gui_handle_get(&g_windows, h);
    if (native && g_backend)
        g_backend->window_get_size(native, &w, &ht);
    gui_push_i32(vm, w, error);
    return gui_push_i32(vm, ht, error);
}

static vigil_status_t gui_window_grid_columnconfigure(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int index = gui_arg_i32(vm, base, 1);
    int weight = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_windows, h);
    if (native && g_backend)
        g_backend->container_grid_columnconfigure(native, index, weight);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_window_grid_rowconfigure(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int index = gui_arg_i32(vm, base, 1);
    int weight = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_windows, h);
    if (native && g_backend)
        g_backend->container_grid_rowconfigure(native, index, weight);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_window_on_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_windows, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CLOSE, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Label ───────────────────────────────────────────────────── */

static vigil_status_t gui_label_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    /* arg 0 is implicit class ref; explicit args: parent(1), text(2) */
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 2, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);

    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_LABEL_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }

    void *native = g_backend->label_create(parent, text);
    int64_t handle;
    gui_handle_store(&g_labels, native, &handle);
    gui_push_handle_instance(vm, GUI_LABEL_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_label_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_labels, h);
    if (native && g_backend)
        g_backend->label_destroy(native);
    gui_handle_clear(&g_labels, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_label_set_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_labels, h);
    if (native && g_backend)
        g_backend->label_set_text(native, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_label_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_labels, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Button ──────────────────────────────────────────────────── */

static vigil_status_t gui_button_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    /* arg 0 is implicit class ref; explicit args: parent(1), text(2) */
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 2, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);

    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_BUTTON_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }

    void *native = g_backend->button_create(parent, text);
    int64_t handle;
    gui_handle_store(&g_buttons, native, &handle);
    gui_push_handle_instance(vm, GUI_BUTTON_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_button_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_buttons, h);
    if (native && g_backend)
        g_backend->button_destroy(native);
    gui_handle_clear(&g_buttons, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_button_set_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_buttons, h);
    if (native && g_backend)
        g_backend->button_set_text(native, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_button_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_buttons, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_button_on_click(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    void *native = gui_handle_get(&g_buttons, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CLICK, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Entry ───────────────────────────────────────────────────── */

static vigil_status_t gui_entry_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_ENTRY_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }

    void *native = g_backend->entry_create(parent);
    int64_t handle;
    gui_handle_store(&g_entries, native, &handle);
    gui_push_handle_instance(vm, GUI_ENTRY_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_entry_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_entries, h);
    if (native && g_backend)
        g_backend->entry_destroy(native);
    gui_handle_clear(&g_entries, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_entry_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);

    char buf[1024] = {0};
    void *native = gui_handle_get(&g_entries, h);
    if (native && g_backend)
        g_backend->entry_get_text(native, buf, sizeof(buf));

    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *str = NULL;
    vigil_status_t st = vigil_string_object_new_cstr(rt, buf, &str, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t v;
    vigil_value_init_object(&v, &str);
    st = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return st;
}

static vigil_status_t gui_entry_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[1024];
    const char *text = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_entries, h);
    if (native && g_backend)
        g_backend->entry_set_text(native, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_entry_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_entries, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_entry_on_change(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_entries, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CHANGE, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Checkbox ────────────────────────────────────────────────── */

static vigil_status_t gui_checkbox_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 2, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);

    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_CHECKBOX_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }

    void *native = g_backend->checkbox_create(parent, text);
    int64_t handle;
    gui_handle_store(&g_checkboxes, native, &handle);
    gui_push_handle_instance(vm, GUI_CHECKBOX_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_checkbox_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_checkboxes, h);
    if (native && g_backend)
        g_backend->checkbox_destroy(native);
    gui_handle_clear(&g_checkboxes, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_checkbox_set_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_checkboxes, h);
    if (native && g_backend)
        g_backend->checkbox_set_text(native, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_checkbox_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    bool checked = false;
    void *native = gui_handle_get(&g_checkboxes, h);
    if (native && g_backend)
        checked = g_backend->checkbox_get_checked(native);
    vigil_value_t v = vigil_nanbox_from_bool(checked);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t gui_checkbox_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int32_t val = gui_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_checkboxes, h);
    if (native && g_backend)
        g_backend->checkbox_set_checked(native, val != 0);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_checkbox_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_checkboxes, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_checkbox_on_change(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_checkboxes, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CHANGE, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Slider ──────────────────────────────────────────────────── */

static vigil_status_t gui_slider_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    double min_val = gui_arg_f64(vm, base, 2);
    double max_val = gui_arg_f64(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);

    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_SLIDER_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }

    void *native = g_backend->slider_create(parent, min_val, max_val);
    int64_t handle;
    gui_handle_store(&g_sliders, native, &handle);
    gui_push_handle_instance(vm, GUI_SLIDER_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_slider_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_sliders, h);
    if (native && g_backend)
        g_backend->slider_destroy(native);
    gui_handle_clear(&g_sliders, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_slider_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    double val = 0.0;
    void *native = gui_handle_get(&g_sliders, h);
    if (native && g_backend)
        val = g_backend->slider_get_value(native);
    vigil_value_t v = vigil_nanbox_encode_double(val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t gui_slider_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    double val = gui_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_sliders, h);
    if (native && g_backend)
        g_backend->slider_set_value(native, val);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_slider_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_sliders, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_slider_on_change(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_sliders, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CHANGE, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Select ──────────────────────────────────────────────────── */

static vigil_status_t gui_select_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_SELECT_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }

    void *native = g_backend->select_create(parent);
    int64_t handle;
    gui_handle_store(&g_selects, native, &handle);
    gui_push_handle_instance(vm, GUI_SELECT_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_select_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_selects, h);
    if (native && g_backend)
        g_backend->select_destroy(native);
    gui_handle_clear(&g_selects, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_select_add_item(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_selects, h);
    if (native && g_backend)
        g_backend->select_add_item(native, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_select_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    int idx = -1;
    void *native = gui_handle_get(&g_selects, h);
    if (native && g_backend)
        idx = g_backend->select_get_index(native);
    return gui_push_i32(vm, (int32_t)idx, error);
}

static vigil_status_t gui_select_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int idx = gui_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_selects, h);
    if (native && g_backend)
        g_backend->select_set_index(native, idx);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_select_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_selects, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_select_on_change(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_selects, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CHANGE, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Text ────────────────────────────────────────────────────── */

static vigil_status_t gui_text_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_TEXT_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }
    void *native = g_backend->text_create(parent);
    int64_t handle;
    gui_handle_store(&g_texts, native, &handle);
    gui_push_handle_instance(vm, GUI_TEXT_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_text_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_texts, h);
    if (native && g_backend)
        g_backend->text_destroy(native);
    gui_handle_clear(&g_texts, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_text_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[4096] = {0};
    void *native = gui_handle_get(&g_texts, h);
    if (native && g_backend)
        g_backend->text_get_text(native, buf, sizeof(buf));
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *str = NULL;
    vigil_status_t st = vigil_string_object_new_cstr(rt, buf, &str, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t v;
    vigil_value_init_object(&v, &str);
    st = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return st;
}

static vigil_status_t gui_text_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[4096];
    const char *text = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_texts, h);
    if (native && g_backend)
        g_backend->text_set_text(native, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_text_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_texts, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Radiobutton ─────────────────────────────────────────────── */

static vigil_status_t gui_radio_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 2, buf, sizeof(buf));
    /* Optional group handle: -1 means no group (first in group). */
    int64_t group_h = (arg_count > 3) ? gui_arg_handle(vm, base, 3) : -1;
    vigil_vm_stack_pop_n(vm, arg_count);

    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_RADIO_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }
    void *group = (group_h >= 0) ? gui_handle_get(&g_radios, group_h) : NULL;
    void *native = g_backend->radio_create(parent, text, group);
    int64_t handle;
    gui_handle_store(&g_radios, native, &handle);
    gui_push_handle_instance(vm, GUI_RADIO_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_radio_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_radios, h);
    if (native && g_backend)
        g_backend->radio_destroy(native);
    gui_handle_clear(&g_radios, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_radio_set_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_radios, h);
    if (native && g_backend)
        g_backend->radio_set_text(native, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_radio_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    bool active = false;
    void *native = gui_handle_get(&g_radios, h);
    if (native && g_backend)
        active = g_backend->radio_get_active(native);
    vigil_value_t v = vigil_nanbox_from_bool(active);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t gui_radio_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int32_t val = gui_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_radios, h);
    if (native && g_backend)
        g_backend->radio_set_active(native, val != 0);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_radio_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_radios, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_radio_on_change(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_radios, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CHANGE, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Spinbox ─────────────────────────────────────────────────── */

static vigil_status_t gui_spinbox_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    double min_val = gui_arg_f64(vm, base, 2);
    double max_val = gui_arg_f64(vm, base, 3);
    double step = gui_arg_f64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_SPINBOX_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }
    void *native = g_backend->spinbox_create(parent, min_val, max_val, step);
    int64_t handle;
    gui_handle_store(&g_spinboxes, native, &handle);
    gui_push_handle_instance(vm, GUI_SPINBOX_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_spinbox_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_spinboxes, h);
    if (native && g_backend)
        g_backend->spinbox_destroy(native);
    gui_handle_clear(&g_spinboxes, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_spinbox_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    double val = 0.0;
    void *native = gui_handle_get(&g_spinboxes, h);
    if (native && g_backend)
        val = g_backend->spinbox_get_value(native);
    vigil_value_t v = vigil_nanbox_encode_double(val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t gui_spinbox_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    double val = gui_arg_f64(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_spinboxes, h);
    if (native && g_backend)
        g_backend->spinbox_set_value(native, val);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_spinbox_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_spinboxes, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_spinbox_on_change(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_spinboxes, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CHANGE, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Frame ───────────────────────────────────────────────────── */

static vigil_status_t gui_frame_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    char buf[256];
    const char *label = gui_arg_str(vm, base, 2, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_FRAME_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }
    void *native = g_backend->frame_create(parent, label);
    int64_t handle;
    gui_handle_store(&g_frames, native, &handle);
    gui_push_handle_instance(vm, GUI_FRAME_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_frame_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_frames, h);
    if (native && g_backend)
        g_backend->frame_destroy(native);
    gui_handle_clear(&g_frames, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_frame_set_label(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *label = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_frames, h);
    if (native && g_backend)
        g_backend->frame_set_label(native, label);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_frame_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_frames, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Listbox ─────────────────────────────────────────────────── */

static vigil_status_t gui_listbox_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_LISTBOX_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }
    void *native = g_backend->listbox_create(parent);
    int64_t handle;
    gui_handle_store(&g_listboxes, native, &handle);
    gui_push_handle_instance(vm, GUI_LISTBOX_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_listbox_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_listboxes, h);
    if (native && g_backend)
        g_backend->listbox_destroy(native);
    gui_handle_clear(&g_listboxes, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_listbox_add_item(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *text = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_listboxes, h);
    if (native && g_backend)
        g_backend->listbox_add_item(native, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_listbox_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    int idx = -1;
    void *native = gui_handle_get(&g_listboxes, h);
    if (native && g_backend)
        idx = g_backend->listbox_get_selected(native);
    return gui_push_i32(vm, (int32_t)idx, error);
}

static vigil_status_t gui_listbox_set(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int idx = gui_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_listboxes, h);
    if (native && g_backend)
        g_backend->listbox_set_selected(native, idx);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_listbox_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_listboxes, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_listbox_on_change(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_listboxes, h);
    if (native && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->set_callback(native, GUI_EVENT_CHANGE, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Menu ────────────────────────────────────────────────────── */

static vigil_status_t gui_menu_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t win_h = gui_arg_handle(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *win = gui_handle_get(&g_windows, win_h);
    if (!win || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_MENU_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }
    void *native = g_backend->menubar_create(win);
    int64_t handle;
    gui_handle_store(&g_menubars, native, &handle);
    gui_push_handle_instance(vm, GUI_MENU_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_menu_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_menubars, h);
    if (native && g_backend)
        g_backend->menubar_destroy(native);
    gui_handle_clear(&g_menubars, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_menu_add_submenu(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    char buf[256];
    const char *label = gui_arg_str(vm, base, 1, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *menubar = gui_handle_get(&g_menubars, h);
    if (!menubar || !g_backend)
        return gui_push_i32(vm, -1, error);
    void *submenu = g_backend->menu_add_submenu(menubar, label);
    int64_t sub_h;
    gui_handle_store(&g_submenus, submenu, &sub_h);
    return gui_push_i32(vm, (int32_t)sub_h, error);
}

static vigil_status_t gui_menu_add_item(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int32_t sub_idx = gui_arg_i32(vm, base, 1);
    char buf[256];
    const char *label = gui_arg_str(vm, base, 2, buf, sizeof(buf));
    vigil_value_t closure = vigil_vm_stack_get(vm, base + 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    (void)h;
    void *submenu = gui_handle_get(&g_submenus, (int64_t)sub_idx);
    if (submenu && g_backend)
    {
        gui_closure_t *c = gui_closure_store(vm, closure);
        if (c)
        {
            gui_callback_t cb = {gui_closure_invoke, c};
            g_backend->menu_add_item(submenu, label, cb);
        }
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.PanedWindow ─────────────────────────────────────────────── */

static vigil_status_t gui_paned_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    int32_t horizontal = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_PANED_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }
    void *native = g_backend->paned_create(parent, horizontal != 0);
    int64_t handle;
    gui_handle_store(&g_paneds, native, &handle);
    gui_push_handle_instance(vm, GUI_PANED_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_paned_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_paneds, h);
    if (native && g_backend)
        g_backend->paned_destroy(native);
    gui_handle_clear(&g_paneds, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_paned_set_position(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int pos = gui_arg_i32(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_paneds, h);
    if (native && g_backend)
        g_backend->paned_set_position(native, pos);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_paned_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_paneds, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.Canvas ──────────────────────────────────────────────────── */

static vigil_status_t gui_canvas_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 1);
    int w = gui_arg_i32(vm, base, 2);
    int h = gui_arg_i32(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (!parent || !g_backend)
    {
        gui_push_handle_instance(vm, GUI_CANVAS_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: invalid parent window", error);
    }
    void *native = g_backend->canvas_create(parent, w, h);
    int64_t handle;
    gui_handle_store(&g_canvases, native, &handle);
    gui_push_handle_instance(vm, GUI_CANVAS_CLASS_INDEX, handle, error);
    return gui_push_ok_err(vm, error);
}

static vigil_status_t gui_canvas_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_canvases, h);
    if (native && g_backend)
        g_backend->canvas_destroy(native);
    gui_handle_clear(&g_canvases, h);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_canvas_clear(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_canvases, h);
    if (native && g_backend)
        g_backend->canvas_clear(native);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_canvas_draw_line(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    double x1 = gui_arg_f64(vm, base, 1);
    double y1 = gui_arg_f64(vm, base, 2);
    double x2 = gui_arg_f64(vm, base, 3);
    double y2 = gui_arg_f64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_canvases, h);
    if (native && g_backend)
        g_backend->canvas_draw_line(native, x1, y1, x2, y2);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_canvas_draw_rect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    double x = gui_arg_f64(vm, base, 1);
    double y = gui_arg_f64(vm, base, 2);
    double w = gui_arg_f64(vm, base, 3);
    double ht = gui_arg_f64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_canvases, h);
    if (native && g_backend)
        g_backend->canvas_draw_rect(native, x, y, w, ht);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_canvas_draw_oval(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    double x = gui_arg_f64(vm, base, 1);
    double y = gui_arg_f64(vm, base, 2);
    double w = gui_arg_f64(vm, base, 3);
    double ht = gui_arg_f64(vm, base, 4);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_canvases, h);
    if (native && g_backend)
        g_backend->canvas_draw_oval(native, x, y, w, ht);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_canvas_draw_text(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    double x = gui_arg_f64(vm, base, 1);
    double y = gui_arg_f64(vm, base, 2);
    char buf[1024];
    const char *text = gui_arg_str(vm, base, 3, buf, sizeof(buf));
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_canvases, h);
    if (native && g_backend)
        g_backend->canvas_draw_text(native, x, y, text);
    (void)error;
    return VIGIL_STATUS_OK;
}

static vigil_status_t gui_canvas_grid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = gui_self_handle(vm, base);
    int col = gui_arg_i32(vm, base, 1);
    int row = gui_arg_i32(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);
    void *native = gui_handle_get(&g_canvases, h);
    if (native && g_backend)
        g_backend->widget_grid(native, col, row);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── gui.message_box (module-level function) ─────────────────────── */

static vigil_status_t gui_fn_message_box(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 0);
    char tbuf[256], mbuf[1024];
    const char *title = gui_arg_str(vm, base, 1, tbuf, sizeof(tbuf));
    const char *message = gui_arg_str(vm, base, 2, mbuf, sizeof(mbuf));
    vigil_vm_stack_pop_n(vm, arg_count);

    void *parent = gui_handle_get(&g_windows, parent_h);
    if (g_backend)
        g_backend->message_box(parent, title, message);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* Helper: push a string result from a C buffer. */
static vigil_status_t gui_push_string(vigil_vm_t *vm, const char *str, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_string_object_new_cstr(rt, str, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    st = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return st;
}

static vigil_status_t gui_fn_open_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 0);
    char tbuf[256];
    const char *title = gui_arg_str(vm, base, 1, tbuf, sizeof(tbuf));
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[1024] = {0};
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (g_backend)
        g_backend->open_file_dialog(parent, title, buf, sizeof(buf));
    return gui_push_string(vm, buf, error);
}

static vigil_status_t gui_fn_save_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 0);
    char tbuf[256];
    const char *title = gui_arg_str(vm, base, 1, tbuf, sizeof(tbuf));
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[1024] = {0};
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (g_backend)
        g_backend->save_file_dialog(parent, title, buf, sizeof(buf));
    return gui_push_string(vm, buf, error);
}

static vigil_status_t gui_fn_choose_directory(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 0);
    char tbuf[256];
    const char *title = gui_arg_str(vm, base, 1, tbuf, sizeof(tbuf));
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[1024] = {0};
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (g_backend)
        g_backend->choose_directory(parent, title, buf, sizeof(buf));
    return gui_push_string(vm, buf, error);
}

static vigil_status_t gui_fn_ask_yes_no(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t parent_h = gui_arg_handle(vm, base, 0);
    char tbuf[256], mbuf[1024];
    const char *title = gui_arg_str(vm, base, 1, tbuf, sizeof(tbuf));
    const char *message = gui_arg_str(vm, base, 2, mbuf, sizeof(mbuf));
    vigil_vm_stack_pop_n(vm, arg_count);
    bool result = false;
    void *parent = gui_handle_get(&g_windows, parent_h);
    if (g_backend)
        result = g_backend->ask_yes_no(parent, title, message);
    vigil_value_t v = vigil_nanbox_from_bool(result);
    return vigil_vm_stack_push(vm, &v, error);
}

/* ── Parameter type arrays ───────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};
static const int p_obj_str_i32_i32[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_obj[] = {VIGIL_TYPE_OBJECT};
static const int p_obj_str[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING};
static const int p_obj_str_str[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};

static const int rt_obj_err[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int rt_i32_i32[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int rt_str[] = {VIGIL_TYPE_STRING};
static const int rt_bool[] = {VIGIL_TYPE_BOOL};
static const int p_bool[] = {VIGIL_TYPE_BOOL};
static const int p_obj_f64_f64[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_f64[] = {VIGIL_TYPE_F64};
static const int p_i32[] = {VIGIL_TYPE_I32};
static const int rt_f64[] = {VIGIL_TYPE_F64};
static const int rt_i32[] = {VIGIL_TYPE_I32};
static const int p_obj_str_obj[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_STRING, VIGIL_TYPE_OBJECT};
static const int p_obj_f64_f64_f64[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_i32_str_obj[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_OBJECT};
static const int p_obj_bool[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_BOOL};
static const int p_f64_f64_f64_f64[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_F64};
static const int p_f64_f64_str[] = {VIGIL_TYPE_F64, VIGIL_TYPE_F64, VIGIL_TYPE_STRING};
static const int p_obj_i32_i32[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_I32, VIGIL_TYPE_I32};

/* ── Macro helpers ───────────────────────────────────────────────── */

/* clang-format off */
#define GUI_PFIELD(n, nl, t) {n, nl, t, 0, NULL, 0U, 0, NULL, NULL}

#define GUI_METHOD(n, nl, fn, pc, pt, rt, rc, rts) \
    {n, nl, fn, pc, pt, rt, rc, rts, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL}

#define GUI_STATIC(n, nl, fn, pc, pt, rt, rc, rts) \
    {n, nl, fn, pc, pt, rt, rc, rts, 1, NULL, 0U, 0, NULL, NULL, NULL, NULL}
/* clang-format on */

/* ── App class ───────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_app_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_app_methods[] = {
    GUI_STATIC("new", 3U, gui_app_new, 1U, p_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("main_loop", 9U, gui_app_main_loop, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("quit", 4U, gui_app_quit, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("destroy", 7U, gui_app_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Window class ────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_window_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_window_methods[] = {
    GUI_STATIC("new", 3U, gui_window_new, 4U, p_obj_str_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_window_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("set_title", 9U, gui_window_set_title, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get_size", 8U, gui_window_get_size, 0U, NULL, VIGIL_TYPE_I32, 2U, rt_i32_i32),
    GUI_METHOD("grid_columnconfigure", 20U, gui_window_grid_columnconfigure, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid_rowconfigure", 17U, gui_window_grid_rowconfigure, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_close", 8U, gui_window_on_close, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Label class ─────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_label_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_label_methods[] = {
    GUI_STATIC("new", 3U, gui_label_new, 2U, p_obj_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_label_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("set_text", 8U, gui_label_set_text, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_label_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Button class ────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_button_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_button_methods[] = {
    GUI_STATIC("new", 3U, gui_button_new, 2U, p_obj_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_button_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("set_text", 8U, gui_button_set_text, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_button_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_click", 8U, gui_button_on_click, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Entry class ─────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_entry_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_entry_methods[] = {
    GUI_STATIC("new", 3U, gui_entry_new, 1U, p_obj, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_entry_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get", 3U, gui_entry_get, 0U, NULL, VIGIL_TYPE_STRING, 1U, rt_str),
    GUI_METHOD("set", 3U, gui_entry_set, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_entry_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_change", 9U, gui_entry_on_change, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Checkbox class ──────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_checkbox_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_checkbox_methods[] = {
    GUI_STATIC("new", 3U, gui_checkbox_new, 2U, p_obj_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_checkbox_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("set_text", 8U, gui_checkbox_set_text, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get", 3U, gui_checkbox_get, 0U, NULL, VIGIL_TYPE_BOOL, 1U, rt_bool),
    GUI_METHOD("set", 3U, gui_checkbox_set, 1U, p_bool, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_checkbox_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_change", 9U, gui_checkbox_on_change, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Slider class ────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_slider_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_slider_methods[] = {
    GUI_STATIC("new", 3U, gui_slider_new, 3U, p_obj_f64_f64, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_slider_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get", 3U, gui_slider_get, 0U, NULL, VIGIL_TYPE_F64, 1U, rt_f64),
    GUI_METHOD("set", 3U, gui_slider_set, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_slider_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_change", 9U, gui_slider_on_change, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Select class ────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_select_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_select_methods[] = {
    GUI_STATIC("new", 3U, gui_select_new, 1U, p_obj, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_select_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("add_item", 8U, gui_select_add_item, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get", 3U, gui_select_get, 0U, NULL, VIGIL_TYPE_I32, 1U, rt_i32),
    GUI_METHOD("set", 3U, gui_select_set, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_select_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_change", 9U, gui_select_on_change, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Text class ──────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_text_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_text_methods[] = {
    GUI_STATIC("new", 3U, gui_text_new, 1U, p_obj, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_text_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get", 3U, gui_text_get, 0U, NULL, VIGIL_TYPE_STRING, 1U, rt_str),
    GUI_METHOD("set", 3U, gui_text_set, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_text_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Radiobutton class ───────────────────────────────────────────── */

static const vigil_native_class_field_t gui_radio_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_radio_methods[] = {
    GUI_STATIC("new", 3U, gui_radio_new, 3U, p_obj_str_obj, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_radio_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("set_text", 8U, gui_radio_set_text, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get", 3U, gui_radio_get, 0U, NULL, VIGIL_TYPE_BOOL, 1U, rt_bool),
    GUI_METHOD("set", 3U, gui_radio_set, 1U, p_bool, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_radio_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_change", 9U, gui_radio_on_change, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Spinbox class ───────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_spinbox_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_spinbox_methods[] = {
    GUI_STATIC("new", 3U, gui_spinbox_new, 4U, p_obj_f64_f64_f64, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_spinbox_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get", 3U, gui_spinbox_get, 0U, NULL, VIGIL_TYPE_F64, 1U, rt_f64),
    GUI_METHOD("set", 3U, gui_spinbox_set, 1U, p_f64, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_spinbox_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_change", 9U, gui_spinbox_on_change, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Frame class ─────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_frame_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_frame_methods[] = {
    GUI_STATIC("new", 3U, gui_frame_new, 2U, p_obj_str, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_frame_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("set_label", 9U, gui_frame_set_label, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_frame_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Listbox class ───────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_listbox_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_listbox_methods[] = {
    GUI_STATIC("new", 3U, gui_listbox_new, 1U, p_obj, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_listbox_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("add_item", 8U, gui_listbox_add_item, 1U, p_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("get", 3U, gui_listbox_get, 0U, NULL, VIGIL_TYPE_I32, 1U, rt_i32),
    GUI_METHOD("set", 3U, gui_listbox_set, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_listbox_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("on_change", 9U, gui_listbox_on_change, 1U, p_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Menu class ──────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_menu_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_menu_methods[] = {
    GUI_STATIC("new", 3U, gui_menu_new, 1U, p_obj, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_menu_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("add_submenu", 11U, gui_menu_add_submenu, 1U, p_str, VIGIL_TYPE_I32, 1U, rt_i32),
    GUI_METHOD("add_item", 8U, gui_menu_add_item, 3U, p_i32_str_obj, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── PanedWindow class ───────────────────────────────────────────── */

static const vigil_native_class_field_t gui_paned_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_paned_methods[] = {
    GUI_STATIC("new", 3U, gui_paned_new, 2U, p_obj_bool, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_paned_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("set_position", 12U, gui_paned_set_position, 1U, p_i32, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_paned_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Canvas class ────────────────────────────────────────────────── */

static const vigil_native_class_field_t gui_canvas_fields[] = {
    GUI_PFIELD("handle", 6U, VIGIL_TYPE_I64),
};

static const vigil_native_class_method_t gui_canvas_methods[] = {
    GUI_STATIC("new", 3U, gui_canvas_new, 3U, p_obj_i32_i32, VIGIL_TYPE_OBJECT, 2U, rt_obj_err),
    GUI_METHOD("destroy", 7U, gui_canvas_destroy, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("clear", 5U, gui_canvas_clear, 0U, NULL, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("draw_line", 9U, gui_canvas_draw_line, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("draw_rect", 9U, gui_canvas_draw_rect, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("draw_oval", 9U, gui_canvas_draw_oval, 4U, p_f64_f64_f64_f64, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("draw_text", 9U, gui_canvas_draw_text, 3U, p_f64_f64_str, VIGIL_TYPE_VOID, 0U, NULL),
    GUI_METHOD("grid", 4U, gui_canvas_grid, 2U, p_i32_i32, VIGIL_TYPE_VOID, 0U, NULL),
};

/* ── Class table ─────────────────────────────────────────────────── */

/* clang-format off */
static const vigil_native_class_t gui_classes[] = {
    {"App",         3U,  gui_app_fields,      1U, gui_app_methods,      sizeof(gui_app_methods)      / sizeof(gui_app_methods[0]),      NULL, NULL},
    {"Window",      6U,  gui_window_fields,   1U, gui_window_methods,   sizeof(gui_window_methods)   / sizeof(gui_window_methods[0]),   NULL, NULL},
    {"Label",       5U,  gui_label_fields,    1U, gui_label_methods,    sizeof(gui_label_methods)    / sizeof(gui_label_methods[0]),    NULL, NULL},
    {"Button",      6U,  gui_button_fields,   1U, gui_button_methods,   sizeof(gui_button_methods)   / sizeof(gui_button_methods[0]),   NULL, NULL},
    {"Entry",       5U,  gui_entry_fields,    1U, gui_entry_methods,    sizeof(gui_entry_methods)    / sizeof(gui_entry_methods[0]),    NULL, NULL},
    {"Checkbox",    8U,  gui_checkbox_fields, 1U, gui_checkbox_methods, sizeof(gui_checkbox_methods) / sizeof(gui_checkbox_methods[0]), NULL, NULL},
    {"Slider",      6U,  gui_slider_fields,   1U, gui_slider_methods,   sizeof(gui_slider_methods)   / sizeof(gui_slider_methods[0]),   NULL, NULL},
    {"Select",      6U,  gui_select_fields,   1U, gui_select_methods,   sizeof(gui_select_methods)   / sizeof(gui_select_methods[0]),   NULL, NULL},
    {"Text",        4U,  gui_text_fields,     1U, gui_text_methods,     sizeof(gui_text_methods)     / sizeof(gui_text_methods[0]),     NULL, NULL},
    {"Radiobutton", 11U, gui_radio_fields,    1U, gui_radio_methods,    sizeof(gui_radio_methods)    / sizeof(gui_radio_methods[0]),    NULL, NULL},
    {"Spinbox",     7U,  gui_spinbox_fields,  1U, gui_spinbox_methods,  sizeof(gui_spinbox_methods)  / sizeof(gui_spinbox_methods[0]),  NULL, NULL},
    {"Frame",       5U,  gui_frame_fields,    1U, gui_frame_methods,    sizeof(gui_frame_methods)    / sizeof(gui_frame_methods[0]),    NULL, NULL},
    {"Listbox",     7U,  gui_listbox_fields,  1U, gui_listbox_methods,  sizeof(gui_listbox_methods)  / sizeof(gui_listbox_methods[0]),  NULL, NULL},
    {"Menu",        4U,  gui_menu_fields,     1U, gui_menu_methods,     sizeof(gui_menu_methods)     / sizeof(gui_menu_methods[0]),     NULL, NULL},
    {"PanedWindow", 11U, gui_paned_fields,    1U, gui_paned_methods,    sizeof(gui_paned_methods)    / sizeof(gui_paned_methods[0]),    NULL, NULL},
    {"Canvas",      6U,  gui_canvas_fields,   1U, gui_canvas_methods,   sizeof(gui_canvas_methods)   / sizeof(gui_canvas_methods[0]),   NULL, NULL},
};
/* clang-format on */

#define GUI_CLASS_COUNT (sizeof(gui_classes) / sizeof(gui_classes[0]))

/* ── Module-level functions ──────────────────────────────────────── */

static const vigil_native_module_function_t gui_functions[] = {
    {"message_box", 11U, gui_fn_message_box, 3U, p_obj_str_str, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
    {"open_file", 9U, gui_fn_open_file, 2U, p_obj_str, VIGIL_TYPE_STRING, 1U, rt_str, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"save_file", 9U, gui_fn_save_file, 2U, p_obj_str, VIGIL_TYPE_STRING, 1U, rt_str, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"choose_directory", 16U, gui_fn_choose_directory, 2U, p_obj_str, VIGIL_TYPE_STRING, 1U, rt_str, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, NULL},
    {"ask_yes_no", 10U, gui_fn_ask_yes_no, 3U, p_obj_str_str, VIGIL_TYPE_BOOL, 1U, rt_bool, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, NULL},
};

#define GUI_FUNCTION_COUNT (sizeof(gui_functions) / sizeof(gui_functions[0]))

/* ── Module doc ──────────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t gui_module_doc = {
    "Cross-platform GUI widget toolkit.",
    "Provides Tk-style widgets using native platform backends "
    "(Cocoa on macOS, Win32 on Windows, GTK on Linux). "
    "Widgets are laid out using a grid system.",
    NULL,
};

/* ── Module export ───────────────────────────────────────────────── */

VIGIL_API const vigil_native_module_t vigil_plugin_gui = {
    "gui", 3U, gui_functions, GUI_FUNCTION_COUNT, gui_classes, GUI_CLASS_COUNT, &gui_module_doc, NULL, 0U,
};
