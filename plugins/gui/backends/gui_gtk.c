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

/* GTK3-only function pointer types */
typedef GtkWidget *(*fn_gtk_window_new_gtk3)(int type);
typedef void (*fn_gtk_window_get_size_gtk3)(GtkWindow *, gint *, gint *);
typedef void (*fn_gtk_widget_show_all)(GtkWidget *);
typedef void (*fn_gtk_widget_destroy)(GtkWidget *);
typedef void (*fn_gtk_container_add)(GtkWidget *, GtkWidget *);
typedef GtkWidget *(*fn_gtk_message_dialog_new)(GtkWindow *, int, int, int, const char *, ...);
typedef gint (*fn_gtk_dialog_run)(GtkWidget *);
typedef void (*fn_gtk_main)(void);
typedef void (*fn_gtk_main_quit)(void);

/* GTK4-only function pointer types */
typedef GtkWidget *(*fn_gtk_window_new_gtk4)(void);
typedef void (*fn_gtk_window_get_default_size)(GtkWindow *, gint *, gint *);
typedef void (*fn_gtk_widget_set_visible)(GtkWidget *, gboolean);
typedef void (*fn_gtk_window_set_child)(GtkWindow *, GtkWidget *);
typedef void (*fn_gtk_grid_remove)(GtkGrid *, GtkWidget *);
typedef void (*fn_gtk_window_destroy)(GtkWindow *);

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

/* ── Grid layout ─────────────────────────────────────────────────── */

static void gtk_be_widget_grid(void *handle, int col, int row)
{
    void *grid = get_widget_grid(handle);
    if (!grid)
        return;
    G.gtk_grid_attach(grid, handle, col, row, 1, 1);
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
    .widget_grid = gtk_be_widget_grid,
    .widget_grid_remove = gtk_be_widget_grid_remove,
    .container_grid_columnconfigure = gtk_be_container_grid_columnconfigure,
    .container_grid_rowconfigure = gtk_be_container_grid_rowconfigure,
    .set_callback = gtk_be_set_callback,
    .message_box = gtk_be_message_box,
};

#endif /* __linux__ */
