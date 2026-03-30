# Build-Time Plugin System

Vigil supports build-time plugins that let you extend the language with custom native modules. Plugins are compiled into `libvigil` and available via `import` like any stdlib module.

## Quick Start

Create a plugin directory under `plugins/`:

```
plugins/
  my_math/
    plugin.cmake
    my_math.c
```

**plugin.cmake:**

```cmake
# Register the plugin with the Vigil build system.
# This file is standard CMake — you can use find_package(), set variables,
# add compile definitions, or any other CMake commands before or after
# the vigil_add_plugin() call.
vigil_add_plugin(NAME my_math SOURCES my_math.c)
```

**my_math.c:**

```c
#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"
#include "internal/vigil_nanbox.h"

/* ── Business logic ──────────────────────────────────────────────
   Pure C functions that do the actual work. These don't know about
   the Vigil VM — they just take inputs and return outputs. */

static int32_t compute_square(int32_t x)
{
    return x * x;
}

/* ── VM glue ─────────────────────────────────────────────────────
   Each exported function needs a thin wrapper that reads arguments
   from the VM stack, calls the business logic, and pushes the
   result back. This is the only boilerplate per function. */

static vigil_status_t square(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Read arguments from the stack */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    int32_t x = (int32_t)vigil_nanbox_decode_int(v);

    /* Pop arguments, compute, push result */
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t result;
    vigil_value_init_int(&result, (int64_t)compute_square(x));
    return vigil_vm_stack_push(vm, &result, error);
}

/* ── Module registration ─────────────────────────────────────────
   Declare parameter types, build the function table, and export
   the module. The symbol must be named vigil_plugin_<name>. */

static const int i32_params[] = {VIGIL_TYPE_I32};

static const vigil_native_module_function_t functions[] = {
    {"square", 6U, square, 1U, i32_params, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_plugin_my_math = {
    "my_math", 7U, functions, 1U, NULL, 0U
};
```

Build and use:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```vigil
import "my_math";

fn main() -> i32 {
    i32 result = my_math.square(7);
    return result;  // 49
}
```

## Plugin CMake API

```cmake
vigil_add_plugin(
    NAME <name>
    SOURCES <source_files...>
    [LIBRARIES <link_targets...>]
    [FIND_PACKAGES <packages...>]
)
```

- `NAME` — module name, used in `import "name"` and as the symbol suffix
- `SOURCES` — C source files, relative to the plugin directory
- `LIBRARIES` — CMake link targets (e.g., `SDL2::SDL2`)
- `FIND_PACKAGES` — packages to find via `find_package()`. If any package is missing, the plugin is skipped with a warning instead of failing the build

Example with an external dependency:

```cmake
# The SDL plugin uses FetchContent to download and statically link SDL3.
# It defines VIGIL_PLUGIN_SDL as an opt-out option.
option(VIGIL_PLUGIN_SDL "Build the SDL3 plugin" ON)

if(NOT VIGIL_PLUGIN_SDL)
    message(STATUS "Plugin 'sdl': disabled (VIGIL_PLUGIN_SDL=OFF)")
    return()
endif()

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

vigil_add_plugin(
    NAME sdl
    SOURCES sdl.c
    LIBRARIES SDL3::SDL3-static
)
```

If the plugin is disabled, the build prints `Plugin 'sdl': disabled (VIGIL_PLUGIN_SDL=OFF)` and continues.

## Module Export Convention

Every plugin must export a `vigil_native_module_t` named `vigil_plugin_<name>`:

```c
VIGIL_API const vigil_native_module_t vigil_plugin_<name> = { ... };
```

The `VIGIL_API` macro is required for Windows shared library builds (`dllexport`/`dllimport`).

## Native Function Signature

Each function receives the VM, argument count, and error output:

```c
static vigil_status_t my_func(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
```

Arguments are on the VM stack. Pop them, compute the result, push the return value:

```c
// Read arguments
size_t base = vigil_vm_stack_depth(vm) - arg_count;
vigil_value_t arg0 = vigil_vm_stack_get(vm, base);

// Pop arguments
vigil_vm_stack_pop_n(vm, arg_count);

// Push result
vigil_value_t result;
vigil_value_init_int(&result, 42);
return vigil_vm_stack_push(vm, &result, error);
```

For string return values, create the string object, init the value, then release both:

```c
vigil_object_t *obj = NULL;
vigil_string_object_new_cstr(vigil_vm_runtime(vm), "hello", &obj, error);
vigil_value_t val;
vigil_value_init_object(&val, &obj);
vigil_object_release(&obj);
vigil_status_t st = vigil_vm_stack_push(vm, &val, error);
vigil_value_release(&val);
return st;
```

## Function Table Entry

Each entry in the function table describes one callable function:

```c
{
    "name",           // function name
    4U,               // name length
    my_func,          // C function pointer
    2U,               // parameter count
    param_types,      // array of VIGIL_TYPE_* constants
    VIGIL_TYPE_I32,   // return type
    1U,               // return count
    NULL,             // doc string (optional)
    0,                // doc string length
    NULL,             // error return types (optional)
    NULL              // reserved
}
```

## Disabling Plugins

```bash
cmake -S . -B build -DVIGIL_PLUGINS=OFF
```

When disabled, no plugins are compiled and `import "plugin_name"` produces a compile error.

Individual plugins can also be disabled if they define an option. For example, the SDL plugin:

```bash
cmake -S . -B build -DVIGIL_PLUGIN_SDL=OFF
```

Some plugins are off by default and require opt-in:

```bash
cmake -S . -B build -DVIGIL_PLUGIN_SDL=ON
```

Check each plugin's `plugin.cmake` for its option name. The convention is `VIGIL_PLUGIN_<NAME>` in uppercase.

## File Layout Reference

```
plugins/
  my_plugin/
    plugin.cmake          # vigil_add_plugin() call
    my_plugin.c           # native module implementation
    helper.c              # additional sources (list in SOURCES)
cmake/
  plugin_registry.h.in    # template for auto-generated registry
```

The build system scans `plugins/*/plugin.cmake` automatically. No manual registration is needed.
