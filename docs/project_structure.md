# Project Structure

VIGIL enforces a standard directory layout for all projects. This makes tooling predictable — `vigil test`, `vigil fmt`, `vigil package`, and `vigil get` all work without configuration.

## Layout

```
myproject/
├── vigil.toml              # project manifest
├── vigil.lock              # locked dependency versions (auto-generated)
├── main.vigil              # entry point (applications only)
├── lib/                   # project library modules
│   ├── utils.vigil
│   └── http/
│       ├── router.vigil
│       └── middleware.vigil
├── test/                  # test files (*_test.vigil)
│   ├── utils_test.vigil
│   └── http/
│       └── router_test.vigil
├── deps/                  # downloaded dependencies (gitignored)
│   └── github.com/
│       └── user/
│           └── repo/
└── assets/                # embedded files (optional)
```

## `vigil.toml`

Minimal project manifest:

```toml
name = "myproject"
version = "0.1.0"

[deps]
"github.com/user/json" = "v1.2.0"
"github.com/user/http" = "main"
```

No build flags, no source paths, no output config. The directory structure is the config.

## `vigil get`

Manage dependencies using git for distribution:

```sh
vigil get                              # sync all deps from vigil.toml
vigil get github.com/user/repo         # install latest
vigil get github.com/user/repo@v1.0.0  # install specific version/tag
vigil get github.com/user/repo@main    # install branch
```

Dependencies are cloned to `deps/` and can be imported by their full path:

```vigil
import "github.com/user/json"

fn main() -> i32 {
    json.parse("{}")
    return 0
}
```

## `vigil new`

Scaffolds a new project:

```sh
vigil new myapp              # application (with main.vigil)
vigil new mylib --lib        # library (no main.vigil)
```

### Application project

```sh
$ vigil new myapp
created myapp
  vigil.toml
  main.vigil
  lib/
  test/
  .gitignore
```

`main.vigil`:
```c
import "fmt"

fn main() -> i32 {
    fmt.println("hello, world!")
    return 0
}
```

### Library project

```sh
$ vigil new mylib --lib
created mylib
  vigil.toml
  lib/mylib.vigil
  test/mylib_test.vigil
  .gitignore
```

`lib/mylib.vigil`:
```c
/// mylib library module.

pub fn hello() -> string {
    return "hello from mylib"
}
```

`test/mylib_test.vigil`:
```c
import "test"
import "mylib"

fn test_hello(test.T t) -> void {
    t.assert(mylib.hello() == "hello from mylib", "hello should match")
}
```

## Import Resolution

Imports are resolved in order:

1. **Stdlib** — built-in modules (`fmt`, `os`, `math`, etc.)
2. **`lib/`** — project source modules
3. **`deps/`** — downloaded dependencies

```c
import "fmt";              // stdlib
import "utils";            // lib/utils.vigil
import "http/router";      // lib/http/router.vigil
import "json_schema";      // deps/json_schema/
```

No relative paths (`../`) needed. No path configuration.

## How Tools Use the Structure

| Command | Behavior |
|---------|----------|
| `vigil new` | Scaffolds the standard layout |
| `vigil test` | Runs everything in `test/` |
| `vigil fmt ./...` | Formats all `.vigil` files under the target tree |
| `vigil embed` | Output conventionally goes to `lib/` |
| `vigil package` | Packages an app (`main.vigil`) or a library bundle |
| `vigil deps` | Downloads into `deps/`, reads `vigil.toml` |

## Conventions

- One entry point per project: `main.vigil` at the root.
- Library code goes in `lib/`. Subdirectories map to import paths.
- Tests go in `test/`. Mirror the `lib/` structure.
- Dependencies go in `deps/`. Always gitignored.
- `vigil.toml` is the single source of truth for project metadata.

## Repository Tooling

The VIGIL repository itself also keeps benchmark cases under `benchmarks/`.
Those files are used by CI to detect material performance regressions in the
compiler, CLI, and VM by comparing PR builds against `origin/main`.
