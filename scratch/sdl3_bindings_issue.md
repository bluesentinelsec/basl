# SDL3 Bindings for Vigil

## Overview

Implement a Vigil plugin providing idiomatic bindings for SDL3 (v3.5.0+). The bindings should feel native to Vigil — opaque handles as typed classes with methods, SDL structs as native classes with fields, errors surfaced through Vigil's `err` system, and constants as module-level values.

**Target user experience:**

```vigil
import "sdl";

fn main() -> i32 {
    guard bool _, err e = sdl.init(sdl.INIT_VIDEO) {
        fmt.eprintln(f"SDL init failed: {e.message()}");
        return 1;
    }
    defer sdl.quit();

    sdl.Window win, err we = sdl.Window.create("My Game", 800, 600, sdl.WINDOW_RESIZABLE);
    guard bool _, err _ = we { return 1; }
    defer win.destroy();

    sdl.Renderer ren, err re = sdl.Renderer.create(win, "");
    guard bool _, err _ = re { return 1; }
    defer ren.destroy();

    bool running = true;
    sdl.Event ev = sdl.Event.new();
    while (running) {
        while (ev.poll()) {
            if (ev.type() == sdl.EVENT_QUIT) {
                running = false;
            }
        }
        ren.set_draw_color(25, 25, 25, 255);
        ren.clear();
        ren.present();
    }
    return 0;
}
```

## Design Decisions

### Opaque Handles → Native Classes with Slot Tables

SDL3 opaque pointers (`SDL_Window*`, `SDL_Renderer*`, etc.) are represented as native classes with a single `i64 handle` field. The C plugin maintains an internal slot table per type (same pattern as `fs.Reader`/`fs.Writer`). This gives us:

- **Type safety**: can't pass a `Window` where a `Renderer` is expected
- **Method syntax**: `win.set_title("x")` instead of `sdl.set_window_title(handle, "x")`
- **Validated access**: use-after-destroy returns `err` instead of segfaulting

### SDL Structs → Native Classes with Fields

Value-type structs (`SDL_Rect`, `SDL_FRect`, `SDL_Color`, `SDL_Point`) become native classes with typed fields (same pattern as `math.Vec2`). The C glue marshals between Vigil fields and C structs at each call boundary.

### Events → Polling with Typed Accessors

`sdl.Event` is a native class backed by an internal `SDL_Event` buffer. Accessor methods (`ev.type()`, `ev.key_scancode()`, `ev.mouse_x()`) read the appropriate union member. No callback needed for the core event loop.

### Callbacks → ffi_callback Trampoline Pool

For APIs that require callbacks (timers, audio streams), the plugin uses the existing `ffi_callback` trampoline infrastructure. The plugin registers its own dispatch function that knows SDL callback signatures. The 8-slot limit is sufficient for typical SDL usage.

### Constants → Zero-arg Module Functions

SDL3 constants/flags (`SDL_INIT_VIDEO`, `SDL_WINDOW_RESIZABLE`, event types) are exported as zero-arg functions returning `i32`. This allows bitwise OR for flag combinations: `sdl.INIT_VIDEO | sdl.INIT_AUDIO`.

### Type Mapping

| SDL3 Type | Vigil Type | Notes |
|-----------|-----------|-------|
| `int`, `Sint32`, `Uint32` | `i32` | Unsigned values clamped in C glue |
| `Sint64`, `Uint64` | `i64` | |
| `float` | `f64` | Truncated to float in C glue, promoted on return |
| `Uint16` | `i32` | Clamped to 0..65535 in C glue |
| `Uint8` | `i32` | Clamped to 0..255 in C glue |
| `bool` | `bool` | Direct mapping |
| `const char*` | `string` | |
| `SDL_Window*` etc. | `sdl.Window` class | Slot-table backed |
| `SDL_Rect` etc. | `sdl.Rect` class | Field-mapped |

### No Language Changes Required

All work is at the plugin layer using existing patterns from the stdlib (`fs.Reader`, `math.Vec2`, `ffi_callback`).

---

## Implementation Slices

### Slice 0: Plugin Infrastructure

**Goal**: Reusable C infrastructure shared by all subsequent slices.

- [ ] Create `plugins/sdl/` directory with `plugin.cmake` and initial `sdl.c`
- [ ] `plugin.cmake` using `vigil_add_plugin(NAME sdl SOURCES sdl.c LIBRARIES SDL3::SDL3 FIND_PACKAGES SDL3)`
- [ ] Handle registry: generic slot-table utility (macro or inline functions) for mapping `i64` slot ↔ `void*` pointer, with validation, use-after-free protection, and configurable capacity
- [ ] Struct marshaling helpers: macros/inlines to extract native class fields into C structs and vice versa (read field at index → `int32_t`/`double`, push new instance with field values)
- [ ] Error helper: convert `SDL_GetError()` into Vigil `err` push with appropriate error kind
- [ ] Constant export helper: macro to define zero-arg functions that return `i32` constants
- [ ] Verify build: `cmake -DCMAKE_BUILD_TYPE=Release` succeeds with SDL3 installed, skips gracefully without it

**Acceptance**: `import "sdl";` compiles (empty module). Build skips cleanly when SDL3 is absent.

---

### Slice 1: Init, Version, and Constants

**Goal**: `sdl.init()` / `sdl.quit()` work. Core constants are available.

- [ ] `sdl.init(i32 flags) -> (bool, err)` — wraps `SDL_Init`
- [ ] `sdl.init_sub_system(i32 flags) -> (bool, err)`
- [ ] `sdl.quit_sub_system(i32 flags)`
- [ ] `sdl.quit()`
- [ ] `sdl.was_init(i32 flags) -> i32`
- [ ] `sdl.get_error() -> string`
- [ ] `sdl.get_version() -> i32`
- [ ] `sdl.get_revision() -> string`
- [ ] Init flag constants: `sdl.INIT_VIDEO`, `sdl.INIT_AUDIO`, `sdl.INIT_TIMER`, `sdl.INIT_EVENTS`, `sdl.INIT_JOYSTICK`, `sdl.INIT_HAPTIC`, `sdl.INIT_GAMEPAD`, `sdl.INIT_SENSOR`, `sdl.INIT_CAMERA`
- [ ] Write `examples/sdl_hello.vigil` that inits and quits SDL

**Acceptance**: Example runs, prints SDL version, exits cleanly.

---

### Slice 2: Window Management

**Goal**: Create, configure, and destroy windows.

- [ ] `sdl.Window` native class with `i64 handle` field
- [ ] `sdl.Window.create(string title, i32 w, i32 h, i32 flags) -> (sdl.Window, err)` — static constructor
- [ ] `win.destroy()`
- [ ] `win.get_id() -> i32`
- [ ] `win.set_title(string title) -> (bool, err)`
- [ ] `win.get_title() -> string`
- [ ] `win.set_position(i32 x, i32 y) -> (bool, err)`
- [ ] `win.get_position() -> (i32, i32)` — returns x, y
- [ ] `win.set_size(i32 w, i32 h) -> (bool, err)`
- [ ] `win.get_size() -> (i32, i32)` — returns w, h
- [ ] `win.set_fullscreen(bool fullscreen) -> (bool, err)`
- [ ] `win.show()` / `win.hide()` / `win.raise()` / `win.minimize()` / `win.maximize()` / `win.restore()`
- [ ] `win.set_resizable(bool resizable)`
- [ ] `win.set_bordered(bool bordered)`
- [ ] `win.get_flags() -> i32`
- [ ] Window flag constants: `sdl.WINDOW_FULLSCREEN`, `sdl.WINDOW_RESIZABLE`, `sdl.WINDOW_HIDDEN`, `sdl.WINDOW_BORDERLESS`, `sdl.WINDOW_MINIMIZED`, `sdl.WINDOW_MAXIMIZED`, `sdl.WINDOW_ALWAYS_ON_TOP`
- [ ] Write `examples/sdl_window.vigil` — creates a window, sets title, waits 2 seconds, exits

**Acceptance**: Window appears on screen with correct title and size.

---

### Slice 3: Renderer (2D Drawing)

**Goal**: Create a renderer, clear/present, draw primitives.

- [ ] `sdl.Renderer` native class with `i64 handle` field
- [ ] `sdl.Renderer.create(sdl.Window win, string driver) -> (sdl.Renderer, err)` — static constructor
- [ ] `ren.destroy()`
- [ ] `ren.clear() -> (bool, err)`
- [ ] `ren.present() -> (bool, err)`
- [ ] `ren.set_draw_color(i32 r, i32 g, i32 b, i32 a) -> (bool, err)`
- [ ] `ren.get_draw_color() -> (i32, i32, i32, i32)` — r, g, b, a
- [ ] `ren.draw_point(f64 x, f64 y) -> (bool, err)`
- [ ] `ren.draw_line(f64 x1, f64 y1, f64 x2, f64 y2) -> (bool, err)`
- [ ] `ren.draw_rect(sdl.FRect rect) -> (bool, err)`
- [ ] `ren.fill_rect(sdl.FRect rect) -> (bool, err)`
- [ ] `ren.set_vsync(i32 vsync) -> (bool, err)`
- [ ] Struct classes: `sdl.Rect` (x, y, w, h as `i32`), `sdl.FRect` (x, y, w, h as `f64`), `sdl.Point` (x, y as `i32`), `sdl.FPoint` (x, y as `f64`), `sdl.Color` (r, g, b, a as `i32`)
- [ ] Write `examples/sdl_draw.vigil` — draws colored rectangles and lines

**Acceptance**: Colored shapes render on screen.

---

### Slice 4: Event System

**Goal**: Poll events, read event fields by type.

- [ ] `sdl.Event` native class backed by internal `SDL_Event` buffer
- [ ] `sdl.Event.new() -> sdl.Event` — allocates event buffer
- [ ] `ev.poll() -> bool` — wraps `SDL_PollEvent`, returns true if event available
- [ ] `ev.wait() -> bool` — wraps `SDL_WaitEvent`
- [ ] `ev.wait_timeout(i32 ms) -> bool`
- [ ] `ev.type() -> i32` — event type
- [ ] Common event type constants: `sdl.EVENT_QUIT`, `sdl.EVENT_KEY_DOWN`, `sdl.EVENT_KEY_UP`, `sdl.EVENT_MOUSE_MOTION`, `sdl.EVENT_MOUSE_BUTTON_DOWN`, `sdl.EVENT_MOUSE_BUTTON_UP`, `sdl.EVENT_MOUSE_WHEEL`, `sdl.EVENT_WINDOW_*` (close, resized, etc.)
- [ ] Keyboard accessors: `ev.key_scancode() -> i32`, `ev.key_keycode() -> i32`, `ev.key_mod() -> i32`, `ev.key_repeat() -> bool`
- [ ] Mouse motion accessors: `ev.mouse_x() -> f64`, `ev.mouse_y() -> f64`, `ev.mouse_xrel() -> f64`, `ev.mouse_yrel() -> f64`
- [ ] Mouse button accessors: `ev.mouse_button() -> i32`, `ev.mouse_clicks() -> i32`
- [ ] Mouse wheel accessors: `ev.wheel_x() -> f64`, `ev.wheel_y() -> f64`
- [ ] Window event accessors: `ev.window_event() -> i32`, `ev.window_data1() -> i32`, `ev.window_data2() -> i32`
- [ ] `sdl.pump_events()`
- [ ] `sdl.has_event(i32 type) -> bool`
- [ ] `sdl.flush_event(i32 type)`
- [ ] Write `examples/sdl_events.vigil` — prints event info as user interacts

**Acceptance**: Interactive example correctly reports key presses, mouse movement, and quit.

---

### Slice 5: Keyboard and Mouse Queries

**Goal**: Query keyboard/mouse state outside of events.

- [ ] `sdl.get_keyboard_state() -> (i64, i32)` — returns pointer and numkeys (use with `unsafe.peek_u8` for raw access, or provide helper)
- [ ] `sdl.is_key_pressed(i32 scancode) -> bool` — convenience wrapper that checks keyboard state
- [ ] `sdl.get_mod_state() -> i32`
- [ ] `sdl.get_key_name(i32 keycode) -> string`
- [ ] `sdl.get_scancode_name(i32 scancode) -> string`
- [ ] `sdl.get_mouse_state() -> (f64, f64, i32)` — x, y, button_mask
- [ ] `sdl.get_global_mouse_state() -> (f64, f64, i32)`
- [ ] `sdl.warp_mouse(sdl.Window win, f64 x, f64 y)`
- [ ] `sdl.show_cursor()` / `sdl.hide_cursor()` / `sdl.cursor_visible() -> bool`
- [ ] Scancode constants: `sdl.SCANCODE_A` through `sdl.SCANCODE_Z`, arrow keys, modifiers, function keys, common keys
- [ ] Keycode constants: `sdl.KEY_RETURN`, `sdl.KEY_ESCAPE`, `sdl.KEY_SPACE`, etc.
- [ ] Mouse button constants: `sdl.BUTTON_LEFT`, `sdl.BUTTON_MIDDLE`, `sdl.BUTTON_RIGHT`

**Acceptance**: Example that shows real-time key/mouse state on screen.

---

### Slice 6: Textures and Surface Loading

**Goal**: Load images, create textures, render sprites.

- [ ] `sdl.Texture` native class with `i64 handle` field
- [ ] `sdl.Texture.create(sdl.Renderer ren, i32 format, i32 access, i32 w, i32 h) -> (sdl.Texture, err)`
- [ ] `sdl.Surface` native class with `i64 handle` field
- [ ] `sdl.Surface.load(string path) -> (sdl.Surface, err)` — wraps `SDL_LoadSurface` (BMP/PNG)
- [ ] `sdl.Surface.load_bmp(string path) -> (sdl.Surface, err)`
- [ ] `surf.destroy()`
- [ ] `sdl.Texture.from_surface(sdl.Renderer ren, sdl.Surface surf) -> (sdl.Texture, err)`
- [ ] `tex.destroy()`
- [ ] `tex.get_size() -> (f64, f64)` — w, h
- [ ] `tex.set_color_mod(i32 r, i32 g, i32 b)`
- [ ] `tex.set_alpha_mod(i32 alpha)`
- [ ] `tex.set_blend_mode(i32 mode)`
- [ ] `ren.render_texture(sdl.Texture tex, sdl.FRect src, sdl.FRect dst) -> (bool, err)` — NULL-safe (pass zero-size rect for full)
- [ ] `ren.render_texture_rotated(sdl.Texture tex, sdl.FRect src, sdl.FRect dst, f64 angle, sdl.FPoint center, i32 flip) -> (bool, err)`
- [ ] Pixel format constants, texture access constants, blend mode constants, flip mode constants
- [ ] Write `examples/sdl_texture.vigil` — loads and renders a BMP sprite

**Acceptance**: Sprite renders on screen from a BMP file.

---

### Slice 7: Timer and Delay

**Goal**: Frame timing and timer callbacks.

- [ ] `sdl.get_ticks() -> i64` — milliseconds since init
- [ ] `sdl.get_ticks_ns() -> i64`
- [ ] `sdl.get_performance_counter() -> i64`
- [ ] `sdl.get_performance_frequency() -> i64`
- [ ] `sdl.delay(i32 ms)`
- [ ] `sdl.delay_ns(i64 ns)`
- [ ] `sdl.delay_precise(i64 ns)`
- [ ] `sdl.Timer` native class for callback-based timers (uses ffi_callback trampoline)
- [ ] `sdl.Timer.add(i32 interval_ms, fn callback) -> (sdl.Timer, err)`
- [ ] `timer.remove() -> (bool, err)`
- [ ] Write `examples/sdl_timer.vigil` — frame-rate-limited loop with delta time display

**Acceptance**: Smooth 60fps loop with accurate delta time reporting.

---

### Slice 8: Audio

**Goal**: Play audio, load WAV files, basic streaming.

- [ ] `sdl.AudioStream` native class
- [ ] `sdl.AudioSpec` native class (freq, format, channels as fields)
- [ ] `sdl.AudioStream.open(i32 device_id, sdl.AudioSpec spec) -> (sdl.AudioStream, err)`
- [ ] `stream.put_data(i64 buf, i32 len) -> (bool, err)` — accepts unsafe buffer
- [ ] `stream.get_queued() -> i32`
- [ ] `stream.pause()` / `stream.resume()`
- [ ] `stream.destroy()`
- [ ] `sdl.load_wav(string path) -> (sdl.AudioSpec, i64, i32, err)` — returns spec, buffer pointer, length
- [ ] `sdl.get_audio_playback_devices() -> (array<i32>, err)` — device IDs
- [ ] `sdl.get_audio_device_name(i32 device_id) -> string`
- [ ] Audio format constants: `sdl.AUDIO_S16`, `sdl.AUDIO_S32`, `sdl.AUDIO_F32`, etc.
- [ ] Write `examples/sdl_audio.vigil` — loads and plays a WAV file

**Acceptance**: WAV file plays through speakers.

---

### Slice 9: Gamepad and Joystick

**Goal**: Gamepad input for game development.

- [ ] `sdl.Gamepad` native class
- [ ] `sdl.has_gamepad() -> bool`
- [ ] `sdl.get_gamepads() -> array<i32>` — instance IDs
- [ ] `sdl.Gamepad.open(i32 instance_id) -> (sdl.Gamepad, err)`
- [ ] `gp.close()`
- [ ] `gp.get_name() -> string`
- [ ] `gp.get_axis(i32 axis) -> i32`
- [ ] `gp.get_button(i32 button) -> bool`
- [ ] `gp.rumble(i32 low, i32 high, i32 duration_ms) -> (bool, err)`
- [ ] Gamepad axis/button constants
- [ ] Gamepad event accessors on `sdl.Event`: `ev.gamepad_axis()`, `ev.gamepad_button()`, `ev.gamepad_which()`
- [ ] Write `examples/sdl_gamepad.vigil` — displays gamepad state

**Acceptance**: Gamepad input reads correctly, rumble works.

---

### Slice 10: Clipboard, MessageBox, Misc

**Goal**: Remaining commonly-used utilities.

- [ ] `sdl.set_clipboard_text(string text) -> (bool, err)`
- [ ] `sdl.get_clipboard_text() -> string`
- [ ] `sdl.has_clipboard_text() -> bool`
- [ ] `sdl.show_simple_message_box(i32 flags, string title, string message, sdl.Window win) -> (bool, err)`
- [ ] `sdl.open_url(string url) -> (bool, err)`
- [ ] `sdl.get_base_path() -> string`
- [ ] `sdl.get_pref_path(string org, string app) -> string`
- [ ] `sdl.get_num_cpu_cores() -> i32`
- [ ] `sdl.get_system_ram() -> i32`
- [ ] `sdl.log(string msg)` / `sdl.log_error(string msg)` / `sdl.log_warn(string msg)`
- [ ] MessageBox flag constants

**Acceptance**: Clipboard round-trips text, message box displays.

---

## Future Slices (not in initial scope)

These can be added incrementally after the core 0–10 slices ship:

- **GPU API** — `SDL_GPU*` functions (complex struct parameters, shader management)
- **Haptic** — force feedback beyond simple rumble
- **Camera** — webcam capture
- **Vulkan/Metal/OpenGL** — low-level graphics context management
- **Sensor** — accelerometer/gyroscope
- **Touch** — multi-touch input
- **Tray** — system tray icons
- **Dialog** — native file open/save dialogs
- **Display** — multi-monitor queries, display modes
- **Process** — already covered by Vigil's stdlib

## Dependencies

- SDL3 (>= 3.5.0) installed on the build system
- Plugin gracefully skips if SDL3 is not found (`FIND_PACKAGES SDL3`)

## References

- [SDL3 API Reference](https://wiki.libsdl.org/SDL3/QuickReference) (also in `scratch/sdl3_reference.txt`)
- Vigil plugin system: `docs/plugins.md`
- Existing patterns: `src/stdlib/fs.c` (handle classes), `src/stdlib/math.c` (struct classes), `src/ffi_callback.c` (callback trampolines)
