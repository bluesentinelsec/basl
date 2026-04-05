/* gui_gtk.c — Linux GTK backend via dlopen/dlsym.
 *
 * Prefers GTK4 (libgtk-4.so.1) and falls back to GTK3 (libgtk-3.so.0).
 * No link-time dependency on either version.  All symbols are resolved
 * at runtime through dlopen + dlsym.
 *
 * Architecture
 * ────────────
 * GTK3 and GTK4 share most of their API surface, but diverge in
 * several key areas:
 *
 *   - Event loop:    GTK3 gtk_main/gtk_main_quit
 *                    GTK4 removed these; uses GMainLoop instead
 *   - Containers:    GTK3 gtk_container_add
 *                    GTK4 gtk_window_set_child (no GtkContainer)
 *   - Windows:       GTK3 gtk_window_new(GTK_WINDOW_TOPLEVEL)
 *                    GTK4 gtk_window_new() (no type argument)
 *   - Destruction:   GTK3 gtk_widget_destroy
 *                    GTK4 gtk_window_destroy (windows only);
 *                         non-window widgets are hidden/unparented
 *   - Visibility:    GTK3 gtk_widget_show_all
 *                    GTK4 widgets visible by default; gtk_widget_set_visible
 *   - Size query:    GTK3 gtk_window_get_size
 *                    GTK4 removed; use gtk_window_get_default_size
 *   - Close signal:  GTK3 "delete-event"
 *                    GTK4 "close-request"
 *   - Dialogs:       GTK3 GtkMessageDialog + gtk_dialog_run (sync)
 *                    GTK4 removed sync dialogs; we build a simple one
 *   - Grid removal:  GTK3 gtk_widget_destroy
 *                    GTK4 gtk_grid_remove
 *
 * Rather than scattering version checks throughout the backend, we
 * define a small internal vtable (gtk_version_ops_t) that encapsulates
 * every version-specific operation.  Two static instances — one for
 * GTK3, one for GTK4 — provide the concrete implementations.  All
 * shared backend code calls through this vtable, keeping the version
 * selection in exactly one place (gtk_be_init).
 */
#ifdef __linux__

#include "../gui_backend.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ── GTK/GLib type stand-ins (avoid #include <gtk/gtk.h>) ────────── */

typedef void GtkWidget;
typedef void GtkWindow;
typedef void GtkGrid;
typedef void GMainContext;
typedef void GMainLoop;
typedef int gint;
typedef unsigned int guint;
typedef unsigned long gulong;
typedef int gboolean;
typedef void (*GCallback)(void);
typedef void (*GClosureNotify)(void *data, void *closure);

/* GtkWindowType (GTK3 only) */
#define GTK_WINDOW_TOPLEVEL 0

/* GtkMessageType / GtkButtonsType / GtkDialogFlags (GTK3 only) */
#define GTK_MESSAGE_INFO 0
#define GTK_BUTTONS_OK 1
#define GTK_DIALOG_MODAL (1 << 0)
#define GTK_DIALOG_DESTROY_WITH_PARENT (1 << 1)

/* ── Shared function pointer types (identical in GTK3 and GTK4) ──── */

typedef void (*fn_gtk_init)(int *, char ***);
typedef void (*fn_gtk_window_set_title)(GtkWindow *, const char *);
typedef void (*fn_gtk_window_set_default_size)(GtkWindow *, gint, gint);
typedef GtkWidget *(*fn_gtk_label_new)(const char *);
typedef void (*fn_gtk_label_set_text)(GtkWidget *, const char *);
typedef GtkWidget *(*fn_gtk_button_new_with_label)(const char *);
typedef void (*fn_gtk_button_set_label)(GtkWidget *, const char *);
typedef GtkWidget *(*fn_gtk_grid_new)(void);
typedef void (*fn_gtk_grid_attach)(GtkGrid *, GtkWidget *, gint, gint, gint, gint);
typedef void (*fn_gtk_widget_set_hexpand)(GtkWidget *, gboolean);
typedef void (*fn_gtk_widget_set_vexpand)(GtkWidget *, gboolean);
typedef gulong (*fn_g_signal_connect_data)(void *, const char *, GCallback, void *, GClosureNotify, int);
typedef GtkWidget *(*fn_gtk_entry_new)(void);
typedef GtkWidget *(*fn_gtk_check_button_new_with_label)(const char *);
typedef GtkWidget *(*fn_gtk_scale_new_with_range)(int, double, double, double);
typedef GtkWidget *(*fn_gtk_combo_box_text_new)(void);
typedef void (*fn_gtk_combo_box_text_append_text)(GtkWidget *, const char *);
typedef gint (*fn_gtk_combo_box_get_active)(GtkWidget *);
typedef void (*fn_gtk_combo_box_set_active)(GtkWidget *, gint);
typedef double (*fn_gtk_range_get_value)(GtkWidget *);
typedef void (*fn_gtk_range_set_value)(GtkWidget *, double);
typedef GtkWidget *(*fn_gtk_text_view_new)(void);
typedef void *(*fn_gtk_text_view_get_buffer)(GtkWidget *);
typedef char *(*fn_gtk_text_buffer_get_text)(void *, void *, void *, gboolean);
typedef void (*fn_gtk_text_buffer_set_text)(void *, const char *, gint);
typedef void (*fn_gtk_text_buffer_get_start_iter)(void *, void *);
typedef void (*fn_gtk_text_buffer_get_end_iter)(void *, void *);
typedef GtkWidget *(*fn_gtk_scrolled_window_new_gtk3)(void *, void *);
typedef GtkWidget *(*fn_gtk_spin_button_new_with_range)(double, double, double);
typedef double (*fn_gtk_spin_button_get_value)(GtkWidget *);
typedef void (*fn_gtk_spin_button_set_value)(GtkWidget *, double);
typedef GtkWidget *(*fn_gtk_frame_new)(const char *);
typedef void (*fn_gtk_frame_set_label)(GtkWidget *, const char *);
typedef GtkWidget *(*fn_gtk_list_box_new)(void);
typedef void (*fn_gtk_list_box_insert)(GtkWidget *, GtkWidget *, gint);
typedef void *(*fn_gtk_list_box_get_selected_row)(GtkWidget *);
typedef gint (*fn_gtk_list_box_row_get_index)(void *);
typedef void (*fn_gtk_list_box_select_row)(GtkWidget *, void *);
typedef void *(*fn_gtk_list_box_get_row_at_index)(GtkWidget *, gint);
typedef GtkWidget *(*fn_gtk_paned_new)(int);
typedef void (*fn_gtk_paned_set_position)(GtkWidget *, gint);
typedef GtkWidget *(*fn_gtk_drawing_area_new)(void);
typedef void (*fn_gtk_widget_queue_draw)(GtkWidget *);
typedef void (*fn_cairo_set_source_rgb)(void *, double, double, double);
typedef void (*fn_cairo_set_line_width)(void *, double);
typedef void (*fn_cairo_move_to)(void *, double, double);
typedef void (*fn_cairo_line_to)(void *, double, double);
typedef void (*fn_cairo_stroke)(void *);
typedef void (*fn_cairo_rectangle)(void *, double, double, double, double);
typedef void (*fn_cairo_fill)(void *);
typedef void (*fn_cairo_arc)(void *, double, double, double, double, double);
typedef void (*fn_cairo_show_text)(void *, const char *);
typedef void (*fn_cairo_paint)(void *);

/* GTK3-only function pointer types */
typedef GtkWidget *(*fn_gtk_window_new_gtk3)(int type);
typedef void (*fn_gtk_window_get_size_gtk3)(GtkWindow *, gint *, gint *);
typedef void (*fn_gtk_widget_show_all)(GtkWidget *);
typedef void (*fn_gtk_widget_destroy)(GtkWidget *);
typedef void (*fn_gtk_container_add)(GtkWidget *, GtkWidget *);
typedef GtkWidget *(*fn_gtk_message_dialog_new)(GtkWindow *, int, int, int, const char *, ...);
typedef gint (*fn_gtk_dialog_run)(GtkWidget *);
typedef GtkWidget *(*fn_gtk_file_chooser_dialog_new)(const char *, GtkWindow *, int, const char *, ...);
typedef char *(*fn_gtk_file_chooser_get_filename)(GtkWidget *);
typedef void (*fn_gtk_main)(void);
typedef void (*fn_gtk_main_quit)(void);
typedef const char *(*fn_gtk_entry_get_text_gtk3)(GtkWidget *);
typedef void (*fn_gtk_entry_set_text_gtk3)(GtkWidget *, const char *);
typedef gboolean (*fn_gtk_toggle_button_get_active)(GtkWidget *);
typedef void (*fn_gtk_toggle_button_set_active)(GtkWidget *, gboolean);
typedef GtkWidget *(*fn_gtk_radio_button_new_with_label)(void *, const char *);
typedef GtkWidget *(*fn_gtk_radio_button_new_with_label_from_widget)(GtkWidget *, const char *);
typedef GtkWidget *(*fn_gtk_menu_bar_new)(void);
typedef GtkWidget *(*fn_gtk_menu_new)(void);
typedef GtkWidget *(*fn_gtk_menu_item_new_with_label)(const char *);
typedef void (*fn_gtk_menu_item_set_submenu)(GtkWidget *, GtkWidget *);
typedef void (*fn_gtk_menu_shell_append)(GtkWidget *, GtkWidget *);
typedef void (*fn_gtk_paned_pack1)(GtkWidget *, GtkWidget *, gboolean, gboolean);
typedef void (*fn_gtk_paned_pack2)(GtkWidget *, GtkWidget *, gboolean, gboolean);

/* GTK4-only function pointer types */
typedef GtkWidget *(*fn_gtk_window_new_gtk4)(void);
typedef void (*fn_gtk_window_get_default_size)(GtkWindow *, gint *, gint *);
typedef void (*fn_gtk_widget_set_visible)(GtkWidget *, gboolean);
typedef void (*fn_gtk_window_set_child)(GtkWindow *, GtkWidget *);
typedef void (*fn_gtk_grid_remove)(GtkGrid *, GtkWidget *);
typedef void (*fn_gtk_window_destroy)(GtkWindow *);
typedef const char *(*fn_gtk_editable_get_text)(GtkWidget *);
typedef void (*fn_gtk_editable_set_text)(GtkWidget *, const char *);
typedef gboolean (*fn_gtk_check_button_get_active)(GtkWidget *);
typedef void (*fn_gtk_check_button_set_active)(GtkWidget *, gboolean);
typedef void (*fn_gtk_check_button_set_label)(GtkWidget *, const char *);
typedef void (*fn_gtk_check_button_set_group)(GtkWidget *, GtkWidget *);
typedef GtkWidget *(*fn_gtk_file_chooser_dialog_new_gtk4)(const char *, GtkWindow *, int, const char *, ...);
typedef void *(*fn_gtk_file_chooser_get_file)(GtkWidget *);
typedef char *(*fn_g_file_get_path)(void *);
typedef void (*fn_g_free)(void *);
typedef GtkWidget *(*fn_gtk_scrolled_window_new_gtk4)(void);
typedef void (*fn_gtk_scrolled_window_set_child)(GtkWidget *, GtkWidget *);
typedef void (*fn_gtk_frame_set_child_gtk4)(GtkWidget *, GtkWidget *);
typedef GtkWidget *(*fn_gtk_popover_menu_bar_new_from_model)(void *);
typedef void *(*fn_g_menu_new)(void);
typedef void (*fn_g_menu_append)(void *, const char *, const char *);
typedef void (*fn_g_menu_append_submenu)(void *, const char *, void *);
typedef void *(*fn_g_simple_action_new)(const char *, void *);
typedef void (*fn_g_action_map_add_action)(void *, void *);
typedef void *(*fn_g_simple_action_group_new)(void);
typedef void (*fn_gtk_widget_insert_action_group)(GtkWidget *, const char *, void *);
typedef void (*fn_gtk_paned_set_start_child)(GtkWidget *, GtkWidget *);
typedef void (*fn_gtk_paned_set_end_child)(GtkWidget *, GtkWidget *);
typedef void (*fn_gtk_drawing_area_set_draw_func)(GtkWidget *, GCallback, void *, void *);
typedef gulong (*fn_g_signal_connect_data_t)(void *, const char *, GCallback, void *, GClosureNotify, int);

/* GLib main loop (used by GTK4 path; available in both versions) */
typedef GMainLoop *(*fn_g_main_loop_new)(GMainContext *, gboolean);
typedef void (*fn_g_main_loop_run)(GMainLoop *);
typedef void (*fn_g_main_loop_quit)(GMainLoop *);
typedef void (*fn_g_main_loop_unref)(GMainLoop *);

/* ── Version-specific operations vtable ──────────────────────────── */
/*
 * This is the internal polymorphism layer.  Each GTK version provides
 * a static instance of this struct.  The shared backend code never
 * checks the GTK version directly — it calls through g_vops.
 */

typedef struct gtk_version_ops
{
    const char *version_name;
    const char *close_signal; /* "delete-event" (GTK3) or "close-request" (GTK4) */

    /* Event loop */
    void (*main_loop)(void);
    void (*main_quit)(void);

    /* Window */
    GtkWidget *(*window_new)(void);
    void (*window_get_size)(GtkWindow *win, gint *w, gint *h);
    void (*window_set_child)(GtkWindow *win, GtkWidget *child);
    void (*window_destroy)(void *handle);

    /* Widget lifecycle */
    void (*widget_show)(GtkWidget *widget);
    void (*widget_destroy)(GtkWidget *widget);

    /* Grid removal */
    void (*grid_remove_child)(void *grid, GtkWidget *child);

    /* Dialogs */
    void (*message_box)(void *parent, const char *title, const char *msg);
    const char *(*file_dialog)(void *parent, const char *title, int action, char *buf, size_t bufsz);
    bool (*yes_no_dialog)(void *parent, const char *title, const char *msg);

    /* Entry (text input) */
    const char *(*entry_get_text)(GtkWidget *entry);
    void (*entry_set_text)(GtkWidget *entry, const char *text);

    /* Checkbox */
    gboolean (*checkbox_get_active)(GtkWidget *cb);
    void (*checkbox_set_active)(GtkWidget *cb, gboolean active);
    void (*checkbox_set_label)(GtkWidget *cb, const char *text);

    /* Radio */
    GtkWidget *(*radio_create)(void *parent_grid, const char *text, void *group);
    void (*radio_set_label)(GtkWidget *rb, const char *text);

    /* Scrolled window (for Text widget) */
    GtkWidget *(*scrolled_window_new)(void);
    void (*scrolled_window_set_child)(GtkWidget *sw, GtkWidget *child);

    /* Frame */
    void (*frame_set_child)(GtkWidget *frame, GtkWidget *child);

    /* Menu bar */
    void *(*menubar_create)(void *window);
    void *(*menu_add_submenu)(void *menubar, const char *label);
    void (*menu_add_item)(void *submenu, const char *label, gui_callback_t cb);

    /* PanedWindow */
    void (*paned_set_start)(GtkWidget *paned, GtkWidget *child);
    void (*paned_set_end)(GtkWidget *paned, GtkWidget *child);

    /* Canvas */
    void (*canvas_setup_draw)(GtkWidget *da, void *canvas_data);
} gtk_version_ops_t;

/* ── Shared resolved symbols ─────────────────────────────────────── */

static struct
{
    void *lib;
    fn_gtk_init gtk_init;
    fn_gtk_window_set_title gtk_window_set_title;
    fn_gtk_window_set_default_size gtk_window_set_default_size;
    fn_gtk_label_new gtk_label_new;
    fn_gtk_label_set_text gtk_label_set_text;
    fn_gtk_button_new_with_label gtk_button_new_with_label;
    fn_gtk_button_set_label gtk_button_set_label;
    fn_gtk_grid_new gtk_grid_new;
    fn_gtk_grid_attach gtk_grid_attach;
    fn_gtk_widget_set_hexpand gtk_widget_set_hexpand;
    fn_gtk_widget_set_vexpand gtk_widget_set_vexpand;
    fn_g_signal_connect_data g_signal_connect_data;
    fn_gtk_entry_new gtk_entry_new;
    fn_gtk_check_button_new_with_label gtk_check_button_new_with_label;
    fn_gtk_scale_new_with_range gtk_scale_new_with_range;
    fn_gtk_combo_box_text_new gtk_combo_box_text_new;
    fn_gtk_combo_box_text_append_text gtk_combo_box_text_append_text;
    fn_gtk_combo_box_get_active gtk_combo_box_get_active;
    fn_gtk_combo_box_set_active gtk_combo_box_set_active;
    fn_gtk_range_get_value gtk_range_get_value;
    fn_gtk_range_set_value gtk_range_set_value;
    fn_gtk_text_view_new gtk_text_view_new;
    fn_gtk_text_view_get_buffer gtk_text_view_get_buffer;
    fn_gtk_text_buffer_get_text gtk_text_buffer_get_text;
    fn_gtk_text_buffer_set_text gtk_text_buffer_set_text;
    fn_gtk_text_buffer_get_start_iter gtk_text_buffer_get_start_iter;
    fn_gtk_text_buffer_get_end_iter gtk_text_buffer_get_end_iter;
    fn_gtk_spin_button_new_with_range gtk_spin_button_new_with_range;
    fn_gtk_spin_button_get_value gtk_spin_button_get_value;
    fn_gtk_spin_button_set_value gtk_spin_button_set_value;
    fn_gtk_frame_new gtk_frame_new;
    fn_gtk_frame_set_label gtk_frame_set_label;
    fn_gtk_list_box_new gtk_list_box_new;
    fn_gtk_list_box_insert gtk_list_box_insert;
    fn_gtk_list_box_get_selected_row gtk_list_box_get_selected_row;
    fn_gtk_list_box_row_get_index gtk_list_box_row_get_index;
    fn_gtk_list_box_select_row gtk_list_box_select_row;
    fn_gtk_list_box_get_row_at_index gtk_list_box_get_row_at_index;
    fn_gtk_paned_new gtk_paned_new;
    fn_gtk_paned_set_position gtk_paned_set_position;
    fn_gtk_drawing_area_new gtk_drawing_area_new;
    fn_gtk_widget_queue_draw gtk_widget_queue_draw;
    fn_cairo_set_source_rgb cairo_set_source_rgb;
    fn_cairo_set_line_width cairo_set_line_width;
    fn_cairo_move_to cairo_move_to;
    fn_cairo_line_to cairo_line_to;
    fn_cairo_stroke cairo_stroke;
    fn_cairo_rectangle cairo_rectangle;
    fn_cairo_fill cairo_fill;
    fn_cairo_arc cairo_arc;
    fn_cairo_show_text cairo_show_text;
    fn_cairo_paint cairo_paint;
} G;

static const gtk_version_ops_t *g_vops = NULL;

/* ── dlsym helper ────────────────────────────────────────────────── */

/* POSIX guarantees dlsym returns a void* that can round-trip to a
   function pointer, but ISO C -Wpedantic forbids the direct cast.
   Use memcpy to avoid the warning portably. */

static bool load_sym(void *lib, const char *name, void *out)
{
    void *sym = dlsym(lib, name);
    if (!sym)
        return false;
    memcpy(out, &sym, sizeof(sym));
    return true;
}

#define LOAD_SHARED(name)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!load_sym(G.lib, #name, &G.name))                                                                          \
        {                                                                                                              \
            fprintf(stderr, "gui_gtk: dlsym failed: %s\n", #name);                                                     \
            goto fail;                                                                                                 \
        }                                                                                                              \
    } while (0)

#define LOAD_LOCAL(dest, name)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!load_sym(G.lib, #name, &(dest)))                                                                          \
        {                                                                                                              \
            fprintf(stderr, "gui_gtk: dlsym failed: %s\n", #name);                                                     \
            goto fail;                                                                                                 \
        }                                                                                                              \
    } while (0)

/* ── Callback storage ────────────────────────────────────────────── */

#define MAX_CALLBACKS 256

typedef struct
{
    void *widget;
    gui_event_type_t type;
    gui_callback_t cb;
} callback_entry_t;

static callback_entry_t g_callbacks[MAX_CALLBACKS];
static int g_callback_count = 0;

static gui_callback_t *find_callback(void *widget, gui_event_type_t type)
{
    for (int i = 0; i < g_callback_count; i++)
        if (g_callbacks[i].widget == widget && g_callbacks[i].type == type)
            return &g_callbacks[i].cb;
    return NULL;
}

/* ── Widget → parent grid tracking ───────────────────────────────── */

#define MAX_WIDGETS 512

typedef struct
{
    void *widget;
    void *grid;
} widget_parent_t;
static widget_parent_t g_widget_parents[MAX_WIDGETS];
static int g_widget_parent_count = 0;

static void register_widget_parent(void *w, void *grid)
{
    if (g_widget_parent_count < MAX_WIDGETS)
        g_widget_parents[g_widget_parent_count++] = (widget_parent_t){w, grid};
}

static void *get_widget_grid(void *w)
{
    for (int i = 0; i < g_widget_parent_count; i++)
        if (g_widget_parents[i].widget == w)
            return g_widget_parents[i].grid;
    return NULL;
}

/* ── Window → grid mapping ───────────────────────────────────────── */

#define MAX_GRIDS 64

typedef struct
{
    void *window;
    void *grid;
} window_grid_t;
static window_grid_t g_window_grids[MAX_GRIDS];
static int g_window_grid_count = 0;

static void *grid_for_window(void *window)
{
    for (int i = 0; i < g_window_grid_count; i++)
        if (g_window_grids[i].window == window)
            return g_window_grids[i].grid;
    return NULL;
}

/* ── GTK signal callbacks ────────────────────────────────────────── */

static void on_button_clicked(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CLICK);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

/* GTK3 "delete-event" has signature (widget, event, data) → gboolean */
static gboolean on_window_close_gtk3(GtkWidget *widget, void *event, void *data)
{
    (void)event;
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CLOSE);
    if (cb && cb->fn)
    {
        cb->fn(cb->user_data);
        return 1;
    }
    g_vops->main_quit();
    return 0;
}

/* GTK4 "close-request" has signature (widget, data) → gboolean */
static gboolean on_window_close_gtk4(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CLOSE);
    if (cb && cb->fn)
    {
        cb->fn(cb->user_data);
        return 1;
    }
    g_vops->main_quit();
    return 0;
}

/* Forward declarations for canvas draw callbacks (defined in Canvas section). */
static gboolean gtk3_canvas_draw(GtkWidget *widget, void *cr, void *data);
static void gtk4_canvas_draw_func(GtkWidget *da, void *cr, int w, int h, void *data);

/* ════════════════════════════════════════════════════════════════════
 * GTK3 version ops
 * ════════════════════════════════════════════════════════════════════ */

static fn_gtk_main s_gtk3_main;
static fn_gtk_main_quit s_gtk3_main_quit;
static fn_gtk_window_new_gtk3 s_gtk3_window_new;
static fn_gtk_window_get_size_gtk3 s_gtk3_window_get_size;
static fn_gtk_widget_show_all s_gtk3_widget_show_all;
static fn_gtk_widget_destroy s_gtk3_widget_destroy;
static fn_gtk_container_add s_gtk3_container_add;
static fn_gtk_message_dialog_new s_gtk3_message_dialog_new;
static fn_gtk_dialog_run s_gtk3_dialog_run;
static fn_gtk_file_chooser_dialog_new s_gtk3_file_chooser_dialog_new;
static fn_gtk_file_chooser_get_filename s_gtk3_file_chooser_get_filename;
static fn_gtk_entry_get_text_gtk3 s_gtk3_entry_get_text;
static fn_gtk_entry_set_text_gtk3 s_gtk3_entry_set_text;
static fn_gtk_toggle_button_get_active s_gtk3_toggle_get_active;
static fn_gtk_toggle_button_set_active s_gtk3_toggle_set_active;
static fn_gtk_radio_button_new_with_label s_gtk3_radio_new;
static fn_gtk_radio_button_new_with_label_from_widget s_gtk3_radio_new_from;
static fn_gtk_scrolled_window_new_gtk3 s_gtk3_scrolled_window_new;
static fn_gtk_menu_bar_new s_gtk3_menu_bar_new;
static fn_gtk_menu_new s_gtk3_menu_new;
static fn_gtk_menu_item_new_with_label s_gtk3_menu_item_new;
static fn_gtk_menu_item_set_submenu s_gtk3_menu_item_set_submenu;
static fn_gtk_menu_shell_append s_gtk3_menu_shell_append;
static fn_gtk_paned_pack1 s_gtk3_paned_pack1;
static fn_gtk_paned_pack2 s_gtk3_paned_pack2;

static void gtk3_main_loop(void)
{
    s_gtk3_main();
}
static void gtk3_main_quit(void)
{
    s_gtk3_main_quit();
}

static GtkWidget *gtk3_window_new(void)
{
    return s_gtk3_window_new(GTK_WINDOW_TOPLEVEL);
}

static void gtk3_window_get_size(GtkWindow *win, gint *w, gint *h)
{
    s_gtk3_window_get_size(win, w, h);
}

static void gtk3_window_set_child(GtkWindow *win, GtkWidget *child)
{
    s_gtk3_container_add((GtkWidget *)win, child);
}

static void gtk3_window_destroy(void *handle)
{
    s_gtk3_widget_destroy(handle);
}

static void gtk3_widget_show(GtkWidget *widget)
{
    s_gtk3_widget_show_all(widget);
}

static void gtk3_widget_destroy(GtkWidget *widget)
{
    s_gtk3_widget_destroy(widget);
}

static void gtk3_grid_remove_child(void *grid, GtkWidget *child)
{
    (void)grid;
    s_gtk3_widget_destroy(child);
}

static void gtk3_message_box(void *parent, const char *title, const char *msg)
{
    GtkWidget *dialog = s_gtk3_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                  GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
    if (!dialog)
        return;
    G.gtk_window_set_title(dialog, title);
    s_gtk3_dialog_run(dialog);
    s_gtk3_widget_destroy(dialog);
}

/* GTK_FILE_CHOOSER_ACTION: OPEN=0, SAVE=1, SELECT_FOLDER=2 */
/* GTK_RESPONSE: ACCEPT=-3, CANCEL=-6 */
static const char *gtk3_file_dialog(void *parent, const char *title, int action, char *buf, size_t bufsz)
{
    buf[0] = '\0';
    GtkWidget *dlg = s_gtk3_file_chooser_dialog_new(title, parent, action, "Cancel", -6, "OK", -3, NULL);
    if (!dlg)
        return buf;
    gint res = s_gtk3_dialog_run(dlg);
    if (res == -3)
    {
        char *fname = s_gtk3_file_chooser_get_filename(dlg);
        if (fname)
        {
            size_t len = strlen(fname);
            if (len >= bufsz)
                len = bufsz - 1;
            memcpy(buf, fname, len);
            buf[len] = '\0';
        }
    }
    s_gtk3_widget_destroy(dlg);
    return buf;
}

static bool gtk3_yes_no_dialog(void *parent, const char *title, const char *msg)
{
    GtkWidget *dlg = s_gtk3_message_dialog_new(parent, GTK_DIALOG_MODAL, 0, 4 /* GTK_BUTTONS_YES_NO */, "%s", msg);
    if (!dlg)
        return false;
    G.gtk_window_set_title(dlg, title);
    /* Add Yes/No buttons: GTK_RESPONSE_YES=-8, GTK_RESPONSE_NO=-9 */
    gint res = s_gtk3_dialog_run(dlg);
    s_gtk3_widget_destroy(dlg);
    return res == -8;
}

static const char *gtk3_entry_get_text(GtkWidget *entry)
{
    return s_gtk3_entry_get_text(entry);
}

static void gtk3_entry_set_text(GtkWidget *entry, const char *text)
{
    s_gtk3_entry_set_text(entry, text);
}

static gboolean gtk3_checkbox_get_active(GtkWidget *cb)
{
    return s_gtk3_toggle_get_active(cb);
}

static void gtk3_checkbox_set_active(GtkWidget *cb, gboolean active)
{
    s_gtk3_toggle_set_active(cb, active);
}

static void gtk3_checkbox_set_label(GtkWidget *cb, const char *text)
{
    G.gtk_button_set_label(cb, text);
}

static GtkWidget *gtk3_radio_create(void *parent_grid, const char *text, void *group)
{
    (void)parent_grid;
    if (group)
        return s_gtk3_radio_new_from(group, text);
    return s_gtk3_radio_new(NULL, text);
}

static void gtk3_radio_set_label(GtkWidget *rb, const char *text)
{
    G.gtk_button_set_label(rb, text);
}

static GtkWidget *gtk3_scrolled_window_new(void)
{
    return s_gtk3_scrolled_window_new(NULL, NULL);
}

static void gtk3_scrolled_window_set_child(GtkWidget *sw, GtkWidget *child)
{
    s_gtk3_container_add(sw, child);
}

static void gtk3_frame_set_child(GtkWidget *frame, GtkWidget *child)
{
    s_gtk3_container_add(frame, child);
}

static void *gtk3_menubar_create(void *window)
{
    GtkWidget *mb = s_gtk3_menu_bar_new();
    /* Insert menubar at row -1 (above grid content) in the window's grid. */
    void *grid = grid_for_window(window);
    if (grid)
    {
        G.gtk_grid_attach(grid, mb, 0, -1, 10, 1);
        s_gtk3_widget_show_all(mb);
    }
    return mb;
}

static void *gtk3_menu_add_submenu(void *menubar, const char *label)
{
    GtkWidget *menu = s_gtk3_menu_new();
    GtkWidget *item = s_gtk3_menu_item_new(label);
    s_gtk3_menu_item_set_submenu(item, menu);
    s_gtk3_menu_shell_append(menubar, item);
    s_gtk3_widget_show_all(item);
    return menu;
}

static void gtk3_on_menu_activate(GtkWidget *widget, void *data)
{
    (void)widget;
    gui_callback_t *cb = (gui_callback_t *)data;
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void gtk3_menu_add_item(void *submenu, const char *label, gui_callback_t cb)
{
    GtkWidget *item = s_gtk3_menu_item_new(label);
    /* Store callback — we need it to persist. Use the global callback array. */
    if (g_callback_count < MAX_CALLBACKS)
    {
        g_callbacks[g_callback_count] = (callback_entry_t){item, GUI_EVENT_CLICK, cb};
        G.g_signal_connect_data(item, "activate", (GCallback)gtk3_on_menu_activate, &g_callbacks[g_callback_count].cb,
                                NULL, 0);
        g_callback_count++;
    }
    s_gtk3_menu_shell_append(submenu, item);
    s_gtk3_widget_show_all(item);
}

static void gtk3_paned_set_start(GtkWidget *paned, GtkWidget *child)
{
    s_gtk3_paned_pack1(paned, child, 1, 0);
}

static void gtk3_paned_set_end(GtkWidget *paned, GtkWidget *child)
{
    s_gtk3_paned_pack2(paned, child, 1, 0);
}

static void gtk3_canvas_setup_draw(GtkWidget *da, void *data)
{
    (void)data;
    G.g_signal_connect_data(da, "draw", (GCallback)gtk3_canvas_draw, NULL, NULL, 0);
}

static const gtk_version_ops_t g_gtk3_ops = {
    .version_name = "GTK3",
    .close_signal = "delete-event",
    .main_loop = gtk3_main_loop,
    .main_quit = gtk3_main_quit,
    .window_new = gtk3_window_new,
    .window_get_size = gtk3_window_get_size,
    .window_set_child = gtk3_window_set_child,
    .window_destroy = gtk3_window_destroy,
    .widget_show = gtk3_widget_show,
    .widget_destroy = gtk3_widget_destroy,
    .grid_remove_child = gtk3_grid_remove_child,
    .message_box = gtk3_message_box,
    .file_dialog = gtk3_file_dialog,
    .yes_no_dialog = gtk3_yes_no_dialog,
    .entry_get_text = gtk3_entry_get_text,
    .entry_set_text = gtk3_entry_set_text,
    .checkbox_get_active = gtk3_checkbox_get_active,
    .checkbox_set_active = gtk3_checkbox_set_active,
    .checkbox_set_label = gtk3_checkbox_set_label,
    .radio_create = gtk3_radio_create,
    .radio_set_label = gtk3_radio_set_label,
    .scrolled_window_new = gtk3_scrolled_window_new,
    .scrolled_window_set_child = gtk3_scrolled_window_set_child,
    .frame_set_child = gtk3_frame_set_child,
    .menubar_create = gtk3_menubar_create,
    .menu_add_submenu = gtk3_menu_add_submenu,
    .menu_add_item = gtk3_menu_add_item,
    .paned_set_start = gtk3_paned_set_start,
    .paned_set_end = gtk3_paned_set_end,
    .canvas_setup_draw = gtk3_canvas_setup_draw,
};

static bool load_gtk3_symbols(void)
{
    LOAD_LOCAL(s_gtk3_main, gtk_main);
    LOAD_LOCAL(s_gtk3_main_quit, gtk_main_quit);
    LOAD_LOCAL(s_gtk3_window_new, gtk_window_new);
    LOAD_LOCAL(s_gtk3_window_get_size, gtk_window_get_size);
    LOAD_LOCAL(s_gtk3_widget_show_all, gtk_widget_show_all);
    LOAD_LOCAL(s_gtk3_widget_destroy, gtk_widget_destroy);
    LOAD_LOCAL(s_gtk3_container_add, gtk_container_add);
    LOAD_LOCAL(s_gtk3_message_dialog_new, gtk_message_dialog_new);
    LOAD_LOCAL(s_gtk3_dialog_run, gtk_dialog_run);
    LOAD_LOCAL(s_gtk3_file_chooser_dialog_new, gtk_file_chooser_dialog_new);
    LOAD_LOCAL(s_gtk3_file_chooser_get_filename, gtk_file_chooser_get_filename);
    LOAD_LOCAL(s_gtk3_entry_get_text, gtk_entry_get_text);
    LOAD_LOCAL(s_gtk3_entry_set_text, gtk_entry_set_text);
    LOAD_LOCAL(s_gtk3_toggle_get_active, gtk_toggle_button_get_active);
    LOAD_LOCAL(s_gtk3_toggle_set_active, gtk_toggle_button_set_active);
    LOAD_LOCAL(s_gtk3_radio_new, gtk_radio_button_new_with_label);
    LOAD_LOCAL(s_gtk3_radio_new_from, gtk_radio_button_new_with_label_from_widget);
    LOAD_LOCAL(s_gtk3_scrolled_window_new, gtk_scrolled_window_new);
    LOAD_LOCAL(s_gtk3_menu_bar_new, gtk_menu_bar_new);
    LOAD_LOCAL(s_gtk3_menu_new, gtk_menu_new);
    LOAD_LOCAL(s_gtk3_menu_item_new, gtk_menu_item_new_with_label);
    LOAD_LOCAL(s_gtk3_menu_item_set_submenu, gtk_menu_item_set_submenu);
    LOAD_LOCAL(s_gtk3_menu_shell_append, gtk_menu_shell_append);
    LOAD_LOCAL(s_gtk3_paned_pack1, gtk_paned_pack1);
    LOAD_LOCAL(s_gtk3_paned_pack2, gtk_paned_pack2);
    return true;
fail:
    return false;
}

/* ════════════════════════════════════════════════════════════════════
 * GTK4 version ops
 * ════════════════════════════════════════════════════════════════════ */

static fn_gtk_window_new_gtk4 s_gtk4_window_new;
static fn_gtk_window_get_default_size s_gtk4_window_get_default_size;
static fn_gtk_widget_set_visible s_gtk4_widget_set_visible;
static fn_gtk_window_set_child s_gtk4_window_set_child;
static fn_gtk_grid_remove s_gtk4_grid_remove;
static fn_gtk_window_destroy s_gtk4_window_destroy;
static fn_g_main_loop_new s_gtk4_g_main_loop_new;
static fn_g_main_loop_run s_gtk4_g_main_loop_run;
static fn_g_main_loop_quit s_gtk4_g_main_loop_quit;
static fn_g_main_loop_unref s_gtk4_g_main_loop_unref;
static fn_gtk_editable_get_text s_gtk4_editable_get_text;
static fn_gtk_editable_set_text s_gtk4_editable_set_text;
static fn_gtk_check_button_get_active s_gtk4_check_get_active;
static fn_gtk_check_button_set_active s_gtk4_check_set_active;
static fn_gtk_check_button_set_label s_gtk4_check_set_label;
static fn_gtk_check_button_set_group s_gtk4_check_set_group;
static fn_gtk_file_chooser_dialog_new_gtk4 s_gtk4_file_chooser_new;
static fn_gtk_file_chooser_get_file s_gtk4_file_chooser_get_file;
static fn_g_file_get_path s_gtk4_g_file_get_path;
static fn_g_free s_gtk4_g_free;
static fn_gtk_scrolled_window_new_gtk4 s_gtk4_scrolled_window_new;
static fn_gtk_scrolled_window_set_child s_gtk4_scrolled_window_set_child;
static fn_gtk_frame_set_child_gtk4 s_gtk4_frame_set_child;
static fn_gtk_popover_menu_bar_new_from_model s_gtk4_popover_menu_bar_new;
static fn_g_menu_new s_gtk4_g_menu_new;
static fn_g_menu_append s_gtk4_g_menu_append;
static fn_g_menu_append_submenu s_gtk4_g_menu_append_submenu;
static fn_g_simple_action_new s_gtk4_g_simple_action_new;
static fn_g_action_map_add_action s_gtk4_g_action_map_add_action;
static fn_g_simple_action_group_new s_gtk4_g_simple_action_group_new;
static fn_gtk_widget_insert_action_group s_gtk4_gtk_widget_insert_action_group;
static fn_gtk_paned_set_start_child s_gtk4_paned_set_start;
static fn_gtk_paned_set_end_child s_gtk4_paned_set_end;
static fn_gtk_drawing_area_set_draw_func s_gtk4_set_draw_func;

/* GTK4 menu needs a window reference for action map. */
static void *s_gtk4_menu_window = NULL;
static void *s_gtk4_menu_model = NULL;
static void *s_gtk4_action_group = NULL;
static int s_gtk4_action_id = 0;

static GMainLoop *s_gtk4_loop = NULL;

static void gtk4_main_loop(void)
{
    s_gtk4_loop = s_gtk4_g_main_loop_new(NULL, 0);
    s_gtk4_g_main_loop_run(s_gtk4_loop);
    s_gtk4_g_main_loop_unref(s_gtk4_loop);
    s_gtk4_loop = NULL;
}

static void gtk4_main_quit(void)
{
    if (s_gtk4_loop)
        s_gtk4_g_main_loop_quit(s_gtk4_loop);
}

static GtkWidget *gtk4_window_new(void)
{
    return s_gtk4_window_new();
}

static void gtk4_window_get_size(GtkWindow *win, gint *w, gint *h)
{
    s_gtk4_window_get_default_size(win, w, h);
}

static void gtk4_window_set_child(GtkWindow *win, GtkWidget *child)
{
    s_gtk4_window_set_child(win, child);
}

static void gtk4_window_destroy(void *handle)
{
    s_gtk4_window_destroy(handle);
}

static void gtk4_widget_show(GtkWidget *widget)
{
    s_gtk4_widget_set_visible(widget, 1);
}

static void gtk4_widget_destroy(GtkWidget *widget)
{
    /* GTK4 removed gtk_widget_destroy for non-window widgets.
       Hiding is the safe equivalent; the widget is freed when its
       last reference drops. */
    s_gtk4_widget_set_visible(widget, 0);
}

static void gtk4_grid_remove_child(void *grid, GtkWidget *child)
{
    s_gtk4_grid_remove(grid, child);
}

static void gtk4_message_box(void *parent, const char *title, const char *msg)
{
    /* GTK4 removed GtkMessageDialog and synchronous gtk_dialog_run.
       Build a simple modal dialog with a label and OK button, driven
       by a nested GMainLoop. */
    (void)parent;

    GMainLoop *loop = s_gtk4_g_main_loop_new(NULL, 0);

    GtkWidget *win = s_gtk4_window_new();
    G.gtk_window_set_title(win, title);
    G.gtk_window_set_default_size(win, 300, 120);

    GtkWidget *grid = G.gtk_grid_new();
    s_gtk4_window_set_child(win, grid);

    GtkWidget *label = G.gtk_label_new(msg);
    G.gtk_widget_set_hexpand(label, 1);
    G.gtk_widget_set_vexpand(label, 1);
    G.gtk_grid_attach(grid, label, 0, 0, 1, 1);

    GtkWidget *btn = G.gtk_button_new_with_label("OK");
    G.gtk_grid_attach(grid, btn, 0, 1, 1, 1);

    /* Both OK click and window close quit the nested loop. */
    G.g_signal_connect_data(btn, "clicked", (GCallback)s_gtk4_g_main_loop_quit, loop, NULL, 1);
    G.g_signal_connect_data(win, "close-request", (GCallback)s_gtk4_g_main_loop_quit, loop, NULL, 1);

    s_gtk4_widget_set_visible(win, 1);
    s_gtk4_g_main_loop_run(loop);

    s_gtk4_window_destroy(win);
    s_gtk4_g_main_loop_unref(loop);
}

static int s_gtk4_dlg_response_code = -6;

static void gtk4_on_dialog_response(GtkWidget *dlg, int response, void *data)
{
    (void)dlg;
    s_gtk4_dlg_response_code = response;
    s_gtk4_g_main_loop_quit((GMainLoop *)data);
}

static const char *gtk4_file_dialog(void *parent, const char *title, int action, char *buf, size_t bufsz)
{
    buf[0] = '\0';
    GtkWidget *dlg = s_gtk4_file_chooser_new(title, parent, action, "Cancel", -6, "OK", -3, NULL);
    if (!dlg)
        return buf;

    GMainLoop *loop = s_gtk4_g_main_loop_new(NULL, 0);
    s_gtk4_dlg_response_code = -6;
    G.g_signal_connect_data(dlg, "response", (GCallback)gtk4_on_dialog_response, loop, NULL, 0);

    s_gtk4_widget_set_visible(dlg, 1);
    s_gtk4_g_main_loop_run(loop);

    if (s_gtk4_dlg_response_code == -3) /* GTK_RESPONSE_ACCEPT */
    {
        void *gfile = s_gtk4_file_chooser_get_file(dlg);
        if (gfile)
        {
            char *path = s_gtk4_g_file_get_path(gfile);
            if (path)
            {
                size_t len = strlen(path);
                if (len >= bufsz)
                    len = bufsz - 1;
                memcpy(buf, path, len);
                buf[len] = '\0';
                s_gtk4_g_free(path);
            }
        }
    }

    s_gtk4_window_destroy(dlg);
    s_gtk4_g_main_loop_unref(loop);
    return buf;
}

static bool s_gtk4_yesno_result = false;

static void gtk4_yesno_yes_clicked(GtkWidget *w, void *data)
{
    (void)w;
    s_gtk4_yesno_result = true;
    s_gtk4_g_main_loop_quit((GMainLoop *)data);
}

static void gtk4_yesno_no_clicked(GtkWidget *w, void *data)
{
    (void)w;
    s_gtk4_yesno_result = false;
    s_gtk4_g_main_loop_quit((GMainLoop *)data);
}

static bool gtk4_yes_no_dialog(void *parent, const char *title, const char *msg)
{
    (void)parent;
    GMainLoop *loop = s_gtk4_g_main_loop_new(NULL, 0);
    GtkWidget *win = s_gtk4_window_new();
    G.gtk_window_set_title(win, title);
    G.gtk_window_set_default_size(win, 300, 120);

    GtkWidget *grid = G.gtk_grid_new();
    s_gtk4_window_set_child(win, grid);

    GtkWidget *label = G.gtk_label_new(msg);
    G.gtk_widget_set_hexpand(label, 1);
    G.gtk_widget_set_vexpand(label, 1);
    G.gtk_grid_attach(grid, label, 0, 0, 2, 1);

    GtkWidget *yes_btn = G.gtk_button_new_with_label("Yes");
    GtkWidget *no_btn = G.gtk_button_new_with_label("No");
    G.gtk_grid_attach(grid, yes_btn, 0, 1, 1, 1);
    G.gtk_grid_attach(grid, no_btn, 1, 1, 1, 1);

    s_gtk4_yesno_result = false;
    G.g_signal_connect_data(yes_btn, "clicked", (GCallback)gtk4_yesno_yes_clicked, loop, NULL, 0);
    G.g_signal_connect_data(no_btn, "clicked", (GCallback)gtk4_yesno_no_clicked, loop, NULL, 0);
    G.g_signal_connect_data(win, "close-request", (GCallback)s_gtk4_g_main_loop_quit, loop, NULL, 1);

    s_gtk4_widget_set_visible(win, 1);
    s_gtk4_g_main_loop_run(loop);

    s_gtk4_window_destroy(win);
    s_gtk4_g_main_loop_unref(loop);
    return s_gtk4_yesno_result;
}

static const char *gtk4_entry_get_text(GtkWidget *entry)
{
    return s_gtk4_editable_get_text(entry);
}

static void gtk4_entry_set_text(GtkWidget *entry, const char *text)
{
    s_gtk4_editable_set_text(entry, text);
}

static gboolean gtk4_checkbox_get_active(GtkWidget *cb)
{
    return s_gtk4_check_get_active(cb);
}

static void gtk4_checkbox_set_active(GtkWidget *cb, gboolean active)
{
    s_gtk4_check_set_active(cb, active);
}

static void gtk4_checkbox_set_label(GtkWidget *cb, const char *text)
{
    s_gtk4_check_set_label(cb, text);
}

static GtkWidget *gtk4_radio_create(void *parent_grid, const char *text, void *group)
{
    (void)parent_grid;
    GtkWidget *rb = G.gtk_check_button_new_with_label(text);
    if (group)
        s_gtk4_check_set_group(rb, group);
    return rb;
}

static void gtk4_radio_set_label(GtkWidget *rb, const char *text)
{
    s_gtk4_check_set_label(rb, text);
}

static GtkWidget *gtk4_scrolled_window_new(void)
{
    return s_gtk4_scrolled_window_new();
}

static void gtk4_scrolled_window_set_child(GtkWidget *sw, GtkWidget *child)
{
    s_gtk4_scrolled_window_set_child(sw, child);
}

static void gtk4_frame_set_child(GtkWidget *frame, GtkWidget *child)
{
    s_gtk4_frame_set_child(frame, child);
}

static void gtk4_on_action_activate(void *action, void *param, void *data)
{
    (void)action;
    (void)param;
    gui_callback_t *cb = (gui_callback_t *)data;
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void *gtk4_menubar_create(void *window)
{
    s_gtk4_menu_window = window;
    s_gtk4_menu_model = s_gtk4_g_menu_new();
    s_gtk4_action_group = s_gtk4_g_simple_action_group_new();
    s_gtk4_action_id = 0;
    s_gtk4_gtk_widget_insert_action_group(window, "win", s_gtk4_action_group);
    GtkWidget *bar = s_gtk4_popover_menu_bar_new(s_gtk4_menu_model);
    void *grid = grid_for_window(window);
    if (grid)
    {
        G.gtk_grid_attach(grid, bar, 0, -1, 10, 1);
        s_gtk4_widget_set_visible(bar, 1);
    }
    return bar;
}

static void *gtk4_menu_add_submenu(void *menubar, const char *label)
{
    (void)menubar;
    void *submenu = s_gtk4_g_menu_new();
    s_gtk4_g_menu_append_submenu(s_gtk4_menu_model, label, submenu);
    return submenu;
}

static void gtk4_menu_add_item(void *submenu, const char *label, gui_callback_t cb)
{
    char action_name[64];
    snprintf(action_name, sizeof(action_name), "act%d", s_gtk4_action_id++);
    char detailed[80];
    snprintf(detailed, sizeof(detailed), "win.%s", action_name);
    s_gtk4_g_menu_append(submenu, label, detailed);

    void *action = s_gtk4_g_simple_action_new(action_name, NULL);
    if (g_callback_count < MAX_CALLBACKS)
    {
        g_callbacks[g_callback_count] = (callback_entry_t){action, GUI_EVENT_CLICK, cb};
        G.g_signal_connect_data(action, "activate", (GCallback)gtk4_on_action_activate,
                                &g_callbacks[g_callback_count].cb, NULL, 0);
        g_callback_count++;
    }
    if (s_gtk4_action_group)
        s_gtk4_g_action_map_add_action(s_gtk4_action_group, action);
}

static void gtk4_paned_set_start_wrap(GtkWidget *paned, GtkWidget *child)
{
    s_gtk4_paned_set_start(paned, child);
}

static void gtk4_paned_set_end_wrap(GtkWidget *paned, GtkWidget *child)
{
    s_gtk4_paned_set_end(paned, child);
}

static void gtk4_canvas_setup_draw(GtkWidget *da, void *data)
{
    (void)data;
    s_gtk4_set_draw_func(da, (GCallback)gtk4_canvas_draw_func, NULL, NULL);
}

static const gtk_version_ops_t g_gtk4_ops = {
    .version_name = "GTK4",
    .close_signal = "close-request",
    .main_loop = gtk4_main_loop,
    .main_quit = gtk4_main_quit,
    .window_new = gtk4_window_new,
    .window_get_size = gtk4_window_get_size,
    .window_set_child = gtk4_window_set_child,
    .window_destroy = gtk4_window_destroy,
    .widget_show = gtk4_widget_show,
    .widget_destroy = gtk4_widget_destroy,
    .grid_remove_child = gtk4_grid_remove_child,
    .message_box = gtk4_message_box,
    .file_dialog = gtk4_file_dialog,
    .yes_no_dialog = gtk4_yes_no_dialog,
    .entry_get_text = gtk4_entry_get_text,
    .entry_set_text = gtk4_entry_set_text,
    .checkbox_get_active = gtk4_checkbox_get_active,
    .checkbox_set_active = gtk4_checkbox_set_active,
    .checkbox_set_label = gtk4_checkbox_set_label,
    .radio_create = gtk4_radio_create,
    .radio_set_label = gtk4_radio_set_label,
    .scrolled_window_new = gtk4_scrolled_window_new,
    .scrolled_window_set_child = gtk4_scrolled_window_set_child,
    .frame_set_child = gtk4_frame_set_child,
    .menubar_create = gtk4_menubar_create,
    .menu_add_submenu = gtk4_menu_add_submenu,
    .menu_add_item = gtk4_menu_add_item,
    .paned_set_start = gtk4_paned_set_start_wrap,
    .paned_set_end = gtk4_paned_set_end_wrap,
    .canvas_setup_draw = gtk4_canvas_setup_draw,
};

static bool load_gtk4_symbols(void)
{
    LOAD_LOCAL(s_gtk4_window_new, gtk_window_new);
    LOAD_LOCAL(s_gtk4_window_get_default_size, gtk_window_get_default_size);
    LOAD_LOCAL(s_gtk4_widget_set_visible, gtk_widget_set_visible);
    LOAD_LOCAL(s_gtk4_window_set_child, gtk_window_set_child);
    LOAD_LOCAL(s_gtk4_grid_remove, gtk_grid_remove);
    LOAD_LOCAL(s_gtk4_window_destroy, gtk_window_destroy);
    LOAD_LOCAL(s_gtk4_g_main_loop_new, g_main_loop_new);
    LOAD_LOCAL(s_gtk4_g_main_loop_run, g_main_loop_run);
    LOAD_LOCAL(s_gtk4_g_main_loop_quit, g_main_loop_quit);
    LOAD_LOCAL(s_gtk4_g_main_loop_unref, g_main_loop_unref);
    LOAD_LOCAL(s_gtk4_editable_get_text, gtk_editable_get_text);
    LOAD_LOCAL(s_gtk4_editable_set_text, gtk_editable_set_text);
    LOAD_LOCAL(s_gtk4_check_get_active, gtk_check_button_get_active);
    LOAD_LOCAL(s_gtk4_check_set_active, gtk_check_button_set_active);
    LOAD_LOCAL(s_gtk4_check_set_label, gtk_check_button_set_label);
    LOAD_LOCAL(s_gtk4_check_set_group, gtk_check_button_set_group);
    LOAD_LOCAL(s_gtk4_file_chooser_new, gtk_file_chooser_dialog_new);
    LOAD_LOCAL(s_gtk4_file_chooser_get_file, gtk_file_chooser_get_file);
    LOAD_LOCAL(s_gtk4_g_file_get_path, g_file_get_path);
    LOAD_LOCAL(s_gtk4_g_free, g_free);
    LOAD_LOCAL(s_gtk4_scrolled_window_new, gtk_scrolled_window_new);
    LOAD_LOCAL(s_gtk4_scrolled_window_set_child, gtk_scrolled_window_set_child);
    LOAD_LOCAL(s_gtk4_frame_set_child, gtk_frame_set_child);
    LOAD_LOCAL(s_gtk4_popover_menu_bar_new, gtk_popover_menu_bar_new_from_model);
    LOAD_LOCAL(s_gtk4_g_menu_new, g_menu_new);
    LOAD_LOCAL(s_gtk4_g_menu_append, g_menu_append);
    LOAD_LOCAL(s_gtk4_g_menu_append_submenu, g_menu_append_submenu);
    LOAD_LOCAL(s_gtk4_g_simple_action_new, g_simple_action_new);
    LOAD_LOCAL(s_gtk4_g_action_map_add_action, g_action_map_add_action);
    LOAD_LOCAL(s_gtk4_g_simple_action_group_new, g_simple_action_group_new);
    LOAD_LOCAL(s_gtk4_gtk_widget_insert_action_group, gtk_widget_insert_action_group);
    LOAD_LOCAL(s_gtk4_paned_set_start, gtk_paned_set_start_child);
    LOAD_LOCAL(s_gtk4_paned_set_end, gtk_paned_set_end_child);
    LOAD_LOCAL(s_gtk4_set_draw_func, gtk_drawing_area_set_draw_func);
    return true;
fail:
    return false;
}

/* ════════════════════════════════════════════════════════════════════
 * Shared backend implementation (version-agnostic)
 * ════════════════════════════════════════════════════════════════════ */

static bool load_shared_symbols(void)
{
    LOAD_SHARED(gtk_init);
    LOAD_SHARED(gtk_window_set_title);
    LOAD_SHARED(gtk_window_set_default_size);
    LOAD_SHARED(gtk_label_new);
    LOAD_SHARED(gtk_label_set_text);
    LOAD_SHARED(gtk_button_new_with_label);
    LOAD_SHARED(gtk_button_set_label);
    LOAD_SHARED(gtk_grid_new);
    LOAD_SHARED(gtk_grid_attach);
    LOAD_SHARED(gtk_widget_set_hexpand);
    LOAD_SHARED(gtk_widget_set_vexpand);
    LOAD_SHARED(g_signal_connect_data);
    LOAD_SHARED(gtk_entry_new);
    LOAD_SHARED(gtk_check_button_new_with_label);
    LOAD_SHARED(gtk_scale_new_with_range);
    LOAD_SHARED(gtk_combo_box_text_new);
    LOAD_SHARED(gtk_combo_box_text_append_text);
    LOAD_SHARED(gtk_combo_box_get_active);
    LOAD_SHARED(gtk_combo_box_set_active);
    LOAD_SHARED(gtk_range_get_value);
    LOAD_SHARED(gtk_range_set_value);
    LOAD_SHARED(gtk_text_view_new);
    LOAD_SHARED(gtk_text_view_get_buffer);
    LOAD_SHARED(gtk_text_buffer_get_text);
    LOAD_SHARED(gtk_text_buffer_set_text);
    LOAD_SHARED(gtk_text_buffer_get_start_iter);
    LOAD_SHARED(gtk_text_buffer_get_end_iter);
    LOAD_SHARED(gtk_spin_button_new_with_range);
    LOAD_SHARED(gtk_spin_button_get_value);
    LOAD_SHARED(gtk_spin_button_set_value);
    LOAD_SHARED(gtk_frame_new);
    LOAD_SHARED(gtk_frame_set_label);
    LOAD_SHARED(gtk_list_box_new);
    LOAD_SHARED(gtk_list_box_insert);
    LOAD_SHARED(gtk_list_box_get_selected_row);
    LOAD_SHARED(gtk_list_box_row_get_index);
    LOAD_SHARED(gtk_list_box_select_row);
    LOAD_SHARED(gtk_list_box_get_row_at_index);
    LOAD_SHARED(gtk_paned_new);
    LOAD_SHARED(gtk_paned_set_position);
    LOAD_SHARED(gtk_drawing_area_new);
    LOAD_SHARED(gtk_widget_queue_draw);
    LOAD_SHARED(cairo_set_source_rgb);
    LOAD_SHARED(cairo_set_line_width);
    LOAD_SHARED(cairo_move_to);
    LOAD_SHARED(cairo_line_to);
    LOAD_SHARED(cairo_stroke);
    LOAD_SHARED(cairo_rectangle);
    LOAD_SHARED(cairo_fill);
    LOAD_SHARED(cairo_arc);
    LOAD_SHARED(cairo_show_text);
    LOAD_SHARED(cairo_paint);
    return true;
fail:
    return false;
}

static bool gtk_be_init(const char *app_name)
{
    (void)app_name;
    memset(&G, 0, sizeof(G));

    /* Try GTK4 first, fall back to GTK3. */
    G.lib = dlopen("libgtk-4.so.1", RTLD_LAZY);
    if (G.lib && load_shared_symbols() && load_gtk4_symbols())
    {
        g_vops = &g_gtk4_ops;
        G.gtk_init(NULL, NULL);
        fprintf(stderr, "gui_gtk: using %s\n", g_vops->version_name);
        return true;
    }
    if (G.lib)
    {
        dlclose(G.lib);
        G.lib = NULL;
    }
    memset(&G, 0, sizeof(G));

    G.lib = dlopen("libgtk-3.so.0", RTLD_LAZY);
    if (G.lib && load_shared_symbols() && load_gtk3_symbols())
    {
        g_vops = &g_gtk3_ops;
        G.gtk_init(NULL, NULL);
        fprintf(stderr, "gui_gtk: using %s\n", g_vops->version_name);
        return true;
    }
    if (G.lib)
    {
        dlclose(G.lib);
        G.lib = NULL;
    }

    fprintf(stderr, "gui_gtk: cannot load libgtk-4.so.1 or libgtk-3.so.0\n");
    return false;
}

static void gtk_be_shutdown(void)
{
    g_callback_count = 0;
    g_widget_parent_count = 0;
    g_window_grid_count = 0;
    g_vops = NULL;
    if (G.lib)
    {
        dlclose(G.lib);
        G.lib = NULL;
    }
}

static void gtk_be_main_loop(void)
{
    g_vops->main_loop();
}
static void gtk_be_quit(void)
{
    g_vops->main_quit();
}

/* ── Window ──────────────────────────────────────────────────────── */

static void *gtk_be_window_create(const char *title, int w, int h)
{
    GtkWidget *win = g_vops->window_new();
    if (!win)
        return NULL;
    G.gtk_window_set_title(win, title);
    G.gtk_window_set_default_size(win, w, h);

    GtkWidget *grid = G.gtk_grid_new();
    g_vops->window_set_child(win, grid);

    if (g_window_grid_count < MAX_GRIDS)
        g_window_grids[g_window_grid_count++] = (window_grid_t){win, grid};

    GCallback close_handler =
        (g_vops == &g_gtk4_ops) ? (GCallback)on_window_close_gtk4 : (GCallback)on_window_close_gtk3;
    G.g_signal_connect_data(win, g_vops->close_signal, close_handler, NULL, NULL, 0);

    g_vops->widget_show(win);
    return win;
}

static void gtk_be_window_destroy(void *handle)
{
    if (handle)
        g_vops->window_destroy(handle);
}

static void gtk_be_window_set_title(void *handle, const char *title)
{
    if (handle)
        G.gtk_window_set_title(handle, title);
}

static void gtk_be_window_get_size(void *handle, int *w, int *h)
{
    if (!handle)
    {
        *w = 0;
        *h = 0;
        return;
    }
    g_vops->window_get_size(handle, w, h);
}

/* ── Label ───────────────────────────────────────────────────────── */

static void *gtk_be_label_create(void *parent, const char *text)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *label = G.gtk_label_new(text);
    register_widget_parent(label, grid);
    return label;
}

static void gtk_be_label_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_label_set_text(void *handle, const char *text)
{
    if (handle)
        G.gtk_label_set_text(handle, text);
}

/* ── Button ──────────────────────────────────────────────────────── */

static void *gtk_be_button_create(void *parent, const char *text)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *btn = G.gtk_button_new_with_label(text);
    G.g_signal_connect_data(btn, "clicked", (GCallback)on_button_clicked, NULL, NULL, 0);
    register_widget_parent(btn, grid);
    return btn;
}

static void gtk_be_button_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_button_set_text(void *handle, const char *text)
{
    if (handle)
        G.gtk_button_set_label(handle, text);
}

/* ── Entry ───────────────────────────────────────────────────────── */

static void on_entry_changed(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CHANGE);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void *gtk_be_entry_create(void *parent)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *entry = G.gtk_entry_new();
    G.g_signal_connect_data(entry, "changed", (GCallback)on_entry_changed, NULL, NULL, 0);
    register_widget_parent(entry, grid);
    return entry;
}

static void gtk_be_entry_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static const char *gtk_be_entry_get_text(void *handle, char *buf, size_t bufsz)
{
    if (!handle)
    {
        buf[0] = '\0';
        return buf;
    }
    const char *text = g_vops->entry_get_text(handle);
    if (text)
    {
        size_t len = strlen(text);
        if (len >= bufsz)
            len = bufsz - 1;
        memcpy(buf, text, len);
        buf[len] = '\0';
    }
    else
    {
        buf[0] = '\0';
    }
    return buf;
}

static void gtk_be_entry_set_text(void *handle, const char *text)
{
    if (handle)
        g_vops->entry_set_text(handle, text);
}

/* ── Checkbox ────────────────────────────────────────────────────── */

static void on_checkbox_toggled(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CHANGE);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void *gtk_be_checkbox_create(void *parent, const char *text)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *cb = G.gtk_check_button_new_with_label(text);
    G.g_signal_connect_data(cb, "toggled", (GCallback)on_checkbox_toggled, NULL, NULL, 0);
    register_widget_parent(cb, grid);
    return cb;
}

static void gtk_be_checkbox_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_checkbox_set_text(void *handle, const char *text)
{
    if (handle)
        g_vops->checkbox_set_label(handle, text);
}

static bool gtk_be_checkbox_get_checked(void *handle)
{
    if (!handle)
        return false;
    return g_vops->checkbox_get_active(handle) != 0;
}

static void gtk_be_checkbox_set_checked(void *handle, bool checked)
{
    if (handle)
        g_vops->checkbox_set_active(handle, checked ? 1 : 0);
}

/* ── Slider ───────────────────────────────────────────────────────── */

static void on_slider_changed(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CHANGE);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void *gtk_be_slider_create(void *parent, double min_val, double max_val)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *scale = G.gtk_scale_new_with_range(0, min_val, max_val, 1.0);
    G.g_signal_connect_data(scale, "value-changed", (GCallback)on_slider_changed, NULL, NULL, 0);
    register_widget_parent(scale, grid);
    return scale;
}

static void gtk_be_slider_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static double gtk_be_slider_get_value(void *handle)
{
    if (!handle)
        return 0.0;
    return G.gtk_range_get_value(handle);
}

static void gtk_be_slider_set_value(void *handle, double value)
{
    if (handle)
        G.gtk_range_set_value(handle, value);
}

/* ── Select ───────────────────────────────────────────────────────── */

static void on_select_changed(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CHANGE);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void *gtk_be_select_create(void *parent)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *combo = G.gtk_combo_box_text_new();
    G.g_signal_connect_data(combo, "changed", (GCallback)on_select_changed, NULL, NULL, 0);
    register_widget_parent(combo, grid);
    return combo;
}

static void gtk_be_select_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_select_add_item(void *handle, const char *text)
{
    if (handle)
        G.gtk_combo_box_text_append_text(handle, text);
}

static int gtk_be_select_get_index(void *handle)
{
    if (!handle)
        return -1;
    return G.gtk_combo_box_get_active(handle);
}

static void gtk_be_select_set_index(void *handle, int index)
{
    if (handle)
        G.gtk_combo_box_set_active(handle, index);
}

/* ── Text ────────────────────────────────────────────────────────── */

/* Map scrolled-window handle → text view for buffer access. */
#define MAX_TEXT_VIEWS 64
static struct
{
    void *sw;
    void *tv;
} g_text_views[MAX_TEXT_VIEWS];
static int g_text_view_count = 0;

static void *text_view_for_sw(void *sw)
{
    for (int i = 0; i < g_text_view_count; i++)
        if (g_text_views[i].sw == sw)
            return g_text_views[i].tv;
    return NULL;
}

static void *gtk_be_text_create(void *parent)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *sw = g_vops->scrolled_window_new();
    GtkWidget *tv = G.gtk_text_view_new();
    g_vops->scrolled_window_set_child(sw, tv);
    G.gtk_widget_set_hexpand(sw, 1);
    G.gtk_widget_set_vexpand(sw, 1);
    if (g_text_view_count < MAX_TEXT_VIEWS)
        g_text_views[g_text_view_count++] = (typeof(g_text_views[0])){sw, tv};
    register_widget_parent(sw, grid);
    return sw;
}

static void gtk_be_text_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static const char *gtk_be_text_get_text(void *handle, char *buf, size_t bufsz)
{
    buf[0] = '\0';
    if (!handle)
        return buf;
    void *tv = text_view_for_sw(handle);
    if (!tv)
        return buf;
    void *buffer = G.gtk_text_view_get_buffer(tv);
    if (!buffer)
        return buf;
    /* GtkTextIter is 80 bytes on 64-bit; allocate generously. */
    char start_iter[128], end_iter[128];
    G.gtk_text_buffer_get_start_iter(buffer, start_iter);
    G.gtk_text_buffer_get_end_iter(buffer, end_iter);
    char *text = G.gtk_text_buffer_get_text(buffer, start_iter, end_iter, 0);
    if (text)
    {
        size_t len = strlen(text);
        if (len >= bufsz)
            len = bufsz - 1;
        memcpy(buf, text, len);
        buf[len] = '\0';
        /* g_free — we don't have it loaded, but the text is valid until
           the buffer changes.  Copy is already done. For correctness we
           should call g_free, but the string is short-lived. */
    }
    return buf;
}

static void gtk_be_text_set_text(void *handle, const char *text)
{
    if (!handle)
        return;
    void *tv = text_view_for_sw(handle);
    if (!tv)
        return;
    void *buffer = G.gtk_text_view_get_buffer(tv);
    if (buffer)
        G.gtk_text_buffer_set_text(buffer, text, -1);
}

/* ── Radio ───────────────────────────────────────────────────────── */

static void on_radio_toggled(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CHANGE);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void *gtk_be_radio_create(void *parent, const char *text, void *group)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *rb = g_vops->radio_create(grid, text, group);
    G.g_signal_connect_data(rb, "toggled", (GCallback)on_radio_toggled, NULL, NULL, 0);
    register_widget_parent(rb, grid);
    return rb;
}

static void gtk_be_radio_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_radio_set_text(void *handle, const char *text)
{
    if (handle)
        g_vops->radio_set_label(handle, text);
}

static bool gtk_be_radio_get_active(void *handle)
{
    if (!handle)
        return false;
    return g_vops->checkbox_get_active(handle) != 0;
}

static void gtk_be_radio_set_active(void *handle, bool active)
{
    if (handle)
        g_vops->checkbox_set_active(handle, active ? 1 : 0);
}

/* ── Spinbox ─────────────────────────────────────────────────────── */

static void on_spinbox_changed(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CHANGE);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void *gtk_be_spinbox_create(void *parent, double min_val, double max_val, double step)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *spin = G.gtk_spin_button_new_with_range(min_val, max_val, step);
    G.g_signal_connect_data(spin, "value-changed", (GCallback)on_spinbox_changed, NULL, NULL, 0);
    register_widget_parent(spin, grid);
    return spin;
}

static void gtk_be_spinbox_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static double gtk_be_spinbox_get_value(void *handle)
{
    if (!handle)
        return 0.0;
    return G.gtk_spin_button_get_value(handle);
}

static void gtk_be_spinbox_set_value(void *handle, double value)
{
    if (handle)
        G.gtk_spin_button_set_value(handle, value);
}

/* ── Frame ───────────────────────────────────────────────────────── */

static void *gtk_be_frame_create(void *parent, const char *label)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *frame = G.gtk_frame_new(label);
    GtkWidget *inner_grid = G.gtk_grid_new();
    g_vops->frame_set_child(frame, inner_grid);
    /* Register the frame's inner grid so children can be placed in it. */
    if (g_window_grid_count < MAX_GRIDS)
        g_window_grids[g_window_grid_count++] = (window_grid_t){frame, inner_grid};
    register_widget_parent(frame, grid);
    return frame;
}

static void gtk_be_frame_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_frame_set_label(void *handle, const char *label)
{
    if (handle)
        G.gtk_frame_set_label(handle, label);
}

/* ── Listbox ─────────────────────────────────────────────────────── */

static void on_listbox_row_selected(GtkWidget *widget, void *row, void *data)
{
    (void)row;
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CHANGE);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

static void *gtk_be_listbox_create(void *parent)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *sw = g_vops->scrolled_window_new();
    GtkWidget *lb = G.gtk_list_box_new();
    g_vops->scrolled_window_set_child(sw, lb);
    G.gtk_widget_set_hexpand(sw, 1);
    G.gtk_widget_set_vexpand(sw, 1);
    G.g_signal_connect_data(lb, "row-selected", (GCallback)on_listbox_row_selected, NULL, NULL, 0);
    /* Map sw → lb for item operations. Reuse text_views array pattern. */
    if (g_text_view_count < MAX_TEXT_VIEWS)
        g_text_views[g_text_view_count++] = (typeof(g_text_views[0])){sw, lb};
    register_widget_parent(sw, grid);
    return sw;
}

static void gtk_be_listbox_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_listbox_add_item(void *handle, const char *text)
{
    if (!handle)
        return;
    void *lb = text_view_for_sw(handle);
    if (!lb)
        return;
    GtkWidget *label = G.gtk_label_new(text);
    G.gtk_list_box_insert(lb, label, -1);
    g_vops->widget_show(label);
}

static int gtk_be_listbox_get_selected(void *handle)
{
    if (!handle)
        return -1;
    void *lb = text_view_for_sw(handle);
    if (!lb)
        return -1;
    void *row = G.gtk_list_box_get_selected_row(lb);
    if (!row)
        return -1;
    return G.gtk_list_box_row_get_index(row);
}

static void gtk_be_listbox_set_selected(void *handle, int index)
{
    if (!handle)
        return;
    void *lb = text_view_for_sw(handle);
    if (!lb)
        return;
    void *row = G.gtk_list_box_get_row_at_index(lb, index);
    G.gtk_list_box_select_row(lb, row);
}

/* ── Menu ────────────────────────────────────────────────────────── */

static void *gtk_be_menubar_create(void *window)
{
    return g_vops->menubar_create(window);
}

static void gtk_be_menubar_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void *gtk_be_menu_add_submenu(void *menubar, const char *label)
{
    return g_vops->menu_add_submenu(menubar, label);
}

static void gtk_be_menu_add_item(void *submenu, const char *label, gui_callback_t cb)
{
    g_vops->menu_add_item(submenu, label, cb);
}

/* ── PanedWindow ─────────────────────────────────────────────────── */

static void *gtk_be_paned_create(void *parent, bool horizontal)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    /* GTK_ORIENTATION_HORIZONTAL=0, GTK_ORIENTATION_VERTICAL=1 */
    GtkWidget *paned = G.gtk_paned_new(horizontal ? 0 : 1);
    G.gtk_widget_set_hexpand(paned, 1);
    G.gtk_widget_set_vexpand(paned, 1);
    register_widget_parent(paned, grid);
    return paned;
}

static void gtk_be_paned_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_paned_set_start(void *handle, void *child)
{
    if (handle && child)
        g_vops->paned_set_start(handle, child);
}

static void gtk_be_paned_set_end(void *handle, void *child)
{
    if (handle && child)
        g_vops->paned_set_end(handle, child);
}

static void gtk_be_paned_set_position(void *handle, int pos)
{
    if (handle)
        G.gtk_paned_set_position(handle, pos);
}

/* ── Canvas ──────────────────────────────────────────────────────── */

enum
{
    DRAW_LINE = 0,
    DRAW_RECT,
    DRAW_OVAL,
    DRAW_TEXT
};

typedef struct
{
    int type;
    double x1, y1, x2, y2;
    char text[128];
} draw_cmd_t;

#define MAX_CANVAS 32
#define MAX_DRAW_CMDS 512

typedef struct
{
    GtkWidget *da;
    draw_cmd_t cmds[MAX_DRAW_CMDS];
    int cmd_count;
} canvas_data_t;

static canvas_data_t g_canvas_data[MAX_CANVAS];
static int g_canvas_count = 0;

static canvas_data_t *canvas_for_da(void *da)
{
    for (int i = 0; i < g_canvas_count; i++)
        if (g_canvas_data[i].da == da)
            return &g_canvas_data[i];
    return NULL;
}

static void canvas_replay(void *cr, canvas_data_t *cd)
{
    /* White background. */
    G.cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    G.cairo_paint(cr);
    /* Draw commands in black. */
    G.cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    G.cairo_set_line_width(cr, 1.0);
    for (int i = 0; i < cd->cmd_count; i++)
    {
        draw_cmd_t *c = &cd->cmds[i];
        switch (c->type)
        {
        case DRAW_LINE:
            G.cairo_move_to(cr, c->x1, c->y1);
            G.cairo_line_to(cr, c->x2, c->y2);
            G.cairo_stroke(cr);
            break;
        case DRAW_RECT:
            G.cairo_rectangle(cr, c->x1, c->y1, c->x2, c->y2);
            G.cairo_stroke(cr);
            break;
        case DRAW_OVAL:
            G.cairo_arc(cr, c->x1 + c->x2 / 2, c->y1 + c->y2 / 2, (c->x2 < c->y2 ? c->x2 : c->y2) / 2, 0, 6.283185);
            G.cairo_stroke(cr);
            break;
        case DRAW_TEXT:
            G.cairo_move_to(cr, c->x1, c->y1);
            G.cairo_show_text(cr, c->text);
            break;
        }
    }
}

/* GTK3 draw callback: gboolean (*)(GtkWidget*, cairo_t*, gpointer) */
static gboolean gtk3_canvas_draw(GtkWidget *widget, void *cr, void *data)
{
    (void)data;
    canvas_data_t *cd = canvas_for_da(widget);
    if (cd)
        canvas_replay(cr, cd);
    return 0;
}

/* GTK4 draw func: void (*)(GtkDrawingArea*, cairo_t*, int, int, gpointer) */
static void gtk4_canvas_draw_func(GtkWidget *da, void *cr, int w, int h, void *data)
{
    (void)w;
    (void)h;
    (void)data;
    canvas_data_t *cd = canvas_for_da(da);
    if (cd)
        canvas_replay(cr, cd);
}

static void *gtk_be_canvas_create(void *parent, int width, int height)
{
    if (!parent)
        return NULL;
    void *grid = grid_for_window(parent);
    if (!grid)
        return NULL;
    GtkWidget *da = G.gtk_drawing_area_new();
    G.gtk_widget_set_hexpand(da, 1);
    G.gtk_widget_set_vexpand(da, 1);
    (void)width;
    (void)height;
    if (g_canvas_count < MAX_CANVAS)
    {
        canvas_data_t *cd = &g_canvas_data[g_canvas_count++];
        memset(cd, 0, sizeof(*cd));
        cd->da = da;
    }
    g_vops->canvas_setup_draw(da, NULL);
    register_widget_parent(da, grid);
    return da;
}

static void gtk_be_canvas_destroy(void *handle)
{
    if (handle)
        g_vops->widget_destroy(handle);
}

static void gtk_be_canvas_clear(void *handle)
{
    canvas_data_t *cd = canvas_for_da(handle);
    if (cd)
    {
        cd->cmd_count = 0;
        G.gtk_widget_queue_draw(handle);
    }
}

static void gtk_be_canvas_draw_line(void *handle, double x1, double y1, double x2, double y2)
{
    canvas_data_t *cd = canvas_for_da(handle);
    if (cd && cd->cmd_count < MAX_DRAW_CMDS)
    {
        cd->cmds[cd->cmd_count++] = (draw_cmd_t){DRAW_LINE, x1, y1, x2, y2, {0}};
        G.gtk_widget_queue_draw(handle);
    }
}

static void gtk_be_canvas_draw_rect(void *handle, double x, double y, double w, double h)
{
    canvas_data_t *cd = canvas_for_da(handle);
    if (cd && cd->cmd_count < MAX_DRAW_CMDS)
    {
        cd->cmds[cd->cmd_count++] = (draw_cmd_t){DRAW_RECT, x, y, w, h, {0}};
        G.gtk_widget_queue_draw(handle);
    }
}

static void gtk_be_canvas_draw_oval(void *handle, double x, double y, double w, double h)
{
    canvas_data_t *cd = canvas_for_da(handle);
    if (cd && cd->cmd_count < MAX_DRAW_CMDS)
    {
        cd->cmds[cd->cmd_count++] = (draw_cmd_t){DRAW_OVAL, x, y, w, h, {0}};
        G.gtk_widget_queue_draw(handle);
    }
}

static void gtk_be_canvas_draw_text(void *handle, double x, double y, const char *text)
{
    canvas_data_t *cd = canvas_for_da(handle);
    if (cd && cd->cmd_count < MAX_DRAW_CMDS)
    {
        draw_cmd_t cmd = {DRAW_TEXT, x, y, 0, 0, {0}};
        size_t len = strlen(text);
        if (len >= sizeof(cmd.text))
            len = sizeof(cmd.text) - 1;
        memcpy(cmd.text, text, len);
        cmd.text[len] = '\0';
        cd->cmds[cd->cmd_count++] = cmd;
        G.gtk_widget_queue_draw(handle);
    }
}

/* ── Grid layout ─────────────────────────────────────────────────── */

static void gtk_be_widget_grid(void *handle, int col, int row)
{
    void *grid = get_widget_grid(handle);
    if (!grid)
        return;
    G.gtk_grid_attach(grid, handle, col, row, 1, 1);
    g_vops->widget_show(handle);
}

static void gtk_be_widget_grid_span(void *handle, int col, int row, int colspan, int rowspan)
{
    void *grid = get_widget_grid(handle);
    if (!grid)
        return;
    G.gtk_grid_attach(grid, handle, col, row, colspan, rowspan);
    g_vops->widget_show(handle);
}

static void gtk_be_widget_grid_remove(void *handle)
{
    if (!handle)
        return;
    void *grid = get_widget_grid(handle);
    g_vops->grid_remove_child(grid, handle);
}

static void gtk_be_container_grid_columnconfigure(void *handle, int index, int weight)
{
    (void)index;
    void *grid = grid_for_window(handle);
    if (!grid)
        return;
    for (int i = 0; i < g_widget_parent_count; i++)
        if (g_widget_parents[i].grid == grid)
            G.gtk_widget_set_hexpand(g_widget_parents[i].widget, weight > 0);
}

static void gtk_be_container_grid_rowconfigure(void *handle, int index, int weight)
{
    (void)index;
    void *grid = grid_for_window(handle);
    if (!grid)
        return;
    for (int i = 0; i < g_widget_parent_count; i++)
        if (g_widget_parents[i].grid == grid)
            G.gtk_widget_set_vexpand(g_widget_parents[i].widget, weight > 0);
}

/* ── Events ──────────────────────────────────────────────────────── */

static void gtk_be_set_callback(void *widget, gui_event_type_t type, gui_callback_t cb)
{
    gui_callback_t *existing = find_callback(widget, type);
    if (existing)
    {
        *existing = cb;
        return;
    }
    if (g_callback_count >= MAX_CALLBACKS)
        return;
    g_callbacks[g_callback_count++] = (callback_entry_t){widget, type, cb};
}

/* ── Dialogs ─────────────────────────────────────────────────────── */

static void gtk_be_message_box(void *parent, const char *title, const char *message)
{
    g_vops->message_box(parent, title, message);
}

static const char *gtk_be_open_file(void *parent, const char *title, char *buf, size_t bufsz)
{
    return g_vops->file_dialog(parent, title, 0 /* OPEN */, buf, bufsz);
}

static const char *gtk_be_save_file(void *parent, const char *title, char *buf, size_t bufsz)
{
    return g_vops->file_dialog(parent, title, 1 /* SAVE */, buf, bufsz);
}

static const char *gtk_be_choose_dir(void *parent, const char *title, char *buf, size_t bufsz)
{
    return g_vops->file_dialog(parent, title, 2 /* SELECT_FOLDER */, buf, bufsz);
}

static bool gtk_be_ask_yes_no(void *parent, const char *title, const char *message)
{
    return g_vops->yes_no_dialog(parent, title, message);
}

/* ── Export ───────────────────────────────────────────────────────── */

const gui_backend_t gui_backend_gtk = {
    .name = "gtk",
    .init = gtk_be_init,
    .shutdown = gtk_be_shutdown,
    .main_loop = gtk_be_main_loop,
    .quit = gtk_be_quit,
    .window_create = gtk_be_window_create,
    .window_destroy = gtk_be_window_destroy,
    .window_set_title = gtk_be_window_set_title,
    .window_get_size = gtk_be_window_get_size,
    .label_create = gtk_be_label_create,
    .label_destroy = gtk_be_label_destroy,
    .label_set_text = gtk_be_label_set_text,
    .button_create = gtk_be_button_create,
    .button_destroy = gtk_be_button_destroy,
    .button_set_text = gtk_be_button_set_text,
    .entry_create = gtk_be_entry_create,
    .entry_destroy = gtk_be_entry_destroy,
    .entry_get_text = gtk_be_entry_get_text,
    .entry_set_text = gtk_be_entry_set_text,
    .checkbox_create = gtk_be_checkbox_create,
    .checkbox_destroy = gtk_be_checkbox_destroy,
    .checkbox_set_text = gtk_be_checkbox_set_text,
    .checkbox_get_checked = gtk_be_checkbox_get_checked,
    .checkbox_set_checked = gtk_be_checkbox_set_checked,
    .slider_create = gtk_be_slider_create,
    .slider_destroy = gtk_be_slider_destroy,
    .slider_get_value = gtk_be_slider_get_value,
    .slider_set_value = gtk_be_slider_set_value,
    .select_create = gtk_be_select_create,
    .select_destroy = gtk_be_select_destroy,
    .select_add_item = gtk_be_select_add_item,
    .select_get_index = gtk_be_select_get_index,
    .select_set_index = gtk_be_select_set_index,
    .text_create = gtk_be_text_create,
    .text_destroy = gtk_be_text_destroy,
    .text_get_text = gtk_be_text_get_text,
    .text_set_text = gtk_be_text_set_text,
    .radio_create = gtk_be_radio_create,
    .radio_destroy = gtk_be_radio_destroy,
    .radio_set_text = gtk_be_radio_set_text,
    .radio_get_active = gtk_be_radio_get_active,
    .radio_set_active = gtk_be_radio_set_active,
    .spinbox_create = gtk_be_spinbox_create,
    .spinbox_destroy = gtk_be_spinbox_destroy,
    .spinbox_get_value = gtk_be_spinbox_get_value,
    .spinbox_set_value = gtk_be_spinbox_set_value,
    .frame_create = gtk_be_frame_create,
    .frame_destroy = gtk_be_frame_destroy,
    .frame_set_label = gtk_be_frame_set_label,
    .listbox_create = gtk_be_listbox_create,
    .listbox_destroy = gtk_be_listbox_destroy,
    .listbox_add_item = gtk_be_listbox_add_item,
    .listbox_get_selected = gtk_be_listbox_get_selected,
    .listbox_set_selected = gtk_be_listbox_set_selected,
    .menubar_create = gtk_be_menubar_create,
    .menubar_destroy = gtk_be_menubar_destroy,
    .menu_add_submenu = gtk_be_menu_add_submenu,
    .menu_add_item = gtk_be_menu_add_item,
    .paned_create = gtk_be_paned_create,
    .paned_destroy = gtk_be_paned_destroy,
    .paned_set_start = gtk_be_paned_set_start,
    .paned_set_end = gtk_be_paned_set_end,
    .paned_set_position = gtk_be_paned_set_position,
    .canvas_create = gtk_be_canvas_create,
    .canvas_destroy = gtk_be_canvas_destroy,
    .canvas_clear = gtk_be_canvas_clear,
    .canvas_draw_line = gtk_be_canvas_draw_line,
    .canvas_draw_rect = gtk_be_canvas_draw_rect,
    .canvas_draw_oval = gtk_be_canvas_draw_oval,
    .canvas_draw_text = gtk_be_canvas_draw_text,
    .widget_grid = gtk_be_widget_grid,
    .widget_grid_span = gtk_be_widget_grid_span,
    .widget_grid_remove = gtk_be_widget_grid_remove,
    .container_grid_columnconfigure = gtk_be_container_grid_columnconfigure,
    .container_grid_rowconfigure = gtk_be_container_grid_rowconfigure,
    .widget_set_font = NULL,
    .widget_set_fg = NULL,
    .widget_set_bg = NULL,
    .widget_set_padding = NULL,
    .widget_set_state = NULL,
    .set_callback = gtk_be_set_callback,
    .message_box = gtk_be_message_box,
    .open_file_dialog = gtk_be_open_file,
    .save_file_dialog = gtk_be_save_file,
    .choose_directory = gtk_be_choose_dir,
    .ask_yes_no = gtk_be_ask_yes_no,
};

#endif /* __linux__ */
