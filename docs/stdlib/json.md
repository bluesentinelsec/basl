# JSON Standard Library

The `json` module gives Vigil programs two ways to work with JSON:

- `json.Value` for arbitrary JSON documents with unknown or changing shape
- `json.encode()` / `json.decode()` for typed marshaling to and from Vigil values

This module is backed by Vigil's in-tree JSON implementation. The same JSON engine is used by the runtime and by the standard library.

## Import

```vigil
import "json";
```

## Quick Start

### Parse and traverse arbitrary JSON

```vigil
import "json";

fn main() -> i32 {
    json.Value root, err parse_err =
        json.Value.parse("{\"name\":\"vigil\",\"flags\":[true,false],\"count\":3}");
    if parse_err != ok { return 1; }

    json.Value name, err name_err = root.get("name");
    if name_err != ok { return 2; }

    string text, err text_err = name.as_string();
    if text_err != ok { return 3; }

    return text == "vigil" ? 0 : 4;
}
```

### Encode and decode a typed object

```vigil
import "json";

class Meta {
    pub i32 count;
}

class Person {
    pub string name;
    pub array<i32> scores;
    pub map<string, string> tags;
    pub Meta meta;
}

fn main() -> i32 {
    Person person = Person("vigil", [1, 2, 3], {"role": "tooling"}, Meta(3));

    string encoded, err encode_err = json.encode(person);
    if encode_err != ok { return 1; }

    Person decoded, err decode_err =
        json.decode(encoded, Person("", [0], {"": ""}, Meta(0)));
    if decode_err != ok { return 2; }

    return decoded.meta.count == 3 ? 0 : 3;
}
```

## Working With `json.Value`

`json.Value` is the right API when:

- the JSON shape is not known ahead of time
- you are inspecting API responses or config files
- you need to preserve arbitrary JSON and re-emit it later

### Constructing values

#### `json.Value.parse(text: string) -> (json.Value, err)`

Parse JSON text from a string.

```vigil
json.Value root, err parse_err = json.Value.parse("{\"ok\":true}");
if parse_err != ok {
    // invalid JSON
}
```

#### `json.Value.read(path: string) -> (json.Value, err)`

Read a file from disk and parse it as JSON.

```vigil
json.Value config, err read_err = json.Value.read("config.json");
if read_err != ok {
    // file I/O error or parse error
}
```

### Discovering value types

#### `kind() -> string`

Returns one of:

- `"null"`
- `"bool"`
- `"number"`
- `"string"`
- `"array"`
- `"object"`

#### `is_null() -> bool`
#### `is_bool() -> bool`
#### `is_number() -> bool`
#### `is_string() -> bool`
#### `is_array() -> bool`
#### `is_object() -> bool`

These helpers are the safest way to branch before calling typed accessors.

```vigil
if root.is_object() {
    // ...
}
```

### Reading arrays and objects

#### `len() -> (i32, err)`

Returns the length of an array or the number of keys in an object.

It returns an error for non-array, non-object values.

```vigil
i32 count, err len_err = root.len();
```

#### `at(index: i32) -> (json.Value, err)`

Reads an array element by index.

It returns an error if:

- the value is not an array
- the index is out of bounds

```vigil
json.Value first, err first_err = flags.at(0);
```

#### `get(key: string) -> (json.Value, err)`

Reads an object member by key.

It returns an error if:

- the value is not an object
- the key is missing

```vigil
json.Value name, err name_err = root.get("name");
```

#### `has(key: string) -> bool`

Returns `true` when an object contains the key.

For non-object values it returns `false`.

```vigil
if root.has("name") {
    // ...
}
```

#### `keys() -> array<string>`

Returns the object's keys in object iteration order.

```vigil
array<string> keys = root.keys();
```

### Typed scalar accessors

#### `as_bool() -> (bool, err)`
#### `as_number() -> (f64, err)`
#### `as_string() -> (string, err)`

These accessors require the underlying JSON value to already have the matching type. They do not coerce across JSON types.

Examples:

```vigil
bool ok, err ok_err = value.as_bool();
f64 n, err num_err = value.as_number();
string s, err str_err = value.as_string();
```

If the value is the wrong JSON type, the accessor returns an error.

### Serializing dynamic JSON

#### `stringify() -> string`

Serialize the `json.Value` back to compact JSON text.

```vigil
string text = root.stringify();
```

#### `write(path: string) -> err`

Serialize the value and write it to a file.

```vigil
err write_err = root.write("out.json");
```

## Typed Marshaling

Phase 2 adds a typed layer:

- `json.encode(value) -> (string, err)`
- `json.decode(text, prototype) -> (T, err)`

This layer is for JSON that should map into ordinary Vigil values and classes.

### `json.encode(value: T) -> (string, err)`

Encodes a Vigil value as JSON text.

Currently supported input shapes:

- `bool`
- integer types
- `f64`
- `string`
- `array<T>`
- `map<string, T>`
- class instances with public fields

Nested arrays, maps, and class instances are supported as long as their contents are also supported.

Current restrictions:

- map keys must be strings
- non-public class fields are skipped during encoding
- unsupported values return an error
- `nil` is not currently encoded as JSON `null`

### `json.decode(text: string, prototype: T) -> (T, err)`

Parses JSON text and decodes it into the same type as `prototype`.

The prototype is used for type information. Its runtime contents are not the decoded result.

```vigil
Person person, err decode_err =
    json.decode(text, Person("", [0], {"": ""}, Meta(0)));
```

Current decode support:

- `bool`
- `i32`
- `i64`
- `u8`
- `u32`
- `u64`
- `f64`
- `string`
- `array<T>`
- `map<string, T>`
- classes with public fields
- nested combinations of the above

Current decode rules:

- JSON object keys must exactly match public field names
- missing fields are errors
- extra fields are errors
- non-public class fields are not supported
- only `map<string, T>` is supported
- JSON values must match the target type directly
- numeric decoding is range-checked for integer targets

### Why `decode` takes a prototype

Vigil needs a concrete target type at compile time. The prototype tells `json.decode` what type to produce.

For simple classes:

```vigil
User user, err err1 = json.decode(text, User(""));
```

For classes that contain arrays or maps, make the member types explicit in the prototype:

```vigil
Person person, err err2 =
    json.decode(text, Person("", [0], {"": ""}, Meta(0)));
```

The placeholder values are only there to provide a concrete typed value.

## Type Mapping

### JSON to Vigil

| JSON | Vigil target |
| --- | --- |
| `true` / `false` | `bool` |
| number | `f64`, `i32`, `i64`, `u8`, `u32`, `u64` |
| string | `string` |
| array | `array<T>` |
| object | `map<string, T>` or class instance |

Notes:

- integer targets require a whole-number JSON value in range
- object-to-class decode is strict about missing and extra fields
- JSON `null` does not currently decode into arbitrary typed fields

### Vigil to JSON

| Vigil value | JSON |
| --- | --- |
| `bool` | boolean |
| integer types | number |
| `f64` | number |
| `string` | string |
| `array<T>` | array |
| `map<string, T>` | object |
| class instance | object of public fields |

## Error Handling

The module follows normal Vigil multi-return error style.

Examples:

```vigil
json.Value root, err parse_err = json.Value.parse(text);
if parse_err != ok {
    return 1;
}

string encoded, err encode_err = json.encode(value);
if encode_err != ok {
    return 2;
}
```

Typical error cases:

- invalid JSON syntax
- file I/O failures in `read()` and `write()`
- calling `get()` on a non-object
- calling `at()` on a non-array
- calling `as_string()` on a non-string
- trying to encode an unsupported value
- type mismatch during typed decode
- missing or extra object fields during typed decode

## Patterns

### Pattern: inspect arbitrary JSON safely

```vigil
import "json";

fn main() -> i32 {
    json.Value root, err parse_err = json.Value.parse("{\"items\":[1,2,3]}");
    if parse_err != ok { return 1; }

    if !root.is_object() || !root.has("items") { return 2; }

    json.Value items, err items_err = root.get("items");
    if items_err != ok || !items.is_array() { return 3; }

    i32 count, err len_err = items.len();
    if len_err != ok { return 4; }

    return count == 3 ? 0 : 5;
}
```

### Pattern: typed config loading

```vigil
import "json";

class ServerConfig {
    pub string host;
    pub i32 port;
}

fn main() -> i32 {
    json.Value raw, err read_err = json.Value.read("config.json");
    if read_err != ok { return 1; }

    ServerConfig cfg, err decode_err =
        json.decode(raw.stringify(), ServerConfig("", 0));
    if decode_err != ok { return 2; }

    return cfg.port == 8080 ? 0 : 3;
}
```

### Pattern: encode a class for transport or storage

```vigil
import "json";

class BuildInfo {
    pub string version;
    pub i32 build;
}

fn main() -> i32 {
    string text, err encode_err = json.encode(BuildInfo("0.2.2", 42));
    if encode_err != ok { return 1; }

    return text.len() > 0 ? 0 : 2;
}
```

## Current Limitations

These are the important current limits of the module:

- there is no pretty-print formatter yet; `stringify()` emits compact JSON
- `json.decode()` is strict and does not ignore extra fields
- `json.decode()` currently targets typed values through a prototype, not a generic `decode<T>()` form
- only public class fields participate in typed decoding
- `map` support is limited to string keys
- `nil` / JSON `null` is not yet a full typed marshaling story
- typed decode is designed for concrete classes and containers, not arbitrary interface-based shapes

## Recommended Usage

Use `json.Value` when:

- you need to inspect unknown JSON
- you are dealing with loosely typed external payloads
- you want to preserve arbitrary JSON structure

Use `json.encode()` / `json.decode()` when:

- you control the schema
- you want typed program data instead of manual tree traversal
- your JSON maps naturally to Vigil classes, arrays, and maps
