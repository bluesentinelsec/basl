/* gui_stub.c — Stub backend for unsupported platforms.
 * Returns false from init() so gui.c reports an error to the user.
 */
#include "../gui_backend.h"

static bool stub_init(const char *app_name)
{
    (void)app_name;
    return false;
}

static void stub_noop(void)
{
}
static void stub_quit(void)
{
}
static void stub_shutdown(void)
{
}

static void *stub_create(void *p, const char *t)
{
    (void)p;
    (void)t;
    return NULL;
}
static void *stub_create_notext(void *p)
{
    (void)p;
    return NULL;
}
static void *stub_create_wh(const char *t, int w, int h)
{
    (void)t;
    (void)w;
    (void)h;
    return NULL;
}
static void stub_destroy(void *h)
{
    (void)h;
}
static void stub_set_text(void *h, const char *t)
{
    (void)h;
    (void)t;
}
static const char *stub_get_text(void *h, char *b, size_t s)
{
    (void)h;
    b[0] = '\0';
    (void)s;
    return b;
}
static void stub_get_size(void *h, int *w, int *ht)
{
    (void)h;
    *w = 0;
    *ht = 0;
}
static bool stub_get_bool(void *h)
{
    (void)h;
    return false;
}
static void stub_set_bool(void *h, bool v)
{
    (void)h;
    (void)v;
}
static void stub_grid(void *h, int c, int r)
{
    (void)h;
    (void)c;
    (void)r;
}
static void stub_grid_remove(void *h)
{
    (void)h;
}
static void stub_grid_configure(void *h, int i, int w)
{
    (void)h;
    (void)i;
    (void)w;
}
static void stub_set_cb(void *w, gui_event_type_t t, gui_callback_t cb)
{
    (void)w;
    (void)t;
    (void)cb;
}
static void stub_msgbox(void *p, const char *t, const char *m)
{
    (void)p;
    (void)t;
    (void)m;
}

const gui_backend_t gui_backend_stub = {
    .name = "stub",
    .init = stub_init,
    .shutdown = stub_shutdown,
    .main_loop = stub_noop,
    .quit = stub_quit,
    .window_create = stub_create_wh,
    .window_destroy = stub_destroy,
    .window_set_title = stub_set_text,
    .window_get_size = stub_get_size,
    .label_create = stub_create,
    .label_destroy = stub_destroy,
    .label_set_text = stub_set_text,
    .button_create = stub_create,
    .button_destroy = stub_destroy,
    .button_set_text = stub_set_text,
    .entry_create = stub_create_notext,
    .entry_destroy = stub_destroy,
    .entry_get_text = stub_get_text,
    .entry_set_text = stub_set_text,
    .checkbox_create = stub_create,
    .checkbox_destroy = stub_destroy,
    .checkbox_set_text = stub_set_text,
    .checkbox_get_checked = stub_get_bool,
    .checkbox_set_checked = stub_set_bool,
    .widget_grid = stub_grid,
    .widget_grid_remove = stub_grid_remove,
    .container_grid_columnconfigure = stub_grid_configure,
    .container_grid_rowconfigure = stub_grid_configure,
    .set_callback = stub_set_cb,
    .message_box = stub_msgbox,
};
