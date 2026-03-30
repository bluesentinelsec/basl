# VIGIL

**The VIGIL Scripting Language**

VIGIL is a statically typed, bytecode-compiled scripting language for building CLI tools, graphical programs, and libraries. It favors explicit behavior, batteries-included tooling, portability, and easy distribution.

```vigil
import "fmt";

fn main() -> i32 {
    fmt.println("Hello, World!");
    return 0;
}
```

## Quick Start

```bash
make build
make test
```

Run a program:

```bash
./build/vigil run hello.vigil
```

Create a new project:

```bash
./build/vigil new myapp
cd myapp
../build/vigil run main.vigil
```

Type-check a program without running it:

```bash
./build/vigil check hello.vigil
```

## Tooling

| Command | Description |
| --- | --- |
| `vigil run` | Compile and run a VIGIL script |
| `vigil check` | Type-check a VIGIL script without running it |
| `vigil new` | Create a new VIGIL project |
| `vigil debug` | Debug a script with DAP or the interactive debugger |
| `vigil doc` | Show documentation for modules, builtins, or source files |
| `vigil fmt` | Format VIGIL source files |
| `vigil repl` | Start the interactive REPL |
| `vigil lsp` | Start the Language Server Protocol server |
| `vigil version` | Print version information |
| `vigil embed` | Embed files as generated VIGIL source |
| `vigil test` | Discover and run `*_test.vigil` files |
| `vigil get` | Sync, install, or remove dependencies |
| `vigil editor` | Manage editor integrations |
| `vigil profile` | Print compile/runtime timing and memory stats |
| `vigil complexity` | Analyze cyclomatic complexity in Vigil code |
| `vigil package` | Package a program as a standalone binary |

For full command details, flags, and examples, see [CLI Reference](docs/cli_reference.md).

## Language Highlights

- Static typing with type inference for locals
- First-class functions and closures
- Classes, interfaces, and enums
- Multi-return values and explicit error handling (`guard`)
- `defer` for cleanup
- Testing, formatting, docs, packaging, debugging, and editor/LSP tooling built into the toolchain
- Standard library modules including `fmt`, `math`, `fs`, `net`, `http`, `time`, `crypto`, `regex`, `csv`, `yaml`, `thread`, `atomic`, `args`, and more
- Portable across Linux, macOS, Windows, and WebAssembly

## Repository Layout

```
include/vigil/       Public C API headers
src/                Compiler, VM, runtime, CLI, stdlib, platform layer
tests/              Unit tests
integration_tests/  CLI integration tests
examples/           Example programs
benchmarks/         Performance regression benchmark cases and thresholds
coverage/           Coverage thresholds and test-surface manifests
docs/               Language and project documentation
```

## Documentation

- [CLI Reference](docs/cli_reference.md)
- [Syntax Reference](docs/syntax.md)
- [Project Structure](docs/project_structure.md)
- [Stdlib Portability](docs/stdlib-portability.md)

## License

Apache License 2.0 — see [LICENSE](LICENSE) for details.
