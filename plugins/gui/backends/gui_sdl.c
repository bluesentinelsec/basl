/* gui_sdl.c — SDL3 fallback backend for the gui plugin.
 *
 * Renders all widgets using SDL3's 2D renderer.  This backend serves
 * as a fallback when no native toolkit (GTK, Cocoa, Win32) is
 * available — for example on embedded Linux with only a framebuffer,
 * Raspberry Pi without a desktop environment, or WebAssembly.
 *
 * Architecture
 * ────────────
 * All widgets are stored in a flat array of sdl_widget_t structs.
 * Each widget has a type, bounding rectangle, text, state, parent,
 * and optional callback.  The main loop renders all widgets each
 * frame and dispatches SDL events (mouse clicks, text input) to
 * the appropriate widget callbacks.
 *
 * Text is rendered using vigil_font.h (stb_truetype) which produces
 * grayscale bitmaps that are uploaded as SDL textures.
 */
#ifdef VIGIL_GUI_SDL_BACKEND

#include "../gui_backend.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ── Widget types ────────────────────────────────────────────────── */

enum
{
    SW_WINDOW = 0,
    SW_LABEL,
    SW_BUTTON,
    SW_ENTRY,
    SW_CHECKBOX,
    SW_SLIDER,
    SW_SELECT,
    SW_TEXT,
    SW_RADIO,
    SW_SPINBOX,
    SW_FRAME,
    SW_LISTBOX,
    SW_CANVAS,
};

/* ── Widget struct ───────────────────────────────────────────────── */

#define SW_MAX_TEXT 256
#define SW_MAX_ITEMS 64
#define SW_MAX_WIDGETS 512

typedef struct
{
    int type;
    int parent_id; /* -1 for windows */
    SDL_FRect bounds;
    char text[SW_MAX_TEXT];
    bool active;  /* checkbox/radio checked state */
    double value; /* slider/spinbox value */
    double min_val, max_val, step;
    int selected;                          /* select/listbox selected index */
    char items[SW_MAX_ITEMS][SW_MAX_TEXT]; /* select/listbox items */
    int item_count;
    int grid_col, grid_row;
    gui_callback_t on_click;
    gui_callback_t on_change;
    bool has_focus;
} sdl_widget_t;

static sdl_widget_t g_widgets[SW_MAX_WIDGETS];
static int g_widget_count = 0;

/* ── SDL state ───────────────────────────────────────────────────── */

static SDL_Window *g_sdl_window = NULL;
static SDL_Renderer *g_sdl_renderer = NULL;
static bool g_sdl_running = false;
static int g_focused_widget = -1;
static int g_win_w = 640, g_win_h = 480;

/* ── Colors ──────────────────────────────────────────────────────── */

#define COL_BG_R 240
#define COL_BG_G 240
#define COL_BG_B 240
#define COL_FG_R 30
#define COL_FG_G 30
#define COL_FG_B 30
#define COL_BTN_R 200
#define COL_BTN_G 200
#define COL_BTN_B 200
#define COL_ACCENT_R 60
#define COL_ACCENT_G 120
#define COL_ACCENT_B 200
#define COL_WHITE_R 255
#define COL_WHITE_G 255
#define COL_WHITE_B 255
#define COL_BORDER_R 160
#define COL_BORDER_G 160
#define COL_BORDER_B 160

/* ── Widget helpers ──────────────────────────────────────────────── */

static int sw_alloc(int type, int parent_id)
{
    if (g_widget_count >= SW_MAX_WIDGETS)
        return -1;
    int id = g_widget_count++;
    memset(&g_widgets[id], 0, sizeof(sdl_widget_t));
    g_widgets[id].type = type;
    g_widgets[id].parent_id = parent_id;
    g_widgets[id].selected = -1;
    return id;
}

static sdl_widget_t *sw_get(void *handle)
{
    int id = (int)(intptr_t)handle;
    if (id < 0 || id >= g_widget_count)
        return NULL;
    return &g_widgets[id];
}

static void *sw_handle(int id)
{
    return (void *)(intptr_t)id;
}

/* ── Simple text rendering (SDL_RenderDebugText) ─────────────────── */

static void draw_text(float x, float y, const char *text)
{
    /* SDL3 has SDL_RenderDebugText for basic 8x8 text. */
    SDL_SetRenderDrawColor(g_sdl_renderer, COL_FG_R, COL_FG_G, COL_FG_B, 255);
    SDL_RenderDebugText(g_sdl_renderer, x, y, text);
}

static void draw_rect_fill(SDL_FRect *r, int cr, int cg, int cb)
{
    SDL_SetRenderDrawColor(g_sdl_renderer, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);
    SDL_RenderFillRect(g_sdl_renderer, r);
}

static void draw_rect_outline(SDL_FRect *r, int cr, int cg, int cb)
{
    SDL_SetRenderDrawColor(g_sdl_renderer, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);
    SDL_RenderRect(g_sdl_renderer, r);
}

/* ── Grid layout ─────────────────────────────────────────────────── */

static void layout_children(int parent_id)
{
    sdl_widget_t *pw = sw_get(sw_handle(parent_id));
    if (!pw)
        return;
    float ox = pw->bounds.x + 4;
    float oy = pw->bounds.y + 4;
    float cw = pw->bounds.w - 8;
    /* Simple vertical stack layout based on grid row. */
    int max_row = 0;
    for (int i = 0; i < g_widget_count; i++)
        if (g_widgets[i].parent_id == parent_id && g_widgets[i].grid_row > max_row)
            max_row = g_widgets[i].grid_row;
    float row_h = (max_row > 0) ? (pw->bounds.h - 8) / (float)(max_row + 1) : (pw->bounds.h - 8);
    for (int i = 0; i < g_widget_count; i++)
    {
        if (g_widgets[i].parent_id != parent_id)
            continue;
        g_widgets[i].bounds.x = ox;
        g_widgets[i].bounds.y = oy + g_widgets[i].grid_row * row_h;
        g_widgets[i].bounds.w = cw;
        g_widgets[i].bounds.h = row_h - 2;
    }
}

/* ── Widget rendering ────────────────────────────────────────────── */

static void render_widget(sdl_widget_t *w)
{
    SDL_FRect *r = &w->bounds;
    switch (w->type)
    {
    case SW_LABEL:
        draw_text(r->x + 4, r->y + (r->h - 8) / 2, w->text);
        break;
    case SW_BUTTON:
        draw_rect_fill(r, COL_BTN_R, COL_BTN_G, COL_BTN_B);
        draw_rect_outline(r, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        draw_text(r->x + 8, r->y + (r->h - 8) / 2, w->text);
        break;
    case SW_ENTRY: {
        SDL_FRect bg = *r;
        draw_rect_fill(&bg, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B);
        int is_focused = (w == &g_widgets[g_focused_widget]);
        draw_rect_outline(&bg, is_focused ? COL_ACCENT_R : COL_BORDER_R, is_focused ? COL_ACCENT_G : COL_BORDER_G,
                          is_focused ? COL_ACCENT_B : COL_BORDER_B);
        draw_text(r->x + 4, r->y + (r->h - 8) / 2, w->text);
        if (is_focused)
        {
            float cx = r->x + 4 + (float)strlen(w->text) * 8;
            SDL_SetRenderDrawColor(g_sdl_renderer, COL_FG_R, COL_FG_G, COL_FG_B, 255);
            SDL_RenderLine(g_sdl_renderer, cx, r->y + 4, cx, r->y + r->h - 4);
        }
        break;
    }
    case SW_CHECKBOX: {
        SDL_FRect box = {r->x + 2, r->y + (r->h - 14) / 2, 14, 14};
        draw_rect_fill(&box, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B);
        draw_rect_outline(&box, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        if (w->active)
            draw_text(box.x + 3, box.y + 3, "X");
        draw_text(r->x + 22, r->y + (r->h - 8) / 2, w->text);
        break;
    }
    case SW_RADIO: {
        SDL_FRect box = {r->x + 2, r->y + (r->h - 14) / 2, 14, 14};
        draw_rect_outline(&box, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        if (w->active)
        {
            SDL_FRect inner = {box.x + 3, box.y + 3, 8, 8};
            draw_rect_fill(&inner, COL_FG_R, COL_FG_G, COL_FG_B);
        }
        draw_text(r->x + 22, r->y + (r->h - 8) / 2, w->text);
        break;
    }
    case SW_SLIDER: {
        float track_y = r->y + r->h / 2 - 2;
        SDL_FRect track = {r->x, track_y, r->w, 4};
        draw_rect_fill(&track, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        float range = (float)(w->max_val - w->min_val);
        float frac = (range > 0) ? (float)((w->value - w->min_val) / range) : 0;
        float knob_x = r->x + frac * (r->w - 12);
        SDL_FRect knob = {knob_x, r->y + r->h / 2 - 8, 12, 16};
        draw_rect_fill(&knob, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B);
        break;
    }
    case SW_SELECT:
        draw_rect_fill(r, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B);
        draw_rect_outline(r, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        if (w->selected >= 0 && w->selected < w->item_count)
            draw_text(r->x + 4, r->y + (r->h - 8) / 2, w->items[w->selected]);
        else
            draw_text(r->x + 4, r->y + (r->h - 8) / 2, "(none)");
        break;
    case SW_TEXT:
        draw_rect_fill(r, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B);
        draw_rect_outline(r, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        draw_text(r->x + 4, r->y + 4, w->text);
        break;
    case SW_SPINBOX: {
        SDL_FRect box = {r->x, r->y, r->w - 24, r->h};
        draw_rect_fill(&box, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B);
        draw_rect_outline(&box, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        char vbuf[32];
        snprintf(vbuf, sizeof(vbuf), "%.1f", w->value);
        draw_text(r->x + 4, r->y + (r->h - 8) / 2, vbuf);
        SDL_FRect up = {r->x + r->w - 22, r->y, 22, r->h / 2};
        SDL_FRect dn = {r->x + r->w - 22, r->y + r->h / 2, 22, r->h / 2};
        draw_rect_fill(&up, COL_BTN_R, COL_BTN_G, COL_BTN_B);
        draw_rect_fill(&dn, COL_BTN_R, COL_BTN_G, COL_BTN_B);
        draw_text(up.x + 6, up.y + 2, "+");
        draw_text(dn.x + 6, dn.y + 2, "-");
        break;
    }
    case SW_FRAME:
        draw_rect_outline(r, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        draw_text(r->x + 8, r->y + 2, w->text);
        break;
    case SW_LISTBOX: {
        draw_rect_fill(r, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B);
        draw_rect_outline(r, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        for (int i = 0; i < w->item_count && i < 10; i++)
        {
            float iy = r->y + 2 + i * 12;
            if (i == w->selected)
            {
                SDL_FRect sel = {r->x + 1, iy, r->w - 2, 12};
                draw_rect_fill(&sel, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B);
            }
            draw_text(r->x + 4, iy + 2, w->items[i]);
        }
        break;
    }
    case SW_CANVAS:
        draw_rect_fill(r, COL_WHITE_R, COL_WHITE_G, COL_WHITE_B);
        draw_rect_outline(r, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
        break;
    default:
        break;
    }
}

/* ── Event handling ──────────────────────────────────────────────── */

static bool point_in_rect(float px, float py, SDL_FRect *r)
{
    return px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h;
}

static void handle_mouse_click(float mx, float my)
{
    for (int i = g_widget_count - 1; i >= 0; i--)
    {
        sdl_widget_t *w = &g_widgets[i];
        if (!point_in_rect(mx, my, &w->bounds))
            continue;
        switch (w->type)
        {
        case SW_BUTTON:
            if (w->on_click.fn)
                w->on_click.fn(w->on_click.user_data);
            return;
        case SW_CHECKBOX:
            w->active = !w->active;
            if (w->on_change.fn)
                w->on_change.fn(w->on_change.user_data);
            return;
        case SW_RADIO:
            w->active = true;
            if (w->on_change.fn)
                w->on_change.fn(w->on_change.user_data);
            return;
        case SW_SLIDER: {
            float frac = (mx - w->bounds.x) / w->bounds.w;
            if (frac < 0)
                frac = 0;
            if (frac > 1)
                frac = 1;
            w->value = w->min_val + frac * (w->max_val - w->min_val);
            if (w->on_change.fn)
                w->on_change.fn(w->on_change.user_data);
            return;
        }
        case SW_SELECT:
            w->selected = (w->selected + 1) % (w->item_count > 0 ? w->item_count : 1);
            if (w->on_change.fn)
                w->on_change.fn(w->on_change.user_data);
            return;
        case SW_LISTBOX: {
            int row = (int)((my - w->bounds.y) / 12);
            if (row >= 0 && row < w->item_count)
            {
                w->selected = row;
                if (w->on_change.fn)
                    w->on_change.fn(w->on_change.user_data);
            }
            return;
        }
        case SW_SPINBOX: {
            float btn_x = w->bounds.x + w->bounds.w - 22;
            if (mx >= btn_x)
            {
                if (my < w->bounds.y + w->bounds.h / 2)
                    w->value += w->step;
                else
                    w->value -= w->step;
                if (w->value < w->min_val)
                    w->value = w->min_val;
                if (w->value > w->max_val)
                    w->value = w->max_val;
                if (w->on_change.fn)
                    w->on_change.fn(w->on_change.user_data);
            }
            return;
        }
        default:
            break;
        }
    }
}

static void handle_focus(float mx, float my)
{
    g_focused_widget = -1;
    for (int i = g_widget_count - 1; i >= 0; i--)
    {
        if ((g_widgets[i].type == SW_ENTRY || g_widgets[i].type == SW_TEXT) &&
            point_in_rect(mx, my, &g_widgets[i].bounds))
        {
            g_focused_widget = i;
            SDL_StartTextInput(g_sdl_window);
            return;
        }
    }
    SDL_StopTextInput(g_sdl_window);
}

/* Forward declarations for menu rendering (defined in Menu section). */
static void sdl_render_menubar(void);
static void sdl_handle_menubar_click(float mx, float my);

static bool sdl_init(const char *app_name)
{
    (void)app_name;
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "gui_sdl: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    g_widget_count = 0;
    g_sdl_running = false;
    fprintf(stderr, "gui_sdl: using SDL3 fallback renderer\n");
    return true;
}

static void sdl_shutdown(void)
{
    if (g_sdl_renderer)
    {
        SDL_DestroyRenderer(g_sdl_renderer);
        g_sdl_renderer = NULL;
    }
    if (g_sdl_window)
    {
        SDL_DestroyWindow(g_sdl_window);
        g_sdl_window = NULL;
    }
    g_widget_count = 0;
    g_sdl_running = false;
    SDL_Quit();
}

static void sdl_main_loop(void)
{
    if (!g_sdl_window || !g_sdl_renderer)
        return;
    /* Layout all top-level windows' children. */
    for (int i = 0; i < g_widget_count; i++)
        if (g_widgets[i].type == SW_WINDOW)
            layout_children(i);

    g_sdl_running = true;
    while (g_sdl_running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            switch (ev.type)
            {
            case SDL_EVENT_QUIT:
                g_sdl_running = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_LEFT)
                {
                    handle_focus(ev.button.x, ev.button.y);
                    sdl_handle_menubar_click(ev.button.x, ev.button.y);
                    handle_mouse_click(ev.button.x, ev.button.y);
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (g_focused_widget >= 0)
                {
                    sdl_widget_t *fw = &g_widgets[g_focused_widget];
                    size_t len = strlen(fw->text);
                    size_t ilen = strlen(ev.text.text);
                    if (len + ilen < SW_MAX_TEXT - 1)
                    {
                        memcpy(fw->text + len, ev.text.text, ilen);
                        fw->text[len + ilen] = '\0';
                        if (fw->on_change.fn)
                            fw->on_change.fn(fw->on_change.user_data);
                    }
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (g_focused_widget >= 0 && ev.key.key == SDLK_BACKSPACE)
                {
                    sdl_widget_t *fw = &g_widgets[g_focused_widget];
                    size_t len = strlen(fw->text);
                    if (len > 0)
                    {
                        fw->text[len - 1] = '\0';
                        if (fw->on_change.fn)
                            fw->on_change.fn(fw->on_change.user_data);
                    }
                }
                break;
            default:
                break;
            }
        }
        /* Render. */
        SDL_SetRenderDrawColor(g_sdl_renderer, COL_BG_R, COL_BG_G, COL_BG_B, 255);
        SDL_RenderClear(g_sdl_renderer);
        for (int i = 0; i < g_widget_count; i++)
            if (g_widgets[i].type != SW_WINDOW)
                render_widget(&g_widgets[i]);
        sdl_render_menubar();
        SDL_RenderPresent(g_sdl_renderer);
        SDL_Delay(16); /* ~60 fps */
    }
}

static void sdl_quit(void)
{
    g_sdl_running = false;
}

/* ── Window ──────────────────────────────────────────────────────── */

static void *sdl_window_create(const char *title, int w, int h)
{
    g_win_w = w;
    g_win_h = h;
    g_sdl_window = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
    if (!g_sdl_window)
        return NULL;
    g_sdl_renderer = SDL_CreateRenderer(g_sdl_window, NULL);
    if (!g_sdl_renderer)
    {
        SDL_DestroyWindow(g_sdl_window);
        g_sdl_window = NULL;
        return NULL;
    }
    int id = sw_alloc(SW_WINDOW, -1);
    g_widgets[id].bounds = (SDL_FRect){0, 0, (float)w, (float)h};
    snprintf(g_widgets[id].text, SW_MAX_TEXT, "%s", title);
    return sw_handle(id);
}

static void sdl_window_destroy(void *handle)
{
    (void)handle;
}
static void sdl_window_set_title(void *handle, const char *title)
{
    (void)handle;
    if (g_sdl_window)
        SDL_SetWindowTitle(g_sdl_window, title);
}
static void sdl_window_get_size(void *handle, int *w, int *h)
{
    (void)handle;
    *w = g_win_w;
    *h = g_win_h;
}

/* ── Label ───────────────────────────────────────────────────────── */

static void *sdl_label_create(void *parent, const char *text)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_LABEL, pid);
    if (id < 0)
        return NULL;
    snprintf(g_widgets[id].text, SW_MAX_TEXT, "%s", text);
    return sw_handle(id);
}
static void sdl_label_destroy(void *h)
{
    (void)h;
}
static void sdl_label_set_text(void *h, const char *t)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        snprintf(w->text, SW_MAX_TEXT, "%s", t);
}

/* ── Button ──────────────────────────────────────────────────────── */

static void *sdl_button_create(void *parent, const char *text)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_BUTTON, pid);
    if (id < 0)
        return NULL;
    snprintf(g_widgets[id].text, SW_MAX_TEXT, "%s", text);
    return sw_handle(id);
}
static void sdl_button_destroy(void *h)
{
    (void)h;
}
static void sdl_button_set_text(void *h, const char *t)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        snprintf(w->text, SW_MAX_TEXT, "%s", t);
}

/* ── Entry ───────────────────────────────────────────────────────── */

static void *sdl_entry_create(void *parent)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_ENTRY, pid);
    if (id < 0)
        return NULL;
    return sw_handle(id);
}
static void sdl_entry_destroy(void *h)
{
    (void)h;
}
static const char *sdl_entry_get_text(void *h, char *buf, size_t bufsz)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
    {
        size_t len = strlen(w->text);
        if (len >= bufsz)
            len = bufsz - 1;
        memcpy(buf, w->text, len);
        buf[len] = '\0';
    }
    else
        buf[0] = '\0';
    return buf;
}
static void sdl_entry_set_text(void *h, const char *t)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        snprintf(w->text, SW_MAX_TEXT, "%s", t);
}

/* ── Checkbox ────────────────────────────────────────────────────── */

static void *sdl_checkbox_create(void *parent, const char *text)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_CHECKBOX, pid);
    if (id < 0)
        return NULL;
    snprintf(g_widgets[id].text, SW_MAX_TEXT, "%s", text);
    return sw_handle(id);
}
static void sdl_checkbox_destroy(void *h)
{
    (void)h;
}
static void sdl_checkbox_set_text(void *h, const char *t)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        snprintf(w->text, SW_MAX_TEXT, "%s", t);
}
static bool sdl_checkbox_get(void *h)
{
    sdl_widget_t *w = sw_get(h);
    return w ? w->active : false;
}
static void sdl_checkbox_set(void *h, bool v)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        w->active = v;
}

/* ── Slider ──────────────────────────────────────────────────────── */

static void *sdl_slider_create(void *parent, double mn, double mx)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_SLIDER, pid);
    if (id < 0)
        return NULL;
    g_widgets[id].min_val = mn;
    g_widgets[id].max_val = mx;
    g_widgets[id].value = mn;
    return sw_handle(id);
}
static void sdl_slider_destroy(void *h)
{
    (void)h;
}
static double sdl_slider_get(void *h)
{
    sdl_widget_t *w = sw_get(h);
    return w ? w->value : 0;
}
static void sdl_slider_set(void *h, double v)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        w->value = v;
}

/* ── Select ──────────────────────────────────────────────────────── */

static void *sdl_select_create(void *parent)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_SELECT, pid);
    if (id < 0)
        return NULL;
    return sw_handle(id);
}
static void sdl_select_destroy(void *h)
{
    (void)h;
}
static void sdl_select_add(void *h, const char *t)
{
    sdl_widget_t *w = sw_get(h);
    if (w && w->item_count < SW_MAX_ITEMS)
        snprintf(w->items[w->item_count++], SW_MAX_TEXT, "%s", t);
}
static int sdl_select_get(void *h)
{
    sdl_widget_t *w = sw_get(h);
    return w ? w->selected : -1;
}
static void sdl_select_set(void *h, int i)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        w->selected = i;
}

/* ── Text ────────────────────────────────────────────────────────── */

static void *sdl_text_create(void *parent)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_TEXT, pid);
    if (id < 0)
        return NULL;
    return sw_handle(id);
}
static void sdl_text_destroy(void *h)
{
    (void)h;
}
static const char *sdl_text_get(void *h, char *buf, size_t bufsz)
{
    return sdl_entry_get_text(h, buf, bufsz);
}
static void sdl_text_set(void *h, const char *t)
{
    sdl_entry_set_text(h, t);
}

/* ── Radio ───────────────────────────────────────────────────────── */

static void *sdl_radio_create(void *parent, const char *text, void *group)
{
    (void)group;
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_RADIO, pid);
    if (id < 0)
        return NULL;
    snprintf(g_widgets[id].text, SW_MAX_TEXT, "%s", text);
    return sw_handle(id);
}
static void sdl_radio_destroy(void *h)
{
    (void)h;
}
static void sdl_radio_set_text(void *h, const char *t)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        snprintf(w->text, SW_MAX_TEXT, "%s", t);
}
static bool sdl_radio_get(void *h)
{
    sdl_widget_t *w = sw_get(h);
    return w ? w->active : false;
}
static void sdl_radio_set(void *h, bool v)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        w->active = v;
}

/* ── Spinbox ─────────────────────────────────────────────────────── */

static void *sdl_spinbox_create(void *parent, double mn, double mx, double step)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_SPINBOX, pid);
    if (id < 0)
        return NULL;
    g_widgets[id].min_val = mn;
    g_widgets[id].max_val = mx;
    g_widgets[id].step = step;
    g_widgets[id].value = mn;
    return sw_handle(id);
}
static void sdl_spinbox_destroy(void *h)
{
    (void)h;
}
static double sdl_spinbox_get(void *h)
{
    sdl_widget_t *w = sw_get(h);
    return w ? w->value : 0;
}
static void sdl_spinbox_set(void *h, double v)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        w->value = v;
}

/* ── Frame ───────────────────────────────────────────────────────── */

static void *sdl_frame_create(void *parent, const char *label)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_FRAME, pid);
    if (id < 0)
        return NULL;
    snprintf(g_widgets[id].text, SW_MAX_TEXT, "%s", label);
    return sw_handle(id);
}
static void sdl_frame_destroy(void *h)
{
    (void)h;
}
static void sdl_frame_set_label(void *h, const char *t)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
        snprintf(w->text, SW_MAX_TEXT, "%s", t);
}

/* ── Listbox ─────────────────────────────────────────────────────── */

static void *sdl_listbox_create(void *parent)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_LISTBOX, pid);
    if (id < 0)
        return NULL;
    return sw_handle(id);
}
static void sdl_listbox_destroy(void *h)
{
    (void)h;
}
static void sdl_listbox_add(void *h, const char *t)
{
    sdl_select_add(h, t);
}
static int sdl_listbox_get(void *h)
{
    return sdl_select_get(h);
}
static void sdl_listbox_set(void *h, int i)
{
    sdl_select_set(h, i);
}

/* ── Menu (flat bar with clickable items) ─────────────────────────── */

#define SDL_MAX_MENU_ITEMS 64

typedef struct
{
    char label[64];
    gui_callback_t cb;
    float x, w;
} sdl_menu_item_t;

static sdl_menu_item_t g_sdl_menu_items[SDL_MAX_MENU_ITEMS];
static int g_sdl_menu_item_count = 0;
static bool g_sdl_has_menubar = false;

static void sdl_render_menubar(void)
{
    if (!g_sdl_has_menubar)
        return;
    SDL_FRect bar = {0, 0, (float)g_win_w, 20};
    draw_rect_fill(&bar, COL_BTN_R, COL_BTN_G, COL_BTN_B);
    for (int i = 0; i < g_sdl_menu_item_count; i++)
        draw_text(g_sdl_menu_items[i].x + 4, 6, g_sdl_menu_items[i].label);
}

static void sdl_handle_menubar_click(float mx, float my)
{
    if (!g_sdl_has_menubar || my > 20)
        return;
    for (int i = 0; i < g_sdl_menu_item_count; i++)
    {
        if (mx >= g_sdl_menu_items[i].x && mx < g_sdl_menu_items[i].x + g_sdl_menu_items[i].w)
        {
            if (g_sdl_menu_items[i].cb.fn)
                g_sdl_menu_items[i].cb.fn(g_sdl_menu_items[i].cb.user_data);
            return;
        }
    }
}

static void *sdl_menubar_create(void *win)
{
    (void)win;
    g_sdl_has_menubar = true;
    g_sdl_menu_item_count = 0;
    return sw_handle(0);
}

static void sdl_menubar_destroy(void *h)
{
    (void)h;
    g_sdl_has_menubar = false;
}

static void *sdl_menu_add_submenu(void *mb, const char *l)
{
    (void)mb;
    (void)l;
    return sw_handle(0);
}

static void sdl_menu_add_item(void *sm, const char *l, gui_callback_t cb)
{
    (void)sm;
    if (g_sdl_menu_item_count >= SDL_MAX_MENU_ITEMS)
        return;
    sdl_menu_item_t *item = &g_sdl_menu_items[g_sdl_menu_item_count];
    snprintf(item->label, sizeof(item->label), "%s", l);
    item->cb = cb;
    float x = 4;
    for (int i = 0; i < g_sdl_menu_item_count; i++)
        x += g_sdl_menu_items[i].w;
    item->x = x;
    item->w = (float)(strlen(l) * 8 + 16);
    g_sdl_menu_item_count++;
}

/* ── PanedWindow ─────────────────────────────────────────────────── */

static void *sdl_paned_create(void *parent, bool horiz)
{
    (void)horiz;
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_FRAME, pid);
    if (id < 0)
        return NULL;
    return sw_handle(id);
}
static void sdl_paned_destroy(void *h)
{
    (void)h;
}
static void sdl_paned_set_start(void *h, void *c)
{
    (void)h;
    (void)c;
}
static void sdl_paned_set_end(void *h, void *c)
{
    (void)h;
    (void)c;
}
static void sdl_paned_set_pos(void *h, int p)
{
    (void)h;
    (void)p;
}

/* ── Canvas ──────────────────────────────────────────────────────── */

static void *sdl_canvas_create(void *parent, int w, int h)
{
    int pid = (int)(intptr_t)parent;
    int id = sw_alloc(SW_CANVAS, pid);
    if (id < 0)
        return NULL;
    g_widgets[id].bounds.w = (float)w;
    g_widgets[id].bounds.h = (float)h;
    return sw_handle(id);
}
static void sdl_canvas_destroy(void *h)
{
    (void)h;
}
static void sdl_canvas_clear(void *h)
{
    (void)h;
}
static void sdl_canvas_draw_line(void *h, double x1, double y1, double x2, double y2)
{
    sdl_widget_t *w = sw_get(h);
    if (!w || !g_sdl_renderer)
        return;
    SDL_SetRenderDrawColor(g_sdl_renderer, COL_FG_R, COL_FG_G, COL_FG_B, 255);
    SDL_RenderLine(g_sdl_renderer, w->bounds.x + (float)x1, w->bounds.y + (float)y1, w->bounds.x + (float)x2,
                   w->bounds.y + (float)y2);
}
static void sdl_canvas_draw_rect(void *h, double x, double y, double rw, double rh)
{
    sdl_widget_t *w = sw_get(h);
    if (!w || !g_sdl_renderer)
        return;
    SDL_FRect r = {w->bounds.x + (float)x, w->bounds.y + (float)y, (float)rw, (float)rh};
    draw_rect_outline(&r, COL_FG_R, COL_FG_G, COL_FG_B);
}
static void sdl_canvas_draw_oval(void *h, double x, double y, double ow, double oh)
{
    /* Approximate oval with a rect for now. */
    sdl_canvas_draw_rect(h, x, y, ow, oh);
}
static void sdl_canvas_draw_text(void *h, double x, double y, const char *t)
{
    sdl_widget_t *w = sw_get(h);
    if (!w)
        return;
    draw_text(w->bounds.x + (float)x, w->bounds.y + (float)y, t);
}

/* ── Grid / Events / Dialogs ─────────────────────────────────────── */

static void sdl_widget_grid(void *h, int col, int row)
{
    sdl_widget_t *w = sw_get(h);
    if (w)
    {
        w->grid_col = col;
        w->grid_row = row;
    }
}
static void sdl_widget_grid_span(void *h, int col, int row, int cs, int rs)
{
    (void)cs;
    (void)rs;
    sdl_widget_grid(h, col, row);
}
static void sdl_widget_grid_remove(void *h)
{
    (void)h;
}
static void sdl_grid_col_cfg(void *h, int i, int w)
{
    (void)h;
    (void)i;
    (void)w;
}
static void sdl_grid_row_cfg(void *h, int i, int w)
{
    (void)h;
    (void)i;
    (void)w;
}

static void sdl_set_callback(void *widget, gui_event_type_t type, gui_callback_t cb)
{
    sdl_widget_t *w = sw_get(widget);
    if (!w)
        return;
    if (type == GUI_EVENT_CLICK)
        w->on_click = cb;
    else if (type == GUI_EVENT_CHANGE)
        w->on_change = cb;
    else if (type == GUI_EVENT_CLOSE)
        w->on_click = cb; /* reuse for close */
}

static void sdl_message_box(void *parent, const char *title, const char *msg)
{
    (void)parent;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, msg, g_sdl_window);
}

static const char *sdl_stub_file(void *p, const char *t, char *b, size_t s)
{
    (void)p;
    (void)t;
    (void)s;
    b[0] = '\0';
    return b;
}
static bool sdl_stub_yesno(void *p, const char *t, const char *m)
{
    (void)p;
    (void)t;
    (void)m;
    return false;
}

/* ── Export ───────────────────────────────────────────────────────── */

const gui_backend_t gui_backend_sdl = {
    .name = "sdl",
    .init = sdl_init,
    .shutdown = sdl_shutdown,
    .main_loop = sdl_main_loop,
    .quit = sdl_quit,
    .window_create = sdl_window_create,
    .window_destroy = sdl_window_destroy,
    .window_set_title = sdl_window_set_title,
    .window_get_size = sdl_window_get_size,
    .label_create = sdl_label_create,
    .label_destroy = sdl_label_destroy,
    .label_set_text = sdl_label_set_text,
    .button_create = sdl_button_create,
    .button_destroy = sdl_button_destroy,
    .button_set_text = sdl_button_set_text,
    .entry_create = sdl_entry_create,
    .entry_destroy = sdl_entry_destroy,
    .entry_get_text = sdl_entry_get_text,
    .entry_set_text = sdl_entry_set_text,
    .checkbox_create = sdl_checkbox_create,
    .checkbox_destroy = sdl_checkbox_destroy,
    .checkbox_set_text = sdl_checkbox_set_text,
    .checkbox_get_checked = sdl_checkbox_get,
    .checkbox_set_checked = sdl_checkbox_set,
    .slider_create = sdl_slider_create,
    .slider_destroy = sdl_slider_destroy,
    .slider_get_value = sdl_slider_get,
    .slider_set_value = sdl_slider_set,
    .select_create = sdl_select_create,
    .select_destroy = sdl_select_destroy,
    .select_add_item = sdl_select_add,
    .select_get_index = sdl_select_get,
    .select_set_index = sdl_select_set,
    .text_create = sdl_text_create,
    .text_destroy = sdl_text_destroy,
    .text_get_text = sdl_text_get,
    .text_set_text = sdl_text_set,
    .radio_create = sdl_radio_create,
    .radio_destroy = sdl_radio_destroy,
    .radio_set_text = sdl_radio_set_text,
    .radio_get_active = sdl_radio_get,
    .radio_set_active = sdl_radio_set,
    .spinbox_create = sdl_spinbox_create,
    .spinbox_destroy = sdl_spinbox_destroy,
    .spinbox_get_value = sdl_spinbox_get,
    .spinbox_set_value = sdl_spinbox_set,
    .frame_create = sdl_frame_create,
    .frame_destroy = sdl_frame_destroy,
    .frame_set_label = sdl_frame_set_label,
    .listbox_create = sdl_listbox_create,
    .listbox_destroy = sdl_listbox_destroy,
    .listbox_add_item = sdl_listbox_add,
    .listbox_get_selected = sdl_listbox_get,
    .listbox_set_selected = sdl_listbox_set,
    .menubar_create = sdl_menubar_create,
    .menubar_destroy = sdl_menubar_destroy,
    .menu_add_submenu = sdl_menu_add_submenu,
    .menu_add_item = sdl_menu_add_item,
    .paned_create = sdl_paned_create,
    .paned_destroy = sdl_paned_destroy,
    .paned_set_start = sdl_paned_set_start,
    .paned_set_end = sdl_paned_set_end,
    .paned_set_position = sdl_paned_set_pos,
    .canvas_create = sdl_canvas_create,
    .canvas_destroy = sdl_canvas_destroy,
    .canvas_clear = sdl_canvas_clear,
    .canvas_draw_line = sdl_canvas_draw_line,
    .canvas_draw_rect = sdl_canvas_draw_rect,
    .canvas_draw_oval = sdl_canvas_draw_oval,
    .canvas_draw_text = sdl_canvas_draw_text,
    .canvas_set_stroke_color = NULL,
    .canvas_set_fill_color = NULL,
    .canvas_set_line_width = NULL,
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
    .widget_grid = sdl_widget_grid,
    .widget_grid_span = sdl_widget_grid_span,
    .widget_grid_remove = sdl_widget_grid_remove,
    .container_grid_columnconfigure = sdl_grid_col_cfg,
    .container_grid_rowconfigure = sdl_grid_row_cfg,
    .widget_set_font = NULL,
    .widget_set_fg = NULL,
    .widget_set_bg = NULL,
    .widget_set_padding = NULL,
    .widget_set_state = NULL,
    .set_callback = sdl_set_callback,
    .message_box = sdl_message_box,
    .open_file_dialog = sdl_stub_file,
    .save_file_dialog = sdl_stub_file,
    .choose_directory = sdl_stub_file,
    .ask_yes_no = sdl_stub_yesno,
};

#endif /* VIGIL_GUI_SDL_BACKEND */
