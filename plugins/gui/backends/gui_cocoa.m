/* gui_cocoa.m — macOS Cocoa/AppKit backend via dlopen + objc_msgSend.
 *
 * No link-time dependency on AppKit or Cocoa.  All framework symbols
 * are resolved at runtime through dlopen/dlsym.
 *
 * IMPORTANT: On arm64, objc_msgSend must NEVER be called through a
 * variadic function pointer cast.  Every call site must use a cast
 * whose prototype exactly matches the target method's signature.
 */
#ifdef __APPLE__

#include "../gui_backend.h"

#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Objective-C helpers ─────────────────────────────────────────── */

#define cls(name)  objc_getClass(name)
#define sel(name)  sel_registerName(name)

/* Typed objc_msgSend casts.  Variadic casts silently break on arm64
   because the ABI passes variadic args on the stack while fixed args
   go in registers. */

typedef struct { double x, y, w, h; } CGRect;

static CGRect CGRectMake_(double x, double y, double w, double h)
{
    return (CGRect){x, y, w, h};
}

typedef id   (*msg_id_t)(id, SEL);
typedef id   (*msg_id_id_t)(id, SEL, id);
typedef id   (*msg_id_cstr_t)(id, SEL, const char *);
typedef id   (*msg_id_long_t)(id, SEL, long);
typedef id   (*msg_id_bool_t)(id, SEL, BOOL);
typedef id   (*msg_id_sel_t)(id, SEL, SEL);
typedef id   (*msg_id_rect_t)(id, SEL, CGRect);
typedef id   (*msg_id_id_id_t)(id, SEL, id, id);
typedef id   (*msg_id_rect_ulong_ulong_bool_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL);
typedef void (*msg_void_t)(id, SEL);
typedef void (*msg_void_id_t)(id, SEL, id);
typedef void (*msg_void_long_t)(id, SEL, long);
typedef void (*msg_void_bool_t)(id, SEL, BOOL);
typedef void (*msg_void_sel_t)(id, SEL, SEL);
typedef void (*msg_void_rect_t)(id, SEL, CGRect);
typedef void (*msg_void_id_bool_t)(id, SEL, id, BOOL);

/* Convenience wrappers. */
static id   send0(id obj, SEL s)                    { return ((msg_id_t)objc_msgSend)(obj, s); }
static id   send_cstr(id obj, SEL s, const char *a) { return ((msg_id_cstr_t)objc_msgSend)(obj, s, a); }
static void sendv(id obj, SEL s)                     { ((msg_void_t)objc_msgSend)(obj, s); }
static void sendv_id(id obj, SEL s, id a)            { ((msg_void_id_t)objc_msgSend)(obj, s, a); }
static void sendv_long(id obj, SEL s, long a)        { ((msg_void_long_t)objc_msgSend)(obj, s, a); }
static void sendv_bool(id obj, SEL s, BOOL a)        { ((msg_void_bool_t)objc_msgSend)(obj, s, a); }
static void sendv_sel(id obj, SEL s, SEL a)          { ((msg_void_sel_t)objc_msgSend)(obj, s, a); }
static void sendv_rect(id obj, SEL s, CGRect a)      { ((msg_void_rect_t)objc_msgSend)(obj, s, a); }
static id   send_rect(id obj, SEL s, CGRect a)       { return ((msg_id_rect_t)objc_msgSend)(obj, s, a); }

static id nsstr(const char *s) { return send_cstr(cls("NSString"), sel("stringWithUTF8String:"), s); }

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

/* ── AppDelegate class (created at runtime) ──────────────────────── */

static BOOL app_should_terminate(id self, SEL _cmd, id sender)
{
    (void)self; (void)_cmd; (void)sender;
    sendv_id(send0(cls("NSApplication"), sel("sharedApplication")),
             sel("stop:"), (id)NULL);
    return 0; /* NSTerminateCancel */
}

/* ── ButtonTarget class (dispatches on_click) ────────────────────── */

static void button_clicked(id self, SEL _cmd, id sender)
{
    (void)_cmd; (void)self;
    gui_callback_t *cb = find_callback((void *)sender, GUI_EVENT_CLICK);
    if (cb && cb->fn)
        cb->fn(cb->user_data);
}

/* ── WindowDelegate class (dispatches on_close) ──────────────────── */

static BOOL window_should_close(id self, SEL _cmd, id sender)
{
    (void)self; (void)_cmd;
    gui_callback_t *cb = find_callback((void *)sender, GUI_EVENT_CLOSE);
    if (cb && cb->fn)
    {
        cb->fn(cb->user_data);
        return 0;
    }
    return 1;
}

/* ── Shared state ────────────────────────────────────────────────── */

static id g_app = NULL;
static id g_app_delegate = NULL;
static id g_button_target = NULL;

/* ── Grid layout engine ──────────────────────────────────────────── */

#define MAX_GRID_CHILDREN 128
#define MAX_GRID_DIM 32

typedef struct { void *widget; int col, row; } grid_child_t;

typedef struct {
    void *container;
    grid_child_t children[MAX_GRID_CHILDREN];
    int child_count;
    int col_weights[MAX_GRID_DIM];
    int row_weights[MAX_GRID_DIM];
    int col_count, row_count;
} grid_layout_t;

#define MAX_GRIDS 64
static grid_layout_t g_grids[MAX_GRIDS];
static int g_grid_count = 0;

static grid_layout_t *grid_for_container(void *c)
{
    for (int i = 0; i < g_grid_count; i++)
        if (g_grids[i].container == c) return &g_grids[i];
    return NULL;
}

static grid_layout_t *grid_ensure(void *c)
{
    grid_layout_t *g = grid_for_container(c);
    if (g) return g;
    if (g_grid_count >= MAX_GRIDS) return NULL;
    g = &g_grids[g_grid_count++];
    memset(g, 0, sizeof(*g));
    g->container = c;
    return g;
}

static void grid_add_child(void *container, void *widget, int col, int row)
{
    grid_layout_t *g = grid_ensure(container);
    if (!g || g->child_count >= MAX_GRID_CHILDREN) return;
    g->children[g->child_count++] = (grid_child_t){widget, col, row};
    if (col + 1 > g->col_count) g->col_count = col + 1;
    if (row + 1 > g->row_count) g->row_count = row + 1;
}

static void grid_relayout(grid_layout_t *g)
{
    if (!g || g->child_count == 0) return;
    id container = (id)g->container;

    CGRect bounds;
#if defined(__aarch64__)
    bounds = ((CGRect (*)(id, SEL))objc_msgSend)(container, sel("bounds"));
#else
    ((void (*)(CGRect *, id, SEL))objc_msgSend_stret)(&bounds, container, sel("bounds"));
#endif

    double tw = bounds.w, th = bounds.h;

    int cw_total = 0;
    for (int i = 0; i < g->col_count; i++)
        cw_total += (g->col_weights[i] > 0) ? g->col_weights[i] : 1;
    double *col_x = (double *)__builtin_alloca(sizeof(double) * (size_t)(g->col_count + 1));
    double x = 0;
    for (int i = 0; i < g->col_count; i++) {
        col_x[i] = x;
        x += tw * ((g->col_weights[i] > 0) ? g->col_weights[i] : 1) / cw_total;
    }
    col_x[g->col_count] = tw;

    int rw_total = 0;
    for (int i = 0; i < g->row_count; i++)
        rw_total += (g->row_weights[i] > 0) ? g->row_weights[i] : 1;
    double *row_y = (double *)__builtin_alloca(sizeof(double) * (size_t)(g->row_count + 1));
    double y = 0;
    for (int i = 0; i < g->row_count; i++) {
        row_y[i] = y;
        y += th * ((g->row_weights[i] > 0) ? g->row_weights[i] : 1) / rw_total;
    }
    row_y[g->row_count] = th;

    for (int i = 0; i < g->child_count; i++) {
        grid_child_t *c = &g->children[i];
        if (c->col >= g->col_count || c->row >= g->row_count) continue;
        int fr = g->row_count - 1 - c->row;
        CGRect frame = CGRectMake_(col_x[c->col],
                                   row_y[fr],
                                   col_x[c->col + 1] - col_x[c->col],
                                   row_y[fr + 1] - row_y[fr]);
        sendv_rect((id)c->widget, sel("setFrame:"), frame);
    }
}

/* ── Widget parent tracking ──────────────────────────────────────── */

#define MAX_WIDGETS 512
typedef struct { void *widget; void *parent_content_view; } widget_parent_t;
static widget_parent_t g_widget_parents[MAX_WIDGETS];
static int g_widget_parent_count = 0;

static void register_widget_parent(void *w, void *pcv)
{
    if (g_widget_parent_count < MAX_WIDGETS)
        g_widget_parents[g_widget_parent_count++] = (widget_parent_t){w, pcv};
}

static void *get_widget_parent(void *w)
{
    for (int i = 0; i < g_widget_parent_count; i++)
        if (g_widget_parents[i].widget == w) return g_widget_parents[i].parent_content_view;
    return NULL;
}

/* ── Backend implementation ──────────────────────────────────────── */

static bool cocoa_init(const char *app_name)
{
    (void)app_name;
    void *appkit = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_LAZY);
    if (!appkit) return false;

    g_app = send0(cls("NSApplication"), sel("sharedApplication"));
    if (!g_app) return false;

    sendv_long(g_app, sel("setActivationPolicy:"), 0); /* NSApplicationActivationPolicyRegular */

    /* Register runtime classes. */
    Class del_cls = objc_allocateClassPair(objc_getClass("NSObject"), "VigilAppDelegate", 0);
    if (del_cls) {
        class_addMethod(del_cls, sel("applicationShouldTerminate:"),
                        (IMP)app_should_terminate, "l@:@");
        objc_registerClassPair(del_cls);
    }
    /* Skip setDelegate: — uses objc_storeWeak which crashes with
       runtime-created classes on arm64 macOS. */

    Class btn_cls = objc_allocateClassPair(objc_getClass("NSObject"), "VigilButtonTarget", 0);
    if (btn_cls) {
        class_addMethod(btn_cls, sel("buttonClicked:"), (IMP)button_clicked, "v@:@");
        objc_registerClassPair(btn_cls);
        g_button_target = send0((id)btn_cls, sel("new"));
    }

    Class win_del_cls = objc_allocateClassPair(objc_getClass("NSObject"), "VigilWindowDelegate", 0);
    if (win_del_cls) {
        class_addMethod(win_del_cls, sel("windowShouldClose:"),
                        (IMP)window_should_close, "c@:@");
        objc_registerClassPair(win_del_cls);
    }

    return true;
}

static void cocoa_shutdown(void)
{
    g_app = NULL;
    g_app_delegate = NULL;
    g_button_target = NULL;
    g_callback_count = 0;
    g_grid_count = 0;
    g_widget_parent_count = 0;
}

static void cocoa_main_loop(void)
{
    sendv_bool(g_app, sel("activateIgnoringOtherApps:"), 1);
    for (int i = 0; i < g_grid_count; i++)
        grid_relayout(&g_grids[i]);
    sendv(g_app, sel("run"));
}

static void cocoa_quit(void)
{
    sendv_id(g_app, sel("stop:"), (id)NULL);
    /* Post a dummy event to unblock the run loop. */
    typedef id (*event_fn_t)(id, SEL, long, CGRect, unsigned long,
                             double, long, id, short, long, long);
    CGRect zero = {0, 0, 0, 0};
    id event = ((event_fn_t)objc_msgSend)(
        cls("NSEvent"),
        sel("otherEventWithType:location:modifierFlags:"
            "timestamp:windowNumber:context:subtype:data1:data2:"),
        15 /* NSEventTypeApplicationDefined */,
        zero, 0UL, 0.0, 0L, (id)NULL, (short)0, 0L, 0L);
    ((msg_void_id_bool_t)objc_msgSend)(g_app, sel("postEvent:atStart:"), event, (BOOL)1);
}

/* ── Window ──────────────────────────────────────────────────────── */

static void *cocoa_window_create(const char *title, int w, int h)
{
    CGRect frame = CGRectMake_(100, 100, w, h);
    unsigned long style = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);

    id window = ((msg_id_rect_ulong_ulong_bool_t)objc_msgSend)(
        send0(cls("NSWindow"), sel("alloc")),
        sel("initWithContentRect:styleMask:backing:defer:"),
        frame, style, 2 /* NSBackingStoreBuffered */, 0);
    if (!window) return NULL;

    sendv_id(window, sel("setTitle:"), nsstr(title));

    id content_view = send0(window, sel("contentView"));
    sendv_id(window, sel("makeKeyAndOrderFront:"), (id)NULL);
    sendv(window, sel("center"));
    grid_ensure(content_view);
    return (void *)window;
}

static void cocoa_window_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("close"));
}

static void cocoa_window_set_title(void *handle, const char *title)
{
    if (handle) sendv_id((id)handle, sel("setTitle:"), nsstr(title));
}

static void cocoa_window_get_size(void *handle, int *w, int *h)
{
    if (!handle) { *w = 0; *h = 0; return; }
    id cv = send0((id)handle, sel("contentView"));
    CGRect bounds;
#if defined(__aarch64__)
    bounds = ((CGRect (*)(id, SEL))objc_msgSend)(cv, sel("bounds"));
#else
    ((void (*)(CGRect *, id, SEL))objc_msgSend_stret)(&bounds, cv, sel("bounds"));
#endif
    *w = (int)bounds.w;
    *h = (int)bounds.h;
}

/* ── Label ───────────────────────────────────────────────────────── */

static void *cocoa_label_create(void *parent, const char *text)
{
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id label = send_rect(send0(cls("NSTextField"), sel("alloc")),
                         sel("initWithFrame:"), CGRectMake_(0, 0, 200, 24));
    sendv_id(label, sel("setStringValue:"), nsstr(text));
    sendv_bool(label, sel("setBezeled:"), 0);
    sendv_bool(label, sel("setDrawsBackground:"), 0);
    sendv_bool(label, sel("setEditable:"), 0);
    sendv_bool(label, sel("setSelectable:"), 0);
    sendv_id(cv, sel("addSubview:"), label);
    register_widget_parent(label, cv);
    return (void *)label;
}

static void cocoa_label_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static void cocoa_label_set_text(void *handle, const char *text)
{
    if (handle) sendv_id((id)handle, sel("setStringValue:"), nsstr(text));
}

/* ── Button ──────────────────────────────────────────────────────── */

static void *cocoa_button_create(void *parent, const char *text)
{
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id button = send_rect(send0(cls("NSButton"), sel("alloc")),
                          sel("initWithFrame:"), CGRectMake_(0, 0, 100, 32));
    sendv_id(button, sel("setTitle:"), nsstr(text));
    sendv_long(button, sel("setBezelStyle:"), 1);
    sendv_long(button, sel("setButtonType:"), 0);
    if (g_button_target) {
        sendv_id(button, sel("setTarget:"), g_button_target);
        sendv_sel(button, sel("setAction:"), sel("buttonClicked:"));
    }
    sendv_id(cv, sel("addSubview:"), button);
    register_widget_parent(button, cv);
    return (void *)button;
}

static void cocoa_button_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static void cocoa_button_set_text(void *handle, const char *text)
{
    if (handle) sendv_id((id)handle, sel("setTitle:"), nsstr(text));
}

/* ── Entry ───────────────────────────────────────────────────────── */

static void *cocoa_entry_create(void *parent)
{
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id field = send_rect(send0(cls("NSTextField"), sel("alloc")),
                         sel("initWithFrame:"), CGRectMake_(0, 0, 200, 24));
    sendv_id(field, sel("setStringValue:"), nsstr(""));
    sendv_bool(field, sel("setEditable:"), 1);
    sendv_bool(field, sel("setBezeled:"), 1);
    sendv_bool(field, sel("setDrawsBackground:"), 1);
    sendv_id(cv, sel("addSubview:"), field);
    register_widget_parent(field, cv);
    return (void *)field;
}

static void cocoa_entry_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static const char *cocoa_entry_get_text(void *handle, char *buf, size_t bufsz)
{
    if (!handle) { buf[0] = '\0'; return buf; }
    id nsval = send0((id)handle, sel("stringValue"));
    const char *s = ((const char *(*)(id, SEL))objc_msgSend)(nsval, sel("UTF8String"));
    if (s) {
        size_t len = strlen(s);
        if (len >= bufsz) len = bufsz - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
    } else {
        buf[0] = '\0';
    }
    return buf;
}

static void cocoa_entry_set_text(void *handle, const char *text)
{
    if (handle) sendv_id((id)handle, sel("setStringValue:"), nsstr(text));
}

/* ── Checkbox ────────────────────────────────────────────────────── */

static void *cocoa_checkbox_create(void *parent, const char *text)
{
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id button = send_rect(send0(cls("NSButton"), sel("alloc")),
                          sel("initWithFrame:"), CGRectMake_(0, 0, 200, 24));
    sendv_id(button, sel("setTitle:"), nsstr(text));
    sendv_long(button, sel("setButtonType:"), 3); /* NSSwitchButton */
    if (g_button_target) {
        sendv_id(button, sel("setTarget:"), g_button_target);
        sendv_sel(button, sel("setAction:"), sel("buttonClicked:"));
    }
    sendv_id(cv, sel("addSubview:"), button);
    register_widget_parent(button, cv);
    return (void *)button;
}

static void cocoa_checkbox_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static void cocoa_checkbox_set_text(void *handle, const char *text)
{
    if (handle) sendv_id((id)handle, sel("setTitle:"), nsstr(text));
}

static bool cocoa_checkbox_get_checked(void *handle)
{
    if (!handle) return false;
    long state = ((long (*)(id, SEL))objc_msgSend)((id)handle, sel("state"));
    return state != 0;
}

static void cocoa_checkbox_set_checked(void *handle, bool checked)
{
    if (handle) sendv_long((id)handle, sel("setState:"), checked ? 1 : 0);
}

/* ── Slider ───────────────────────────────────────────────────────── */

static void *cocoa_slider_create(void *parent, double min_val, double max_val)
{
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id slider = send_rect(send0(cls("NSSlider"), sel("alloc")),
                          sel("initWithFrame:"), CGRectMake_(0, 0, 200, 24));
    ((void (*)(id, SEL, double))objc_msgSend)(slider, sel("setMinValue:"), min_val);
    ((void (*)(id, SEL, double))objc_msgSend)(slider, sel("setMaxValue:"), max_val);
    ((void (*)(id, SEL, double))objc_msgSend)(slider, sel("setDoubleValue:"), min_val);
    sendv_id(cv, sel("addSubview:"), slider);
    register_widget_parent(slider, cv);
    return (void *)slider;
}

static void cocoa_slider_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static double cocoa_slider_get_value(void *handle)
{
    if (!handle) return 0.0;
    return ((double (*)(id, SEL))objc_msgSend)((id)handle, sel("doubleValue"));
}

static void cocoa_slider_set_value(void *handle, double value)
{
    if (handle) ((void (*)(id, SEL, double))objc_msgSend)((id)handle, sel("setDoubleValue:"), value);
}

/* ── Select ───────────────────────────────────────────────────────── */

static void *cocoa_select_create(void *parent)
{
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id popup = send_rect(send0(cls("NSPopUpButton"), sel("alloc")),
                         sel("initWithFrame:"), CGRectMake_(0, 0, 200, 24));
    sendv_id(cv, sel("addSubview:"), popup);
    register_widget_parent(popup, cv);
    return (void *)popup;
}

static void cocoa_select_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static void cocoa_select_add_item(void *handle, const char *text)
{
    if (handle) sendv_id((id)handle, sel("addItemWithTitle:"), nsstr(text));
}

static int cocoa_select_get_index(void *handle)
{
    if (!handle) return -1;
    return (int)((long (*)(id, SEL))objc_msgSend)((id)handle, sel("indexOfSelectedItem"));
}

static void cocoa_select_set_index(void *handle, int index)
{
    if (handle) sendv_long((id)handle, sel("selectItemAtIndex:"), (long)index);
}

/* ── Text ─────────────────────────────────────────────────────────── */

static void *cocoa_text_create(void *parent)
{
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id sv = send_rect(send0(cls("NSScrollView"), sel("alloc")),
                      sel("initWithFrame:"), CGRectMake_(0, 0, 200, 100));
    sendv_bool(sv, sel("setHasVerticalScroller:"), 1);
    id tv = send_rect(send0(cls("NSTextView"), sel("alloc")),
                      sel("initWithFrame:"), CGRectMake_(0, 0, 200, 100));
    sendv_bool(tv, sel("setEditable:"), 1);
    sendv_id(sv, sel("setDocumentView:"), tv);
    sendv_id(cv, sel("addSubview:"), sv);
    register_widget_parent(sv, cv);
    return (void *)sv;
}

static void cocoa_text_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static const char *cocoa_text_get_text(void *handle, char *buf, size_t bufsz)
{
    if (!handle) { buf[0] = '\0'; return buf; }
    id tv = send0((id)handle, sel("documentView"));
    id nsval = send0(tv, sel("string"));
    const char *s = ((const char *(*)(id, SEL))objc_msgSend)(nsval, sel("UTF8String"));
    if (s) {
        size_t len = strlen(s);
        if (len >= bufsz) len = bufsz - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
    } else {
        buf[0] = '\0';
    }
    return buf;
}

static void cocoa_text_set_text(void *handle, const char *text)
{
    if (!handle) return;
    id tv = send0((id)handle, sel("documentView"));
    sendv_id(tv, sel("setString:"), nsstr(text));
}

/* ── Radio ────────────────────────────────────────────────────────── */

static void *cocoa_radio_create(void *parent, const char *text, void *group)
{
    (void)group; /* Cocoa radio buttons are grouped by sharing an action/target */
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id button = send_rect(send0(cls("NSButton"), sel("alloc")),
                          sel("initWithFrame:"), CGRectMake_(0, 0, 200, 24));
    sendv_id(button, sel("setTitle:"), nsstr(text));
    sendv_long(button, sel("setButtonType:"), 4); /* NSRadioButton */
    if (g_button_target) {
        sendv_id(button, sel("setTarget:"), g_button_target);
        sendv_sel(button, sel("setAction:"), sel("buttonClicked:"));
    }
    sendv_id(cv, sel("addSubview:"), button);
    register_widget_parent(button, cv);
    return (void *)button;
}

static void cocoa_radio_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static void cocoa_radio_set_text(void *handle, const char *text)
{
    if (handle) sendv_id((id)handle, sel("setTitle:"), nsstr(text));
}

static bool cocoa_radio_get_active(void *handle)
{
    if (!handle) return false;
    long state = ((long (*)(id, SEL))objc_msgSend)((id)handle, sel("state"));
    return state != 0;
}

static void cocoa_radio_set_active(void *handle, bool active)
{
    if (handle) sendv_long((id)handle, sel("setState:"), active ? 1 : 0);
}

/* ── Spinbox ──────────────────────────────────────────────────────── */

static void *cocoa_spinbox_create(void *parent, double min_val, double max_val, double step)
{
    (void)step;
    if (!parent) return NULL;
    id cv = send0((id)parent, sel("contentView"));
    id stepper = send_rect(send0(cls("NSStepper"), sel("alloc")),
                           sel("initWithFrame:"), CGRectMake_(0, 0, 100, 24));
    ((void (*)(id, SEL, double))objc_msgSend)(stepper, sel("setMinValue:"), min_val);
    ((void (*)(id, SEL, double))objc_msgSend)(stepper, sel("setMaxValue:"), max_val);
    ((void (*)(id, SEL, double))objc_msgSend)(stepper, sel("setIncrement:"), step);
    ((void (*)(id, SEL, double))objc_msgSend)(stepper, sel("setDoubleValue:"), min_val);
    sendv_id(cv, sel("addSubview:"), stepper);
    register_widget_parent(stepper, cv);
    return (void *)stepper;
}

static void cocoa_spinbox_destroy(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static double cocoa_spinbox_get_value(void *handle)
{
    if (!handle) return 0.0;
    return ((double (*)(id, SEL))objc_msgSend)((id)handle, sel("doubleValue"));
}

static void cocoa_spinbox_set_value(void *handle, double value)
{
    if (handle) ((void (*)(id, SEL, double))objc_msgSend)((id)handle, sel("setDoubleValue:"), value);
}

/* ── Grid layout ─────────────────────────────────────────────────── */

static void cocoa_widget_grid(void *handle, int col, int row)
{
    void *pcv = get_widget_parent(handle);
    if (pcv) grid_add_child(pcv, handle, col, row);
}

static void cocoa_widget_grid_remove(void *handle)
{
    if (handle) sendv((id)handle, sel("removeFromSuperview"));
}

static void cocoa_container_grid_columnconfigure(void *handle, int index, int weight)
{
    id cv = send0((id)handle, sel("contentView"));
    grid_layout_t *g = grid_ensure(cv);
    if (g && index >= 0 && index < MAX_GRID_DIM) g->col_weights[index] = weight;
}

static void cocoa_container_grid_rowconfigure(void *handle, int index, int weight)
{
    id cv = send0((id)handle, sel("contentView"));
    grid_layout_t *g = grid_ensure(cv);
    if (g && index >= 0 && index < MAX_GRID_DIM) g->row_weights[index] = weight;
}

/* ── Events ──────────────────────────────────────────────────────── */

static void cocoa_set_callback(void *widget, gui_event_type_t type, gui_callback_t cb)
{
    gui_callback_t *existing = find_callback(widget, type);
    if (existing) { *existing = cb; return; }
    if (g_callback_count >= MAX_CALLBACKS) return;
    g_callbacks[g_callback_count++] = (callback_entry_t){widget, type, cb};
}

/* ── Dialogs ─────────────────────────────────────────────────────── */

static void cocoa_message_box(void *parent, const char *title, const char *message)
{
    id alert = send0(send0(cls("NSAlert"), sel("alloc")), sel("init"));
    sendv_id(alert, sel("setMessageText:"), nsstr(title));
    sendv_id(alert, sel("setInformativeText:"), nsstr(message));
    sendv_id(alert, sel("addButtonWithTitle:"), nsstr("OK"));
    if (parent)
        ((msg_void_id_bool_t)objc_msgSend)(alert,
            sel("beginSheetModalForWindow:completionHandler:"), (id)parent, (id)NULL);
    else
        sendv(alert, sel("runModal"));
}

/* ── Export ───────────────────────────────────────────────────────── */

const gui_backend_t gui_backend_cocoa = {
    .name                          = "cocoa",
    .init                          = cocoa_init,
    .shutdown                      = cocoa_shutdown,
    .main_loop                     = cocoa_main_loop,
    .quit                          = cocoa_quit,
    .window_create                 = cocoa_window_create,
    .window_destroy                = cocoa_window_destroy,
    .window_set_title              = cocoa_window_set_title,
    .window_get_size               = cocoa_window_get_size,
    .label_create                  = cocoa_label_create,
    .label_destroy                 = cocoa_label_destroy,
    .label_set_text                = cocoa_label_set_text,
    .button_create                 = cocoa_button_create,
    .button_destroy                = cocoa_button_destroy,
    .button_set_text               = cocoa_button_set_text,
    .entry_create                  = cocoa_entry_create,
    .entry_destroy                 = cocoa_entry_destroy,
    .entry_get_text                = cocoa_entry_get_text,
    .entry_set_text                = cocoa_entry_set_text,
    .checkbox_create               = cocoa_checkbox_create,
    .checkbox_destroy              = cocoa_checkbox_destroy,
    .checkbox_set_text             = cocoa_checkbox_set_text,
    .checkbox_get_checked          = cocoa_checkbox_get_checked,
    .checkbox_set_checked          = cocoa_checkbox_set_checked,
    .slider_create                 = cocoa_slider_create,
    .slider_destroy                = cocoa_slider_destroy,
    .slider_get_value              = cocoa_slider_get_value,
    .slider_set_value              = cocoa_slider_set_value,
    .select_create                 = cocoa_select_create,
    .select_destroy                = cocoa_select_destroy,
    .select_add_item               = cocoa_select_add_item,
    .select_get_index              = cocoa_select_get_index,
    .select_set_index              = cocoa_select_set_index,
    .text_create                   = cocoa_text_create,
    .text_destroy                  = cocoa_text_destroy,
    .text_get_text                 = cocoa_text_get_text,
    .text_set_text                 = cocoa_text_set_text,
    .radio_create                  = cocoa_radio_create,
    .radio_destroy                 = cocoa_radio_destroy,
    .radio_set_text                = cocoa_radio_set_text,
    .radio_get_active              = cocoa_radio_get_active,
    .radio_set_active              = cocoa_radio_set_active,
    .spinbox_create                = cocoa_spinbox_create,
    .spinbox_destroy               = cocoa_spinbox_destroy,
    .spinbox_get_value             = cocoa_spinbox_get_value,
    .spinbox_set_value             = cocoa_spinbox_set_value,
    .widget_grid                   = cocoa_widget_grid,
    .widget_grid_remove            = cocoa_widget_grid_remove,
    .container_grid_columnconfigure = cocoa_container_grid_columnconfigure,
    .container_grid_rowconfigure   = cocoa_container_grid_rowconfigure,
    .set_callback                  = cocoa_set_callback,
    .message_box                   = cocoa_message_box,
};

#endif /* __APPLE__ */
