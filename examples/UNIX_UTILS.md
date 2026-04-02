# Unix Utilities Implementation - Language Validation

This directory contains implementations of common Unix utilities in VIGIL to validate the language and identify gaps.

## Implemented Utilities

### 1. vigil-sort
Sorts lines from stdin alphabetically.

**Usage:** `cat file.txt | vigil-sort`

**Status:** ✅ Working

### 2. vigil-uniq
Removes consecutive duplicate lines from stdin.

**Usage:** `cat file.txt | vigil-uniq`

**Status:** ✅ Working

### 3. vigil-nl
Numbers lines from stdin.

**Usage:** `cat file.txt | vigil-nl`

**Status:** ✅ Working

### 4. vigil-cut
Extracts fields from delimited text.

**Usage:** `cat file.txt | vigil-cut --f 2 --d :`

**Status:** ✅ Working

### 5. vigil-tr
Translates characters (character-by-character replacement).

**Usage:** `echo "hello" | vigil-tr aeiou 12345`

**Status:** ✅ Working

### 6. vigil-paste
Merges lines from multiple files side-by-side.

**Usage:** `vigil-paste file1.txt file2.txt`

**Status:** ✅ Working

## Language Gaps Identified

### 1. Missing `strings.split()` function ⚠️
**Impact:** Medium  
**Workaround:** Use built-in `string.split()` method instead  
**Found in:** vigil-cut implementation  
**Resolution:** Documentation shows `string.split()` method exists, so no stdlib function needed

### 2. No `io.read()` for chunked stdin reading ⚠️
**Impact:** High  
**Status:** Implemented in PR #20  
**Found in:** vigil-tr (needs to process large streams efficiently)  
**Resolution:** Using `io.read(4096)` for chunked processing

### 3. No documented way to distinguish EOF from other errors ⚠️
**Impact:** Medium  
**Status:** ✅ Resolved - Typed error kinds distinguish EOF from other errors  
**Issue:** Previously had to rely on `string(err) == "err(\"EOF\")"` to detect EOF vs I/O errors  
**Found in:** vigil-paste (needs to distinguish EOF from read failures)  
**Resolution:** Typed error kinds: `e.kind() == err.eof` distinguishes EOF from other errors  
**Usage:** `if e.kind() == err.eof { /* handle EOF */ }`

### 4. Type casting verbosity
**Impact:** Low  
**Example:** `c >= u8(48) && c <= u8(57)` requires explicit casts  
**Found in:** vigil-cut (parsing field numbers)  
**Note:** This is by design for type safety

### 5. No string interpolation in map access
**Impact:** Low  
**Example:** Cannot use `f"{opts['f']}"` directly in f-strings  
**Workaround:** Assign to variable first: `string val = opts["f"]; fmt.println(f"{val}");`  
**Found in:** vigil-cut

## Language Features Validated ✅

1. **I/O Operations**
   - `io.read_line()` - line-by-line stdin reading
   - `io.read()` - chunked stdin reading (new)
   - `file.open()` and `File.read_line()` - file operations

2. **String Operations**
   - `string.split()` - split by delimiter
   - `string.char_at()` - character access
   - `string.len()` - length
   - String concatenation with `+=`

3. **Array Operations**
   - `array.push()` - append elements
   - `array.len()` - length
   - Array indexing with `[]`
   - For-in loops over arrays

4. **Control Flow**
   - While loops with break
   - For loops with counters
   - For-in loops
   - If/else conditionals

5. **Error Handling**
   - Multi-return values `(value, err)`
   - Error checking with `e != ok`
   - Error propagation

6. **Command-line Parsing**
   - `args.parser()` - argument parser
   - `p.flag()` - define flags
   - `p.parse()` - parse arguments
   - `os.args()` - raw arguments

7. **Type System**
   - Static typing with inference
   - Explicit type casts
   - Type safety enforcement

## Testing

All utilities have automated test suites using the VIGIL test framework (`t.assert`):

```bash
# Run all utility tests
./vigil test examples/vigil-*/test/

# Or test individually
./vigil test examples/vigil-sort/test/
./vigil test examples/vigil-uniq/test/
./vigil test examples/vigil-nl/test/
./vigil test examples/vigil-cut/test/
./vigil test examples/vigil-tr/test/
./vigil test examples/vigil-paste/test/
```

**Platform note:** Most tests require a Unix-like shell (`sh`) for stdin redirection and pipes. Windows users should run tests in WSL, Git Bash, or similar environment. The `paste` test is fully cross-platform (uses file arguments only).

**Test coverage:**
- Exact output validation (full string equality, not substring checks)
- Error case handling (invalid inputs, malformed arguments)
- Edge cases (empty input, single file, uneven files)
- Exit code verification
- I/O error handling (tr and paste distinguish EOF from read errors)

**Example test assertions:**
```c
// sort_test.vigil
t.assert(out == "apple\nbanana\nzebra\n", "should sort lines alphabetically");

// uniq_test.vigil
t.assert(out == "a\nb\nc\n", "should remove consecutive duplicates");

// cut_test.vigil
t.assert(out == "b\ne\n", "should extract field 2 from both lines");

// tr_test.vigil
t.assert(out == "h2ll4 w4rld\n", "should translate all vowels");

// paste_test.vigil
t.assert(out == "a\t1\nb\t2\nc\t3\n", "should merge lines with tabs");
```

All tests verify exact output, ensuring implementations are correct and complete.

## Conclusion

VIGIL successfully implements all target Unix utilities with minimal friction. The language provides:

- ✅ Sufficient I/O primitives
- ✅ Good string manipulation
- ✅ Solid array operations
- ✅ Clean error handling
- ✅ Effective command-line parsing

**Main finding:** The language is production-ready for systems scripting and CLI tool development. The only significant gap (`io.read()`) has been addressed in PR #20.
