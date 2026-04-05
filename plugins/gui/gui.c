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
    return &gui_backend_stub;
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

/* ── Stack helpers ───────────────────────────────────────────────── */

static int32_t gui_arg_i32(vigil_vm_t *vm, size_t base, size_t idx)
{
    return vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base + idx));
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
        gui_push_handle_instance(vm, GUI_APP_CLASS_INDEX, -1, error);
        return gui_push_fail_err(vm, "gui: no supported backend available on this platform", error);
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

/* ── Class table ─────────────────────────────────────────────────── */

/* clang-format off */
static const vigil_native_class_t gui_classes[] = {
    {"App",      3U, gui_app_fields,      1U, gui_app_methods,      sizeof(gui_app_methods)      / sizeof(gui_app_methods[0]),      NULL, NULL},
    {"Window",   6U, gui_window_fields,   1U, gui_window_methods,   sizeof(gui_window_methods)   / sizeof(gui_window_methods[0]),   NULL, NULL},
    {"Label",    5U, gui_label_fields,    1U, gui_label_methods,    sizeof(gui_label_methods)    / sizeof(gui_label_methods[0]),    NULL, NULL},
    {"Button",   6U, gui_button_fields,   1U, gui_button_methods,   sizeof(gui_button_methods)   / sizeof(gui_button_methods[0]),   NULL, NULL},
    {"Entry",    5U, gui_entry_fields,    1U, gui_entry_methods,    sizeof(gui_entry_methods)    / sizeof(gui_entry_methods[0]),    NULL, NULL},
    {"Checkbox", 8U, gui_checkbox_fields, 1U, gui_checkbox_methods, sizeof(gui_checkbox_methods) / sizeof(gui_checkbox_methods[0]), NULL, NULL},
};
/* clang-format on */

#define GUI_CLASS_COUNT (sizeof(gui_classes) / sizeof(gui_classes[0]))

/* ── Module-level functions ──────────────────────────────────────── */

static const vigil_native_module_function_t gui_functions[] = {
    {"message_box", 11U, gui_fn_message_box, 3U, p_obj_str_str, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, NULL,
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
