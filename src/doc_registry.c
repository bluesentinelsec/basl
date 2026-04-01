#include "vigil/doc_registry.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/stdlib.h"
#include "vigil/type.h"

/* ── Builtin Function Docs ────────────────────────────────── */

static const vigil_doc_entry_t builtin_docs[] = {
    {"builtins", NULL, "Built-in functions available without import.",
     "These functions are always available in VIGIL programs.", NULL},
    {"len", "len(value: string | array | map) -> int", "Return the length of a string, array, or map.", NULL,
     "len(\"hello\")  // 5\nlen([1, 2, 3])  // 3"},
    {"type", "type(value: any) -> string", "Return the type name of a value.", NULL,
     "type(42)       // \"int\"\ntype(\"hello\")  // \"string\""},
    {"str", "str(value: any) -> string", "Convert a value to its string representation.", NULL,
     "str(42)    // \"42\"\nstr(true)  // \"true\""},
    {"int", "int(value: string | float) -> int", "Convert a string or float to an integer.", NULL,
     "int(\"42\")   // 42\nint(3.14)   // 3"},
    {"float", "float(value: string | int) -> float", "Convert a string or integer to a float.", NULL,
     "float(\"3.14\")  // 3.14\nfloat(42)      // 42.0"},
    {"exit", "exit(code: int) -> void", "Exit the program with the given status code.", NULL,
     "exit(0)  // success\nexit(1)  // failure"},
    {"char", "char(code: int) -> string", "Convert a byte value (0-255) to a single-character string.", NULL,
     "char(65)   // \"A\"\nchar(0x0a) // \"\\n\""},
};

#define BUILTIN_COUNT (sizeof(builtin_docs) / sizeof(builtin_docs[0]))

/* ── strings Module Docs ──────────────────────────────────── */

static const vigil_doc_entry_t strings_docs[] = {
    {"strings", NULL, "String methods available on all string values.",
     "These methods are called on string values using dot notation.", NULL},
    {"strings.len", "s.len() -> i32", "Return the length of the string in bytes.", NULL, "\"hello\".len()  // 5"},
    {"strings.contains", "s.contains(sub: string) -> bool", "Return true if s contains the substring sub.", NULL,
     "\"hello\".contains(\"ell\")  // true"},
    {"strings.starts_with", "s.starts_with(prefix: string) -> bool", "Return true if s starts with prefix.", NULL,
     "\"hello\".starts_with(\"he\")  // true"},
    {"strings.ends_with", "s.ends_with(suffix: string) -> bool", "Return true if s ends with suffix.", NULL,
     "\"hello\".ends_with(\"lo\")  // true"},
    {"strings.trim", "s.trim() -> string", "Return s with leading and trailing whitespace removed.", NULL,
     "\"  hello  \".trim()  // \"hello\""},
    {"strings.trim_left", "s.trim_left() -> string", "Return s with leading whitespace removed.", NULL,
     "\"  hello\".trim_left()  // \"hello\""},
    {"strings.trim_right", "s.trim_right() -> string", "Return s with trailing whitespace removed.", NULL,
     "\"hello  \".trim_right()  // \"hello\""},
    {"strings.trim_prefix", "s.trim_prefix(prefix: string) -> string",
     "Return s without the leading prefix if present.", NULL, "\"hello\".trim_prefix(\"he\")  // \"llo\""},
    {"strings.trim_suffix", "s.trim_suffix(suffix: string) -> string",
     "Return s without the trailing suffix if present.", NULL, "\"hello\".trim_suffix(\"lo\")  // \"hel\""},
    {"strings.to_upper", "s.to_upper() -> string", "Return s with all ASCII letters converted to uppercase.", NULL,
     "\"Hello\".to_upper()  // \"HELLO\""},
    {"strings.to_lower", "s.to_lower() -> string", "Return s with all ASCII letters converted to lowercase.", NULL,
     "\"Hello\".to_lower()  // \"hello\""},
    {"strings.replace", "s.replace(old: string, new: string) -> string",
     "Return s with all occurrences of old replaced by new.", NULL, "\"hello\".replace(\"l\", \"L\")  // \"heLLo\""},
    {"strings.split", "s.split(sep: string) -> array<string>",
     "Split s by separator and return an array of substrings.", NULL,
     "\"a,b,c\".split(\",\")  // [\"a\", \"b\", \"c\"]"},
    {"strings.index_of", "s.index_of(sub: string) -> (i32, bool)",
     "Return the index of the first occurrence of sub, or (-1, false) if not found.", NULL,
     "i32 idx, bool found = \"hello\".index_of(\"l\")  // 2, true"},
    {"strings.last_index_of", "s.last_index_of(sub: string) -> (i32, bool)",
     "Return the index of the last occurrence of sub, or (-1, false) if not found.", NULL,
     "i32 idx, bool found = \"hello\".last_index_of(\"l\")  // 3, true"},
    {"strings.substr", "s.substr(start: i32, len: i32) -> (string, err)",
     "Return a substring starting at start with length len.", NULL,
     "string sub, err e = \"hello\".substr(1, 3)  // \"ell\""},
    {"strings.char_at", "s.char_at(i: i32) -> (string, err)",
     "Return the character at index i as a single-character string.", NULL,
     "string c, err e = \"hello\".char_at(0)  // \"h\""},
    {"strings.bytes", "s.bytes() -> array<u8>", "Return the raw bytes of the string as an array.", NULL,
     "\"AB\".bytes()  // [65, 66]"},
    {"strings.reverse", "s.reverse() -> string", "Return s with characters in reverse order.", NULL,
     "\"hello\".reverse()  // \"olleh\""},
    {"strings.is_empty", "s.is_empty() -> bool", "Return true if s has length zero.", NULL, "\"\".is_empty()  // true"},
    {"strings.char_count", "s.char_count() -> i32", "Return the number of Unicode code points in s (not bytes).", NULL,
     "\"café\".char_count()  // 4"},
    {"strings.repeat", "s.repeat(n: i32) -> string", "Return s repeated n times.", NULL,
     "\"ab\".repeat(3)  // \"ababab\""},
    {"strings.count", "s.count(sub: string) -> i32", "Return the number of non-overlapping occurrences of sub in s.",
     NULL, "\"banana\".count(\"a\")  // 3"},
    {"strings.fields", "s.fields() -> array<string>", "Split s on whitespace and return non-empty fields.",
     "Similar to Go's strings.Fields. Splits on runs of whitespace.",
     "\"  a  b  c  \".fields()  // [\"a\", \"b\", \"c\"]"},
    {"strings.join", "sep.join(arr: array<string>) -> string", "Join array elements with sep as separator.",
     "The separator is the receiver, the array is the argument.", "\",\".join([\"a\", \"b\", \"c\"])  // \"a,b,c\""},
    {"strings.cut", "s.cut(sep: string) -> (string, string, bool)", "Cut s around the first instance of sep.",
     "Returns (before, after, found). If sep is not found, returns (s, \"\", false).",
     "string k, string v, bool ok = \"key=val\".cut(\"=\")  // \"key\", \"val\", true"},
    {"strings.equal_fold", "s.equal_fold(t: string) -> bool",
     "Return true if s equals t under case-insensitive comparison.", "Compares ASCII letters case-insensitively.",
     "\"Go\".equal_fold(\"go\")  // true"},
};

#define STRINGS_COUNT (sizeof(strings_docs) / sizeof(strings_docs[0]))

/* ── regex Module Docs ────────────────────────────────────── */

/* ── random Module Docs ───────────────────────────────────── */

/* ── url Module Docs ──────────────────────────────────────── */

/* ── log module ───────────────────────────────────────────── */

/* ── csv Module Docs ──────────────────────────────────────── */

/* ── net Module Docs ──────────────────────────────────────── */

/* ── crypto Module Docs ───────────────────────────────────── */

/* ── readline module ─────────────────────────────────────────────── */

static const vigil_doc_entry_t readline_docs[] = {
    {"readline", NULL, "Interactive line input.", "Read user input with prompt and history support.", NULL},
    {"readline.input", "readline.input(prompt: string) -> string", "Read a line of input.",
     "Displays the prompt and reads a line from the terminal.", "string line = readline.input(\"> \")"},
    {"readline.history_add", "readline.history_add(line: string) -> void", "Add a line to history.",
     "Stores the line for recall with history_get.", "readline.history_add(line)"},
    {"readline.history_get", "readline.history_get(index: i32) -> string", "Get a history entry.",
     "Returns the history entry at the given index.", "string h = readline.history_get(0)"},
    {"readline.history_length", "readline.history_length() -> i32", "Get history length.",
     "Returns the number of entries in the history.", "i32 n = readline.history_length()"},
    {"readline.history_clear", "readline.history_clear() -> void", "Clear line history.",
     "Removes all entries from the in-memory history list.", "readline.history_clear()"},
    {"readline.history_load", "readline.history_load(path: string) -> void", "Load history from a file.",
     "Loads previously saved history entries from the given file path.", "readline.history_load(\".vigil_history\")"},
    {"readline.history_save", "readline.history_save(path: string) -> void", "Save history to a file.",
     "Writes the current in-memory history entries to the given file path.",
     "readline.history_save(\".vigil_history\")"},
};

#define READLINE_COUNT (sizeof(readline_docs) / sizeof(readline_docs[0]))

/* ── ffi module ──────────────────────────────────────────────────── */

/* ── parse module ────────────────────────────────────────────────── */

/* ── sdl module ──────────────────────────────────────────────────── */

static const vigil_doc_entry_t sdl_docs[] = {
    {"sdl", NULL, "SDL3 multimedia library bindings.",
     "Provides access to SDL3 for window management, rendering, input,\n"
     "audio, and more. The SDL3 library is statically linked into Vigil.\n"
     "Disable at build time with -DVIGIL_PLUGIN_SDL=OFF.",
     "import \"sdl\";\n\n"
     "sdl.init(sdl.INIT_VIDEO());\n"
     "defer sdl.quit();"},
    {"sdl.init", "sdl.init(flags: i32) -> (bool, err)", "Initialize SDL subsystems.",
     "Initializes the SDL library. Pass one or more INIT_* flag constants\n"
     "combined with bitwise OR. Returns (true, ok) on success.",
     "bool ok, err e = sdl.init(sdl.INIT_VIDEO() | sdl.INIT_AUDIO());\n"
     "defer sdl.quit();"},
    {"sdl.init_sub_system", "sdl.init_sub_system(flags: i32) -> (bool, err)", "Initialize additional SDL subsystems.",
     "Initialize subsystems that were not included in the initial sdl.init() call.",
     "sdl.init_sub_system(sdl.INIT_JOYSTICK())"},
    {"sdl.quit_sub_system", "sdl.quit_sub_system(flags: i32) -> void", "Shut down specific SDL subsystems.",
     "Shuts down the specified subsystems without quitting SDL entirely.", "sdl.quit_sub_system(sdl.INIT_AUDIO())"},
    {"sdl.quit", "sdl.quit() -> void", "Clean up all initialized SDL subsystems.",
     "Call this when done with SDL. Typically used with defer.", "defer sdl.quit();"},
    {"sdl.was_init", "sdl.was_init(flags: i32) -> i32", "Check which subsystems are initialized.",
     "Returns a mask of the specified subsystems that are currently initialized.",
     "i32 active = sdl.was_init(sdl.INIT_VIDEO())"},
    {"sdl.get_error", "sdl.get_error() -> string", "Get the last SDL error message.",
     "Returns the error message for the last SDL function that failed.", "string msg = sdl.get_error()"},
    {"sdl.get_version", "sdl.get_version() -> i32", "Get the linked SDL version.",
     "Returns the version as a packed integer. Use bitwise operations to\n"
     "extract major/minor/patch: major = v / 1000000, minor = (v / 1000) % 1000,\n"
     "patch = v % 1000.",
     "i32 v = sdl.get_version()"},
    {"sdl.get_revision", "sdl.get_revision() -> string", "Get the SDL revision string.",
     "Returns the source revision of the linked SDL library.", "string rev = sdl.get_revision()"},
    {"sdl.INIT_VIDEO", "sdl.INIT_VIDEO() -> i32", "Video subsystem init flag.", NULL, NULL},
    {"sdl.INIT_AUDIO", "sdl.INIT_AUDIO() -> i32", "Audio subsystem init flag.", NULL, NULL},
    {"sdl.INIT_CAMERA", "sdl.INIT_CAMERA() -> i32", "Camera subsystem init flag.", NULL, NULL},
    {"sdl.INIT_EVENTS", "sdl.INIT_EVENTS() -> i32", "Events subsystem init flag.", NULL, NULL},
    {"sdl.INIT_JOYSTICK", "sdl.INIT_JOYSTICK() -> i32", "Joystick subsystem init flag.", NULL, NULL},
    {"sdl.INIT_HAPTIC", "sdl.INIT_HAPTIC() -> i32", "Haptic (force feedback) subsystem init flag.", NULL, NULL},
    {"sdl.INIT_GAMEPAD", "sdl.INIT_GAMEPAD() -> i32", "Gamepad subsystem init flag.", NULL, NULL},
    {"sdl.INIT_SENSOR", "sdl.INIT_SENSOR() -> i32", "Sensor subsystem init flag.", NULL, NULL},
    /* Window class */
    {"sdl.Window", NULL, "SDL window handle.", "Create with sdl.Window.create(). Wraps an SDL_Window.", NULL},
    {"sdl.Window.create", "sdl.Window.create(title: string, w: i32, h: i32, flags: i32) -> (sdl.Window, err)",
     "Create a window.", "Returns a window handle and ok, or nil and an error.",
     "sdl.Window win, err e = sdl.Window.create(\"Hello\", 800, 600, sdl.WINDOW_RESIZABLE())"},
    {"sdl.Window.destroy", "win.destroy() -> void", "Destroy a window.", NULL, "win.destroy()"},
    {"sdl.Window.get_id", "win.get_id() -> i32", "Get the window's numeric ID.", NULL, NULL},
    {"sdl.Window.set_title", "win.set_title(title: string) -> (bool, err)", "Set the window title.", NULL, NULL},
    {"sdl.Window.get_title", "win.get_title() -> string", "Get the window title.", NULL, NULL},
    {"sdl.Window.set_position", "win.set_position(x: i32, y: i32) -> (bool, err)", "Set window position.", NULL, NULL},
    {"sdl.Window.get_position", "win.get_position() -> (i32, i32)", "Get window position (x, y).", NULL, NULL},
    {"sdl.Window.set_size", "win.set_size(w: i32, h: i32) -> (bool, err)", "Set window client area size.", NULL, NULL},
    {"sdl.Window.get_size", "win.get_size() -> (i32, i32)", "Get window client area size (w, h).", NULL, NULL},
    {"sdl.Window.set_fullscreen", "win.set_fullscreen(fs: bool) -> (bool, err)", "Toggle fullscreen.", NULL, NULL},
    {"sdl.Window.show", "win.show() -> void", "Show the window.", NULL, NULL},
    {"sdl.Window.hide", "win.hide() -> void", "Hide the window.", NULL, NULL},
    {"sdl.Window.raise", "win.raise() -> void", "Raise the window above others.", NULL, NULL},
    {"sdl.Window.minimize", "win.minimize() -> void", "Minimize the window.", NULL, NULL},
    {"sdl.Window.maximize", "win.maximize() -> void", "Maximize the window.", NULL, NULL},
    {"sdl.Window.restore", "win.restore() -> void", "Restore a minimized/maximized window.", NULL, NULL},
    {"sdl.Window.set_resizable", "win.set_resizable(r: bool) -> void", "Set whether the window is resizable.", NULL,
     NULL},
    {"sdl.Window.set_bordered", "win.set_bordered(b: bool) -> void", "Set whether the window has a border.", NULL,
     NULL},
    {"sdl.Window.get_flags", "win.get_flags() -> i32", "Get the window flags bitmask.", NULL, NULL},
    /* Window flag constants */
    {"sdl.WINDOW_FULLSCREEN", "sdl.WINDOW_FULLSCREEN() -> i32", "Fullscreen window flag.", NULL, NULL},
    {"sdl.WINDOW_RESIZABLE", "sdl.WINDOW_RESIZABLE() -> i32", "Resizable window flag.", NULL, NULL},
    {"sdl.WINDOW_HIDDEN", "sdl.WINDOW_HIDDEN() -> i32", "Hidden window flag.", NULL, NULL},
    {"sdl.WINDOW_BORDERLESS", "sdl.WINDOW_BORDERLESS() -> i32", "Borderless window flag.", NULL, NULL},
    {"sdl.WINDOW_MINIMIZED", "sdl.WINDOW_MINIMIZED() -> i32", "Minimized window flag.", NULL, NULL},
    {"sdl.WINDOW_MAXIMIZED", "sdl.WINDOW_MAXIMIZED() -> i32", "Maximized window flag.", NULL, NULL},
    {"sdl.WINDOW_ALWAYS_ON_TOP", "sdl.WINDOW_ALWAYS_ON_TOP() -> i32", "Always-on-top window flag.", NULL, NULL},
    /* Renderer class */
    {"sdl.Renderer", NULL, "SDL 2D renderer handle.", "Create with sdl.Renderer.create(). Wraps an SDL_Renderer.",
     NULL},
    {"sdl.Renderer.create", "sdl.Renderer.create(win: sdl.Window, driver: string) -> (sdl.Renderer, err)",
     "Create a 2D renderer for a window.", "Pass empty string for driver to use the default.",
     "sdl.Renderer ren, err e = sdl.Renderer.create(win, \"\")"},
    {"sdl.Renderer.destroy", "ren.destroy() -> void", "Destroy a renderer.", NULL, NULL},
    {"sdl.Renderer.clear", "ren.clear() -> (bool, err)", "Clear the rendering target.", NULL, NULL},
    {"sdl.Renderer.present", "ren.present() -> (bool, err)", "Present the rendered frame.", NULL, NULL},
    {"sdl.Renderer.set_draw_color", "ren.set_draw_color(r: i32, g: i32, b: i32, a: i32) -> (bool, err)",
     "Set the draw color (0-255 per channel).", NULL, "ren.set_draw_color(255, 0, 0, 255)"},
    {"sdl.Renderer.get_draw_color", "ren.get_draw_color() -> (i32, i32, i32, i32)",
     "Get the current draw color (r, g, b, a).", NULL, NULL},
    {"sdl.Renderer.draw_point", "ren.draw_point(x: f64, y: f64) -> (bool, err)", "Draw a point.", NULL, NULL},
    {"sdl.Renderer.draw_line", "ren.draw_line(x1: f64, y1: f64, x2: f64, y2: f64) -> (bool, err)", "Draw a line.", NULL,
     NULL},
    {"sdl.Renderer.draw_rect", "ren.draw_rect(x: f64, y: f64, w: f64, h: f64) -> (bool, err)",
     "Draw a rectangle outline.", NULL, NULL},
    {"sdl.Renderer.fill_rect", "ren.fill_rect(x: f64, y: f64, w: f64, h: f64) -> (bool, err)",
     "Draw a filled rectangle.", NULL, NULL},
    {"sdl.Renderer.set_vsync", "ren.set_vsync(vsync: i32) -> (bool, err)", "Set VSync mode.", NULL, NULL},
    /* Event class */
    {"sdl.Event", NULL, "SDL event handle.", "Create with sdl.Event.new(). Poll events in a loop with ev.poll().",
     NULL},
    {"sdl.Event.new", "sdl.Event.new() -> (sdl.Event, err)", "Allocate an event object.", NULL,
     "sdl.Event ev, err e = sdl.Event.new()"},
    {"sdl.Event.poll", "ev.poll() -> bool", "Poll for a pending event.", "Returns true if an event was available.",
     NULL},
    {"sdl.Event.wait", "ev.wait() -> bool", "Wait for the next event.", NULL, NULL},
    {"sdl.Event.wait_timeout", "ev.wait_timeout(ms: i32) -> bool", "Wait for an event with timeout.", NULL, NULL},
    {"sdl.Event.type", "ev.type() -> i32", "Get the event type.", "Compare with EVENT_* constants.", NULL},
    {"sdl.Event.key_scancode", "ev.key_scancode() -> i32", "Get keyboard scancode.", NULL, NULL},
    {"sdl.Event.key_keycode", "ev.key_keycode() -> i32", "Get keyboard keycode.", NULL, NULL},
    {"sdl.Event.key_mod", "ev.key_mod() -> i32", "Get keyboard modifier state.", NULL, NULL},
    {"sdl.Event.key_repeat", "ev.key_repeat() -> bool", "Check if key event is a repeat.", NULL, NULL},
    {"sdl.Event.mouse_x", "ev.mouse_x() -> f64", "Get mouse X position.", NULL, NULL},
    {"sdl.Event.mouse_y", "ev.mouse_y() -> f64", "Get mouse Y position.", NULL, NULL},
    {"sdl.Event.mouse_button", "ev.mouse_button() -> i32", "Get mouse button number.", NULL, NULL},
    {"sdl.Event.wheel_x", "ev.wheel_x() -> f64", "Get mouse wheel horizontal scroll.", NULL, NULL},
    {"sdl.Event.wheel_y", "ev.wheel_y() -> f64", "Get mouse wheel vertical scroll.", NULL, NULL},
    {"sdl.EVENT_QUIT", "sdl.EVENT_QUIT() -> i32", "Quit event type.", NULL, NULL},
    {"sdl.EVENT_KEY_DOWN", "sdl.EVENT_KEY_DOWN() -> i32", "Key press event type.", NULL, NULL},
    {"sdl.EVENT_KEY_UP", "sdl.EVENT_KEY_UP() -> i32", "Key release event type.", NULL, NULL},
    {"sdl.EVENT_MOUSE_MOTION", "sdl.EVENT_MOUSE_MOTION() -> i32", "Mouse motion event type.", NULL, NULL},
    {"sdl.EVENT_MOUSE_BUTTON_DOWN", "sdl.EVENT_MOUSE_BUTTON_DOWN() -> i32", "Mouse button press event type.", NULL,
     NULL},
    {"sdl.EVENT_MOUSE_BUTTON_UP", "sdl.EVENT_MOUSE_BUTTON_UP() -> i32", "Mouse button release event type.", NULL, NULL},
    {"sdl.EVENT_MOUSE_WHEEL", "sdl.EVENT_MOUSE_WHEEL() -> i32", "Mouse wheel event type.", NULL, NULL},
    /* Keyboard / mouse queries (slice 5) */
    {"sdl.is_key_pressed", "sdl.is_key_pressed(scancode: i32) -> bool", "Check if a key is currently pressed.",
     "Uses SDL keyboard state snapshot. Pass a SCANCODE_* constant.", "if (sdl.is_key_pressed(sdl.SCANCODE_W())) { }"},
    {"sdl.get_mod_state", "sdl.get_mod_state() -> i32", "Get current keyboard modifier state.", NULL, NULL},
    {"sdl.get_key_name", "sdl.get_key_name(keycode: i32) -> string", "Get human-readable name for a keycode.", NULL,
     NULL},
    {"sdl.get_scancode_name", "sdl.get_scancode_name(scancode: i32) -> string",
     "Get human-readable name for a scancode.", NULL, NULL},
    {"sdl.get_mouse_state", "sdl.get_mouse_state() -> (f64, f64)", "Get mouse position relative to the focused window.",
     "Returns (x, y). Use get_mouse_buttons() for button state.", NULL},
    {"sdl.get_mouse_buttons", "sdl.get_mouse_buttons() -> i32", "Get mouse button bitmask.",
     "Test with BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT constants.", NULL},
    {"sdl.get_global_mouse_state", "sdl.get_global_mouse_state() -> (f64, f64)", "Get desktop-relative mouse position.",
     "Returns (x, y). Use get_mouse_buttons() for button state.", NULL},
    {"sdl.warp_mouse", "sdl.warp_mouse(win: sdl.Window, x: f64, y: f64) -> void",
     "Move the mouse cursor within a window.", NULL, NULL},
    {"sdl.show_cursor", "sdl.show_cursor() -> bool", "Show the mouse cursor.", NULL, NULL},
    {"sdl.hide_cursor", "sdl.hide_cursor() -> bool", "Hide the mouse cursor.", NULL, NULL},
    {"sdl.cursor_visible", "sdl.cursor_visible() -> bool", "Check if the cursor is visible.", NULL, NULL},
    {"sdl.delay", "sdl.delay(ms: i32) -> void", "Wait for the specified number of milliseconds.", NULL, NULL},
    /* Timer / timing (slice 7) */
    {"sdl.get_ticks", "sdl.get_ticks() -> i64", "Milliseconds since SDL_Init.", NULL, NULL},
    {"sdl.get_ticks_ns", "sdl.get_ticks_ns() -> i64", "Nanoseconds since SDL_Init.", NULL, NULL},
    {"sdl.get_performance_counter", "sdl.get_performance_counter() -> i64", "High-resolution counter value.",
     "Divide by get_performance_frequency() for seconds.", NULL},
    {"sdl.get_performance_frequency", "sdl.get_performance_frequency() -> i64",
     "High-resolution counter frequency (counts per second).", NULL, NULL},
    {"sdl.delay_ns", "sdl.delay_ns(ns: i64) -> void", "Wait for the specified number of nanoseconds.", NULL, NULL},
    {"sdl.delay_precise", "sdl.delay_precise(ns: i64) -> void", "Precise nanosecond delay using busy-wait.",
     "More accurate than delay_ns but uses more CPU.", NULL},
    {"sdl.SCANCODE_A", "sdl.SCANCODE_A() -> i32", "Scancode for the A key.", NULL, NULL},
    {"sdl.SCANCODE_W", "sdl.SCANCODE_W() -> i32", "Scancode for the W key.", NULL, NULL},
    {"sdl.SCANCODE_S", "sdl.SCANCODE_S() -> i32", "Scancode for the S key.", NULL, NULL},
    {"sdl.SCANCODE_D", "sdl.SCANCODE_D() -> i32", "Scancode for the D key.", NULL, NULL},
    {"sdl.SCANCODE_UP", "sdl.SCANCODE_UP() -> i32", "Scancode for the Up arrow key.", NULL, NULL},
    {"sdl.SCANCODE_DOWN", "sdl.SCANCODE_DOWN() -> i32", "Scancode for the Down arrow key.", NULL, NULL},
    {"sdl.SCANCODE_LEFT", "sdl.SCANCODE_LEFT() -> i32", "Scancode for the Left arrow key.", NULL, NULL},
    {"sdl.SCANCODE_RIGHT", "sdl.SCANCODE_RIGHT() -> i32", "Scancode for the Right arrow key.", NULL, NULL},
    {"sdl.SCANCODE_SPACE", "sdl.SCANCODE_SPACE() -> i32", "Scancode for the Space key.", NULL, NULL},
    {"sdl.SCANCODE_ESCAPE", "sdl.SCANCODE_ESCAPE() -> i32", "Scancode for the Escape key.", NULL, NULL},
    {"sdl.SCANCODE_RETURN", "sdl.SCANCODE_RETURN() -> i32", "Scancode for the Return/Enter key.", NULL, NULL},
    {"sdl.KEY_RETURN", "sdl.KEY_RETURN() -> i32", "Keycode for Return/Enter.", NULL, NULL},
    {"sdl.KEY_ESCAPE", "sdl.KEY_ESCAPE() -> i32", "Keycode for Escape.", NULL, NULL},
    {"sdl.KEY_SPACE", "sdl.KEY_SPACE() -> i32", "Keycode for Space.", NULL, NULL},
    {"sdl.BUTTON_LEFT", "sdl.BUTTON_LEFT() -> i32", "Left mouse button constant.", NULL, NULL},
    {"sdl.BUTTON_MIDDLE", "sdl.BUTTON_MIDDLE() -> i32", "Middle mouse button constant.", NULL, NULL},
    {"sdl.BUTTON_RIGHT", "sdl.BUTTON_RIGHT() -> i32", "Right mouse button constant.", NULL, NULL},
    /* Surface class (slice 6) */
    {"sdl.Surface", NULL, "SDL surface handle.", "Load with sdl.Surface.load() or sdl.Surface.load_bmp().", NULL},
    {"sdl.Surface.load", "sdl.Surface.load(path: string) -> (sdl.Surface, err)", "Load a BMP or PNG image from a file.",
     NULL, "sdl.Surface surf, err e = sdl.Surface.load(\"sprite.png\")"},
    {"sdl.Surface.load_bmp", "sdl.Surface.load_bmp(path: string) -> (sdl.Surface, err)",
     "Load a BMP image from a file.", NULL, NULL},
    {"sdl.Surface.destroy", "surf.destroy() -> void", "Free a surface.", NULL, NULL},
    /* Texture class (slice 6) */
    {"sdl.Texture", NULL, "SDL texture handle.", "Create with sdl.Texture.from_surface() or sdl.Texture.create().",
     NULL},
    {"sdl.Texture.create",
     "sdl.Texture.create(ren: sdl.Renderer, format: i32, access: i32, w: i32, h: i32) -> (sdl.Texture, err)",
     "Create a blank texture.", NULL, NULL},
    {"sdl.Texture.from_surface", "sdl.Texture.from_surface(ren: sdl.Renderer, surf: sdl.Surface) -> (sdl.Texture, err)",
     "Create a texture from a surface.", NULL, "sdl.Texture tex, err e = sdl.Texture.from_surface(ren, surf)"},
    {"sdl.Texture.destroy", "tex.destroy() -> void", "Destroy a texture.", NULL, NULL},
    {"sdl.Texture.get_size", "tex.get_size() -> (f64, f64)", "Get texture size (w, h).", NULL, NULL},
    {"sdl.Texture.set_color_mod", "tex.set_color_mod(r: i32, g: i32, b: i32) -> void", "Set color modulation (0-255).",
     NULL, NULL},
    {"sdl.Texture.set_alpha_mod", "tex.set_alpha_mod(alpha: i32) -> void", "Set alpha modulation (0-255).", NULL, NULL},
    {"sdl.Texture.set_blend_mode", "tex.set_blend_mode(mode: i32) -> void", "Set blend mode.",
     "Use BLENDMODE_* constants.", NULL},
    {"sdl.Renderer.render_texture",
     "ren.render_texture(tex: sdl.Texture, sx: f64, sy: f64, sw: f64, sh: f64, dx: f64, dy: f64, dw: f64, dh: f64) "
     "-> (bool, err)",
     "Render a texture.", "Pass all-zero src rect to use full texture. Pass all-zero dst rect for full target.", NULL},
    {"sdl.Renderer.render_texture_rotated",
     "ren.render_texture_rotated(tex, sx, sy, sw, sh, dx, dy, dw, dh, angle, cx, cy, flip) -> (bool, err)",
     "Render a texture with rotation and flip.", "Use FLIP_* constants for flip parameter.", NULL},
    {"sdl.TEXTUREACCESS_STATIC", "sdl.TEXTUREACCESS_STATIC() -> i32", "Static texture access.", NULL, NULL},
    {"sdl.TEXTUREACCESS_STREAMING", "sdl.TEXTUREACCESS_STREAMING() -> i32", "Streaming texture access.", NULL, NULL},
    {"sdl.TEXTUREACCESS_TARGET", "sdl.TEXTUREACCESS_TARGET() -> i32", "Render target texture access.", NULL, NULL},
    {"sdl.BLENDMODE_NONE", "sdl.BLENDMODE_NONE() -> i32", "No blending.", NULL, NULL},
    {"sdl.BLENDMODE_BLEND", "sdl.BLENDMODE_BLEND() -> i32", "Alpha blending.", NULL, NULL},
    {"sdl.BLENDMODE_ADD", "sdl.BLENDMODE_ADD() -> i32", "Additive blending.", NULL, NULL},
    {"sdl.BLENDMODE_MOD", "sdl.BLENDMODE_MOD() -> i32", "Color modulate blending.", NULL, NULL},
    {"sdl.BLENDMODE_MUL", "sdl.BLENDMODE_MUL() -> i32", "Color multiply blending.", NULL, NULL},
    {"sdl.FLIP_NONE", "sdl.FLIP_NONE() -> i32", "No flip.", NULL, NULL},
    {"sdl.FLIP_HORIZONTAL", "sdl.FLIP_HORIZONTAL() -> i32", "Flip horizontally.", NULL, NULL},
    {"sdl.FLIP_VERTICAL", "sdl.FLIP_VERTICAL() -> i32", "Flip vertically.", NULL, NULL},
    /* Audio (slice 8) */
    {"sdl.load_wav", "sdl.load_wav(path: string) -> (i64, err)", "Load a WAV file.",
     "Returns a wav handle. Query format with wav_format/wav_channels/wav_freq.",
     "i64 wav, err e = sdl.load_wav(\"sound.wav\")"},
    {"sdl.wav_free", "sdl.wav_free(handle: i64) -> void", "Free a loaded WAV buffer.", NULL, NULL},
    {"sdl.wav_format", "sdl.wav_format(handle: i64) -> i32", "Get WAV audio format.", NULL, NULL},
    {"sdl.wav_channels", "sdl.wav_channels(handle: i64) -> i32", "Get WAV channel count.", NULL, NULL},
    {"sdl.wav_freq", "sdl.wav_freq(handle: i64) -> i32", "Get WAV sample rate.", NULL, NULL},
    {"sdl.AudioStream", NULL, "SDL audio stream for playback.",
     "Open with sdl.AudioStream.open(). Push WAV data with put_wav().", NULL},
    {"sdl.AudioStream.open", "sdl.AudioStream.open(format: i32, channels: i32, freq: i32) -> (sdl.AudioStream, err)",
     "Open the default playback device.", "Use AUDIO_* constants for format.",
     "sdl.AudioStream stream, err e = sdl.AudioStream.open(sdl.AUDIO_S16(), 2, 44100)"},
    {"sdl.AudioStream.destroy", "stream.destroy() -> void", "Destroy an audio stream.", NULL, NULL},
    {"sdl.AudioStream.put_wav", "stream.put_wav(wav_handle: i64) -> (bool, err)", "Queue WAV data for playback.", NULL,
     NULL},
    {"sdl.AudioStream.get_queued", "stream.get_queued() -> i32", "Get bytes queued for playback.", NULL, NULL},
    {"sdl.AudioStream.resume", "stream.resume() -> (bool, err)", "Start/resume playback.", NULL, NULL},
    {"sdl.AudioStream.pause", "stream.pause() -> (bool, err)", "Pause playback.", NULL, NULL},
    {"sdl.AUDIO_S16", "sdl.AUDIO_S16() -> i32", "Signed 16-bit audio format.", NULL, NULL},
    {"sdl.AUDIO_S32", "sdl.AUDIO_S32() -> i32", "Signed 32-bit audio format.", NULL, NULL},
    {"sdl.AUDIO_F32", "sdl.AUDIO_F32() -> i32", "32-bit float audio format.", NULL, NULL},
    /* Gamepad (slice 9) */
    {"sdl.has_gamepad", "sdl.has_gamepad() -> bool", "Check if any gamepad is connected.", NULL, NULL},
    {"sdl.get_gamepad_count", "sdl.get_gamepad_count() -> i32", "Get the number of connected gamepads.",
     "Also refreshes the internal gamepad list.", NULL},
    {"sdl.get_gamepad_id", "sdl.get_gamepad_id(index: i32) -> i32", "Get the instance ID of a gamepad by index.",
     "Call get_gamepad_count() first to refresh the list.", NULL},
    {"sdl.Gamepad", NULL, "SDL gamepad handle.", "Open with sdl.Gamepad.open(instance_id).", NULL},
    {"sdl.Gamepad.open", "sdl.Gamepad.open(instance_id: i32) -> (sdl.Gamepad, err)", "Open a gamepad.", NULL,
     "sdl.Gamepad gp, err e = sdl.Gamepad.open(sdl.get_gamepad_id(0))"},
    {"sdl.Gamepad.close", "gp.close() -> void", "Close a gamepad.", NULL, NULL},
    {"sdl.Gamepad.get_name", "gp.get_name() -> string", "Get gamepad name.", NULL, NULL},
    {"sdl.Gamepad.get_axis", "gp.get_axis(axis: i32) -> i32", "Get axis value (-32768 to 32767).",
     "Use GAMEPAD_AXIS_* constants.", NULL},
    {"sdl.Gamepad.get_button", "gp.get_button(button: i32) -> bool", "Check if a button is pressed.",
     "Use GAMEPAD_BUTTON_* constants.", NULL},
    {"sdl.Gamepad.rumble", "gp.rumble(low: i32, high: i32, duration_ms: i32) -> (bool, err)", "Start rumble effect.",
     NULL, NULL},
    {"sdl.Event.gamepad_which", "ev.gamepad_which() -> i32", "Get gamepad instance ID from event.", NULL, NULL},
    {"sdl.Event.gamepad_axis", "ev.gamepad_axis() -> i32", "Get gamepad axis from event.", NULL, NULL},
    {"sdl.Event.gamepad_axis_value", "ev.gamepad_axis_value() -> i32", "Get gamepad axis value from event.", NULL,
     NULL},
    {"sdl.Event.gamepad_button", "ev.gamepad_button() -> i32", "Get gamepad button from event.", NULL, NULL},
    {"sdl.GAMEPAD_AXIS_LEFTX", "sdl.GAMEPAD_AXIS_LEFTX() -> i32", "Left stick X axis.", NULL, NULL},
    {"sdl.GAMEPAD_AXIS_LEFTY", "sdl.GAMEPAD_AXIS_LEFTY() -> i32", "Left stick Y axis.", NULL, NULL},
    {"sdl.GAMEPAD_AXIS_RIGHTX", "sdl.GAMEPAD_AXIS_RIGHTX() -> i32", "Right stick X axis.", NULL, NULL},
    {"sdl.GAMEPAD_AXIS_RIGHTY", "sdl.GAMEPAD_AXIS_RIGHTY() -> i32", "Right stick Y axis.", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_SOUTH", "sdl.GAMEPAD_BUTTON_SOUTH() -> i32", "South face button (A/Cross).", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_EAST", "sdl.GAMEPAD_BUTTON_EAST() -> i32", "East face button (B/Circle).", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_WEST", "sdl.GAMEPAD_BUTTON_WEST() -> i32", "West face button (X/Square).", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_NORTH", "sdl.GAMEPAD_BUTTON_NORTH() -> i32", "North face button (Y/Triangle).", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_START", "sdl.GAMEPAD_BUTTON_START() -> i32", "Start button.", NULL, NULL},
    {"sdl.EVENT_GAMEPAD_AXIS_MOTION", "sdl.EVENT_GAMEPAD_AXIS_MOTION() -> i32", "Gamepad axis motion event.", NULL,
     NULL},
    {"sdl.EVENT_GAMEPAD_BUTTON_DOWN", "sdl.EVENT_GAMEPAD_BUTTON_DOWN() -> i32", "Gamepad button press event.", NULL,
     NULL},
    {"sdl.EVENT_GAMEPAD_BUTTON_UP", "sdl.EVENT_GAMEPAD_BUTTON_UP() -> i32", "Gamepad button release event.", NULL,
     NULL},
    /* Clipboard, MessageBox, Misc (slice 10) */
    {"sdl.set_clipboard_text", "sdl.set_clipboard_text(text: string) -> (bool, err)", "Copy text to the clipboard.",
     NULL, NULL},
    {"sdl.get_clipboard_text", "sdl.get_clipboard_text() -> string", "Get text from the clipboard.", NULL, NULL},
    {"sdl.has_clipboard_text", "sdl.has_clipboard_text() -> bool", "Check if clipboard has text.", NULL, NULL},
    {"sdl.show_simple_message_box",
     "sdl.show_simple_message_box(flags: i32, title: string, message: string) -> (bool, err)",
     "Show a modal message box.", "Use MESSAGEBOX_* constants for flags.",
     "sdl.show_simple_message_box(sdl.MESSAGEBOX_INFORMATION(), \"Hello\", \"World\")"},
    {"sdl.open_url", "sdl.open_url(url: string) -> (bool, err)", "Open a URL in the default browser.", NULL, NULL},
    {"sdl.get_base_path", "sdl.get_base_path() -> string", "Get the application base directory.", NULL, NULL},
    {"sdl.get_pref_path", "sdl.get_pref_path(org: string, app: string) -> string",
     "Get the user preferences directory.", NULL, NULL},
    {"sdl.get_num_cpu_cores", "sdl.get_num_cpu_cores() -> i32", "Get the number of logical CPU cores.", NULL, NULL},
    {"sdl.get_system_ram", "sdl.get_system_ram() -> i32", "Get system RAM in MB.", NULL, NULL},
    {"sdl.log", "sdl.log(msg: string) -> void", "Log an info message via SDL.", NULL, NULL},
    {"sdl.log_error", "sdl.log_error(msg: string) -> void", "Log an error message via SDL.", NULL, NULL},
    {"sdl.log_warn", "sdl.log_warn(msg: string) -> void", "Log a warning message via SDL.", NULL, NULL},
    {"sdl.MESSAGEBOX_ERROR", "sdl.MESSAGEBOX_ERROR() -> i32", "Error message box flag.", NULL, NULL},
    {"sdl.MESSAGEBOX_WARNING", "sdl.MESSAGEBOX_WARNING() -> i32", "Warning message box flag.", NULL, NULL},
    {"sdl.MESSAGEBOX_INFORMATION", "sdl.MESSAGEBOX_INFORMATION() -> i32", "Info message box flag.", NULL, NULL},
    /* Renderer extras & display info (slice 11) */
    {"sdl.Renderer.set_target", "ren.set_target(tex_handle: i64) -> (bool, err)", "Set a texture as the render target.",
     "Pass -1 to reset to the default target (the window).", "ren.set_target(tex_handle)"},
    {"sdl.Renderer.set_scale", "ren.set_scale(sx: f64, sy: f64) -> (bool, err)", "Set the drawing scale.", NULL, NULL},
    {"sdl.Renderer.get_scale", "ren.get_scale() -> (f64, f64)", "Get the current drawing scale.", NULL, NULL},
    {"sdl.Renderer.set_clip_rect", "ren.set_clip_rect(x: i32, y: i32, w: i32, h: i32) -> (bool, err)",
     "Set the clip rectangle.", "Pass all zeros to clear the clip rect.", NULL},
    {"sdl.Renderer.set_logical_size", "ren.set_logical_size(w: i32, h: i32, mode: i32) -> (bool, err)",
     "Set device-independent resolution.", "Use LOGICAL_* constants for mode.", NULL},
    {"sdl.get_display_count", "sdl.get_display_count() -> i32", "Get the number of connected displays.", NULL, NULL},
    {"sdl.get_display_name", "sdl.get_display_name(index: i32) -> string", "Get display name.", NULL, NULL},
    {"sdl.get_display_bounds", "sdl.get_display_bounds(index: i32) -> (i32, i32)", "Get display size (w, h).",
     "Call get_display_count() first to refresh the list.", NULL},
    {"sdl.LOGICAL_DISABLED", "sdl.LOGICAL_DISABLED() -> i32", "No logical presentation.", NULL, NULL},
    {"sdl.LOGICAL_STRETCH", "sdl.LOGICAL_STRETCH() -> i32", "Stretch to fill.", NULL, NULL},
    {"sdl.LOGICAL_LETTERBOX", "sdl.LOGICAL_LETTERBOX() -> i32", "Letterbox to fit.", NULL, NULL},
    {"sdl.LOGICAL_OVERSCAN", "sdl.LOGICAL_OVERSCAN() -> i32", "Overscan to fill.", NULL, NULL},
    {"sdl.LOGICAL_INTEGER_SCALE", "sdl.LOGICAL_INTEGER_SCALE() -> i32", "Integer scaling.", NULL, NULL},
    /* Renderer drawing extras (slice 12) */
    {"sdl.Renderer.render_debug_text", "ren.render_debug_text(x: f64, y: f64, text: string) -> (bool, err)",
     "Draw debug text.", "Uses SDL's built-in 8x8 font. Set color with set_draw_color first.",
     "ren.render_debug_text(10.0, 10.0, \"FPS: 60\")"},
    {"sdl.Renderer.set_viewport", "ren.set_viewport(x: i32, y: i32, w: i32, h: i32) -> (bool, err)",
     "Set the drawing area.", "Pass all zeros to reset to the full target.", NULL},
    {"sdl.Renderer.set_draw_blend_mode", "ren.set_draw_blend_mode(mode: i32) -> (bool, err)",
     "Set blend mode for draw operations.", "Use BLENDMODE_* constants.", NULL},
    {"sdl.Renderer.get_draw_blend_mode", "ren.get_draw_blend_mode() -> i32", "Get current draw blend mode.", NULL,
     NULL},
    {"sdl.Renderer.set_color_scale", "ren.set_color_scale(scale: f64) -> (bool, err)",
     "Set color scale for render operations.", NULL, NULL},
    {"sdl.Renderer.get_color_scale", "ren.get_color_scale() -> f64", "Get current color scale.", NULL, NULL},
    {"sdl.Renderer.flush", "ren.flush() -> (bool, err)", "Flush pending render commands.", NULL, NULL},
    {"sdl.Renderer.get_output_size", "ren.get_output_size() -> (i32, i32)", "Get renderer output size in pixels.", NULL,
     NULL},
    {"sdl.Renderer.get_current_output_size", "ren.get_current_output_size() -> (i32, i32)",
     "Get current output size in pixels.", NULL, NULL},
    {"sdl.Renderer.get_name", "ren.get_name() -> string", "Get renderer driver name.", NULL, NULL},
    /* Extended window, display, power (slice 13) */
    {"sdl.Window.flash", "win.flash(op: i32) -> (bool, err)", "Flash the window taskbar entry.",
     "Use FLASH_* constants.", NULL},
    {"sdl.Window.set_icon", "win.set_icon(surf: sdl.Surface) -> (bool, err)", "Set the window icon.", NULL, NULL},
    {"sdl.Window.set_opacity", "win.set_opacity(opacity: f64) -> (bool, err)", "Set window opacity (0.0-1.0).", NULL,
     NULL},
    {"sdl.Window.get_opacity", "win.get_opacity() -> f64", "Get window opacity.", NULL, NULL},
    {"sdl.Window.set_min_size", "win.set_min_size(w: i32, h: i32) -> (bool, err)", "Set minimum window size.", NULL,
     NULL},
    {"sdl.Window.get_min_size", "win.get_min_size() -> (i32, i32)", "Get minimum window size.", NULL, NULL},
    {"sdl.Window.set_max_size", "win.set_max_size(w: i32, h: i32) -> (bool, err)", "Set maximum window size.", NULL,
     NULL},
    {"sdl.Window.get_max_size", "win.get_max_size() -> (i32, i32)", "Get maximum window size.", NULL, NULL},
    {"sdl.Window.set_always_on_top", "win.set_always_on_top(on: bool) -> (bool, err)", "Set always-on-top.", NULL,
     NULL},
    {"sdl.Window.set_mouse_grab", "win.set_mouse_grab(grabbed: bool) -> (bool, err)", "Confine mouse to window.", NULL,
     NULL},
    {"sdl.Window.set_keyboard_grab", "win.set_keyboard_grab(grabbed: bool) -> (bool, err)", "Grab keyboard input.",
     NULL, NULL},
    {"sdl.Window.get_size_in_pixels", "win.get_size_in_pixels() -> (i32, i32)", "Get size in pixels (HiDPI-aware).",
     NULL, NULL},
    {"sdl.Window.get_display_scale", "win.get_display_scale() -> f64", "Get display content scale.", NULL, NULL},
    {"sdl.Window.set_relative_mouse", "win.set_relative_mouse(enabled: bool) -> (bool, err)",
     "Enable relative mouse mode (FPS-style).", NULL, NULL},
    {"sdl.get_primary_display", "sdl.get_primary_display() -> i32", "Get primary display index.", NULL, NULL},
    {"sdl.get_display_content_scale", "sdl.get_display_content_scale(index: i32) -> f64", "Get display DPI scale.",
     NULL, NULL},
    {"sdl.get_display_usable_bounds", "sdl.get_display_usable_bounds(index: i32) -> (i32, i32)",
     "Get usable display area (w, h), excluding taskbar.", NULL, NULL},
    {"sdl.get_power_info", "sdl.get_power_info() -> (i32, i32)", "Get battery info (percent, seconds).",
     "Returns -1 for unknown values.", NULL},
    {"sdl.screen_saver_enabled", "sdl.screen_saver_enabled() -> bool", "Check if screen saver is enabled.", NULL, NULL},
    {"sdl.enable_screen_saver", "sdl.enable_screen_saver() -> bool", "Enable the screen saver.", NULL, NULL},
    {"sdl.disable_screen_saver", "sdl.disable_screen_saver() -> bool", "Disable the screen saver.", NULL, NULL},
    {"sdl.FLASH_CANCEL", "sdl.FLASH_CANCEL() -> i32", "Cancel window flash.", NULL, NULL},
    {"sdl.FLASH_BRIEFLY", "sdl.FLASH_BRIEFLY() -> i32", "Flash window briefly.", NULL, NULL},
    {"sdl.FLASH_UNTIL_FOCUSED", "sdl.FLASH_UNTIL_FOCUSED() -> i32", "Flash until focused.", NULL, NULL},
    /* Extended audio (slice 14) */
    {"sdl.get_audio_playback_count", "sdl.get_audio_playback_count() -> i32", "Get number of audio playback devices.",
     NULL, NULL},
    {"sdl.get_audio_device_name", "sdl.get_audio_device_name(index: i32) -> string", "Get audio device name by index.",
     NULL, NULL},
    {"sdl.get_current_audio_driver", "sdl.get_current_audio_driver() -> string", "Get the current audio driver name.",
     NULL, NULL},
    {"sdl.AudioStream.set_gain", "stream.set_gain(gain: f64) -> (bool, err)", "Set audio stream gain.", NULL, NULL},
    {"sdl.AudioStream.get_gain", "stream.get_gain() -> f64", "Get audio stream gain.", NULL, NULL},
    {"sdl.AudioStream.set_freq_ratio", "stream.set_freq_ratio(ratio: f64) -> (bool, err)",
     "Set frequency ratio (pitch).", "1.0 = normal, 2.0 = double speed.", NULL},
    {"sdl.AudioStream.get_freq_ratio", "stream.get_freq_ratio() -> f64", "Get frequency ratio.", NULL, NULL},
    {"sdl.AudioStream.get_available", "stream.get_available() -> i32", "Get available converted bytes.", NULL, NULL},
    {"sdl.AudioStream.flush", "stream.flush() -> (bool, err)", "Flush buffered audio data.", NULL, NULL},
    {"sdl.AudioStream.clear", "stream.clear() -> (bool, err)", "Clear pending audio data.", NULL, NULL},
    /* Cursor (slice 15) */
    {"sdl.create_system_cursor", "sdl.create_system_cursor(id: i32) -> (i64, err)", "Create a system cursor.",
     "Use CURSOR_* constants. Returns cursor handle.", NULL},
    {"sdl.create_color_cursor", "sdl.create_color_cursor(surf: sdl.Surface, hot_x: i32, hot_y: i32) -> (i64, err)",
     "Create a color cursor from a surface.", NULL, NULL},
    {"sdl.set_cursor", "sdl.set_cursor(handle: i64) -> (bool, err)", "Set the active cursor.", NULL, NULL},
    {"sdl.destroy_cursor", "sdl.destroy_cursor(handle: i64) -> void", "Free a cursor.", NULL, NULL},
    {"sdl.capture_mouse", "sdl.capture_mouse(enabled: bool) -> (bool, err)", "Capture mouse input outside the window.",
     NULL, NULL},
    {"sdl.get_relative_mouse_state", "sdl.get_relative_mouse_state() -> (f64, f64)", "Get mouse delta since last call.",
     NULL, NULL},
    {"sdl.warp_mouse_global", "sdl.warp_mouse_global(x: f64, y: f64) -> (bool, err)",
     "Move cursor to global screen position.", NULL, NULL},
    {"sdl.CURSOR_DEFAULT", "sdl.CURSOR_DEFAULT() -> i32", "Default arrow cursor.", NULL, NULL},
    {"sdl.CURSOR_TEXT", "sdl.CURSOR_TEXT() -> i32", "Text I-beam cursor.", NULL, NULL},
    {"sdl.CURSOR_WAIT", "sdl.CURSOR_WAIT() -> i32", "Wait/hourglass cursor.", NULL, NULL},
    {"sdl.CURSOR_CROSSHAIR", "sdl.CURSOR_CROSSHAIR() -> i32", "Crosshair cursor.", NULL, NULL},
    {"sdl.CURSOR_POINTER", "sdl.CURSOR_POINTER() -> i32", "Pointing hand cursor.", NULL, NULL},
    {"sdl.CURSOR_MOVE", "sdl.CURSOR_MOVE() -> i32", "Move/drag cursor.", NULL, NULL},
    {"sdl.CURSOR_NOT_ALLOWED", "sdl.CURSOR_NOT_ALLOWED() -> i32", "Not-allowed cursor.", NULL, NULL},
    /* Events (slice 16) */
    {"sdl.pump_events", "sdl.pump_events() -> void", "Pump the event loop.", NULL, NULL},
    {"sdl.has_event", "sdl.has_event(type: i32) -> bool", "Check if event type is queued.", NULL, NULL},
    {"sdl.flush_event", "sdl.flush_event(type: i32) -> void", "Clear events of a specific type.", NULL, NULL},
    {"sdl.flush_events", "sdl.flush_events(min_type: i32, max_type: i32) -> void", "Clear events in a type range.",
     NULL, NULL},
    {"sdl.event_enabled", "sdl.event_enabled(type: i32) -> bool", "Check if event type is enabled.", NULL, NULL},
    {"sdl.set_event_enabled", "sdl.set_event_enabled(type: i32, enabled: bool) -> void",
     "Enable or disable an event type.", NULL, NULL},
    /* Renderer getters (slice 17) */
    {"sdl.Renderer.get_vsync", "ren.get_vsync() -> i32", "Get VSync mode.", NULL, NULL},
    {"sdl.Renderer.clip_enabled", "ren.clip_enabled() -> bool", "Check if clipping is active.", NULL, NULL},
    {"sdl.Renderer.get_viewport", "ren.get_viewport() -> (i32, i32)", "Get viewport size (w, h).", NULL, NULL},
    /* Texture extras (slice 18) */
    {"sdl.Texture.get_color_mod", "tex.get_color_mod() -> i32", "Get color mod as packed 0xRRGGBB.", NULL, NULL},
    {"sdl.Texture.get_alpha_mod", "tex.get_alpha_mod() -> i32", "Get alpha mod (0-255).", NULL, NULL},
    {"sdl.Texture.get_blend_mode", "tex.get_blend_mode() -> i32", "Get blend mode.", NULL, NULL},
    {"sdl.Texture.set_scale_mode", "tex.set_scale_mode(mode: i32) -> (bool, err)", "Set texture scaling filter.",
     "Use SCALEMODE_* constants.", NULL},
    {"sdl.Texture.get_scale_mode", "tex.get_scale_mode() -> i32", "Get texture scaling filter.", NULL, NULL},
    {"sdl.SCALEMODE_NEAREST", "sdl.SCALEMODE_NEAREST() -> i32", "Nearest-pixel sampling.", NULL, NULL},
    {"sdl.SCALEMODE_LINEAR", "sdl.SCALEMODE_LINEAR() -> i32", "Linear filtering.", NULL, NULL},
    /* System info (slice 19) */
    {"sdl.get_current_time", "sdl.get_current_time() -> i64", "Get system time in nanoseconds since Unix epoch.", NULL,
     NULL},
    {"sdl.get_user_folder", "sdl.get_user_folder(folder: i32) -> string", "Get a user folder path.",
     "Use FOLDER_* constants.", "sdl.get_user_folder(sdl.FOLDER_DOCUMENTS())"},
    {"sdl.get_system_theme", "sdl.get_system_theme() -> i32", "Get system light/dark theme.",
     "Use SYSTEM_THEME_* constants.", NULL},
    {"sdl.is_tablet", "sdl.is_tablet() -> bool", "Check if running on a tablet.", NULL, NULL},
    {"sdl.is_tv", "sdl.is_tv() -> bool", "Check if running on a TV.", NULL, NULL},
    {"sdl.set_app_metadata", "sdl.set_app_metadata(name: string, version: string, id: string) -> (bool, err)",
     "Set application metadata.", NULL, NULL},
    {"sdl.get_current_video_driver", "sdl.get_current_video_driver() -> string", "Get the current video driver name.",
     NULL, NULL},
    {"sdl.SYSTEM_THEME_UNKNOWN", "sdl.SYSTEM_THEME_UNKNOWN() -> i32", "Unknown theme.", NULL, NULL},
    {"sdl.SYSTEM_THEME_LIGHT", "sdl.SYSTEM_THEME_LIGHT() -> i32", "Light theme.", NULL, NULL},
    {"sdl.SYSTEM_THEME_DARK", "sdl.SYSTEM_THEME_DARK() -> i32", "Dark theme.", NULL, NULL},
    {"sdl.FOLDER_HOME", "sdl.FOLDER_HOME() -> i32", "User home folder.", NULL, NULL},
    {"sdl.FOLDER_DOCUMENTS", "sdl.FOLDER_DOCUMENTS() -> i32", "Documents folder.", NULL, NULL},
    {"sdl.FOLDER_DOWNLOADS", "sdl.FOLDER_DOWNLOADS() -> i32", "Downloads folder.", NULL, NULL},
    {"sdl.FOLDER_PICTURES", "sdl.FOLDER_PICTURES() -> i32", "Pictures folder.", NULL, NULL},
    {"sdl.FOLDER_SAVEDGAMES", "sdl.FOLDER_SAVEDGAMES() -> i32", "Saved games folder.", NULL, NULL},
    /* Surface operations (slice 20) */
    {"sdl.Surface.clear", "surf.clear(r: f64, g: f64, b: f64, a: f64) -> (bool, err)",
     "Clear surface with a color (0.0-1.0 per channel).", NULL, NULL},
    {"sdl.Surface.fill_rect", "surf.fill_rect(x: i32, y: i32, w: i32, h: i32, color: i32) -> (bool, err)",
     "Fill a rectangle with a packed pixel color.", NULL, NULL},
    {"sdl.Surface.flip", "surf.flip(mode: i32) -> (bool, err)", "Flip surface. Use FLIP_* constants.", NULL, NULL},
    {"sdl.Surface.set_color_mod", "surf.set_color_mod(r: i32, g: i32, b: i32) -> (bool, err)",
     "Set color modulation for blits.", NULL, NULL},
    {"sdl.Surface.set_alpha_mod", "surf.set_alpha_mod(alpha: i32) -> (bool, err)", "Set alpha modulation for blits.",
     NULL, NULL},
    {"sdl.Surface.set_blend_mode", "surf.set_blend_mode(mode: i32) -> (bool, err)", "Set blend mode for blits.", NULL,
     NULL},
    {"sdl.Surface.set_color_key", "surf.set_color_key(enabled: bool, key: i32) -> (bool, err)",
     "Set transparent color key.", NULL, NULL},
    {"sdl.Surface.blit",
     "surf.blit(dst: sdl.Surface, sx: i32, sy: i32, sw: i32, sh: i32, dx: i32, dy: i32) -> (bool, err)",
     "Blit to another surface.", "Zero src size = full surface.", NULL},
    /* Window getters (slice 21) */
    {"sdl.Window.get_pixel_density", "win.get_pixel_density() -> f64", "Get pixel density (HiDPI).", NULL, NULL},
    {"sdl.Window.get_mouse_grab", "win.get_mouse_grab() -> bool", "Check if mouse is grabbed.", NULL, NULL},
    {"sdl.Window.get_keyboard_grab", "win.get_keyboard_grab() -> bool", "Check if keyboard is grabbed.", NULL, NULL},
    {"sdl.Window.get_relative_mouse", "win.get_relative_mouse() -> bool", "Check relative mouse mode.", NULL, NULL},
    {"sdl.Window.set_progress", "win.set_progress(state: i32, value: f64) -> (bool, err)", "Set taskbar progress bar.",
     "Use PROGRESS_* constants. Value 0.0-1.0.", NULL},
    {"sdl.Window.set_aspect_ratio", "win.set_aspect_ratio(min: f64, max: f64) -> (bool, err)",
     "Constrain window aspect ratio.", NULL, NULL},
    {"sdl.PROGRESS_NONE", "sdl.PROGRESS_NONE() -> i32", "No progress bar.", NULL, NULL},
    {"sdl.PROGRESS_NORMAL", "sdl.PROGRESS_NORMAL() -> i32", "Normal progress bar.", NULL, NULL},
    {"sdl.PROGRESS_INDETERMINATE", "sdl.PROGRESS_INDETERMINATE() -> i32", "Indeterminate progress.", NULL, NULL},
    {"sdl.PROGRESS_PAUSED", "sdl.PROGRESS_PAUSED() -> i32", "Paused progress.", NULL, NULL},
    {"sdl.PROGRESS_ERROR", "sdl.PROGRESS_ERROR() -> i32", "Error progress.", NULL, NULL},
    /* Gamepad extras (slice 22) */
    {"sdl.Gamepad.connected", "gp.connected() -> bool", "Check if gamepad is still connected.", NULL, NULL},
    {"sdl.Gamepad.get_type", "gp.get_type() -> i32", "Get gamepad type.", NULL, NULL},
    {"sdl.Gamepad.get_power_percent", "gp.get_power_percent() -> i32", "Get battery percent (-1 if unknown).", NULL,
     NULL},
    {"sdl.Gamepad.set_led", "gp.set_led(r: i32, g: i32, b: i32) -> (bool, err)", "Set LED color.", NULL, NULL},
    {"sdl.Gamepad.rumble_triggers", "gp.rumble_triggers(left: i32, right: i32, ms: i32) -> (bool, err)",
     "Rumble the triggers.", NULL, NULL},
    {"sdl.Gamepad.has_axis", "gp.has_axis(axis: i32) -> bool", "Check if gamepad has an axis.", NULL, NULL},
    {"sdl.Gamepad.has_button", "gp.has_button(button: i32) -> bool", "Check if gamepad has a button.", NULL, NULL},
    {"sdl.update_gamepads", "sdl.update_gamepads() -> void", "Manually pump gamepad updates.", NULL, NULL},
    /* Advanced surface (slice 23) */
    {"sdl.Surface.create", "sdl.Surface.create(w: i32, h: i32, format: i32) -> (sdl.Surface, err)",
     "Create a blank surface.", NULL, NULL},
    {"sdl.Surface.duplicate", "surf.duplicate() -> (sdl.Surface, err)", "Copy a surface.", NULL, NULL},
    {"sdl.Surface.scale", "surf.scale(w: i32, h: i32, mode: i32) -> (sdl.Surface, err)", "Scale to new size.",
     "Use SCALEMODE_* constants.", NULL},
    {"sdl.Surface.rotate", "surf.rotate(angle: f64) -> (sdl.Surface, err)", "Rotate (degrees).", NULL, NULL},
    {"sdl.Surface.save_bmp", "surf.save_bmp(path: string) -> (bool, err)", "Save as BMP.", NULL, NULL},
    {"sdl.Surface.save_png", "surf.save_png(path: string) -> (bool, err)", "Save as PNG.", NULL, NULL},
    {"sdl.Surface.read_pixel", "surf.read_pixel(x: i32, y: i32) -> i32", "Read pixel as packed 0xAARRGGBB.", NULL,
     NULL},
    {"sdl.Surface.write_pixel", "surf.write_pixel(x: i32, y: i32, r: i32, g: i32, b: i32, a: i32) -> (bool, err)",
     "Write a single pixel.", NULL, NULL},
    /* Keyboard/scancode lookups (slice 24) */
    {"sdl.get_key_from_scancode", "sdl.get_key_from_scancode(scancode: i32) -> i32", "Convert scancode to keycode.",
     NULL, NULL},
    {"sdl.get_scancode_from_key", "sdl.get_scancode_from_key(keycode: i32) -> i32", "Convert keycode to scancode.",
     NULL, NULL},
    {"sdl.get_key_from_name", "sdl.get_key_from_name(name: string) -> i32", "Get keycode from name.", NULL, NULL},
    {"sdl.get_scancode_from_name", "sdl.get_scancode_from_name(name: string) -> i32", "Get scancode from name.", NULL,
     NULL},
    {"sdl.has_keyboard", "sdl.has_keyboard() -> bool", "Check if a keyboard is connected.", NULL, NULL},
    {"sdl.has_mouse", "sdl.has_mouse() -> bool", "Check if a mouse is connected.", NULL, NULL},
    {"sdl.start_text_input", "sdl.start_text_input(win: sdl.Window) -> (bool, err)", "Start text input for a window.",
     NULL, NULL},
    {"sdl.stop_text_input", "sdl.stop_text_input(win: sdl.Window) -> (bool, err)", "Stop text input.", NULL, NULL},
    {"sdl.text_input_active", "sdl.text_input_active(win: sdl.Window) -> bool", "Check if text input is active.", NULL,
     NULL},
    /* Tiled rendering (slice 25) */
    {"sdl.Renderer.render_texture_tiled",
     "ren.render_texture_tiled(tex, sx, sy, sw, sh, scale, dx, dy, dw, dh) -> (bool, err)",
     "Tile a texture across a destination rect.", NULL, NULL},
    /* Display modes (slice 26) */
    {"sdl.get_desktop_display_mode", "sdl.get_desktop_display_mode(index: i32) -> (i32, i32)",
     "Get desktop display mode (w, h).", NULL, NULL},
    {"sdl.get_current_display_mode", "sdl.get_current_display_mode(index: i32) -> (i32, i32)",
     "Get current display mode (w, h).", NULL, NULL},
    {"sdl.get_display_refresh_rate", "sdl.get_display_refresh_rate(index: i32) -> f64",
     "Get display refresh rate in Hz.", NULL, NULL},
    {"sdl.get_display_for_window", "sdl.get_display_for_window(win: sdl.Window) -> i32",
     "Get display index for a window.", NULL, NULL},
    {"sdl.Window.sync", "win.sync() -> (bool, err)", "Block until pending window state is finalized.", NULL, NULL},
    /* Renderer completions (slice 27) */
    {"sdl.Renderer.get_render_target", "ren.get_render_target() -> i64",
     "Get current render target handle (-1 for default).", NULL, NULL},
    {"sdl.Renderer.set_draw_color_float", "ren.set_draw_color_float(r: f64, g: f64, b: f64, a: f64) -> (bool, err)",
     "Set draw color with float precision (0.0-1.0).", NULL, NULL},
    {"sdl.Renderer.get_logical_presentation", "ren.get_logical_presentation() -> (i32, i32)",
     "Get logical presentation size (w, h).", NULL, NULL},
    /* Surface extras (slice 28) */
    {"sdl.Surface.load_png", "sdl.Surface.load_png(path: string) -> (sdl.Surface, err)", "Load a PNG image.", NULL,
     NULL},
    {"sdl.Surface.blit_scaled", "surf.blit_scaled(dst, sx, sy, sw, sh, dx, dy, dw, dh, mode) -> (bool, err)",
     "Scaled blit to another surface.", "Use SCALEMODE_* constants.", NULL},
    {"sdl.Surface.get_color_key", "surf.get_color_key() -> i32", "Get transparent color key.", NULL, NULL},
    /* Joystick (slice 29) */
    {"sdl.has_joystick", "sdl.has_joystick() -> bool", "Check if any joystick is connected.", NULL, NULL},
    {"sdl.get_joystick_count", "sdl.get_joystick_count() -> i32", "Get number of connected joysticks.", NULL, NULL},
    {"sdl.get_joystick_id", "sdl.get_joystick_id(index: i32) -> i32", "Get joystick instance ID.", NULL, NULL},
    {"sdl.is_gamepad_id", "sdl.is_gamepad_id(id: i32) -> bool", "Check if joystick ID is a gamepad.", NULL, NULL},
    {"sdl.Joystick", NULL, "Raw joystick handle.", "For non-gamepad devices. Use sdl.Gamepad for standard controllers.",
     NULL},
    {"sdl.Joystick.open", "sdl.Joystick.open(id: i32) -> (sdl.Joystick, err)", "Open a joystick.", NULL, NULL},
    {"sdl.Joystick.close", "joy.close() -> void", "Close a joystick.", NULL, NULL},
    {"sdl.Joystick.get_name", "joy.get_name() -> string", "Get joystick name.", NULL, NULL},
    {"sdl.Joystick.get_type", "joy.get_type() -> i32", "Get joystick type.", NULL, NULL},
    {"sdl.Joystick.connected", "joy.connected() -> bool", "Check if connected.", NULL, NULL},
    {"sdl.Joystick.num_axes", "joy.num_axes() -> i32", "Get number of axes.", NULL, NULL},
    {"sdl.Joystick.num_buttons", "joy.num_buttons() -> i32", "Get number of buttons.", NULL, NULL},
    {"sdl.Joystick.num_hats", "joy.num_hats() -> i32", "Get number of POV hats.", NULL, NULL},
    {"sdl.Joystick.get_axis", "joy.get_axis(axis: i32) -> i32", "Get axis value (-32768..32767).", NULL, NULL},
    {"sdl.Joystick.get_button", "joy.get_button(btn: i32) -> bool", "Get button state.", NULL, NULL},
    {"sdl.Joystick.get_hat", "joy.get_hat(hat: i32) -> i32", "Get hat position. Use HAT_* constants.", NULL, NULL},
    {"sdl.Joystick.rumble", "joy.rumble(low: i32, high: i32, ms: i32) -> (bool, err)", "Rumble effect.", NULL, NULL},
    {"sdl.HAT_CENTERED", "sdl.HAT_CENTERED() -> i32", "Hat centered.", NULL, NULL},
    {"sdl.HAT_UP", "sdl.HAT_UP() -> i32", "Hat up.", NULL, NULL},
    {"sdl.HAT_RIGHT", "sdl.HAT_RIGHT() -> i32", "Hat right.", NULL, NULL},
    {"sdl.HAT_DOWN", "sdl.HAT_DOWN() -> i32", "Hat down.", NULL, NULL},
    {"sdl.HAT_LEFT", "sdl.HAT_LEFT() -> i32", "Hat left.", NULL, NULL},
    /* Window getters (slice 30) */
    {"sdl.Window.get_aspect_ratio", "win.get_aspect_ratio() -> (f64, f64)", "Get aspect ratio (min, max).", NULL, NULL},
    {"sdl.Window.get_pixel_format", "win.get_pixel_format() -> i32", "Get window pixel format.", NULL, NULL},
    /* Renderer/Surface (slice 31) */
    {"sdl.Renderer.read_pixels", "ren.read_pixels(x: i32, y: i32, w: i32, h: i32) -> (sdl.Surface, err)",
     "Read pixels from render target.", "Pass all zeros for full target.", NULL},
    {"sdl.Surface.convert", "surf.convert(format: i32) -> (sdl.Surface, err)",
     "Convert surface to a different pixel format.", NULL, NULL},
    /* Haptic (slice 32) */
    {"sdl.get_haptic_count", "sdl.get_haptic_count() -> i32", "Get number of haptic devices.", NULL, NULL},
    {"sdl.is_mouse_haptic", "sdl.is_mouse_haptic() -> bool", "Check if mouse has haptic.", NULL, NULL},
    {"sdl.Haptic", NULL, "Haptic (force feedback) device.", "Open with sdl.Haptic.open(index).", NULL},
    {"sdl.Haptic.open", "sdl.Haptic.open(index: i32) -> (sdl.Haptic, err)", "Open haptic device.", NULL, NULL},
    {"sdl.Haptic.close", "hap.close() -> void", "Close haptic device.", NULL, NULL},
    {"sdl.Haptic.get_name", "hap.get_name() -> string", "Get device name.", NULL, NULL},
    {"sdl.Haptic.rumble_supported", "hap.rumble_supported() -> bool", "Check rumble support.", NULL, NULL},
    {"sdl.Haptic.init_rumble", "hap.init_rumble() -> (bool, err)", "Initialize simple rumble.", NULL, NULL},
    {"sdl.Haptic.play_rumble", "hap.play_rumble(strength: f64, ms: i32) -> (bool, err)",
     "Play rumble (0.0-1.0 strength).", NULL, NULL},
    {"sdl.Haptic.stop_rumble", "hap.stop_rumble() -> (bool, err)", "Stop rumble.", NULL, NULL},
    {"sdl.Haptic.pause", "hap.pause() -> (bool, err)", "Pause haptic.", NULL, NULL},
    {"sdl.Haptic.resume", "hap.resume() -> (bool, err)", "Resume haptic.", NULL, NULL},
    /* Filesystem (slice 33) */
    {"sdl.get_current_directory", "sdl.get_current_directory() -> string", "Get current working directory.", NULL,
     NULL},
    {"sdl.create_directory", "sdl.create_directory(path: string) -> (bool, err)", "Create directory.", NULL, NULL},
    {"sdl.remove_path", "sdl.remove_path(path: string) -> (bool, err)", "Remove file or empty directory.", NULL, NULL},
    {"sdl.rename_path", "sdl.rename_path(old: string, new: string) -> (bool, err)", "Rename file/dir.", NULL, NULL},
    {"sdl.copy_file", "sdl.copy_file(src: string, dst: string) -> (bool, err)", "Copy a file.", NULL, NULL},
    {"sdl.get_path_type", "sdl.get_path_type(path: string) -> i32", "Get path type. Use PATHTYPE_* constants.", NULL,
     NULL},
    {"sdl.get_path_size", "sdl.get_path_size(path: string) -> i64", "Get file size in bytes.", NULL, NULL},
    {"sdl.PATHTYPE_NONE", "sdl.PATHTYPE_NONE() -> i32", "Path does not exist.", NULL, NULL},
    {"sdl.PATHTYPE_FILE", "sdl.PATHTYPE_FILE() -> i32", "Regular file.", NULL, NULL},
    {"sdl.PATHTYPE_DIRECTORY", "sdl.PATHTYPE_DIRECTORY() -> i32", "Directory.", NULL, NULL},
    /* Window/Surface extras (slice 34) */
    {"sdl.Window.get_borders_size", "win.get_borders_size() -> (i32, i32)",
     "Get border sizes packed as (top<<16|bottom, left<<16|right).", NULL, NULL},
    {"sdl.Window.get_safe_area", "win.get_safe_area() -> (i32, i32)", "Get safe area (w, h).", NULL, NULL},
    {"sdl.Surface.set_clip_rect", "surf.set_clip_rect(x: i32, y: i32, w: i32, h: i32) -> (bool, err)",
     "Set clipping rectangle. Zeros to clear.", NULL, NULL},
    /* Camera (slice 35) */
    {"sdl.get_camera_count", "sdl.get_camera_count() -> i32", "Get number of cameras.", NULL, NULL},
    {"sdl.get_camera_name", "sdl.get_camera_name(index: i32) -> string", "Get camera name.", NULL, NULL},
    {"sdl.get_current_camera_driver", "sdl.get_current_camera_driver() -> string", "Get camera driver.", NULL, NULL},
    {"sdl.Camera", NULL, "Camera (webcam) device.", "Open with sdl.Camera.open(). Acquire frames in a loop.", NULL},
    {"sdl.Camera.open", "sdl.Camera.open(index: i32, w: i32, h: i32, fps: i32) -> (sdl.Camera, err)", "Open a camera.",
     "Pass 0 for w/h/fps to use defaults.", NULL},
    {"sdl.Camera.close", "cam.close() -> void", "Close camera.", NULL, NULL},
    {"sdl.Camera.get_permission", "cam.get_permission() -> i32",
     "Get permission state (-1 denied, 0 pending, 1 approved).", NULL, NULL},
    {"sdl.Camera.get_format", "cam.get_format() -> (i32, i32)", "Get camera resolution (w, h).", NULL, NULL},
    {"sdl.Camera.acquire_frame", "cam.acquire_frame() -> (i64, err)", "Acquire a frame as surface handle.",
     "Returns -1 with ok err if no frame ready yet. Must release_frame after use.", NULL},
    {"sdl.Camera.release_frame", "cam.release_frame(surface_handle: i64) -> void", "Release an acquired camera frame.",
     NULL, NULL},
    /* Window mouse rect (slice 36) */
    {"sdl.Window.set_mouse_rect", "win.set_mouse_rect(x: i32, y: i32, w: i32, h: i32) -> (bool, err)",
     "Confine cursor to area. Zeros to clear.", NULL, NULL},
    /* Event description (slice 36) */
    {"sdl.Event.get_description", "ev.get_description() -> string", "Get English description of the event.", NULL,
     NULL},
    /* Surface extras (slice 37) */
    {"sdl.Surface.get_clip_rect", "surf.get_clip_rect() -> (i32, i32)", "Get clip rect size (w, h).", NULL, NULL},
    {"sdl.Surface.stretch", "surf.stretch(dst, sx, sy, sw, sh, dx, dy, dw, dh, mode) -> (bool, err)",
     "Stretched pixel copy to another surface.", NULL, NULL},
};

#define SDL_DOC_COUNT (sizeof(sdl_docs) / sizeof(sdl_docs[0]))

/* ── Module List ──────────────────────────────────────────── */

typedef struct native_doc_cache
{
    const vigil_native_module_t *module;
    vigil_doc_entry_t *entries;
    size_t count;
} native_doc_cache_t;

typedef struct native_doc_buf
{
    char *data;
    size_t length;
    size_t capacity;
} native_doc_buf_t;

static native_doc_cache_t native_doc_caches[32];
static size_t native_doc_cache_count = 0U;
static const char *generated_module_names[32];
static size_t generated_module_name_count = 0U;
static int generated_module_names_ready = 0;

static int native_doc_module_has_docs(const vigil_native_module_t *module)
{
    size_t i;

    if (module == NULL)
        return 0;
    if (module->doc != NULL)
        return 1;

    for (i = 0U; i < module->function_count; i++)
    {
        if (module->functions[i].doc != NULL)
            return 1;
    }
    for (i = 0U; i < module->class_count; i++)
    {
        size_t j;
        const vigil_native_class_t *klass = &module->classes[i];
        if (klass->doc != NULL)
            return 1;
        for (j = 0U; j < klass->field_count; j++)
        {
            if (klass->fields[j].doc != NULL)
                return 1;
        }
        for (j = 0U; j < klass->method_count; j++)
        {
            if (klass->methods[j].doc != NULL)
                return 1;
        }
    }
    return 0;
}

static void native_doc_buf_init(native_doc_buf_t *buf)
{
    buf->data = NULL;
    buf->length = 0U;
    buf->capacity = 0U;
}

static int native_doc_buf_reserve(native_doc_buf_t *buf, size_t extra)
{
    size_t needed = buf->length + extra + 1U;
    char *next;

    if (needed <= buf->capacity)
        return 1;
    buf->capacity = buf->capacity == 0U ? 64U : buf->capacity;
    while (buf->capacity < needed)
        buf->capacity *= 2U;
    next = (char *)realloc(buf->data, buf->capacity);
    if (next == NULL)
        return 0;
    buf->data = next;
    return 1;
}

static int native_doc_buf_append_len(native_doc_buf_t *buf, const char *text, size_t length)
{
    if (text == NULL || length == 0U)
        return 1;
    if (!native_doc_buf_reserve(buf, length))
        return 0;
    memcpy(buf->data + buf->length, text, length);
    buf->length += length;
    buf->data[buf->length] = '\0';
    return 1;
}

static int native_doc_buf_append(native_doc_buf_t *buf, const char *text)
{
    if (text == NULL)
        return 1;
    return native_doc_buf_append_len(buf, text, strlen(text));
}

static int native_doc_buf_append_char(native_doc_buf_t *buf, char ch)
{
    return native_doc_buf_append_len(buf, &ch, 1U);
}

static char *native_doc_buf_take(native_doc_buf_t *buf)
{
    char *out = buf->data;
    if (out == NULL)
    {
        out = (char *)malloc(1U);
        if (out != NULL)
            out[0] = '\0';
    }
    buf->data = NULL;
    buf->length = 0U;
    buf->capacity = 0U;
    return out;
}

static char *native_doc_printf(const char *fmt, ...)
{
    va_list args;
    va_list copy;
    int length;
    char *out;

    va_start(args, fmt);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0U, fmt, copy);
    va_end(copy);
    if (length < 0)
    {
        va_end(args);
        return NULL;
    }

    out = (char *)malloc((size_t)length + 1U);
    if (out == NULL)
    {
        va_end(args);
        return NULL;
    }
    vsnprintf(out, (size_t)length + 1U, fmt, args);
    va_end(args);
    return out;
}

static void native_doc_append_qualified_class_name(native_doc_buf_t *buf, const char *module_name,
                                                   const char *class_name)
{
    if (class_name == NULL)
    {
        native_doc_buf_append(buf, "object");
        return;
    }
    if (strchr(class_name, '.') != NULL || module_name == NULL)
    {
        native_doc_buf_append(buf, class_name);
        return;
    }
    native_doc_buf_append(buf, module_name);
    native_doc_buf_append_char(buf, '.');
    native_doc_buf_append(buf, class_name);
}

typedef struct native_doc_type_spec
{
    int kind;
    int object_kind;
    int element_type;
    const char *class_name;
    const vigil_native_type_t *ext;
    const char *override_name;
} native_doc_type_spec_t;

static native_doc_type_spec_t native_doc_type_spec(int kind, int object_kind, int element_type, const char *class_name,
                                                   const vigil_native_type_t *ext, const char *override_name)
{
    native_doc_type_spec_t spec;

    spec.kind = kind;
    spec.object_kind = object_kind;
    spec.element_type = element_type;
    spec.class_name = class_name;
    spec.ext = ext;
    spec.override_name = override_name;
    return spec;
}

static void native_doc_append_type_name(native_doc_buf_t *buf, const char *module_name, native_doc_type_spec_t spec)
{
    if (spec.override_name != NULL)
    {
        native_doc_buf_append(buf, spec.override_name);
        return;
    }

    if (spec.class_name != NULL && spec.class_name[0] != '\0')
    {
        native_doc_append_qualified_class_name(buf, module_name, spec.class_name);
        return;
    }

    if (spec.ext != NULL)
    {
        if (spec.ext->kind != VIGIL_TYPE_OBJECT || spec.ext->object_kind == 0)
        {
            native_doc_buf_append(buf, vigil_type_kind_name((vigil_type_kind_t)spec.ext->kind));
            return;
        }
        if (spec.ext->object_kind == 4)
        {
            native_doc_buf_append(buf, "array<");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->element_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append_char(buf, '>');
            return;
        }
        if (spec.ext->object_kind == 5)
        {
            native_doc_buf_append(buf, "map<");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->key_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append(buf, ", ");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->value_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append_char(buf, '>');
            return;
        }
    }

    if (spec.object_kind == VIGIL_NATIVE_FIELD_ARRAY || (spec.kind == VIGIL_TYPE_OBJECT && spec.element_type != 0))
    {
        native_doc_buf_append(buf, "array<");
        native_doc_append_type_name(buf, module_name, native_doc_type_spec(spec.element_type, 0, 0, NULL, NULL, NULL));
        native_doc_buf_append_char(buf, '>');
        return;
    }

    native_doc_buf_append(buf, vigil_type_kind_name((vigil_type_kind_t)spec.kind));
}

static char *native_doc_build_function_signature(const vigil_native_module_t *module,
                                                 const vigil_native_module_function_t *function)
{
    native_doc_buf_t buf;
    size_t i;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, function->name);
    native_doc_buf_append_char(&buf, '(');
    for (i = 0U; i < function->param_count; i++)
    {
        if (i != 0U)
            native_doc_buf_append(&buf, ", ");
        if (function->doc_param_names != NULL && function->doc_param_names[i] != NULL)
        {
            native_doc_buf_append(&buf, function->doc_param_names[i]);
            native_doc_buf_append(&buf, ": ");
        }
        native_doc_append_type_name(
            &buf, module->name,
            native_doc_type_spec(function->param_types != NULL ? function->param_types[i] : VIGIL_TYPE_INVALID, 0, 0,
                                 NULL, function->param_types_ext != NULL ? &function->param_types_ext[i] : NULL,
                                 function->doc_param_type_names != NULL ? function->doc_param_type_names[i] : NULL));
    }
    native_doc_buf_append(&buf, ") -> ");

    if (function->return_count > 1U && function->return_types != NULL)
    {
        native_doc_buf_append_char(&buf, '(');
        for (i = 0U; i < function->return_count; i++)
        {
            if (i != 0U)
                native_doc_buf_append(&buf, ", ");
            native_doc_append_type_name(
                &buf, module->name,
                native_doc_type_spec(function->return_types[i], 0, 0, NULL, NULL,
                                     i == 0U ? function->doc_return_type_name : NULL));
        }
        native_doc_buf_append_char(&buf, ')');
    }
    else
    {
        native_doc_append_type_name(&buf, module->name,
                                    native_doc_type_spec(function->return_type, 0, function->return_element_type, NULL,
                                                         function->return_type_ext, function->doc_return_type_name));
    }

    return native_doc_buf_take(&buf);
}

static char *native_doc_build_method_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass,
                                               const vigil_native_class_method_t *method)
{
    native_doc_buf_t buf;
    size_t i;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, klass->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, method->name);
    native_doc_buf_append_char(&buf, '(');
    for (i = 0U; i < method->param_count; i++)
    {
        if (i != 0U)
            native_doc_buf_append(&buf, ", ");
        if (method->doc_param_names != NULL && method->doc_param_names[i] != NULL)
        {
            native_doc_buf_append(&buf, method->doc_param_names[i]);
            native_doc_buf_append(&buf, ": ");
        }
        native_doc_append_type_name(
            &buf, module->name,
            native_doc_type_spec(method->param_types != NULL ? method->param_types[i] : VIGIL_TYPE_INVALID, 0, 0, NULL,
                                 NULL, method->doc_param_type_names != NULL ? method->doc_param_type_names[i] : NULL));
    }
    native_doc_buf_append(&buf, ") -> ");

    if (method->return_count > 1U && method->return_types != NULL)
    {
        native_doc_buf_append_char(&buf, '(');
        for (i = 0U; i < method->return_count; i++)
        {
            if (i != 0U)
                native_doc_buf_append(&buf, ", ");
            native_doc_append_type_name(
                &buf, module->name,
                native_doc_type_spec(method->return_types[i], 0, 0, NULL, NULL,
                                     i == 0U ? method->doc_return_type_name : NULL));
        }
        native_doc_buf_append_char(&buf, ')');
    }
    else
    {
        native_doc_append_type_name(&buf, module->name,
                                    native_doc_type_spec(method->return_type, 0, method->return_element_type,
                                                         method->return_class_name, NULL,
                                                         method->doc_return_type_name));
    }

    return native_doc_buf_take(&buf);
}

static char *native_doc_build_field_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass,
                                              const vigil_native_class_field_t *field)
{
    native_doc_buf_t buf;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, klass->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, field->name);
    native_doc_buf_append(&buf, ": ");
    native_doc_append_type_name(&buf, module->name,
                                native_doc_type_spec(field->type, field->object_kind, field->element_type,
                                                     field->class_name, NULL, field->doc_type_name));
    return native_doc_buf_take(&buf);
}

static char *native_doc_build_class_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass)
{
    return native_doc_printf("class %s.%s", module->name, klass->name);
}

static const vigil_native_module_t *native_doc_find_stdlib_module(const char *name)
{
    VIGIL_STDLIB_MODULE_TABLE(mods);
    size_t i;

    if (name == NULL)
        return NULL;

    for (i = 0U; i < sizeof(mods) / sizeof(mods[0]); i++)
    {
        if (mods[i].module != NULL && strcmp(mods[i].name, name) == 0)
            return mods[i].module;
    }
    return NULL;
}

static size_t native_doc_count_class_entries(const vigil_native_class_t *klass)
{
    size_t count = 0U;
    size_t i;

    if (klass->doc != NULL)
        count += 1U;
    for (i = 0U; i < klass->field_count; i++)
    {
        if (klass->fields[i].doc != NULL)
            count += 1U;
    }
    for (i = 0U; i < klass->method_count; i++)
    {
        if (klass->methods[i].doc != NULL)
            count += 1U;
    }
    return count;
}

static size_t native_doc_count_module_entries(const vigil_native_module_t *module)
{
    size_t count = 1U;
    size_t i;

    for (i = 0U; i < module->function_count; i++)
    {
        if (module->functions[i].doc != NULL)
            count += 1U;
    }
    for (i = 0U; i < module->class_count; i++)
        count += native_doc_count_class_entries(&module->classes[i]);
    return count;
}

static void native_doc_fill_class_entries(native_doc_cache_t *cache, size_t *index, const vigil_native_module_t *module,
                                          const vigil_native_class_t *klass)
{
    size_t i;

    if (klass->doc != NULL)
    {
        cache->entries[*index].name = native_doc_printf("%s.%s", module->name, klass->name);
        cache->entries[*index].signature = native_doc_build_class_signature(module, klass);
        cache->entries[*index].summary = klass->doc->summary;
        cache->entries[*index].description = klass->doc->description;
        cache->entries[*index].example = klass->doc->example;
        *index += 1U;
    }

    for (i = 0U; i < klass->field_count; i++)
    {
        const vigil_native_class_field_t *field = &klass->fields[i];
        if (field->doc == NULL)
            continue;
        cache->entries[*index].name = native_doc_printf("%s.%s.%s", module->name, klass->name, field->name);
        cache->entries[*index].signature = native_doc_build_field_signature(module, klass, field);
        cache->entries[*index].summary = field->doc->summary;
        cache->entries[*index].description = field->doc->description;
        cache->entries[*index].example = field->doc->example;
        *index += 1U;
    }

    for (i = 0U; i < klass->method_count; i++)
    {
        const vigil_native_class_method_t *method = &klass->methods[i];
        if (method->doc == NULL)
            continue;
        cache->entries[*index].name = native_doc_printf("%s.%s.%s", module->name, klass->name, method->name);
        cache->entries[*index].signature = native_doc_build_method_signature(module, klass, method);
        cache->entries[*index].summary = method->doc->summary;
        cache->entries[*index].description = method->doc->description;
        cache->entries[*index].example = method->doc->example;
        *index += 1U;
    }
}

static native_doc_cache_t *native_doc_build_module_cache(const vigil_native_module_t *module)
{
    native_doc_cache_t *cache;
    size_t count;
    size_t i;
    size_t index = 0U;

    if (module == NULL || !native_doc_module_has_docs(module) || native_doc_cache_count >= 32U)
        return NULL;

    count = native_doc_count_module_entries(module);

    cache = &native_doc_caches[native_doc_cache_count++];
    memset(cache, 0, sizeof(*cache));
    cache->module = module;
    cache->entries = (vigil_doc_entry_t *)calloc(count, sizeof(vigil_doc_entry_t));
    if (cache->entries == NULL)
    {
        native_doc_cache_count -= 1U;
        return NULL;
    }
    cache->count = count;

    cache->entries[index].name = module->name;
    cache->entries[index].signature = NULL;
    cache->entries[index].summary = module->doc != NULL ? module->doc->summary : NULL;
    cache->entries[index].description = module->doc != NULL ? module->doc->description : NULL;
    cache->entries[index].example = module->doc != NULL ? module->doc->example : NULL;
    index += 1U;

    for (i = 0U; i < module->function_count; i++)
    {
        const vigil_native_module_function_t *function = &module->functions[i];
        if (function->doc == NULL)
            continue;
        cache->entries[index].name = native_doc_printf("%s.%s", module->name, function->name);
        cache->entries[index].signature = native_doc_build_function_signature(module, function);
        cache->entries[index].summary = function->doc->summary;
        cache->entries[index].description = function->doc->description;
        cache->entries[index].example = function->doc->example;
        index += 1U;
    }

    for (i = 0U; i < module->class_count; i++)
        native_doc_fill_class_entries(cache, &index, module, &module->classes[i]);

    cache->count = index;
    return cache;
}

static native_doc_cache_t *native_doc_get_module_cache(const char *module_name)
{
    const vigil_native_module_t *module;
    size_t i;

    if (module_name == NULL)
        return NULL;

    for (i = 0U; i < native_doc_cache_count; i++)
    {
        if (strcmp(native_doc_caches[i].module->name, module_name) == 0)
            return &native_doc_caches[i];
    }

    module = native_doc_find_stdlib_module(module_name);
    if (module == NULL)
        return NULL;
    return native_doc_build_module_cache(module);
}

static const vigil_doc_entry_t *native_doc_lookup_entry(const char *name)
{
    const char *dot;
    native_doc_cache_t *cache;
    size_t i;
    char module_name[128];
    size_t module_length;

    if (name == NULL)
        return NULL;

    dot = strchr(name, '.');
    if (dot == NULL)
        cache = native_doc_get_module_cache(name);
    else
    {
        module_length = (size_t)(dot - name);
        if (module_length >= sizeof(module_name))
            return NULL;
        memcpy(module_name, name, module_length);
        module_name[module_length] = '\0';
        cache = native_doc_get_module_cache(module_name);
    }

    if (cache == NULL)
        return NULL;

    for (i = 0U; i < cache->count; i++)
    {
        if (strcmp(cache->entries[i].name, name) == 0)
            return &cache->entries[i];
    }
    return NULL;
}

static void native_doc_init_module_names(void)
{
    VIGIL_STDLIB_MODULE_TABLE(mods);
    size_t i;

    if (generated_module_names_ready)
        return;

    generated_module_name_count = 0U;
    generated_module_names[generated_module_name_count++] = "builtins";
    for (i = 0U; i < sizeof(mods) / sizeof(mods[0]); i++)
    {
        if (mods[i].module != NULL)
            generated_module_names[generated_module_name_count++] = mods[i].name;
    }
    generated_module_names_ready = 1;
}

/* ── Lookup Implementation ────────────────────────────────── */

typedef struct
{
    const char *name;
    const vigil_doc_entry_t *entries;
    size_t count;
} doc_module_table_entry_t;

static const doc_module_table_entry_t doc_module_table[] = {
    {"builtins", builtin_docs, BUILTIN_COUNT},
    {"strings", strings_docs, STRINGS_COUNT},
    {"readline", readline_docs, READLINE_COUNT},
    {"sdl", sdl_docs, SDL_DOC_COUNT},
};

#define DOC_MODULE_TABLE_COUNT (sizeof(doc_module_table) / sizeof(doc_module_table[0]))

const vigil_doc_entry_t *vigil_doc_lookup(const char *name)
{
    const vigil_doc_entry_t *generated;
    size_t m, i;

    if (name == NULL)
        return NULL;

    generated = native_doc_lookup_entry(name);
    if (generated != NULL)
        return generated;

    for (m = 0; m < DOC_MODULE_TABLE_COUNT; m++)
    {
        for (i = 0; i < doc_module_table[m].count; i++)
        {
            if (strcmp(doc_module_table[m].entries[i].name, name) == 0)
                return &doc_module_table[m].entries[i];
        }
    }
    return NULL;
}

const char **vigil_doc_list_modules(size_t *count)
{
    native_doc_init_module_names();
    if (count != NULL)
        *count = generated_module_name_count;
    return generated_module_names;
}

const vigil_doc_entry_t *vigil_doc_list_module(const char *module_name, size_t *count)
{
    native_doc_cache_t *generated;
    size_t m;
    if (module_name == NULL)
        return NULL;

    generated = native_doc_get_module_cache(module_name);
    if (generated != NULL)
    {
        if (count != NULL)
            *count = generated->count;
        return generated->entries;
    }

    for (m = 0; m < DOC_MODULE_TABLE_COUNT; m++)
    {
        if (strcmp(doc_module_table[m].name, module_name) == 0)
        {
            if (count)
                *count = doc_module_table[m].count;
            return doc_module_table[m].entries;
        }
    }
    return NULL;
}

vigil_status_t vigil_doc_entry_render(const vigil_allocator_t *allocator, const vigil_doc_entry_t *entry,
                                      char **out_text, size_t *out_length, vigil_error_t *error)
{
    char *buf;
    size_t len = 0;
    size_t cap = 1024;
    vigil_allocator_t a;

    if (entry == NULL || out_text == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "doc: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (allocator != NULL && vigil_allocator_is_valid(allocator))
        a = *allocator;
    else
        a = vigil_default_allocator();

    buf = (char *)a.allocate(a.user_data, cap);
    if (buf == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    /* Name/signature */
    if (entry->signature != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n\n", entry->signature);
    }
    else
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n\n", entry->name);
    }

    /* Summary */
    if (entry->summary != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n", entry->summary);
    }

    /* Description */
    if (entry->description != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "\n%s\n", entry->description);
    }

    /* Example */
    if (entry->example != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "\nExample:\n  %s\n", entry->example);
    }

    *out_text = buf;
    if (out_length)
        *out_length = len;
    return VIGIL_STATUS_OK;
}
