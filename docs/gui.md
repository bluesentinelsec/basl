# GUI Plugin

The `gui` module is a cross-platform widget toolkit for building desktop applications in Vigil. It provides Tk-style widgets using native platform backends — Cocoa on macOS, GTK on Linux, Win32 on Windows — with an SDL3 software-rendered fallback for systems without a native toolkit.

## Building

The GUI plugin is off by default. Enable it with:

```bash
cmake -B build -DVIGIL_PLUGIN_GUI=ON
cmake --build build
```

To also enable the SDL fallback backend (recommended):

```bash
cmake -B build -DVIGIL_PLUGIN_GUI=ON -DVIGIL_PLUGIN_SDL=ON
cmake --build build
```

The SDL fallback requires the SDL plugin because it links against the vendored SDL3 static library in `deps/sdl3/`.

## Quick Example

```vigil
import "fmt"
import "gui"

fn main() -> i32 {
    gui.App app, err e = gui.App.new("My App")
    if e != ok {
        fmt.println("gui not available")
        return 1
    }

    gui.Window win, err e2 = gui.Window.new(app, "Hello", 400, 300)
    if e2 != ok { return 1 }

    gui.Label lbl, err e3 = gui.Label.new(win, "Hello from Vigil!")
    if e3 != ok { return 1 }
    lbl.grid(0, 0)

    gui.Button btn, err e4 = gui.Button.new(win, "Click Me")
    if e4 != ok { return 1 }
    btn.grid(0, 1)
    btn.on_click(fn() -> void {
        fmt.println("Clicked!")
    })

    app.main_loop()
    app.destroy()
    return 0
}
```

## Widget Classes

| Class | Description | Key Methods |
|---|---|---|
| `App` | Application root, owns the event loop | `new(name)`, `main_loop()`, `quit()`, `destroy()` |
| `Window` | Top-level window | `new(app, title, w, h)`, `set_title()`, `get_size()`, `grid_columnconfigure()`, `grid_rowconfigure()`, `on_close()` |
| `Label` | Static text | `new(parent, text)`, `set_text()`, `grid()` |
| `Button` | Push button | `new(parent, text)`, `set_text()`, `on_click()`, `grid()` |
| `Entry` | Single-line text input | `new(parent)`, `get()`, `set()`, `on_change()`, `grid()` |
| `Checkbox` | Toggle with label | `new(parent, text)`, `get()`, `set()`, `set_text()`, `on_change()`, `grid()` |
| `Slider` | Horizontal scale | `new(parent, min, max)`, `get()`, `set()`, `on_change()`, `grid()` |
| `Select` | Drop-down combo box | `new(parent)`, `add_item()`, `get()`, `set()`, `on_change()`, `grid()` |
| `Text` | Multi-line text area | `new(parent)`, `get()`, `set()`, `grid()` |
| `Radiobutton` | Mutually exclusive toggle | `new(parent, text, group)`, `get()`, `set()`, `set_text()`, `on_change()`, `grid()` |
| `Spinbox` | Numeric entry with arrows | `new(parent, min, max, step)`, `get()`, `set()`, `on_change()`, `grid()` |
| `Frame` | Labeled container | `new(parent, label)`, `set_label()`, `grid()` |
| `Listbox` | Scrollable selectable list | `new(parent)`, `add_item()`, `get()`, `set()`, `on_change()`, `grid()` |
| `Menu` | Application menu bar | `new(window)`, `add_submenu()`, `add_item()` |
| `PanedWindow` | Resizable split container | `new(parent, horizontal)`, `set_position()`, `grid()` |
| `Canvas` | 2D drawing surface | `new(parent, w, h)`, `draw_line()`, `draw_rect()`, `draw_oval()`, `draw_text()`, `clear()`, `grid()` |

## Module-Level Functions

| Function | Description |
|---|---|
| `gui.message_box(window, title, message)` | Show an informational dialog |
| `gui.open_file(window, title) -> string` | File open dialog, returns path or `""` |
| `gui.save_file(window, title) -> string` | File save dialog |
| `gui.choose_directory(window, title) -> string` | Directory picker |
| `gui.ask_yes_no(window, title, message) -> bool` | Yes/No confirmation dialog |

## Architecture

### Backend Vtable

Every platform backend implements the same C vtable (`gui_backend_t` in `gui_backend.h`). This struct contains one function pointer per widget operation — create, destroy, get/set state, grid placement, event callbacks, and dialogs.

```
gui_backend.h          ← vtable definition (the contract)
gui.c                  ← VM bindings, handle registries, backend selection
backends/
  gui_cocoa.m          ← macOS: Cocoa/AppKit via objc_msgSend
  gui_gtk.c            ← Linux: GTK4 preferred, GTK3 fallback (dlopen)
  gui_win32.c          ← Windows: Win32 API (user32, gdi32, comctl32, comdlg32)
  gui_sdl.c            ← Fallback: software-rendered widgets via SDL3
  gui_stub.c           ← Unsupported platforms: returns error from init()
```

### Backend Selection

When `gui.App.new()` is called:

1. Check `VIGIL_GUI_BACKEND` environment variable — if set to `"sdl"`, use SDL directly
2. Try the platform-native backend (Cocoa / GTK / Win32)
3. If native `init()` fails, try the SDL fallback (if compiled in)
4. If all fail, return an error to the Vigil program

### GTK Version Handling

The GTK backend supports both GTK4 and GTK3 via an internal polymorphism layer (`gtk_version_ops_t`). At init time it tries `dlopen("libgtk-4.so.1")` first, then falls back to `dlopen("libgtk-3.so.0")`. All GTK3/GTK4 API differences are encapsulated in two static vtable instances — the shared backend code never checks the GTK version directly.

Key differences handled:

| Concern | GTK3 | GTK4 |
|---|---|---|
| Event loop | `gtk_main` | `GMainLoop` |
| Containers | `gtk_container_add` | `gtk_window_set_child` |
| Window creation | `gtk_window_new(TOPLEVEL)` | `gtk_window_new()` |
| Widget destruction | `gtk_widget_destroy` | `gtk_window_destroy` / hide |
| Visibility | `gtk_widget_show_all` | `gtk_widget_set_visible` |
| Close signal | `"delete-event"` | `"close-request"` |
| Radio buttons | `GtkRadioButton` | `GtkCheckButton` + `set_group` |
| Menus | `GtkMenuBar` + `GtkMenuItem` | `GMenu` + `GtkPopoverMenuBar` |
| File dialogs | `gtk_dialog_run` (sync) | Removed (async only) |

### No Link-Time Dependencies

The GTK backend uses `dlopen`/`dlsym` for all GTK and GLib symbols. The Win32 backend links user32/gdi32/comctl32/comdlg32 (always present on Windows). The Cocoa backend uses `dlopen` for AppKit and `objc_msgSend` for all Objective-C calls. End users never need `-dev` packages.

## Forcing the SDL Backend

Set the `VIGIL_GUI_BACKEND` environment variable:

```bash
VIGIL_GUI_BACKEND=sdl ./build/vigil run my_app.vigil
```

This bypasses the native backend entirely. Useful for:
- Testing the SDL renderer on a system with a native toolkit
- Debugging rendering differences between backends
- Running on systems where the native toolkit is installed but broken

## Grid Layout

All widgets are placed using a grid system:

```vigil
label.grid(0, 0)       // column 0, row 0
button.grid(0, 1)      // column 0, row 1
entry.grid(1, 0)       // column 1, row 0
```

Configure column/row weights for responsive resizing:

```vigil
win.grid_columnconfigure(0, 1)  // column 0 gets weight 1
win.grid_rowconfigure(0, 1)     // row 0 gets weight 1
```

## Adding a New Platform Backend

1. **Create `backends/gui_<platform>.c`** implementing every function pointer in `gui_backend_t`. Use `gui_stub.c` as a starting template and `gui_gtk.c` as a reference for a full implementation.

2. **Export the vtable** as `const gui_backend_t gui_backend_<platform>`.

3. **Add the extern declaration** in `gui_backend.h`:
   ```c
   #elif defined(__YOUR_PLATFORM__)
   extern const gui_backend_t gui_backend_<platform>;
   ```

4. **Update `gui_backend_select()`** in `gui.c` to return your backend on the target platform.

5. **Update `plugin.cmake`** to compile your backend file and link any required libraries:
   ```cmake
   elseif(YOUR_PLATFORM)
       list(APPEND _gui_sources backends/gui_<platform>.c)
       list(APPEND _gui_libs your_libs)
   ```

6. **Build and test.** The unit tests in `tests/gui_test.c` verify the module structure (class count, method names, field names) and will catch missing vtable entries at compile time.

### Vtable Checklist

Every backend must implement these categories:

- **Lifecycle**: `init`, `shutdown`, `main_loop`, `quit`
- **Window**: `window_create`, `window_destroy`, `window_set_title`, `window_get_size`
- **Widgets** (for each of Label, Button, Entry, Checkbox, Slider, Select, Text, Radiobutton, Spinbox, Frame, Listbox, Canvas): `create`, `destroy`, plus type-specific getters/setters
- **Menu**: `menubar_create`, `menubar_destroy`, `menu_add_submenu`, `menu_add_item`
- **PanedWindow**: `paned_create`, `paned_destroy`, `paned_set_start`, `paned_set_end`, `paned_set_position`
- **Grid**: `widget_grid`, `widget_grid_span`, `widget_grid_remove`, `container_grid_columnconfigure`, `container_grid_rowconfigure`
- **Events**: `set_callback` (handles `GUI_EVENT_CLICK`, `GUI_EVENT_CLOSE`, `GUI_EVENT_CHANGE`)
- **Dialogs**: `message_box`, `open_file_dialog`, `save_file_dialog`, `choose_directory`, `ask_yes_no`

If a feature isn't supported on your platform, implement it as a no-op (return NULL/0/false). The stub backend shows the minimal signatures.

## Adding a New Widget

1. **Add vtable entries** to `gui_backend_t` in `gui_backend.h`
2. **Add stub implementations** in `gui_stub.c`
3. **Add VM bindings** in `gui.c`: handle registry, class index, native methods, class descriptor, class table entry
4. **Implement in each backend**: `gui_gtk.c`, `gui_cocoa.m`, `gui_win32.c`, `gui_sdl.c`
5. **Add tests** in `tests/gui_test.c`: class existence, method existence, handle field
6. **Run `clang-format`** on all modified `.c` and `.h` files

## File Reference

| File | Purpose |
|---|---|
| `plugins/gui/gui_backend.h` | Backend vtable definition, event types, callback types |
| `plugins/gui/gui.c` | VM bindings, handle registries, backend selection, module descriptor |
| `plugins/gui/plugin.cmake` | Build configuration, platform detection, SDL fallback |
| `plugins/gui/backends/gui_cocoa.m` | macOS Cocoa backend |
| `plugins/gui/backends/gui_gtk.c` | Linux GTK4/GTK3 backend |
| `plugins/gui/backends/gui_win32.c` | Windows Win32 backend |
| `plugins/gui/backends/gui_sdl.c` | SDL3 fallback backend |
| `plugins/gui/backends/gui_stub.c` | Stub for unsupported platforms |
| `tests/gui_test.c` | Unit tests for module structure |

## Known Limitations

- **Cocoa Canvas**: Drawing primitives are stubbed (NSView created but no Core Graphics rendering yet)
- **Win32 Canvas**: Drawing primitives are stubbed (STATIC control created but no GDI rendering yet)
- **GTK4 file dialogs**: Stubbed because GTK4 removed all synchronous dialog APIs
- **Win32 choose_directory**: Stubbed (needs SHBrowseForFolder from shell32)
- **SDL text rendering**: Uses SDL3's built-in 8×8 debug font; `vigil_font.h` (stb_truetype) integration planned
- **SDL text input**: Entry/Text widgets store text but keyboard input dispatch is not yet implemented
- **SDL menus**: Menu bar is a no-op in the SDL backend
