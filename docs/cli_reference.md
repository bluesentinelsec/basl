# VIGIL CLI Reference

This document is the command reference for the `vigil` toolchain CLI.

It covers:

- every top-level `vigil` subcommand currently implemented in the binary
- positional arguments and flags
- command-specific defaults and behavior that are not always obvious from `--help`
- interactive command sets for REPL and the interactive debugger

The reference is based on the current CLI implementation in [`src/cli/main.c`](/Users/michaellong/projects/codex/vigil/src/cli/main.c), [`src/cli_test.c`](/Users/michaellong/projects/codex/vigil/src/cli_test.c), and the built `./build/vigil` help output.

## Top-Level Usage

```text
vigil <command> [options]
```

Main commands shown by `vigil --help`:

- `run`
- `check`
- `new`
- `debug`
- `doc`
- `fmt`
- `repl`
- `lsp`
- `version`
- `embed`
- `test`
- `get`
- `editor`
- `profile`
- `complexity`
- `package`

Other top-level behaviors:

- `vigil <file.vigil> [args...]` is shorthand for `vigil run <file.vigil> [args...]`.
- `vigil format ...` is accepted as an alias for `vigil fmt ...`.
- When a packaged Vigil binary is executed directly, it runs the embedded `entry.vigil` program instead of entering normal CLI parsing.

## Command Summary

| Command | Purpose | Notes |
| --- | --- | --- |
| `run` | Compile and execute a Vigil program | Script arguments after the file are passed to the program |
| `check` | Type-check a Vigil program | Does not execute the program |
| `new` | Create a new project | Can create app or library layouts |
| `debug` | Debug a Vigil program | DAP server by default, interactive debugger with `-i` |
| `doc` | Show stdlib, builtin, or source docs | No args lists modules |
| `fmt` | Format source files | `format` alias supported |
| `repl` | Start interactive REPL | Has REPL-specific commands like `:help` |
| `lsp` | Start the Language Server Protocol server | Uses stdin/stdout |
| `version` | Print the Vigil version | No flags |
| `embed` | Generate Vigil source that embeds files | Accepts files and directories |
| `test` | Discover and run `*_test.vigil` files | Defaults to `./test` in a project root |
| `get` | Sync, install, or remove dependencies | Uses `vigil.toml` in current project |
| `package` | Build or inspect a packaged binary | Supports optional XOR obfuscation key |
| `editor` | Install editor integration files | Supports `list`, `status`, `install`, and `uninstall` |
| `profile` | Compile/execute a script and print timing/memory stats | Local profiling helper |
| `complexity` | Report cyclomatic complexity for a file or directory | Local analysis helper |

## `run`

Usage:

```text
vigil run <file> [args...]
vigil <file.vigil> [args...]
```

Arguments:

- `file`: entry script to compile and run
- `args...`: additional positional values passed to the running program through the VM argument list

Options:

- none

Behavior:

- Registers the entry file plus its imported source tree.
- Resolves the project root automatically when possible.
- Registers built-in stdlib modules and plugins before compilation.
- Executes the compiled entrypoint.
- Expects `main` to return `i32`.
- Uses the returned `i32` as the process exit code.

Examples:

```text
vigil run main.vigil
vigil run examples/hello.vigil Alice Bob
vigil examples/hello.vigil Alice Bob
```

## `check`

Usage:

```text
vigil check <file>
```

Arguments:

- `file`: entry script to type-check

Options:

- none

Behavior:

- Registers the entry file plus its imported source tree.
- Resolves the project root automatically when possible.
- Registers stdlib modules and plugins so imports can be checked accurately.
- Performs semantic checking only.
- Prints diagnostics on failure.

Example:

```text
vigil check main.vigil
```

## `new`

Usage:

```text
vigil new [options] <name>
```

Arguments:

- `name`: project name

Options:

- `-l`, `--lib`: create a library project instead of an app project
- `-s`, `--scaffold`: for app projects, include an example library module and test
- `-o`, `--output <dir>`: create the project inside `<dir>/<name>`

Behavior:

- If `name` is omitted, Vigil prompts interactively for a project name.
- Rejects project names longer than 100 characters.
- Fails if the destination directory already exists.
- Always creates:
  - `vigil.toml`
  - `lib/`
  - `test/`
  - `.gitignore`
- Default app layout creates `main.vigil`.
- Library layout creates `lib/<name>.vigil` plus `test/<name>_test.vigil`.
- App scaffold mode creates:
  - `main.vigil`
  - `lib/<name>.vigil`
  - `test/<name>_test.vigil`

Examples:

```text
vigil new hello
vigil new mylib --lib
vigil new app --scaffold --output examples
```

## `debug`

Usage:

```text
vigil debug [options] <file>
```

Arguments:

- `file`: script to debug

Options:

- `-i`, `--interactive`: use the interactive terminal debugger instead of the default DAP server mode

Behavior:

- Default mode starts the Debug Adapter Protocol server path.
- Interactive mode compiles with debug symbols, starts paused at entry, and runs a terminal debugger loop.
- Both modes register the source tree, stdlib modules, and plugins.

Examples:

```text
vigil debug main.vigil
vigil debug -i main.vigil
```

### Interactive Debugger Commands

When `vigil debug -i ...` is running, these commands are available:

- `c`, `continue`: resume execution
- `s`, `step`: step into
- `n`, `next`: step over
- `o`, `out`: step out
- `b <line>`: set a breakpoint at a line in the current source
- `b <function>`: set a function breakpoint
- `d <id>`: delete a breakpoint by id
- `bt`, `backtrace`: show the call stack
- `l`, `locals`: show local variables in the current frame
- `p <var>`: print a local variable value
- `list [line]`: show source around the current line or around a specific line
- `w`, `where`: print the current source location
- `q`, `quit`: stop debugging
- `h`, `help`: print the debugger command list

## `doc`

Usage:

```text
vigil doc
vigil doc <target>
vigil doc <target> <symbol>
```

Arguments:

- `target`: one of:
  - a documented stdlib/builtin module such as `math`
  - a documented symbol such as `len`, `math.sqrt`, or `args.Parser.help`
  - a `.vigil` source file path
- `symbol`: optional symbol name when rendering docs extracted from a source file

Options:

- none

Behavior:

- `vigil doc` with no arguments lists available documented modules.
- `vigil doc <module-or-symbol>` looks up builtins and stdlib docs from the built-in registry.
- `vigil doc <file.vigil>` extracts and renders documentation from user source.
- If `<target>` is a bare word that is not a known documented module/symbol and is not a `.vigil` path, the command fails.

Examples:

```text
vigil doc
vigil doc math
vigil doc math.sqrt
vigil doc examples/hello.vigil
vigil doc examples/hello.vigil greet
```

## `fmt`

Usage:

```text
vigil fmt [options] <file>
vigil format [options] <file>
```

Arguments:

- `file`: source file to format

Options:

- `-c`, `--check`: check formatting without rewriting the file

Behavior:

- Reads and lexes the file, then formats it with the Vigil formatter.
- In normal mode, rewrites the file in place when the formatter output differs.
- In `--check` mode:
  - prints the filename if formatting changes would be made
  - exits non-zero when the file is not already formatted

Examples:

```text
vigil fmt main.vigil
vigil fmt --check main.vigil
vigil format main.vigil
```

## `repl`

Usage:

```text
vigil repl
```

Arguments:

- none

Options:

- none

Behavior:

- Starts the interactive REPL.
- Imports `fmt` by default.
- Retains declarations between inputs until cleared.
- If a project root is detected, import resolution is performed relative to that project.
- Stores the last successfully evaluated expression result in `__ans` as a string.

REPL special commands:

- `:help`, `:h`: show REPL help
- `:quit`, `:q`: exit the REPL
- `exit()`: alternate REPL exit form
- `:clear`: clear accumulated REPL state and reset to the default `import "fmt";`
- `:doc <name>`: render docs for a builtin or stdlib symbol/module

Example:

```text
vigil repl
```

## `lsp`

Usage:

```text
vigil lsp
```

Arguments:

- none

Options:

- none

Behavior:

- Starts the Vigil Language Server Protocol server.
- Uses standard input and output for transport.
- Intended to be launched by editor integrations rather than by hand.

Example:

```text
vigil lsp
```

## `version`

Usage:

```text
vigil version
```

Arguments:

- none

Options:

- none

Behavior:

- Prints the Vigil version string and exits.

Example:

```text
vigil version
```

## `embed`

Usage:

```text
vigil embed <file|dir...> [-o output.vigil]
```

Arguments:

- `file|dir...`: one or more files or directories to embed

Options:

- `-o`, `--output <path>`: output file path

Behavior:

- Accepts a mix of files and directories.
- Recursively collects files from directories.
- Fails if no files are found.
- Default output path selection:
  - if `-o/--output` is provided, use it
  - if exactly one file was embedded directly, use `<basename>.vigil`
  - otherwise, use `assets.vigil`
- The current short help for `embed` still prints `embed.vigil` as the generic default, but the implementation uses the selection rules above.

Examples:

```text
vigil embed assets/logo.png
vigil embed assets/ static/ -o embedded_assets.vigil
```

## `test`

Usage:

```text
vigil test [--run pattern] [-v] [--coverage] [path...]
```

Arguments:

- `path...`: optional files or directories to scan

Flags:

- `-v`, `--verbose`: print passing tests as they run
- `-run`, `--run <pattern>`: only run test function names containing the substring pattern
- `--coverage`: collect line and branch coverage while tests execute
- `--format <text|json>`: choose text or JSON coverage output
- `--min-coverage <N>`: fail the command if total line coverage is below `N`
- `--include-deps`: include imported user modules outside the project root in coverage

Behavior:

- Recursively finds files ending in `*_test.vigil`.
- Test functions are top-level functions whose names begin with `test_`.
- If no paths are provided:
  - in a project root with both `vigil.toml` and a `test/` directory, uses `test/`
  - otherwise, scans `.`
- Builds a wrapper `main()` that constructs `test.T` and invokes each matched test function.
- Prints Go-style per-file summaries such as `ok` and `FAIL`.
- Coverage mode tracks line and branch coverage for non-test user source files in the current project.
- `--include-deps` extends coverage to imported user modules outside the project root. Stdlib modules are never tracked.
- `--format json` writes machine-readable coverage JSON to stdout and sends the normal test progress stream to stderr.
- Exits non-zero if any test fails.
- `--min-coverage` also makes the command fail when total line coverage is below the requested threshold.

Examples:

```text
vigil test
vigil test test/
vigil test ./examples --run parse
vigil test -v
vigil test --coverage
vigil test --coverage --format json
vigil test --coverage --min-coverage 85
```

## `get`

Usage:

```text
vigil get [package[@version]...]
```

Arguments:

- `package[@version]...`: zero or more package specs

Options:

- `--remove`: remove packages instead of installing them
- `-remove`: accepted as an alternate remove flag spelling

Behavior:

- Must be run from a Vigil project root containing `vigil.toml`.
- With no package arguments, syncs all dependencies from `vigil.toml`.
- With package arguments:
  - installs each package
  - accepts version/tag/branch suffixes using `@`
- In remove mode, removes each listed package from the project.
- Uses git-based package distribution.

Examples:

```text
vigil get
vigil get github.com/user/repo
vigil get github.com/user/repo@v1.0.0
vigil get github.com/user/repo@main
vigil get --remove github.com/user/repo
```

## `package`

Usage:

```text
vigil package [options] <entry>
```

Arguments:

- `entry`: entry script or project directory

Options:

- `-o`, `--output <path>`: output binary path
- `-k`, `--key <string>`: XOR encryption key used for bundle obfuscation
- `-i`, `--inspect`: inspect an existing packaged binary instead of building one

Behavior:

- Build mode:
  - registers the entry source and imported source tree
  - packages all collected source files into a standalone binary
  - renames the chosen entry source to `entry.vigil` inside the bundle
- Default entry path in build mode is `main.vigil` if none is provided.
- Default output path in build mode is the entry basename with `.vigil` removed.
- Inspect mode:
  - reads a packaged binary
  - prints bundled file paths
  - uses the positional `entry` value as the inspect target when provided
  - otherwise falls back to `--output`

Examples:

```text
vigil package main.vigil
vigil package main.vigil -o dist/myapp
vigil package main.vigil -k secret-key
vigil package --inspect dist/myapp
```

## `editor`

Usage:

```text
vigil editor <list|install|uninstall|status> [editor]
```

Subcommands:

- `list`: show supported editors and whether integration files are installed
- `status`: print installed integrations only
- `install <editor>`: install integration files for one editor
- `uninstall <editor>`: remove integration files for one editor

Supported editors:

- `vim`
- `nvim`
- `vscode`
- `emacs`
- `sublime`

Behavior:

- Uses the current executable path to wire editor integrations back to `vigil lsp` where relevant.
- Requires a resolvable home directory.

Examples:

```text
vigil editor list
vigil editor status
vigil editor install nvim
vigil editor uninstall sublime
```

## `profile`

Usage:

```text
vigil profile <script.vigil>
```

Arguments:

- `script.vigil`: script to compile and run under the built-in timing/memory profiler

Options:

- none

Behavior:

- Measures compile time, execute time, and total time.
- Prints memory/process statistics including peak RSS, allocation count, and allocated bytes.
- Intended for local performance investigation.

Example:

```text
vigil profile examples/hello.vigil
```

## `complexity`

Usage:

```text
vigil complexity <file.vigil|directory>
```

Arguments:

- `file.vigil|directory`: a single Vigil file or a directory tree to scan

Options:

- none

Behavior:

- Lexes Vigil source and reports per-function cyclomatic complexity.
- Reports:
  - function name
  - `ccn`
  - estimated line count
  - parameter count
- Walks directories recursively and scans `.vigil` files.
- Prints an aggregate summary with average and maximum CCN.

Example:

```text
vigil complexity src
vigil complexity examples/hello.vigil
```

## Help Behavior Notes

Most parser-registered commands support `-h` and `--help`.

A few early-dispatch commands have custom behavior instead:

- `embed --help`: supported
- `get --help`: supported
- `test --help`: supported
- `editor --help`: supported through early dispatch
- `profile --help`: supported through early dispatch
- `complexity --help`: supported through early dispatch
- `repl --help`: does not show command help because `repl` starts before the normal CLI parser; use `:help` after entering the REPL

Until those help paths are normalized, this document is the authoritative CLI reference.
