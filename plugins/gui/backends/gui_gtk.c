/* gui_gtk.c — Linux GTK3 backend via dlopen/dlsym.
 *
 * No link-time dependency on GTK.  All symbols are resolved at runtime
 * through dlopen("libgtk-3.so.0").
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
typedef int  gint;
typedef unsigned int guint;
typedef unsigned long gulong;
typedef int  gboolean;
typedef void (*GCallback)(void);
typedef void (*GClosureNotify)(void *data, void *closure);

/* GtkWindowType */
#define GTK_WINDOW_TOPLEVEL 0

/* GtkMessageType / GtkButtonsType / GtkDialogFlags */
#define GTK_MESSAGE_INFO    0
#define GTK_BUTTONS_OK      1
#define GTK_DIALOG_MODAL    (1 << 0)
#define GTK_DIALOG_DESTROY_WITH_PARENT (1 << 1)

/* ── dlsym function pointer types ────────────────────────────────── */

typedef void       (*fn_gtk_init)(int *, char ***);
typedef void       (*fn_gtk_main)(void);
typedef void       (*fn_gtk_main_quit)(void);
typedef GtkWidget *(*fn_gtk_window_new)(int type);
typedef void       (*fn_gtk_window_set_title)(GtkWindow *, const char *);
typedef void       (*fn_gtk_window_set_default_size)(GtkWindow *, gint, gint);
typedef void       (*fn_gtk_window_get_size)(GtkWindow *, gint *, gint *);
typedef void       (*fn_gtk_widget_show_all)(GtkWidget *);
typedef void       (*fn_gtk_widget_destroy)(GtkWidget *);
typedef GtkWidget *(*fn_gtk_label_new)(const char *);
typedef void       (*fn_gtk_label_set_text)(GtkWidget *, const char *);
typedef GtkWidget *(*fn_gtk_button_new_with_label)(const char *);
typedef void       (*fn_gtk_button_set_label)(GtkWidget *, const char *);
typedef GtkWidget *(*fn_gtk_grid_new)(void);
typedef void       (*fn_gtk_grid_attach)(GtkGrid *, GtkWidget *, gint, gint, gint, gint);
typedef void       (*fn_gtk_container_add)(GtkWidget *, GtkWidget *);
typedef void       (*fn_gtk_widget_set_hexpand)(GtkWidget *, gboolean);
typedef void       (*fn_gtk_widget_set_vexpand)(GtkWidget *, gboolean);
typedef GtkWidget *(*fn_gtk_message_dialog_new)(GtkWindow *, int, int, int, const char *, ...);
typedef gint       (*fn_gtk_dialog_run)(GtkWidget *);
typedef gulong     (*fn_g_signal_connect_data)(void *, const char *, GCallback, void *,
                                               GClosureNotify, int);

/* ── Resolved symbols ────────────────────────────────────────────── */

static struct {
    void *lib;
    fn_gtk_init                     gtk_init;
    fn_gtk_main                     gtk_main;
    fn_gtk_main_quit                gtk_main_quit;
    fn_gtk_window_new               gtk_window_new;
    fn_gtk_window_set_title         gtk_window_set_title;
    fn_gtk_window_set_default_size  gtk_window_set_default_size;
    fn_gtk_window_get_size          gtk_window_get_size;
    fn_gtk_widget_show_all          gtk_widget_show_all;
    fn_gtk_widget_destroy           gtk_widget_destroy;
    fn_gtk_label_new                gtk_label_new;
    fn_gtk_label_set_text           gtk_label_set_text;
    fn_gtk_button_new_with_label    gtk_button_new_with_label;
    fn_gtk_button_set_label         gtk_button_set_label;
    fn_gtk_grid_new                 gtk_grid_new;
    fn_gtk_grid_attach              gtk_grid_attach;
    fn_gtk_container_add            gtk_container_add;
    fn_gtk_widget_set_hexpand       gtk_widget_set_hexpand;
    fn_gtk_widget_set_vexpand       gtk_widget_set_vexpand;
    fn_gtk_message_dialog_new       gtk_message_dialog_new;
    fn_gtk_dialog_run               gtk_dialog_run;
    fn_g_signal_connect_data        g_signal_connect_data;
} G;

/* POSIX guarantees dlsym returns a void* that can round-trip to a
   function pointer, but ISO C -Wpedantic forbids the direct cast.
   Use memcpy to avoid the warning portably. */
#define LOAD_SYM(name) \
    do { \
        void *_sym = dlsym(G.lib, #name); \
        if (!_sym) { \
            fprintf(stderr, "gui_gtk: dlsym failed: %s\n", #name); \
            dlclose(G.lib); \
            G.lib = NULL; \
            return false; \
        } \
        memcpy(&G.name, &_sym, sizeof(_sym)); \
    } while (0)

/* ── Callback storage ────────────────────────────────────────────── */

#define MAX_CALLBACKS 256

typedef struct {
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

typedef struct { void *widget; void *grid; } widget_parent_t;
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
        if (g_widget_parents[i].widget == w) return g_widget_parents[i].grid;
    return NULL;
}

/* ── Window → grid mapping ───────────────────────────────────────── */

#define MAX_GRIDS 64

typedef struct { void *window; void *grid; } window_grid_t;
static window_grid_t g_window_grids[MAX_GRIDS];
static int g_window_grid_count = 0;

static void *grid_for_window(void *window)
{
    for (int i = 0; i < g_window_grid_count; i++)
        if (g_window_grids[i].window == window) return g_window_grids[i].grid;
    return NULL;
}

/* ── GTK signal callbacks ────────────────────────────────────────── */

static void on_button_clicked(GtkWidget *widget, void *data)
{
    (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CLICK);
    if (cb && cb->fn) cb->fn(cb->user_data);
}

static gboolean on_window_delete(GtkWidget *widget, void *event, void *data)
{
    (void)event; (void)data;
    gui_callback_t *cb = find_callback(widget, GUI_EVENT_CLOSE);
    if (cb && cb->fn) { cb->fn(cb->user_data); return 1; /* suppress default */ }
    G.gtk_main_quit();
    return 0;
}

/* ── Backend implementation ──────────────────────────────────────── */

static bool gtk_be_init(const char *app_name)
{
    (void)app_name;
    memset(&G, 0, sizeof(G));

    G.lib = dlopen("libgtk-3.so.0", RTLD_LAZY);
    if (!G.lib) {
        fprintf(stderr, "gui_gtk: cannot load libgtk-3.so.0: %s\n", dlerror());
        return false;
    }

    LOAD_SYM(gtk_init);
    LOAD_SYM(gtk_main);
    LOAD_SYM(gtk_main_quit);
    LOAD_SYM(gtk_window_new);
    LOAD_SYM(gtk_window_set_title);
    LOAD_SYM(gtk_window_set_default_size);
    LOAD_SYM(gtk_window_get_size);
    LOAD_SYM(gtk_widget_show_all);
    LOAD_SYM(gtk_widget_destroy);
    LOAD_SYM(gtk_label_new);
    LOAD_SYM(gtk_label_set_text);
    LOAD_SYM(gtk_button_new_with_label);
    LOAD_SYM(gtk_button_set_label);
    LOAD_SYM(gtk_grid_new);
    LOAD_SYM(gtk_grid_attach);
    LOAD_SYM(gtk_container_add);
    LOAD_SYM(gtk_widget_set_hexpand);
    LOAD_SYM(gtk_widget_set_vexpand);
    LOAD_SYM(gtk_message_dialog_new);
    LOAD_SYM(gtk_dialog_run);
    LOAD_SYM(g_signal_connect_data);

    G.gtk_init(NULL, NULL);
    return true;
}

static void gtk_be_shutdown(void)
{
    g_callback_count = 0;
    g_widget_parent_count = 0;
    g_window_grid_count = 0;
    if (G.lib) { dlclose(G.lib); G.lib = NULL; }
}

static void gtk_be_main_loop(void)
{
    G.gtk_main();
}

static void gtk_be_quit(void)
{
    G.gtk_main_quit();
}

/* ── Window ──────────────────────────────────────────────────────── */

static void *gtk_be_window_create(const char *title, int w, int h)
{
    GtkWidget *win = G.gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (!win) return NULL;
    G.gtk_window_set_title(win, title);
    G.gtk_window_set_default_size(win, w, h);

    GtkWidget *grid = G.gtk_grid_new();
    G.gtk_container_add(win, grid);

    if (g_window_grid_count < MAX_GRIDS)
        g_window_grids[g_window_grid_count++] = (window_grid_t){win, grid};

    G.g_signal_connect_data(win, "delete-event", (GCallback)on_window_delete,
                            NULL, NULL, 0);
    G.gtk_widget_show_all(win);
    return win;
}

static void gtk_be_window_destroy(void *handle)
{
    if (handle) G.gtk_widget_destroy(handle);
}

static void gtk_be_window_set_title(void *handle, const char *title)
{
    if (handle) G.gtk_window_set_title(handle, title);
}

static void gtk_be_window_get_size(void *handle, int *w, int *h)
{
    if (!handle) { *w = 0; *h = 0; return; }
    G.gtk_window_get_size(handle, w, h);
}

/* ── Label ───────────────────────────────────────────────────────── */

static void *gtk_be_label_create(void *parent, const char *text)
{
    if (!parent) return NULL;
    void *grid = grid_for_window(parent);
    if (!grid) return NULL;
    GtkWidget *label = G.gtk_label_new(text);
    register_widget_parent(label, grid);
    return label;
}

static void gtk_be_label_destroy(void *handle)
{
    if (handle) G.gtk_widget_destroy(handle);
}

static void gtk_be_label_set_text(void *handle, const char *text)
{
    if (handle) G.gtk_label_set_text(handle, text);
}

/* ── Button ──────────────────────────────────────────────────────── */

static void *gtk_be_button_create(void *parent, const char *text)
{
    if (!parent) return NULL;
    void *grid = grid_for_window(parent);
    if (!grid) return NULL;
    GtkWidget *btn = G.gtk_button_new_with_label(text);
    G.g_signal_connect_data(btn, "clicked", (GCallback)on_button_clicked,
                            NULL, NULL, 0);
    register_widget_parent(btn, grid);
    return btn;
}

static void gtk_be_button_destroy(void *handle)
{
    if (handle) G.gtk_widget_destroy(handle);
}

static void gtk_be_button_set_text(void *handle, const char *text)
{
    if (handle) G.gtk_button_set_label(handle, text);
}

/* ── Grid layout ─────────────────────────────────────────────────── */

static void gtk_be_widget_grid(void *handle, int col, int row)
{
    void *grid = get_widget_grid(handle);
    if (!grid) return;
    G.gtk_grid_attach(grid, handle, col, row, 1, 1);
    G.gtk_widget_show_all(handle);
}

static void gtk_be_widget_grid_remove(void *handle)
{
    if (handle) G.gtk_widget_destroy(handle);
}

static void gtk_be_container_grid_columnconfigure(void *handle, int index, int weight)
{
    (void)index;
    /* GTK grid doesn't have per-column weight on the container.
       Instead, set hexpand on children already attached.  For newly
       attached children, widget_grid will inherit the expand state
       from the grid's homogeneous property.  We mark all current
       children in the grid column as hexpand. */
    void *grid = grid_for_window(handle);
    if (!grid) return;
    for (int i = 0; i < g_widget_parent_count; i++)
        if (g_widget_parents[i].grid == grid)
            G.gtk_widget_set_hexpand(g_widget_parents[i].widget, weight > 0);
}

static void gtk_be_container_grid_rowconfigure(void *handle, int index, int weight)
{
    (void)index;
    void *grid = grid_for_window(handle);
    if (!grid) return;
    for (int i = 0; i < g_widget_parent_count; i++)
        if (g_widget_parents[i].grid == grid)
            G.gtk_widget_set_vexpand(g_widget_parents[i].widget, weight > 0);
}

/* ── Events ──────────────────────────────────────────────────────── */

static void gtk_be_set_callback(void *widget, gui_event_type_t type, gui_callback_t cb)
{
    gui_callback_t *existing = find_callback(widget, type);
    if (existing) { *existing = cb; return; }
    if (g_callback_count >= MAX_CALLBACKS) return;
    g_callbacks[g_callback_count++] = (callback_entry_t){widget, type, cb};
}

/* ── Dialogs ─────────────────────────────────────────────────────── */

static void gtk_be_message_box(void *parent, const char *title, const char *message)
{
    GtkWidget *dialog = G.gtk_message_dialog_new(
        parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", message);
    if (!dialog) return;
    G.gtk_window_set_title(dialog, title);
    G.gtk_dialog_run(dialog);
    G.gtk_widget_destroy(dialog);
}

/* ── Export ───────────────────────────────────────────────────────── */

const gui_backend_t gui_backend_gtk = {
    .name                          = "gtk",
    .init                          = gtk_be_init,
    .shutdown                      = gtk_be_shutdown,
    .main_loop                     = gtk_be_main_loop,
    .quit                          = gtk_be_quit,
    .window_create                 = gtk_be_window_create,
    .window_destroy                = gtk_be_window_destroy,
    .window_set_title              = gtk_be_window_set_title,
    .window_get_size               = gtk_be_window_get_size,
    .label_create                  = gtk_be_label_create,
    .label_destroy                 = gtk_be_label_destroy,
    .label_set_text                = gtk_be_label_set_text,
    .button_create                 = gtk_be_button_create,
    .button_destroy                = gtk_be_button_destroy,
    .button_set_text               = gtk_be_button_set_text,
    .widget_grid                   = gtk_be_widget_grid,
    .widget_grid_remove            = gtk_be_widget_grid_remove,
    .container_grid_columnconfigure = gtk_be_container_grid_columnconfigure,
    .container_grid_rowconfigure   = gtk_be_container_grid_rowconfigure,
    .set_callback                  = gtk_be_set_callback,
    .message_box                   = gtk_be_message_box,
};

#endif /* __linux__ */
