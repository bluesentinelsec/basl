#include "vigil/builtins.h"

#include <string.h>

static const vigil_doc_entry_t builtin_module_doc = {"builtins", NULL, "Built-in functions available without import.",
                                                     "These functions are always available in VIGIL programs.", NULL};

static const vigil_doc_entry_t builtin_symbol_docs[] = {
    {"len", "len(value: string | array | map) -> i32", "Return the length of a string, array, or map.", NULL,
     "len(\"hello\")  // 5\nlen([1, 2, 3])  // 3"},
    {"char", "char(code: integer) -> string", "Convert a byte value (0-255) to a single-character string.", NULL,
     "char(65)   // \"A\"\nchar(0x0a) // \"\\n\""},
    {"err", "err(message: string, kind: i32) -> err", "Construct an error value.", NULL,
     "err e = err(\"missing config\", 1)"},
    {"i32", "i32(value: integer | f64) -> i32", "Convert an integer or f64 value to i32.", NULL,
     "i32 port = i32(8080)"},
    {"i64", "i64(value: integer | f64) -> i64", "Convert an integer or f64 value to i64.", NULL,
     "i64 count = i64(42)"},
    {"u8", "u8(value: integer | f64) -> u8", "Convert an integer or f64 value to u8.", NULL,
     "u8 byte = u8(255)"},
    {"u32", "u32(value: integer | f64) -> u32", "Convert an integer or f64 value to u32.", NULL,
     "u32 mask = u32(7)"},
    {"u64", "u64(value: integer | f64) -> u64", "Convert an integer or f64 value to u64.", NULL,
     "u64 size = u64(4096)"},
    {"f64", "f64(value: string | integer | f64 | bool) -> f64", "Convert a value to f64.", NULL,
     "f64 ratio = f64(\"3.14\")"},
    {"bool", "bool(value: string | bool) -> bool", "Convert a bool value to bool.", NULL,
     "bool enabled = bool(true)"},
    {"string", "string(value: string | integer | f64 | bool) -> string", "Convert a value to string.", NULL,
     "string text = string(42)"},
};

static const vigil_builtin_descriptor_t builtin_descriptors_[] = {
    {VIGIL_BUILTIN_LEN, "len", 3U, 0, VIGIL_TYPE_INVALID, &builtin_symbol_docs[0]},
    {VIGIL_BUILTIN_CHAR, "char", 4U, 0, VIGIL_TYPE_INVALID, &builtin_symbol_docs[1]},
    {VIGIL_BUILTIN_ERR, "err", 3U, 0, VIGIL_TYPE_INVALID, &builtin_symbol_docs[2]},
    {VIGIL_BUILTIN_I32, "i32", 3U, 1, VIGIL_TYPE_I32, &builtin_symbol_docs[3]},
    {VIGIL_BUILTIN_I64, "i64", 3U, 1, VIGIL_TYPE_I64, &builtin_symbol_docs[4]},
    {VIGIL_BUILTIN_U8, "u8", 2U, 1, VIGIL_TYPE_U8, &builtin_symbol_docs[5]},
    {VIGIL_BUILTIN_U32, "u32", 3U, 1, VIGIL_TYPE_U32, &builtin_symbol_docs[6]},
    {VIGIL_BUILTIN_U64, "u64", 3U, 1, VIGIL_TYPE_U64, &builtin_symbol_docs[7]},
    {VIGIL_BUILTIN_F64, "f64", 3U, 1, VIGIL_TYPE_F64, &builtin_symbol_docs[8]},
    {VIGIL_BUILTIN_BOOL, "bool", 4U, 1, VIGIL_TYPE_BOOL, &builtin_symbol_docs[9]},
    {VIGIL_BUILTIN_STRING, "string", 6U, 1, VIGIL_TYPE_STRING, &builtin_symbol_docs[10]},
};

static const vigil_doc_entry_t string_method_module_doc = {"strings", NULL, "String methods available on all string values.",
                                                           "These methods are called on string values using dot notation.",
                                                           NULL};

static const vigil_doc_entry_t string_method_docs[] = {
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
     "Similar to Go's strings.Fields. Splits on runs of whitespace.", "\"  a  b  c  \".fields()  // [\"a\", \"b\", \"c\"]"},
    {"strings.join", "sep.join(arr: array<string>) -> string", "Join array elements with sep as separator.",
     "The separator is the receiver, the array is the argument.", "\",\".join([\"a\", \"b\", \"c\"])  // \"a,b,c\""},
    {"strings.cut", "s.cut(sep: string) -> (string, string, bool)", "Cut s around the first instance of sep.",
     "Returns (before, after, found). If sep is not found, returns (s, \"\", false).",
     "string k, string v, bool ok = \"key=val\".cut(\"=\")  // \"key\", \"val\", true"},
    {"strings.equal_fold", "s.equal_fold(t: string) -> bool",
     "Return true if s equals t under case-insensitive comparison.", "Compares ASCII letters case-insensitively.",
     "\"Go\".equal_fold(\"go\")  // true"},
};

static const vigil_string_method_descriptor_t string_method_descriptors_[] = {
    {"len", 3U, &string_method_docs[0]},
    {"contains", 8U, &string_method_docs[1]},
    {"starts_with", 11U, &string_method_docs[2]},
    {"ends_with", 9U, &string_method_docs[3]},
    {"trim", 4U, &string_method_docs[4]},
    {"trim_left", 9U, &string_method_docs[5]},
    {"trim_right", 10U, &string_method_docs[6]},
    {"trim_prefix", 11U, &string_method_docs[7]},
    {"trim_suffix", 11U, &string_method_docs[8]},
    {"to_upper", 8U, &string_method_docs[9]},
    {"to_lower", 8U, &string_method_docs[10]},
    {"replace", 7U, &string_method_docs[11]},
    {"split", 5U, &string_method_docs[12]},
    {"index_of", 8U, &string_method_docs[13]},
    {"last_index_of", 13U, &string_method_docs[14]},
    {"substr", 6U, &string_method_docs[15]},
    {"char_at", 7U, &string_method_docs[16]},
    {"bytes", 5U, &string_method_docs[17]},
    {"reverse", 7U, &string_method_docs[18]},
    {"is_empty", 8U, &string_method_docs[19]},
    {"char_count", 10U, &string_method_docs[20]},
    {"repeat", 6U, &string_method_docs[21]},
    {"count", 5U, &string_method_docs[22]},
    {"fields", 6U, &string_method_docs[23]},
    {"join", 4U, &string_method_docs[24]},
    {"cut", 3U, &string_method_docs[25]},
    {"equal_fold", 10U, &string_method_docs[26]},
};

static vigil_doc_entry_t builtin_module_entries[1U + sizeof(builtin_descriptors_) / sizeof(builtin_descriptors_[0])];
static vigil_doc_entry_t string_method_module_entries[1U + sizeof(string_method_descriptors_) / sizeof(string_method_descriptors_[0])];
static int builtin_module_entries_ready = 0;
static int string_method_module_entries_ready = 0;

static void init_builtin_module_entries(void)
{
    size_t i;

    if (builtin_module_entries_ready)
    {
        return;
    }

    builtin_module_entries[0] = builtin_module_doc;
    for (i = 0U; i < sizeof(builtin_descriptors_) / sizeof(builtin_descriptors_[0]); i += 1U)
    {
        builtin_module_entries[i + 1U] = *builtin_descriptors_[i].doc_entry;
    }
    builtin_module_entries_ready = 1;
}

static void init_string_method_module_entries(void)
{
    size_t i;

    if (string_method_module_entries_ready)
    {
        return;
    }

    string_method_module_entries[0] = string_method_module_doc;
    for (i = 0U; i < sizeof(string_method_descriptors_) / sizeof(string_method_descriptors_[0]); i += 1U)
    {
        string_method_module_entries[i + 1U] = *string_method_descriptors_[i].doc_entry;
    }
    string_method_module_entries_ready = 1;
}

const vigil_builtin_descriptor_t *vigil_builtin_descriptors(size_t *count)
{
    if (count != NULL)
    {
        *count = sizeof(builtin_descriptors_) / sizeof(builtin_descriptors_[0]);
    }
    return builtin_descriptors_;
}

const vigil_builtin_descriptor_t *vigil_builtin_find(const char *name, size_t name_length)
{
    size_t i;

    if (name == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < sizeof(builtin_descriptors_) / sizeof(builtin_descriptors_[0]); i += 1U)
    {
        if (builtin_descriptors_[i].name_length == name_length &&
            memcmp(builtin_descriptors_[i].name, name, name_length) == 0)
        {
            return &builtin_descriptors_[i];
        }
    }

    return NULL;
}

const vigil_doc_entry_t *vigil_builtin_doc_entries(size_t *count)
{
    init_builtin_module_entries();
    if (count != NULL)
    {
        *count = sizeof(builtin_module_entries) / sizeof(builtin_module_entries[0]);
    }
    return builtin_module_entries;
}

const vigil_doc_entry_t *vigil_builtin_doc_lookup(const char *name)
{
    const vigil_builtin_descriptor_t *descriptor;

    if (name == NULL)
    {
        return NULL;
    }
    if (strcmp(name, builtin_module_doc.name) == 0)
    {
        return &builtin_module_doc;
    }

    descriptor = vigil_builtin_find(name, strlen(name));
    return descriptor == NULL ? NULL : descriptor->doc_entry;
}

const vigil_string_method_descriptor_t *vigil_string_method_descriptors(size_t *count)
{
    if (count != NULL)
    {
        *count = sizeof(string_method_descriptors_) / sizeof(string_method_descriptors_[0]);
    }
    return string_method_descriptors_;
}

const vigil_doc_entry_t *vigil_string_method_doc_entries(size_t *count)
{
    init_string_method_module_entries();
    if (count != NULL)
    {
        *count = sizeof(string_method_module_entries) / sizeof(string_method_module_entries[0]);
    }
    return string_method_module_entries;
}

const vigil_doc_entry_t *vigil_string_method_doc_lookup(const char *name)
{
    size_t i;

    if (name == NULL)
    {
        return NULL;
    }
    if (strcmp(name, string_method_module_doc.name) == 0)
    {
        return &string_method_module_doc;
    }

    for (i = 0U; i < sizeof(string_method_descriptors_) / sizeof(string_method_descriptors_[0]); i += 1U)
    {
        if (strcmp(string_method_descriptors_[i].doc_entry->name, name) == 0)
        {
            return string_method_descriptors_[i].doc_entry;
        }
    }

    return NULL;
}
