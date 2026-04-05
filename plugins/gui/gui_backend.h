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

        /* Grid layout */
        void (*widget_grid)(void *handle, int col, int row);
        void (*widget_grid_remove)(void *handle);
        void (*container_grid_columnconfigure)(void *handle, int index, int weight);
        void (*container_grid_rowconfigure)(void *handle, int index, int weight);

        /* Events */
        void (*set_callback)(void *widget, gui_event_type_t type, gui_callback_t cb);

        /* Dialogs */
        void (*message_box)(void *parent, const char *title, const char *message);
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

    extern const gui_backend_t gui_backend_stub;

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_GUI_BACKEND_H */
