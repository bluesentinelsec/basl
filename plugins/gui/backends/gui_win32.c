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
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
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
        /* Menu item commands (lp == 0 for menu items). */
        if (lp == 0 && HIWORD(wp) == 0)
        {
            int id = LOWORD(wp) - g_menu_id_base;
            if (id >= 0 && id < g_menu_cb_count && g_menu_cbs[id].fn)
                g_menu_cbs[id].fn(g_menu_cbs[id].user_data);
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

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
        w32_canvas_t *cv = w32_canvas_for(dis->hwndItem);
        if (cv)
        {
            HDC hdc = dis->hDC;
            RECT *rc = &dis->rcItem;
            HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, rc, white);
            DeleteObject(white);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            SelectObject(hdc, pen);
            SetBkMode(hdc, TRANSPARENT);
            for (int i = 0; i < cv->cmd_count; i++)
            {
                w32_draw_cmd_t *d = &cv->cmds[i];
                switch (d->type)
                {
                case W32_DRAW_LINE:
                    MoveToEx(hdc, d->x1, d->y1, NULL);
                    LineTo(hdc, d->x2, d->y2);
                    break;
                case W32_DRAW_RECT: {
                    HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
                    SelectObject(hdc, nb);
                    Rectangle(hdc, d->x1, d->y1, d->x1 + d->x2, d->y1 + d->y2);
                    break;
                }
                case W32_DRAW_OVAL: {
                    HBRUSH nb2 = (HBRUSH)GetStockObject(NULL_BRUSH);
                    SelectObject(hdc, nb2);
                    Ellipse(hdc, d->x1, d->y1, d->x1 + d->x2, d->y1 + d->y2);
                    break;
                }
                case W32_DRAW_TEXT: {
                    wchar_t wt[128];
                    MultiByteToWideChar(CP_UTF8, 0, d->text, -1, wt, 128);
                    TextOutW(hdc, d->x1, d->y1, wt, (int)wcslen(wt));
                    break;
                }
                }
            }
            DeleteObject(pen);
            return TRUE;
        }
        break;
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

/* ── Slider ──────────────────────────────────────────────────────── */

static void *win32_slider_create(void *parent, double min_val, double max_val)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS, 0, 0, 200,
                                30, (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
    {
        SendMessageW(hwnd, TBM_SETRANGEMIN, 0, (LPARAM)(int)min_val);
        SendMessageW(hwnd, TBM_SETRANGEMAX, 0, (LPARAM)(int)max_val);
        register_widget_parent(hwnd, (HWND)parent);
    }
    return (void *)hwnd;
}

static void win32_slider_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static double win32_slider_get_value(void *handle)
{
    if (!handle)
        return 0.0;
    return (double)SendMessageW((HWND)handle, TBM_GETPOS, 0, 0);
}

static void win32_slider_set_value(void *handle, double value)
{
    if (handle)
        SendMessageW((HWND)handle, TBM_SETPOS, 1, (LPARAM)(int)value);
}

/* ── Select ──────────────────────────────────────────────────────── */

static void *win32_select_create(void *parent)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0,
                                200, 200, (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
        register_widget_parent(hwnd, (HWND)parent);
    return (void *)hwnd;
}

static void win32_select_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_select_add_item(void *handle, const char *text)
{
    if (handle)
        SendMessageW((HWND)handle, CB_ADDSTRING, 0, (LPARAM)to_wide(text));
}

static int win32_select_get_index(void *handle)
{
    if (!handle)
        return -1;
    return (int)SendMessageW((HWND)handle, CB_GETCURSEL, 0, 0);
}

static void win32_select_set_index(void *handle, int index)
{
    if (handle)
        SendMessageW((HWND)handle, CB_SETCURSEL, (WPARAM)index, 0);
}

/* ── Text ────────────────────────────────────────────────────────── */

static void *win32_text_create(void *parent)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 0,
                                0, 200, 100, (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
        register_widget_parent(hwnd, (HWND)parent);
    return (void *)hwnd;
}

static void win32_text_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static const char *win32_text_get_text(void *handle, char *buf, size_t bufsz)
{
    if (!handle)
    {
        buf[0] = '\0';
        return buf;
    }
    int len = GetWindowTextLengthW((HWND)handle);
    if (len <= 0)
    {
        buf[0] = '\0';
        return buf;
    }
    wchar_t *wbuf = (wchar_t *)_alloca(sizeof(wchar_t) * (len + 1));
    GetWindowTextW((HWND)handle, wbuf, len + 1);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, (int)bufsz, NULL, NULL);
    buf[bufsz - 1] = '\0';
    return buf;
}

static void win32_text_set_text(void *handle, const char *text)
{
    if (handle)
        SetWindowTextW((HWND)handle, to_wide(text));
}

/* ── Radio ───────────────────────────────────────────────────────── */

static void *win32_radio_create(void *parent, const char *text, void *group)
{
    if (!parent)
        return NULL;
    DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
    if (!group)
        style |= WS_GROUP;
    HWND hwnd = CreateWindowExW(0, L"BUTTON", to_wide(text), style, 0, 0, 200, 24, (HWND)parent,
                                (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
        register_widget_parent(hwnd, (HWND)parent);
    return (void *)hwnd;
}

static void win32_radio_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_radio_set_text(void *handle, const char *text)
{
    if (handle)
        SetWindowTextW((HWND)handle, to_wide(text));
}

static bool win32_radio_get_active(void *handle)
{
    if (!handle)
        return false;
    return SendMessageW((HWND)handle, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void win32_radio_set_active(void *handle, bool active)
{
    if (handle)
        SendMessageW((HWND)handle, BM_SETCHECK, active ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* ── Spinbox ─────────────────────────────────────────────────────── */

/* Win32 doesn't have a native spin+edit combo in one call.
   Use an EDIT control paired with an Up-Down control. For simplicity,
   we use a trackbar-style approach: store min/max/step and use
   an EDIT control with up-down buddy. */

static void *win32_spinbox_create(void *parent, double min_val, double max_val, double step)
{
    (void)step;
    if (!parent)
        return NULL;
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 100, 24,
                                (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (!edit)
        return NULL;
    HWND updown = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT, 0, 0,
                                  0, 0, (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (updown)
    {
        SendMessageW(updown, UDM_SETBUDDY, (WPARAM)edit, 0);
        SendMessageW(updown, UDM_SETRANGE32, (WPARAM)(int)min_val, (LPARAM)(int)max_val);
        SendMessageW(updown, UDM_SETPOS32, 0, (LPARAM)(int)min_val);
    }
    register_widget_parent(edit, (HWND)parent);
    return (void *)edit;
}

static void win32_spinbox_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static double win32_spinbox_get_value(void *handle)
{
    if (!handle)
        return 0.0;
    wchar_t wbuf[64];
    GetWindowTextW((HWND)handle, wbuf, 64);
    return _wtof(wbuf);
}

static void win32_spinbox_set_value(void *handle, double value)
{
    if (!handle)
        return;
    wchar_t wbuf[64];
    _snwprintf(wbuf, 64, L"%d", (int)value);
    SetWindowTextW((HWND)handle, wbuf);
}

/* ── Frame ───────────────────────────────────────────────────────── */

static void *win32_frame_create(void *parent, const char *label)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(0, L"BUTTON", to_wide(label), WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 200, 100,
                                (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
    {
        grid_ensure(hwnd);
        register_widget_parent(hwnd, (HWND)parent);
    }
    return (void *)hwnd;
}

static void win32_frame_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_frame_set_label(void *handle, const char *label)
{
    if (handle)
        SetWindowTextW((HWND)handle, to_wide(label));
}

/* ── Listbox ─────────────────────────────────────────────────────── */

static void *win32_listbox_create(void *parent)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, 0,
                                0, 200, 100, (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
        register_widget_parent(hwnd, (HWND)parent);
    return (void *)hwnd;
}

static void win32_listbox_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_listbox_add_item(void *handle, const char *text)
{
    if (handle)
        SendMessageW((HWND)handle, LB_ADDSTRING, 0, (LPARAM)to_wide(text));
}

static int win32_listbox_get_selected(void *handle)
{
    if (!handle)
        return -1;
    return (int)SendMessageW((HWND)handle, LB_GETCURSEL, 0, 0);
}

static void win32_listbox_set_selected(void *handle, int index)
{
    if (handle)
        SendMessageW((HWND)handle, LB_SETCURSEL, (WPARAM)index, 0);
}

/* ── Menu ────────────────────────────────────────────────────────── */

#define MAX_MENU_CALLBACKS 128
static gui_callback_t g_menu_cbs[MAX_MENU_CALLBACKS];
static int g_menu_cb_count = 0;
static int g_menu_id_base = 9000;

static void *win32_menubar_create(void *window)
{
    HMENU mb = CreateMenu();
    if (mb)
        SetMenu((HWND)window, mb);
    return (void *)mb;
}

static void win32_menubar_destroy(void *handle)
{
    if (handle)
        DestroyMenu((HMENU)handle);
}

static void *win32_menu_add_submenu(void *menubar, const char *label)
{
    HMENU sub = CreatePopupMenu();
    if (sub)
        AppendMenuW((HMENU)menubar, MF_POPUP, (UINT_PTR)sub, to_wide(label));
    return (void *)sub;
}

static void win32_menu_add_item(void *submenu, const char *label, gui_callback_t cb)
{
    int id = g_menu_id_base + g_menu_cb_count;
    if (g_menu_cb_count < MAX_MENU_CALLBACKS)
        g_menu_cbs[g_menu_cb_count++] = cb;
    AppendMenuW((HMENU)submenu, MF_STRING, (UINT_PTR)id, to_wide(label));
}

/* ── PanedWindow ─────────────────────────────────────────────────── */

/* Win32 has no native split pane. Use a static container; children
   are positioned manually by the grid engine. */

static void *win32_paned_create(void *parent, bool horizontal)
{
    (void)horizontal;
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 400, 200, (HWND)parent,
                                (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
    {
        grid_ensure(hwnd);
        register_widget_parent(hwnd, (HWND)parent);
    }
    return (void *)hwnd;
}

static void win32_paned_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_paned_set_start(void *handle, void *child)
{
    (void)handle;
    (void)child;
}

static void win32_paned_set_end(void *handle, void *child)
{
    (void)handle;
    (void)child;
}

static void win32_paned_set_position(void *handle, int pos)
{
    (void)handle;
    (void)pos;
}

/* ── Canvas ──────────────────────────────────────────────────────── */

/* Draw command queue — replayed in WM_PAINT via WM_DRAWITEM for
   SS_OWNERDRAW static controls. */

enum
{
    W32_DRAW_LINE = 0,
    W32_DRAW_RECT,
    W32_DRAW_OVAL,
    W32_DRAW_TEXT
};

typedef struct
{
    int type;
    int x1, y1, x2, y2;
    char text[128];
} w32_draw_cmd_t;

#define W32_MAX_CANVAS 32
#define W32_MAX_DRAW_CMDS 512

typedef struct
{
    HWND hwnd;
    w32_draw_cmd_t cmds[W32_MAX_DRAW_CMDS];
    int cmd_count;
} w32_canvas_t;

static w32_canvas_t g_w32_canvas[W32_MAX_CANVAS];
static int g_w32_canvas_count = 0;

static w32_canvas_t *w32_canvas_for(HWND hwnd)
{
    for (int i = 0; i < g_w32_canvas_count; i++)
        if (g_w32_canvas[i].hwnd == hwnd)
            return &g_w32_canvas[i];
    return NULL;
}

static void *win32_canvas_create(void *parent, int width, int height)
{
    if (!parent)
        return NULL;
    HWND hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, width,
                                height, (HWND)parent, (HMENU)(intptr_t)g_next_ctrl_id++, g_hinstance, NULL);
    if (hwnd)
    {
        register_widget_parent(hwnd, (HWND)parent);
        if (g_w32_canvas_count < W32_MAX_CANVAS)
        {
            w32_canvas_t *c = &g_w32_canvas[g_w32_canvas_count++];
            memset(c, 0, sizeof(*c));
            c->hwnd = hwnd;
        }
    }
    return (void *)hwnd;
}

static void win32_canvas_destroy(void *handle)
{
    if (handle)
        DestroyWindow((HWND)handle);
}

static void win32_canvas_clear(void *handle)
{
    w32_canvas_t *c = w32_canvas_for((HWND)handle);
    if (c)
    {
        c->cmd_count = 0;
        InvalidateRect((HWND)handle, NULL, TRUE);
    }
}

static void win32_canvas_draw_line(void *h, double x1, double y1, double x2, double y2)
{
    w32_canvas_t *c = w32_canvas_for((HWND)h);
    if (c && c->cmd_count < W32_MAX_DRAW_CMDS)
    {
        c->cmds[c->cmd_count++] = (w32_draw_cmd_t){W32_DRAW_LINE, (int)x1, (int)y1, (int)x2, (int)y2, {0}};
        InvalidateRect((HWND)h, NULL, FALSE);
    }
}

static void win32_canvas_draw_rect(void *h, double x, double y, double w, double ht)
{
    w32_canvas_t *c = w32_canvas_for((HWND)h);
    if (c && c->cmd_count < W32_MAX_DRAW_CMDS)
    {
        c->cmds[c->cmd_count++] = (w32_draw_cmd_t){W32_DRAW_RECT, (int)x, (int)y, (int)w, (int)ht, {0}};
        InvalidateRect((HWND)h, NULL, FALSE);
    }
}

static void win32_canvas_draw_oval(void *h, double x, double y, double w, double ht)
{
    w32_canvas_t *c = w32_canvas_for((HWND)h);
    if (c && c->cmd_count < W32_MAX_DRAW_CMDS)
    {
        c->cmds[c->cmd_count++] = (w32_draw_cmd_t){W32_DRAW_OVAL, (int)x, (int)y, (int)w, (int)ht, {0}};
        InvalidateRect((HWND)h, NULL, FALSE);
    }
}

static void win32_canvas_draw_text(void *h, double x, double y, const char *t)
{
    w32_canvas_t *c = w32_canvas_for((HWND)h);
    if (c && c->cmd_count < W32_MAX_DRAW_CMDS)
    {
        w32_draw_cmd_t cmd = {W32_DRAW_TEXT, (int)x, (int)y, 0, 0, {0}};
        size_t len = strlen(t);
        if (len >= sizeof(cmd.text))
            len = sizeof(cmd.text) - 1;
        memcpy(cmd.text, t, len);
        cmd.text[len] = '\0';
        c->cmds[c->cmd_count++] = cmd;
        InvalidateRect((HWND)h, NULL, FALSE);
    }
}

/* ── Grid layout ─────────────────────────────────────────────────── */

static void win32_widget_grid(void *handle, int col, int row)
{
    HWND parent = get_widget_parent((HWND)handle);
    if (parent)
        grid_add_child(parent, (HWND)handle, col, row);
}

static void win32_widget_grid_span(void *handle, int col, int row, int colspan, int rowspan)
{
    /* Win32 grid engine doesn't support span yet; place at col/row. */
    (void)colspan;
    (void)rowspan;
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

static const char *win32_open_file(void *parent, const char *title, char *buf, size_t bufsz)
{
    (void)title;
    buf[0] = '\0';
    OPENFILENAMEW ofn;
    wchar_t wbuf[MAX_PATH] = {0};
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)parent;
    ofn.lpstrFile = wbuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn))
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, (int)bufsz, NULL, NULL);
    return buf;
}

static const char *win32_save_file(void *parent, const char *title, char *buf, size_t bufsz)
{
    (void)title;
    buf[0] = '\0';
    OPENFILENAMEW ofn;
    wchar_t wbuf[MAX_PATH] = {0};
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)parent;
    ofn.lpstrFile = wbuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn))
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, (int)bufsz, NULL, NULL);
    return buf;
}

static const char *win32_choose_dir(void *parent, const char *title, char *buf, size_t bufsz)
{
    (void)parent;
    (void)title;
    buf[0] = '\0';
    BROWSEINFOW bi;
    wchar_t wpath[MAX_PATH] = {0};
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = (HWND)parent;
    bi.pszDisplayName = wpath;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl)
    {
        if (SHGetPathFromIDListW(pidl, wpath))
            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, buf, (int)bufsz, NULL, NULL);
        CoTaskMemFree(pidl);
    }
    return buf;
}

static bool win32_ask_yes_no(void *parent, const char *title, const char *message)
{
    wchar_t title_buf[256];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, title_buf, (int)(sizeof(title_buf) / sizeof(title_buf[0])));
    int result = MessageBoxW((HWND)parent, to_wide(message), title_buf, MB_YESNO | MB_ICONQUESTION);
    return result == IDYES;
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
    .slider_create = win32_slider_create,
    .slider_destroy = win32_slider_destroy,
    .slider_get_value = win32_slider_get_value,
    .slider_set_value = win32_slider_set_value,
    .select_create = win32_select_create,
    .select_destroy = win32_select_destroy,
    .select_add_item = win32_select_add_item,
    .select_get_index = win32_select_get_index,
    .select_set_index = win32_select_set_index,
    .text_create = win32_text_create,
    .text_destroy = win32_text_destroy,
    .text_get_text = win32_text_get_text,
    .text_set_text = win32_text_set_text,
    .radio_create = win32_radio_create,
    .radio_destroy = win32_radio_destroy,
    .radio_set_text = win32_radio_set_text,
    .radio_get_active = win32_radio_get_active,
    .radio_set_active = win32_radio_set_active,
    .spinbox_create = win32_spinbox_create,
    .spinbox_destroy = win32_spinbox_destroy,
    .spinbox_get_value = win32_spinbox_get_value,
    .spinbox_set_value = win32_spinbox_set_value,
    .frame_create = win32_frame_create,
    .frame_destroy = win32_frame_destroy,
    .frame_set_label = win32_frame_set_label,
    .listbox_create = win32_listbox_create,
    .listbox_destroy = win32_listbox_destroy,
    .listbox_add_item = win32_listbox_add_item,
    .listbox_get_selected = win32_listbox_get_selected,
    .listbox_set_selected = win32_listbox_set_selected,
    .menubar_create = win32_menubar_create,
    .menubar_destroy = win32_menubar_destroy,
    .menu_add_submenu = win32_menu_add_submenu,
    .menu_add_item = win32_menu_add_item,
    .paned_create = win32_paned_create,
    .paned_destroy = win32_paned_destroy,
    .paned_set_start = win32_paned_set_start,
    .paned_set_end = win32_paned_set_end,
    .paned_set_position = win32_paned_set_position,
    .canvas_create = win32_canvas_create,
    .canvas_destroy = win32_canvas_destroy,
    .canvas_clear = win32_canvas_clear,
    .canvas_draw_line = win32_canvas_draw_line,
    .canvas_draw_rect = win32_canvas_draw_rect,
    .canvas_draw_oval = win32_canvas_draw_oval,
    .canvas_draw_text = win32_canvas_draw_text,
    .widget_grid = win32_widget_grid,
    .widget_grid_span = win32_widget_grid_span,
    .widget_grid_remove = win32_widget_grid_remove,
    .container_grid_columnconfigure = win32_container_grid_columnconfigure,
    .container_grid_rowconfigure = win32_container_grid_rowconfigure,
    .set_callback = win32_set_callback,
    .message_box = win32_message_box,
    .open_file_dialog = win32_open_file,
    .save_file_dialog = win32_save_file,
    .choose_directory = win32_choose_dir,
    .ask_yes_no = win32_ask_yes_no,
};

#endif /* _WIN32 */
