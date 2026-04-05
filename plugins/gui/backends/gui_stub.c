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
static void *stub_create_dd(void *p, double a, double b)
{
    (void)p;
    (void)a;
    (void)b;
    return NULL;
}
static double stub_get_double(void *h)
{
    (void)h;
    return 0.0;
}
static void stub_set_double(void *h, double v)
{
    (void)h;
    (void)v;
}
static int stub_get_int(void *h)
{
    (void)h;
    return -1;
}
static void stub_set_int(void *h, int v)
{
    (void)h;
    (void)v;
}
static void *stub_create_group(void *p, const char *t, void *g)
{
    (void)p;
    (void)t;
    (void)g;
    return NULL;
}
static void *stub_create_ddd(void *p, double a, double b, double c)
{
    (void)p;
    (void)a;
    (void)b;
    (void)c;
    return NULL;
}
static void stub_menu_add_item(void *s, const char *l, gui_callback_t cb)
{
    (void)s;
    (void)l;
    (void)cb;
}
static void *stub_create_paned(void *p, bool h)
{
    (void)p;
    (void)h;
    return NULL;
}
static void stub_set_child(void *h, void *c)
{
    (void)h;
    (void)c;
}
static void stub_grid(void *h, int c, int r)
{
    (void)h;
    (void)c;
    (void)r;
}
static void stub_grid_span(void *h, int c, int r, int cs, int rs)
{
    (void)h;
    (void)c;
    (void)r;
    (void)cs;
    (void)rs;
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
static void stub_set_font(void *h, const char *f, int s)
{
    (void)h;
    (void)f;
    (void)s;
}
static void stub_msgbox(void *p, const char *t, const char *m)
{
    (void)p;
    (void)t;
    (void)m;
}
static bool stub_ask(void *p, const char *t, const char *m)
{
    (void)p;
    (void)t;
    (void)m;
    return false;
}
static void *stub_canvas_create(void *p, int w, int h)
{
    (void)p;
    (void)w;
    (void)h;
    return NULL;
}
static void stub_canvas_draw(void *h, double a, double b, double c, double d)
{
    (void)h;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
static void stub_canvas_text(void *h, double a, double b, const char *t)
{
    (void)h;
    (void)a;
    (void)b;
    (void)t;
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
    .slider_create = stub_create_dd,
    .slider_destroy = stub_destroy,
    .slider_get_value = stub_get_double,
    .slider_set_value = stub_set_double,
    .select_create = stub_create_notext,
    .select_destroy = stub_destroy,
    .select_add_item = stub_set_text,
    .select_get_index = stub_get_int,
    .select_set_index = stub_set_int,
    .text_create = stub_create_notext,
    .text_destroy = stub_destroy,
    .text_get_text = stub_get_text,
    .text_set_text = stub_set_text,
    .radio_create = stub_create_group,
    .radio_destroy = stub_destroy,
    .radio_set_text = stub_set_text,
    .radio_get_active = stub_get_bool,
    .radio_set_active = stub_set_bool,
    .spinbox_create = stub_create_ddd,
    .spinbox_destroy = stub_destroy,
    .spinbox_get_value = stub_get_double,
    .spinbox_set_value = stub_set_double,
    .frame_create = stub_create,
    .frame_destroy = stub_destroy,
    .frame_set_label = stub_set_text,
    .listbox_create = stub_create_notext,
    .listbox_destroy = stub_destroy,
    .listbox_add_item = stub_set_text,
    .listbox_get_selected = stub_get_int,
    .listbox_set_selected = stub_set_int,
    .menubar_create = stub_create_notext,
    .menubar_destroy = stub_destroy,
    .menu_add_submenu = stub_create,
    .menu_add_item = stub_menu_add_item,
    .paned_create = stub_create_paned,
    .paned_destroy = stub_destroy,
    .paned_set_start = stub_set_child,
    .paned_set_end = stub_set_child,
    .paned_set_position = stub_set_int,
    .widget_grid = stub_grid,
    .widget_grid_span = stub_grid_span,
    .widget_grid_remove = stub_grid_remove,
    .container_grid_columnconfigure = stub_grid_configure,
    .container_grid_rowconfigure = stub_grid_configure,
    .set_callback = stub_set_cb,
    .widget_set_font = stub_set_font,
    .widget_set_fg = stub_set_text,
    .widget_set_bg = stub_set_text,
    .widget_set_padding = stub_grid_configure,
    .widget_set_state = stub_set_text,
    .message_box = stub_msgbox,
    .open_file_dialog = stub_get_text,
    .save_file_dialog = stub_get_text,
    .choose_directory = stub_get_text,
    .ask_yes_no = stub_ask,
    .canvas_create = stub_canvas_create,
    .canvas_destroy = stub_destroy,
    .canvas_clear = stub_destroy,
    .canvas_draw_line = stub_canvas_draw,
    .canvas_draw_rect = stub_canvas_draw,
    .canvas_draw_oval = stub_canvas_draw,
    .canvas_draw_text = stub_canvas_text,
    .canvas_set_stroke_color = stub_set_text,
    .canvas_set_fill_color = stub_set_text,
    .canvas_set_line_width = stub_set_double,
    .timer_after = NULL,
    .timer_every = NULL,
    .timer_cancel = NULL,
    .toplevel_create = NULL,
    .toplevel_destroy = NULL,
    .toplevel_set_title = NULL,
    .toplevel_set_modal = NULL,
    .scrollbar_create = NULL,
    .scrollbar_destroy = NULL,
    .scrollbar_attach = NULL,
    .notebook_create = NULL,
    .notebook_destroy = NULL,
    .notebook_add_tab = NULL,
    .notebook_set_selected = NULL,
    .notebook_get_selected = NULL,
    .treeview_create = NULL,
    .treeview_destroy = NULL,
    .treeview_add_root = NULL,
    .treeview_add_child = NULL,
    .treeview_get_selected = NULL,
    .treeview_expand = NULL,
    .treeview_collapse = NULL,
    .toolbar_create = NULL,
    .toolbar_destroy = NULL,
    .toolbar_add_button = NULL,
    .toolbar_add_separator = NULL,
    .statusbar_create = NULL,
    .statusbar_destroy = NULL,
    .statusbar_set_text = NULL,
};
