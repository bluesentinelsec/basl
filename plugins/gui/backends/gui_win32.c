/* gui_win32.c — Windows Win32 backend.
 *
 * Uses the classic Win32 API: WNDCLASS + CreateWindowExW for windows,
 * STATIC/BUTTON window classes for labels and buttons, and a simple
 * grid layout engine that positions children in the parent's client
 * area.  Links user32.dll and gdi32.dll (always present on Windows).
 */
#ifdef _WIN32

#include "../gui_backend.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Minimal Win32 type stand-ins ────────────────────────────────── */
/* Avoid pulling in <windows.h> to keep the translation unit small
   and avoid macro pollution.  We define only what we need. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* ── Grid layout engine ──────────────────────────────────────────── */

#define MAX_GRID_CHILDREN 128
#define MAX_GRID_DIM 32

typedef struct
{
    HWND widget;
    int col, row;
} grid_child_t;

typedef struct
{
    HWND container;
    grid_child_t children[MAX_GRID_CHILDREN];
    int child_count;
    int col_weights[MAX_GRID_DIM];
    int row_weights[MAX_GRID_DIM];
    int col_count, row_count;
} grid_layout_t;

#define MAX_GRIDS 64
static grid_layout_t g_grids[MAX_GRIDS];
static int g_grid_count = 0;

static grid_layout_t *grid_for_container(HWND c)
{
    for (int i = 0; i < g_grid_count; i++)
        if (g_grids[i].container == c)
            return &g_grids[i];
    return NULL;
}

static grid_layout_t *grid_ensure(HWND c)
{
    grid_layout_t *g = grid_for_container(c);
    if (g)
        return g;
    if (g_grid_count >= MAX_GRIDS)
        return NULL;
    g = &g_grids[g_grid_count++];
    memset(g, 0, sizeof(*g));
    g->container = c;
    return g;
}

static void grid_add_child(HWND container, HWND widget, int col, int row)
{
    grid_layout_t *g = grid_ensure(container);
    if (!g || g->child_count >= MAX_GRID_CHILDREN)
        return;
    g->children[g->child_count++] = (grid_child_t){widget, col, row};
    if (col + 1 > g->col_count)
        g->col_count = col + 1;
    if (row + 1 > g->row_count)
        g->row_count = row + 1;
}

static void grid_relayout(grid_layout_t *g)
{
    if (!g || g->child_count == 0)
        return;

    RECT rc;
    GetClientRect(g->container, &rc);
    int tw = rc.right - rc.left;
    int th = rc.bottom - rc.top;

    /* Compute column positions. */
    int cw_total = 0;
    for (int i = 0; i < g->col_count; i++)
        cw_total += (g->col_weights[i] > 0) ? g->col_weights[i] : 1;

    int col_x[MAX_GRID_DIM + 1];
    int x = 0;
    for (int i = 0; i < g->col_count; i++)
    {
        col_x[i] = x;
        int w = (g->col_weights[i] > 0) ? g->col_weights[i] : 1;
        x += tw * w / cw_total;
    }
    col_x[g->col_count] = tw;

    /* Compute row positions. */
    int rw_total = 0;
    for (int i = 0; i < g->row_count; i++)
        rw_total += (g->row_weights[i] > 0) ? g->row_weights[i] : 1;

    int row_y[MAX_GRID_DIM + 1];
    int y = 0;
    for (int i = 0; i < g->row_count; i++)
    {
        row_y[i] = y;
        int h = (g->row_weights[i] > 0) ? g->row_weights[i] : 1;
        y += th * h / rw_total;
    }
    row_y[g->row_count] = th;

    /* Position each child. */
    for (int i = 0; i < g->child_count; i++)
    {
        grid_child_t *c = &g->children[i];
        if (c->col >= g->col_count || c->row >= g->row_count)
            continue;
        int cx = col_x[c->col];
        int cy = row_y[c->row];
        int cw = col_x[c->col + 1] - cx;
        int ch = row_y[c->row + 1] - cy;
        SetWindowPos(c->widget, NULL, cx, cy, cw, ch, SWP_NOZORDER);
    }
}

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

/* ── Widget → parent tracking ────────────────────────────────────── */

#define MAX_WIDGETS 512

typedef struct
{
    HWND widget;
    HWND parent;
} widget_parent_t;

static widget_parent_t g_widget_parents[MAX_WIDGETS];
static int g_widget_parent_count = 0;

static void register_widget_parent(HWND w, HWND parent)
{
    if (g_widget_parent_count < MAX_WIDGETS)
        g_widget_parents[g_widget_parent_count++] = (widget_parent_t){w, parent};
}

static HWND get_widget_parent(HWND w)
{
    for (int i = 0; i < g_widget_parent_count; i++)
        if (g_widget_parents[i].widget == w)
            return g_widget_parents[i].parent;
    return NULL;
}

/* ── Shared state ────────────────────────────────────────────────── */

static HINSTANCE g_hinstance = NULL;
static const wchar_t *g_wndclass_name = L"VigilWindow";
static int g_next_ctrl_id = 1000;
static bool g_running = false;

/* ── UTF-8 → wide string helper ──────────────────────────────────── */

static wchar_t g_wbuf[1024];

static const wchar_t *to_wide(const char *utf8)
{
    if (!utf8 || !utf8[0])
    {
        g_wbuf[0] = L'\0';
        return g_wbuf;
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, g_wbuf, (int)(sizeof(g_wbuf) / sizeof(g_wbuf[0])));
    return g_wbuf;
}

/* ── Window procedure ────────────────────────────────────────────── */

static LRESULT CALLBACK vigil_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED)
        {
            HWND btn = (HWND)lp;
            gui_callback_t *cb = find_callback(btn, GUI_EVENT_CLICK);
            if (cb && cb->fn)
                cb->fn(cb->user_data);
            return 0;
        }
        break;

    case WM_SIZE: {
        grid_layout_t *g = grid_for_container(hwnd);
        if (g)
            grid_relayout(g);
        return 0;
    }

    case WM_CLOSE: {
        gui_callback_t *cb = find_callback(hwnd, GUI_EVENT_CLOSE);
        if (cb && cb->fn)
        {
            cb->fn(cb->user_data);
            return 0; /* Suppress default close. */
        }
        PostQuitMessage(0);
        return 0;
    }

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ── Backend implementation ──────────────────────────────────────── */

static bool win32_init(const char *app_name)
{
    (void)app_name;
    g_hinstance = GetModuleHandleW(NULL);
    if (!g_hinstance)
        return false;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = vigil_wndproc;
    wc.hInstance = g_hinstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = g_wndclass_name;

    if (!RegisterClassExW(&wc))
        return false;

    return true;
}

static void win32_shutdown(void)
{
    UnregisterClassW(g_wndclass_name, g_hinstance);
    g_hinstance = NULL;
    g_callback_count = 0;
    g_widget_parent_count = 0;
    g_grid_count = 0;
    g_next_ctrl_id = 1000;
    g_running = false;
}

static void win32_main_loop(void)
{
    /* Relayout all grids before entering the message loop. */
    for (int i = 0; i < g_grid_count; i++)
        grid_relayout(&g_grids[i]);

    g_running = true;
    MSG msg;
    while (g_running && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void win32_quit(void)
{
    g_running = false;
    PostQuitMessage(0);
}

/* ── Window ──────────────────────────────────────────────────────── */

static void *win32_window_create(const char *title, int w, int h)
{
    /* Adjust for non-client area so the client area matches w×h. */
    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = {0, 0, w, h};
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExW(0, g_wndclass_name, to_wide(title), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, g_hinstance, NULL);
    if (!hwnd)
        return NULL;

    grid_ensure(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return (void *)hwnd;
}

static void win32_window_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_window_set_title(void *handle, const char *title)
{
    if (handle)
        SetWindowTextW((HWND)handle, to_wide(title));
}

static void win32_window_get_size(void *handle, int *w, int *h)
{
    if (!handle)
    {
        *w = 0;
        *h = 0;
        return;
    }
    RECT rc;
    GetClientRect((HWND)handle, &rc);
    *w = rc.right - rc.left;
    *h = rc.bottom - rc.top;
}

/* ── Label ───────────────────────────────────────────────────────── */

static void *win32_label_create(void *parent, const char *text)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(0, L"STATIC", to_wide(text), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24,
                                (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
        register_widget_parent(hwnd, (HWND)parent);
    return (void *)hwnd;
}

static void win32_label_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_label_set_text(void *handle, const char *text)
{
    if (handle)
        SetWindowTextW((HWND)handle, to_wide(text));
}

/* ── Button ──────────────────────────────────────────────────────── */

static void *win32_button_create(void *parent, const char *text)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(0, L"BUTTON", to_wide(text), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 100, 32,
                                (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
        register_widget_parent(hwnd, (HWND)parent);
    return (void *)hwnd;
}

static void win32_button_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_button_set_text(void *handle, const char *text)
{
    if (handle)
        SetWindowTextW((HWND)handle, to_wide(text));
}

/* ── Entry ───────────────────────────────────────────────────────── */

static void *win32_entry_create(void *parent)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 200, 24,
                                (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
        register_widget_parent(hwnd, (HWND)parent);
    return (void *)hwnd;
}

static void win32_entry_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static const char *win32_entry_get_text(void *handle, char *buf, size_t bufsz)
{
    if (!handle)
    {
        buf[0] = '\0';
        return buf;
    }
    wchar_t wbuf[1024];
    int len = GetWindowTextW((HWND)handle, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0])));
    if (len > 0)
        WideCharToMultiByte(CP_UTF8, 0, wbuf, len, buf, (int)bufsz - 1, NULL, NULL);
    buf[(len > 0 && (size_t)len < bufsz) ? (size_t)len : 0] = '\0';
    return buf;
}

static void win32_entry_set_text(void *handle, const char *text)
{
    if (handle)
        SetWindowTextW((HWND)handle, to_wide(text));
}

/* ── Checkbox ────────────────────────────────────────────────────── */

static void *win32_checkbox_create(void *parent, const char *text)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(0, L"BUTTON", to_wide(text), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 200, 24,
                                (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
        register_widget_parent(hwnd, (HWND)parent);
    return (void *)hwnd;
}

static void win32_checkbox_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_checkbox_set_text(void *handle, const char *text)
{
    if (handle)
        SetWindowTextW((HWND)handle, to_wide(text));
}

static bool win32_checkbox_get_checked(void *handle)
{
    if (!handle)
        return false;
    return SendMessageW((HWND)handle, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void win32_checkbox_set_checked(void *handle, bool checked)
{
    if (handle)
        SendMessageW((HWND)handle, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* ── Grid layout ─────────────────────────────────────────────────── */

static void win32_widget_grid(void *handle, int col, int row)
{
    HWND parent = get_widget_parent((HWND)handle);
    if (parent)
        grid_add_child(parent, (HWND)handle, col, row);
}

static void win32_widget_grid_remove(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_container_grid_columnconfigure(void *handle, int index, int weight)
{
    grid_layout_t *g = grid_for_container((HWND)handle);
    if (g && index >= 0 && index < MAX_GRID_DIM)
        g->col_weights[index] = weight;
}

static void win32_container_grid_rowconfigure(void *handle, int index, int weight)
{
    grid_layout_t *g = grid_for_container((HWND)handle);
    if (g && index >= 0 && index < MAX_GRID_DIM)
        g->row_weights[index] = weight;
}

/* ── Events ──────────────────────────────────────────────────────── */

static void win32_set_callback(void *widget, gui_event_type_t type, gui_callback_t cb)
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

static void win32_message_box(void *parent, const char *title, const char *message)
{
    /* MessageBoxW needs wide strings; use a second buffer for title
       since to_wide uses a shared global buffer. */
    wchar_t title_buf[256];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, title_buf, (int)(sizeof(title_buf) / sizeof(title_buf[0])));
    MessageBoxW((HWND)parent, to_wide(message), title_buf, MB_OK | MB_ICONINFORMATION);
}

/* ── Export ───────────────────────────────────────────────────────── */

const gui_backend_t gui_backend_win32 = {
    .name = "win32",
    .init = win32_init,
    .shutdown = win32_shutdown,
    .main_loop = win32_main_loop,
    .quit = win32_quit,
    .window_create = win32_window_create,
    .window_destroy = win32_window_destroy,
    .window_set_title = win32_window_set_title,
    .window_get_size = win32_window_get_size,
    .label_create = win32_label_create,
    .label_destroy = win32_label_destroy,
    .label_set_text = win32_label_set_text,
    .button_create = win32_button_create,
    .button_destroy = win32_button_destroy,
    .button_set_text = win32_button_set_text,
    .entry_create = win32_entry_create,
    .entry_destroy = win32_entry_destroy,
    .entry_get_text = win32_entry_get_text,
    .entry_set_text = win32_entry_set_text,
    .checkbox_create = win32_checkbox_create,
    .checkbox_destroy = win32_checkbox_destroy,
    .checkbox_set_text = win32_checkbox_set_text,
    .checkbox_get_checked = win32_checkbox_get_checked,
    .checkbox_set_checked = win32_checkbox_set_checked,
    .widget_grid = win32_widget_grid,
    .widget_grid_remove = win32_widget_grid_remove,
    .container_grid_columnconfigure = win32_container_grid_columnconfigure,
    .container_grid_rowconfigure = win32_container_grid_rowconfigure,
    .set_callback = win32_set_callback,
    .message_box = win32_message_box,
};

#endif /* _WIN32 */
