# VIGIL Syntax Reference

VIGIL is a statically typed, C-style scripting language with explicit control flow, explicit errors, and a small set of consistent language forms.

## Source Files

- Source files use the `.vigil` extension.
- Statements end with `;`.
- Blocks use `{ }`.
- Every runnable program must define a `main` function. It returns `i32` (the exit code):

```
fn main() -> i32 {
    return 0;
}
```

- Library modules have no `main`. They export declarations with `pub` so other files can import them:

```
// mylib.vigil
pub const string VERSION = "1.0";

pub fn double(i32 x) -> i32 {
    return x * 2;
}
```

## Comments

```
// line comment

/*
block comment
*/
```

Comments placed directly above a declaration are extracted by `vigil doc`:

```
// Compute the area of a circle with the given radius.
pub fn area(f64 radius) -> f64 {
    return math.pi() * radius * radius;
}
```

## Imports

```
import "fmt";                    // stdlib module
import "fmt" as f;               // stdlib with alias
import "utils";                  // resolved relative to this file, or from lib/
import "sub/deep";               // subdirectory relative to this file
```

- `as` sets the local alias. Without it, the alias is the last path component.
- Only `pub` declarations are visible to importers.
- The `.vigil` extension is added automatically.

Resolution order for non-stdlib imports:
1. Relative to the importing file's directory.
2. In a project (directory with `vigil.toml`): the `lib/` directory.
3. For package-style paths (e.g. `"github.com/user/repo"`): the `deps/` directory.

## Top-Level Declarations

A source file may contain:

- `import`
- `fn`
- `class`
- `interface`
- `enum`
- `const`
- typed variable declarations

Most can be exported with `pub`:

```
pub fn greet() -> void {}
pub class Person {}
pub interface Greeter {}
pub enum Status { Ok, Err }
pub const string VERSION = "1.0";
pub i32 answer = 42;
```

## Types

### Primitive Types

| Type | Description |
|---|---|
| `bool` | `true` or `false` |
| `i32` | 32-bit signed integer |
| `i64` | 64-bit signed integer |
| `u8` | unsigned byte |
| `u32` | 32-bit unsigned integer |
| `u64` | 64-bit unsigned integer |
| `f64` | 64-bit IEEE 754 float |
| `string` | UTF-8 string |
| `err` | error value |
| `void` | no value |

There is no `null`. The `nil` literal exists as the internal void value but is not assignable to typed variables.

### Composite Types

```
array<i32> nums = [1, 2, 3];
map<string, i32> scores = {"alice": 95, "bob": 87};
array<array<string>> table = [["a"], ["b"]];
```

Nesting is supported to arbitrary depth: `array<array<f64>>`, `map<string, array<i32>>`.

### Function Types

Functions can be stored in variables with an explicit signature:

```
fn add(i32 a, i32 b) -> i32 { return a + b; }

fn(i32, i32) -> i32 op = add;
i32 result = op(2, 3);
```

Indirect calls require a concrete function signature. The shorthand `fn name = func;` stores a reference but cannot be called without a typed binding.

### Module-Qualified Types

Types from other modules use dot-qualified names:

```
import "models";

models.Point p = models.Point(3, 4);
```

## Literals

### Numeric Literals

```
i32 dec = 255;
i32 hex = 0xFF;
i32 bin = 0b11111111;
i32 oct = 0o377;
f64 pi = 3.14159;
f64 large = 1e6;
f64 small = 5.0e-324;
```

Integer literals support decimal, hexadecimal (`0x`), binary (`0b`), and octal (`0o`) bases. Float literals support scientific notation including subnormal values.

### String Literals

```
string normal = "hello\nworld";
string raw = `no escapes here`;
string msg = f"hello {name}";
```

Escape sequences in double-quoted strings:

| Escape | Meaning |
|---|---|
| `\n` | newline |
| `\r` | carriage return |
| `\t` | tab |
| `\\` | backslash |
| `\"` | double quote |
| `\'` | single quote |
| `\0` | null byte |
| `\xNN` | hex byte (two hex digits) |

Raw strings (backtick-delimited) perform no escape processing.

### Character Literals

Single-quoted character literals produce a one-character `string`:

```
string ch = 'a';
string newline = '\n';
string euro = '€';
```

They are syntax sugar for `string`, not a separate type.

### F-Strings

F-strings support interpolation and optional format specifiers:

```
string name = "Alice";
i32 age = 30;
f64 pi = 3.14159;

fmt.println(f"Name: {name}, Age: {age}");
fmt.println(f"Next year: {age + 1}");
fmt.println(f"pi={pi:.2f}");
fmt.println(f"literal braces: {{ok}}");
```

Format specifiers follow `{expr:[[fill]align][width][,][.precision][type]}`:

| Feature | Example | Output |
|---|---|---|
| Left-align | `f"{name:<20}"` | `Alice               ` |
| Right-align | `f"{name:>20}"` | `               Alice` |
| Center | `f"{name:^20}"` | `       Alice        ` |
| Zero-pad | `f"{age:0>8d}"` | `00000030` |
| Hex | `f"{age:x}"` | `1e` |
| Hex upper | `f"{age:X}"` | `1E` |
| Binary | `f"{age:b}"` | `11110` |
| Octal | `f"{age:o}"` | `36` |
| Thousands | `f"{1234567:,}"` | `1,234,567` |
| Float precision | `f"{pi:.4f}"` | `3.1416` |

Expressions including method calls are allowed inside interpolations:

```
fmt.println(f"{"hello".to_upper()}");
```

### Array and Map Literals

```
array<string> names = ["alice", "bob"];
map<string, i32> counts = {"a": 1, "b": 2};
```

Trailing commas are not permitted in array or map literals.

Empty collections require a type annotation:

```
array<i32> empty = [];
map<string, string> m = {};
```

## Variables and Constants

### Variable Declarations

All variables are typed and initialized at declaration:

```
i32 x = 10;
string name = "vigil";
bool ready = true;
```

### Constants

```
const i32 MAX = 100;
const string VERSION = "1.0";
```

Constants can appear at top level or inside functions.

### Multiple Return Bindings

Functions may return multiple values. Each binding is typed:

```
i32 result, err e = divide(10, 2);
string a, string b, bool c = multi();
```

Use `_` to discard a value:

```
i32 value, err _ = divide(10, 2);
```

## Assignment

```
x = 42;
x += 5;
x -= 1;
x *= 2;
x /= 3;
x %= 2;
x++;
x--;
arr[0] = 99;
m["key"] = 1;
obj.field = 7;
```

`++` and `--` are statements, not expressions.

## Operators

### Arithmetic

`+` `-` `*` `/` `%`

`+` also concatenates strings.

### Bitwise

`&` `|` `^` `~` `<<` `>>`

### Comparison

`==` `!=` `<` `>` `<=` `>=`

### Logical

`&&` `||` `!`

Logical operators short-circuit.

### Ternary

```
i32 max = a > b ? a : b;
```

The condition must be `bool`. The ternary is right-associative and has the lowest precedence. Parenthesize when used inside larger expressions:

```
i32 delta = 10 + (flag ? 1 : 2);
```

### Type Conversions

Conversions are explicit function-call syntax:

```
i32 x = 42;
f64 y = f64(x);
string s = string(x);
i64 big = i64(x);
i32 back = i32(big);
```

VIGIL performs no implicit numeric coercion. Mixed-type operations require explicit casts:

```
i32 a = 10;
i64 b = i64(20);
if (i64(a) < b) { }
```

For parsing strings to numbers, use the `parse` module:

```
import "parse";
i32 n, err e = parse.i32("42");
```

## Functions

### Function Declarations

```
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}

fn greet(string name) -> void {
    fmt.println("hello " + name);
}
```

### Multiple Return Values

```
fn divide(i32 a, i32 b) -> (i32, err) {
    if (b == 0) {
        return 0, err("division by zero", err.arg);
    }
    return a / b, ok;
}
```

A function returning only `err` uses `-> err`:

```
fn validate(i32 age) -> err {
    if (age < 0) {
        return err("negative age", err.arg);
    }
    return ok;
}
```

### Anonymous Functions and Closures

```
fn main() -> i32 {
    i32 factor = 10;

    fn(i32) -> i32 scale = fn(i32 x) -> i32 {
        return x * factor;
    };

    // Immediately invoked
    fn() -> void {
        fmt.println("iife");
    }();

    return 0;
}
```

Anonymous functions capture surrounding variables by value.

### Local Functions

```
fn main() -> i32 {
    fn helper(i32 x) -> i32 {
        return x * 2;
    }
    return helper(5);
}
```

## Control Flow

### if / else if / else

```
if (x > 10) {
    fmt.println("big");
} else if (x > 5) {
    fmt.println("medium");
} else {
    fmt.println("small");
}
```

Parentheses around the condition are required. The condition must be `bool` — there is no truthiness.

### while

```
while (running) {
    tick();
}
```

### C-Style for

```
for (i32 i = 0; i < 10; i++) {
    fmt.println(string(i));
}
```

### for-in

Iterates over arrays (single binding) and maps (key-value pair):

```
for val in arr {
    fmt.println(val);
}

for key, val in m {
    fmt.println(key + "=" + string(val));
}
```

### switch

```
switch (x) {
    case 1:
        fmt.println("one");
    case 2, 3:
        fmt.println("two or three");
    default:
        fmt.println("other");
}
```

- Cases may list multiple values separated by commas.
- There is no fallthrough.
- Works with integers, bools, and enum values.

### break and continue

```
for (i32 i = 0; i < 100; i++) {
    if (i == 50) { break; }
    if (i % 2 == 0) { continue; }
}
```

### defer

`defer` schedules a function call to run when the enclosing function returns:

```
fn process() -> i32 {
    defer cleanup();
    defer close_file();
    // ...
    return 0;
}
```

- The argument must be a function call.
- Deferred calls run in LIFO order.
- Arguments are evaluated eagerly.

### guard

`guard` binds values and handles the error case inline:

```
guard string data, err e = file.read_all("config.txt") {
    fmt.eprintln(f"read failed: {e.message()}");
    return 1;
}
// data is available here
```

Rules:
- The last binding must be `err` and must be named (not `_`).
- The body runs only when the error is not `ok`.

Equivalent expanded form:

```
string data, err e = file.read_all("config.txt");
if (e != ok) {
    fmt.eprintln(f"read failed: {e.message()}");
    return 1;
}
```

## Errors

VIGIL has no exceptions. Errors are values of type `err`.

### Success and Failure

```
return ok;
return err("file not found", err.not_found);
```

`ok` represents success. `err(message, kind)` constructs an error.

### Inspecting Errors

```
if (e != ok) {
    fmt.eprintln(e.message());
    fmt.eprintln(e.kind());
}
```

### Error Kinds

Built-in error kinds:

`err.not_found` `err.permission` `err.exists` `err.eof` `err.io` `err.parse` `err.bounds` `err.type` `err.arg` `err.timeout` `err.closed` `err.state`

### Routing by Kind

```
switch (e.kind()) {
    case err.not_found:
        fmt.println("missing");
    case err.permission:
        fmt.println("denied");
    default:
        fmt.eprintln(f"error: {e.message()}");
}
```

## Classes

```
class Person {
    pub string name;
    pub i32 age;

    fn init(string name, i32 age) -> void {
        self.name = name;
        self.age = age;
    }

    pub fn greet() -> void {
        fmt.println(f"Hello, I'm {self.name}");
    }
}
```

### Construction

```
Person p = Person("Alice", 30);
p.greet();
```

### Fallible Construction

If `init` returns `err`, construction becomes fallible:

```
class Connection {
    pub string host;

    fn init(string host) -> err {
        if (host == "") {
            return err("empty host", err.arg);
        }
        self.host = host;
        return ok;
    }
}

Connection c, err e = Connection("localhost");
```

### Notes

- `self` refers to the current instance inside methods.
- Fields are private by default. Use `pub` to expose them.
- Methods are private by default. Use `pub` to expose them.
- There is no class inheritance. Use interfaces and composition.

## Interfaces

```
interface Drawable {
    fn draw() -> void;
    fn name() -> string;
}
```

Classes implement interfaces explicitly:

```
class Circle implements Drawable {
    pub string label;

    fn init(string label) -> void {
        self.label = label;
    }

    fn draw() -> void {
        fmt.println("drawing circle");
    }

    fn name() -> string {
        return self.label;
    }
}
```

A class may implement multiple interfaces:

```
class Box implements Named, Sized {
    // must implement all methods from both interfaces
}
```

Interface types can be used as parameter types for polymorphic dispatch:

```
fn describe(Named n) -> string {
    return n.name();
}

Box b = Box("test", 42);
string s = describe(b);
```

## Enums

```
enum Color {
    Red,
    Green,
    Blue
}

enum HttpStatus {
    Ok = 200,
    NotFound = 404,
    Error = 500
}
```

Enum values are `i32`-backed. Access members with dot syntax:

```
Color c = Color.Red;

switch (c) {
    case Color.Red:
        fmt.println("red");
    default:
        fmt.println("other");
}
```

## Built-In Methods

### String Methods

| Method | Returns |
|---|---|
| `s.len()` | `i32` |
| `s.contains(sub)` | `bool` |
| `s.starts_with(prefix)` | `bool` |
| `s.ends_with(suffix)` | `bool` |
| `s.trim()` | `string` |
| `s.trim_left()` | `string` |
| `s.trim_right()` | `string` |
| `s.trim_prefix(prefix)` | `string` |
| `s.trim_suffix(suffix)` | `string` |
| `s.to_upper()` | `string` |
| `s.to_lower()` | `string` |
| `s.replace(old, new)` | `string` |
| `s.split(sep)` | `array<string>` |
| `s.fields()` | `array<string>` |
| `sep.join(arr)` | `string` |
| `s.index_of(sub)` | `(i32, bool)` |
| `s.last_index_of(sub)` | `(i32, bool)` |
| `s.substr(start, len)` | `(string, err)` |
| `s.char_at(i)` | `(string, err)` |
| `s.char_count()` | `i32` |
| `s.bytes()` | `array<u8>` |
| `s.reverse()` | `string` |
| `s.is_empty()` | `bool` |
| `s.repeat(n)` | `string` |
| `s.count(sub)` | `i32` |
| `s.cut(sep)` | `(string, string, bool)` |
| `s.equal_fold(t)` | `bool` |

### Array Methods

| Method | Returns |
|---|---|
| `a.len()` | `i32` |
| `a.push(val)` | `void` |
| `a.pop()` | `(T, err)` |
| `a.get(i)` | `(T, err)` |
| `a.set(i, val)` | `err` |
| `a.slice(start, end)` | `array<T>` |
| `a.contains(val)` | `bool` |

### Map Methods

| Method | Returns |
|---|---|
| `m.len()` | `i32` |
| `m.get(key)` | `(V, bool)` |
| `m.set(key, val)` | `err` |
| `m.remove(key)` | `(V, bool)` |
| `m.has(key)` | `bool` |
| `m.keys()` | `array<K>` |
| `m.values()` | `array<V>` |

Index syntax is also supported:

```
arr[0];
m["key"];
arr[0] = 1;
m["key"] = 2;
```

## Projects

A VIGIL project is a directory with a `vigil.toml` file:

```
name = "myapp"
version = "0.1.0"
```

Standard project layout:

```
myapp/
  vigil.toml
  main.vigil
  lib/
  test/
  deps/
```

Create a new project with `vigil new myapp`.

### Dependencies

Dependencies are managed with `vigil get`:

```
vigil get github.com/user/repo
vigil get github.com/user/repo@v1.0.0
vigil get                                  # sync all deps from vigil.toml
vigil get -remove github.com/user/repo     # remove a dependency
```

Dependencies are cloned into `deps/`.

### Testing

Test files are named `*_test.vigil` and use the `test` module:

```
import "test";

fn test_addition(test.T t) -> void {
    i32 result = 1 + 1;
    t.assert(result == 2, "1 + 1 should equal 2");
}

fn test_failure(test.T t) -> void {
    t.fail("explicit failure");
}
```

Test functions take a `test.T` parameter and return `void`. Run tests with:

```
vigil test                     # discover and run *_test.vigil files
vigil test path/to/test.vigil   # run a specific test file
vigil test -v                  # verbose output
vigil test -run pattern        # filter by test name
```

In a project, `vigil test` defaults to the `test/` directory.

## Tooling

| Command | Purpose |
|---|---|
| `vigil run file.vigil` | Run a script (also: `vigil file.vigil`) |
| `vigil check file.vigil` | Type-check without running |
| `vigil fmt file.vigil` | Format source code |
| `vigil fmt -c file.vigil` | Check formatting without rewriting |
| `vigil doc module` | Show stdlib module documentation |
| `vigil doc file.vigil` | Show documentation for a source file |
| `vigil test` | Run tests |
| `vigil new name` | Create a new project |
| `vigil get` | Manage dependencies |
| `vigil debug file.vigil` | Debug (DAP server by default) |
| `vigil debug -i file.vigil` | Interactive CLI debugger |
| `vigil repl` | Start interactive REPL |
| `vigil embed file` | Embed files as VIGIL source |
| `vigil package file.vigil` | Package as standalone binary |
| `vigil lsp` | Start Language Server Protocol server |
| `vigil version` | Print version |

## Language Rules

- No implicit type conversions.
- No hidden error propagation.
- Conditions must be `bool` — no truthiness.
- No `null`.
- Errors are explicit values.
- All variables must be initialized at declaration.
- Use `vigil fmt` for canonical formatting.
