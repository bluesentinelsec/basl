/* gui_backend.h — Platform backend interface for the gui plugin.
 *
 * Each platform implements this vtable.  gui.c selects the appropriate
 * backend at runtime via gui_backend_select().  New backends can be
 * added without modifying gui.c (open/closed principle).
 */
#ifndef VIGIL_GUI_BACKEND_H
#define VIGIL_GUI_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Event types ─────────────────────────────────────────────────── */

    typedef enum
    {
        GUI_EVENT_CLICK = 0,
        GUI_EVENT_CLOSE,
        GUI_EVENT_CHANGE,
        GUI_EVENT_MOUSE_MOVE,
        GUI_EVENT_MOUSE_RELEASE,
        GUI_EVENT_SCROLL,
        GUI_EVENT_KEY_PRESS,
        GUI_EVENT_KEY_RELEASE,
        GUI_EVENT_COUNT
    } gui_event_type_t;

    /* ── Callback thunk ──────────────────────────────────────────────── */

    /* The gui.c layer stores Vigil closures and invokes them through this. */
    typedef void (*gui_callback_fn)(void *user_data);

    typedef struct
    {
        gui_callback_fn fn;
        void *user_data;
    } gui_callback_t;

    /* ── Backend vtable ──────────────────────────────────────────────── */

    typedef struct gui_backend
    {
        const char *name;

        /* Lifecycle */
        bool (*init)(const char *app_name);
        void (*shutdown)(void);
        void (*main_loop)(void);
        void (*quit)(void);

        /* Window */
        void *(*window_create)(const char *title, int w, int h);
        void (*window_destroy)(void *handle);
        void (*window_set_title)(void *handle, const char *title);
        void (*window_get_size)(void *handle, int *w, int *h);

        /* Label */
        void *(*label_create)(void *parent, const char *text);
        void (*label_destroy)(void *handle);
        void (*label_set_text)(void *handle, const char *text);

        /* Button */
        void *(*button_create)(void *parent, const char *text);
        void (*button_destroy)(void *handle);
        void (*button_set_text)(void *handle, const char *text);

        /* Entry (single-line text input) */
        void *(*entry_create)(void *parent);
        void (*entry_destroy)(void *handle);
        const char *(*entry_get_text)(void *handle, char *buf, size_t bufsz);
        void (*entry_set_text)(void *handle, const char *text);

        /* Checkbox (toggle) */
        void *(*checkbox_create)(void *parent, const char *text);
        void (*checkbox_destroy)(void *handle);
        void (*checkbox_set_text)(void *handle, const char *text);
        bool (*checkbox_get_checked)(void *handle);
        void (*checkbox_set_checked)(void *handle, bool checked);

        /* Slider (horizontal scale) */
        void *(*slider_create)(void *parent, double min_val, double max_val);
        void (*slider_destroy)(void *handle);
        double (*slider_get_value)(void *handle);
        void (*slider_set_value)(void *handle, double value);

        /* Select (drop-down / combo box) */
        void *(*select_create)(void *parent);
        void (*select_destroy)(void *handle);
        void (*select_add_item)(void *handle, const char *text);
        int (*select_get_index)(void *handle);
        void (*select_set_index)(void *handle, int index);

        /* Text (multi-line text area) */
        void *(*text_create)(void *parent);
        void (*text_destroy)(void *handle);
        const char *(*text_get_text)(void *handle, char *buf, size_t bufsz);
        void (*text_set_text)(void *handle, const char *text);

        /* Radiobutton */
        void *(*radio_create)(void *parent, const char *text, void *group);
        void (*radio_destroy)(void *handle);
        void (*radio_set_text)(void *handle, const char *text);
        bool (*radio_get_active)(void *handle);
        void (*radio_set_active)(void *handle, bool active);

        /* Spinbox (numeric entry with arrows) */
        void *(*spinbox_create)(void *parent, double min_val, double max_val, double step);
        void (*spinbox_destroy)(void *handle);
        double (*spinbox_get_value)(void *handle);
        void (*spinbox_set_value)(void *handle, double value);

        /* Frame (container with optional label) */
        void *(*frame_create)(void *parent, const char *label);
        void (*frame_destroy)(void *handle);
        void (*frame_set_label)(void *handle, const char *label);

        /* Listbox (scrollable list of selectable items) */
        void *(*listbox_create)(void *parent);
        void (*listbox_destroy)(void *handle);
        void (*listbox_add_item)(void *handle, const char *text);
        int (*listbox_get_selected)(void *handle);
        void (*listbox_set_selected)(void *handle, int index);

        /* Menu bar */
        void *(*menubar_create)(void *window);
        void (*menubar_destroy)(void *handle);
        void *(*menu_add_submenu)(void *menubar, const char *label);
        void (*menu_add_item)(void *submenu, const char *label, gui_callback_t cb);

        /* PanedWindow (resizable split container) */
        void *(*paned_create)(void *parent, bool horizontal);
        void (*paned_destroy)(void *handle);
        void (*paned_set_start)(void *handle, void *child);
        void (*paned_set_end)(void *handle, void *child);
        void (*paned_set_position)(void *handle, int pos);

        /* Grid layout */
        void (*widget_grid)(void *handle, int col, int row);
        void (*widget_grid_span)(void *handle, int col, int row, int colspan, int rowspan);
        void (*widget_grid_remove)(void *handle);
        void (*container_grid_columnconfigure)(void *handle, int index, int weight);
        void (*container_grid_rowconfigure)(void *handle, int index, int weight);

        /* Events */
        void (*set_callback)(void *widget, gui_event_type_t type, gui_callback_t cb);

        /* Dialogs */
        void (*message_box)(void *parent, const char *title, const char *message);
        const char *(*open_file_dialog)(void *parent, const char *title, char *buf, size_t bufsz);
        const char *(*save_file_dialog)(void *parent, const char *title, char *buf, size_t bufsz);
        const char *(*choose_directory)(void *parent, const char *title, char *buf, size_t bufsz);
        bool (*ask_yes_no)(void *parent, const char *title, const char *message);

        /* Canvas (drawing surface) */
        void *(*canvas_create)(void *parent, int width, int height);
        void (*canvas_destroy)(void *handle);
        void (*canvas_clear)(void *handle);
        void (*canvas_draw_line)(void *handle, double x1, double y1, double x2, double y2);
        void (*canvas_draw_rect)(void *handle, double x, double y, double w, double h);
        void (*canvas_draw_oval)(void *handle, double x, double y, double w, double h);
        void (*canvas_draw_text)(void *handle, double x, double y, const char *text);
        void (*canvas_set_stroke_color)(void *handle, const char *color);
        void (*canvas_set_fill_color)(void *handle, const char *color);
        void (*canvas_set_line_width)(void *handle, double width);
    } gui_backend_t;

    /* ── Backend selection ───────────────────────────────────────────── */

    /* Returns the best available backend for this platform, or NULL. */
    const gui_backend_t *gui_backend_select(void);

/* Platform backends — defined in backends/ */
#if defined(__APPLE__)
    extern const gui_backend_t gui_backend_cocoa;
#elif defined(_WIN32)
extern const gui_backend_t gui_backend_win32;
#elif defined(__linux__)
extern const gui_backend_t gui_backend_gtk;
#endif

#ifdef VIGIL_GUI_SDL_BACKEND
    extern const gui_backend_t gui_backend_sdl;
#endif

    extern const gui_backend_t gui_backend_stub;

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_GUI_BACKEND_H */
