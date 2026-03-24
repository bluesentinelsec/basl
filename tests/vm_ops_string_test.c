/*
 * vm_ops_string_test.c — Unit tests for vm_ops_string.c string opcode handlers.
 *
 * Tests are structured as compiled Vigil programs that return 0 on success
 * or a non-zero code identifying the failing assertion.
 */

#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "vigil/vigil.h"

/* ── Test helper ───────────────────────────────────────────────── */

static vigil_status_t RunStringSource(const char *source_text, int64_t *out_result, vigil_error_t *error)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_source_id_t source_id = 0U;
    vigil_status_t status;

    status = vigil_runtime_open(&runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_vm_open(&vm, runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_runtime_close(&runtime);
        return status;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    status = vigil_source_registry_register_cstr(&registry, "main.vigil", source_text, &source_id, error);
    if (status == VIGIL_STATUS_OK)
        status = vigil_compile_source(&registry, source_id, &function, &diagnostics, error);
    if (status == VIGIL_STATUS_OK)
    {
        vigil_value_init_nil(&result);
        status = vigil_vm_execute_function(vm, function, &result, error);
        if (status == VIGIL_STATUS_OK && out_result != NULL)
            *out_result = vigil_value_as_int(&result);
        vigil_value_release(&result);
    }

    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return status;
}

/* TEST() expands into generated functions with many assertion branches.
   Suppress cognitive-complexity diagnostics for this assertion-heavy test region. */
// NOLINTBEGIN(readability-function-cognitive-complexity)

/* ── GET_STRING_SIZE (len) ─────────────────────────────────────── */

TEST(VmOpsStringTest, StringLenReturnsCorrectLength)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hello\".len() != 5) { return 1; }\n"
                  "    if (\"\".len() != 0) { return 2; }\n"
                  "    if (\"a\".len() != 1) { return 3; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_SEARCH (contains, starts_with, ends_with) ──────────── */

TEST(VmOpsStringTest, StringContainsMatchAndNoMatch)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (!\"hello\".contains(\"ell\")) { return 1; }\n"
                  "    if (!\"hello\".contains(\"hello\")) { return 2; }\n"
                  "    if (!\"hello\".contains(\"h\")) { return 3; }\n"
                  "    if (\"hello\".contains(\"xyz\")) { return 4; }\n"
                  "    if (\"hello\".contains(\"Hello\")) { return 5; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringStartsWithMatchAndNoMatch)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (!\"hello\".starts_with(\"hel\")) { return 1; }\n"
                  "    if (!\"hello\".starts_with(\"hello\")) { return 2; }\n"
                  "    if (!\"hello\".starts_with(\"\")) { return 3; }\n"
                  "    if (\"hello\".starts_with(\"ell\")) { return 4; }\n"
                  "    if (\"hello\".starts_with(\"helloo\")) { return 5; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringEndsWithMatchAndNoMatch)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (!\"hello\".ends_with(\"llo\")) { return 1; }\n"
                  "    if (!\"hello\".ends_with(\"hello\")) { return 2; }\n"
                  "    if (!\"hello\".ends_with(\"\")) { return 3; }\n"
                  "    if (\"hello\".ends_with(\"ell\")) { return 4; }\n"
                  "    if (\"hello\".ends_with(\"hhelllo\")) { return 5; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_TRANSFORM (trim, to_upper, to_lower) ────────────────── */

TEST(VmOpsStringTest, StringTrimRemovesWhitespace)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"  hello  \".trim() != \"hello\") { return 1; }\n"
                  "    if (\"  \".trim() != \"\") { return 2; }\n"
                  "    if (\"hello\".trim() != \"hello\") { return 3; }\n"
                  "    if (\"\".trim() != \"\") { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringToUpperAndToLower)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hello\".to_upper() != \"HELLO\") { return 1; }\n"
                  "    if (\"HELLO\".to_lower() != \"hello\") { return 2; }\n"
                  "    if (\"\".to_upper() != \"\") { return 3; }\n"
                  "    if (\"\".to_lower() != \"\") { return 4; }\n"
                  "    if (\"Hello World\".to_lower() != \"hello world\") { return 5; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_REPLACE ─────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringReplaceSubstitutesAllOccurrences)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hello\".replace(\"l\", \"r\") != \"herro\") { return 1; }\n"
                  "    if (\"hello\".replace(\"x\", \"y\") != \"hello\") { return 2; }\n"
                  "    if (\"hello\".replace(\"l\", \"\") != \"heo\") { return 3; }\n"
                  "    if (\"\".replace(\"x\", \"y\") != \"\") { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringReplaceWithEmptyOldReturnsOriginal)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hello\".replace(\"\", \"x\") != \"hello\") { return 1; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_SPLIT ───────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringSplitBySeparator)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    array<string> parts = \"a,b,c\".split(\",\");\n"
                  "    if (parts.len() != 3) { return 1; }\n"
                  "    if (parts[0] != \"a\") { return 2; }\n"
                  "    if (parts[1] != \"b\") { return 3; }\n"
                  "    if (parts[2] != \"c\") { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringSplitNoMatchReturnsWholeString)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    array<string> parts = \"hello\".split(\",\");\n"
                  "    if (parts.len() != 1) { return 1; }\n"
                  "    if (parts[0] != \"hello\") { return 2; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringSplitByEmptySeparatorSplitsChars)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    array<string> parts = \"abc\".split(\"\");\n"
                  "    if (parts.len() != 3) { return 1; }\n"
                  "    if (parts[0] != \"a\") { return 2; }\n"
                  "    if (parts[1] != \"b\") { return 3; }\n"
                  "    if (parts[2] != \"c\") { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_INDEX_OF ────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringIndexOfReturnsPositionWhenFound)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    i32 idx, bool found = \"hello\".index_of(\"ell\");\n"
                  "    if (!found) { return 1; }\n"
                  "    if (idx != 1) { return 2; }\n"
                  "    i32 idx2, bool found2 = \"hello\".index_of(\"h\");\n"
                  "    if (!found2) { return 3; }\n"
                  "    if (idx2 != 0) { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringIndexOfReturnsNegativeOneWhenNotFound)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    i32 idx, bool found = \"hello\".index_of(\"xyz\");\n"
                  "    if (found) { return 1; }\n"
                  "    if (idx != -1) { return 2; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_SUBSTR ──────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringSubstrExtractsSlice)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    string sub, err e = \"hello\".substr(1, 3);\n"
                  "    if (e != ok) { return 1; }\n"
                  "    if (sub != \"ell\") { return 2; }\n"
                  "    string sub2, err e2 = \"hello\".substr(0, 5);\n"
                  "    if (e2 != ok) { return 3; }\n"
                  "    if (sub2 != \"hello\") { return 4; }\n"
                  "    string sub3, err e3 = \"hello\".substr(2, 0);\n"
                  "    if (e3 != ok) { return 5; }\n"
                  "    if (sub3 != \"\") { return 6; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringSubstrReturnsErrorForOutOfRange)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    string sub, err e = \"hello\".substr(10, 1);\n"
                  "    if (e == ok) { return 1; }\n"
                  "    string sub2, err e2 = \"hello\".substr(0, 10);\n"
                  "    if (e2 == ok) { return 2; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_BYTES ───────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringBytesReturnsCorrectLength)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"abc\".bytes().len() != 3) { return 1; }\n"
                  "    if (\"\".bytes().len() != 0) { return 2; }\n"
                  "    if (\"x\".bytes().len() != 1) { return 3; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_CHAR_AT ─────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringCharAtReturnsCharacterInRange)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    string ch, err e = \"hello\".char_at(0);\n"
                  "    if (e != ok) { return 1; }\n"
                  "    if (ch != \"h\") { return 2; }\n"
                  "    string ch2, err e2 = \"hello\".char_at(4);\n"
                  "    if (e2 != ok) { return 3; }\n"
                  "    if (ch2 != \"o\") { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringCharAtReturnsErrorOutOfRange)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    string ch, err e = \"hello\".char_at(10);\n"
                  "    if (e == ok) { return 1; }\n"
                  "    string ch2, err e2 = \"hello\".char_at(-1);\n"
                  "    if (e2 == ok) { return 2; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_TRIM_LEFT / STRING_TRIM_RIGHT ───────────────────────── */

TEST(VmOpsStringTest, StringTrimLeftAndTrimRight)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"  hello  \".trim_left() != \"hello  \") { return 1; }\n"
                  "    if (\"  hello  \".trim_right() != \"  hello\") { return 2; }\n"
                  "    if (\"  \".trim_left() != \"\") { return 3; }\n"
                  "    if (\"  \".trim_right() != \"\") { return 4; }\n"
                  "    if (\"hello\".trim_left() != \"hello\") { return 5; }\n"
                  "    if (\"hello\".trim_right() != \"hello\") { return 6; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_REVERSE ─────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringReverseReversesCharacters)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hello\".reverse() != \"olleh\") { return 1; }\n"
                  "    if (\"\".reverse() != \"\") { return 2; }\n"
                  "    if (\"a\".reverse() != \"a\") { return 3; }\n"
                  "    if (\"abcd\".reverse() != \"dcba\") { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_IS_EMPTY ────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringIsEmptyTrueAndFalse)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (!\"\".is_empty()) { return 1; }\n"
                  "    if (\"x\".is_empty()) { return 2; }\n"
                  "    if (\" \".is_empty()) { return 3; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_CHAR_COUNT ──────────────────────────────────────────── */

TEST(VmOpsStringTest, StringCharCountCountsAsciiCharacters)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hello\".char_count() != 5) { return 1; }\n"
                  "    if (\"\".char_count() != 0) { return 2; }\n"
                  "    if (\"a\".char_count() != 1) { return 3; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_REPEAT ──────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringRepeatReplicatesString)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"ab\".repeat(3) != \"ababab\") { return 1; }\n"
                  "    if (\"ab\".repeat(1) != \"ab\") { return 2; }\n"
                  "    if (\"ab\".repeat(0) != \"\") { return 3; }\n"
                  "    if (\"ab\".repeat(-1) != \"\") { return 4; }\n"
                  "    if (\"\".repeat(5) != \"\") { return 5; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_COUNT ───────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringCountCountsOccurrences)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hello\".count(\"l\") != 2) { return 1; }\n"
                  "    if (\"hello\".count(\"x\") != 0) { return 2; }\n"
                  "    if (\"aaa\".count(\"aa\") != 1) { return 3; }\n"
                  "    if (\"\".count(\"x\") != 0) { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_LAST_INDEX_OF ───────────────────────────────────────── */

TEST(VmOpsStringTest, StringLastIndexOfReturnsLastPosition)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    i32 idx, bool found = \"hello\".last_index_of(\"l\");\n"
                  "    if (!found) { return 1; }\n"
                  "    if (idx != 3) { return 2; }\n"
                  "    i32 idx2, bool found2 = \"hello\".last_index_of(\"h\");\n"
                  "    if (!found2) { return 3; }\n"
                  "    if (idx2 != 0) { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringLastIndexOfReturnsFalseWhenNotFound)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    i32 idx, bool found = \"hello\".last_index_of(\"xyz\");\n"
                  "    if (found) { return 1; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_TRIM_PREFIX / STRING_TRIM_SUFFIX ────────────────────── */

TEST(VmOpsStringTest, StringTrimPrefixAndSuffix)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hello\".trim_prefix(\"hel\") != \"lo\") { return 1; }\n"
                  "    if (\"hello\".trim_prefix(\"xyz\") != \"hello\") { return 2; }\n"
                  "    if (\"hello\".trim_prefix(\"\") != \"hello\") { return 3; }\n"
                  "    if (\"hello\".trim_suffix(\"llo\") != \"he\") { return 4; }\n"
                  "    if (\"hello\".trim_suffix(\"xyz\") != \"hello\") { return 5; }\n"
                  "    if (\"hello\".trim_suffix(\"\") != \"hello\") { return 6; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── CHAR_FROM_INT ──────────────────────────────────────────────── */

TEST(VmOpsStringTest, CharFromIntConvertsCodePoint)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (char(65) != \"A\") { return 1; }\n"
                  "    if (char(97) != \"a\") { return 2; }\n"
                  "    if (char(48) != \"0\") { return 3; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_TO_C ────────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringToCWrapsInQuotes)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    /* "hi" (2 bytes) → "hi" (4 bytes: quote + h + i + quote) */
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (\"hi\".to_c().len() != 4) { return 1; }\n"
                  "    if (\"\".to_c().len() != 2) { return 2; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringToCEscapesSpecialCharacters)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    /* "a\nb" (3 bytes) → "a\nb" (6 bytes: quote + a + backslash + n + b + quote) */
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    string s = \"a\\nb\";\n"
                  "    if (s.to_c().len() != 6) { return 1; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_FIELDS ──────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringFieldsSplitsOnWhitespace)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    array<string> parts = \"  hello   world  \".fields();\n"
                  "    if (parts.len() != 2) { return 1; }\n"
                  "    if (parts[0] != \"hello\") { return 2; }\n"
                  "    if (parts[1] != \"world\") { return 3; }\n"
                  "    array<string> empty = \"\".fields();\n"
                  "    if (empty.len() != 0) { return 4; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_EQUAL_FOLD ──────────────────────────────────────────── */

TEST(VmOpsStringTest, StringEqualFoldMatchesCaseInsensitively)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    if (!\"Go\".equal_fold(\"go\")) { return 1; }\n"
                  "    if (!\"HELLO\".equal_fold(\"hello\")) { return 2; }\n"
                  "    if (!\"MixedCase\".equal_fold(\"MIXEDCASE\")) { return 3; }\n"
                  "    if (\"abc\".equal_fold(\"abcd\")) { return 4; }\n"
                  "    if (\"a\".equal_fold(\"b\")) { return 5; }\n"
                  "    if (!\"\".equal_fold(\"\")) { return 6; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_CUT ─────────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringCutSplitsOnFirstSeparator)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    string before, string after, bool found = \"key=value\".cut(\"=\");\n"
                  "    if (!found) { return 1; }\n"
                  "    if (before != \"key\") { return 2; }\n"
                  "    if (after != \"value\") { return 3; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringCutReturnsFalseWhenSeparatorAbsent)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    string before, string after, bool found = \"no sep\".cut(\"=\");\n"
                  "    if (found) { return 1; }\n"
                  "    if (before != \"no sep\") { return 2; }\n"
                  "    if (after != \"\") { return 3; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

/* ── STRING_JOIN ────────────────────────────────────────────────── */

TEST(VmOpsStringTest, StringJoinCombinesArray)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    array<string> parts = [\"a\", \"b\", \"c\"];\n"
                  "    if (\",\".join(parts) != \"a,b,c\") { return 1; }\n"
                  "    if (\"\".join(parts) != \"abc\") { return 2; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

TEST(VmOpsStringTest, StringJoinEmptyArray)
{
    vigil_error_t error = {0};
    int64_t result = -1;
    ASSERT_EQ(RunStringSource(
                  "fn main() -> i32 {\n"
                  "    array<string> empty = [];\n"
                  "    if (\",\".join(empty) != \"\") { return 1; }\n"
                  "    return 0;\n"
                  "}",
                  &result, &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(result, 0);
    vigil_error_clear(&error);
}

// NOLINTEND(readability-function-cognitive-complexity)

void register_vm_ops_string_tests(void)
{
    REGISTER_TEST(VmOpsStringTest, StringLenReturnsCorrectLength);
    REGISTER_TEST(VmOpsStringTest, StringContainsMatchAndNoMatch);
    REGISTER_TEST(VmOpsStringTest, StringStartsWithMatchAndNoMatch);
    REGISTER_TEST(VmOpsStringTest, StringEndsWithMatchAndNoMatch);
    REGISTER_TEST(VmOpsStringTest, StringTrimRemovesWhitespace);
    REGISTER_TEST(VmOpsStringTest, StringToUpperAndToLower);
    REGISTER_TEST(VmOpsStringTest, StringReplaceSubstitutesAllOccurrences);
    REGISTER_TEST(VmOpsStringTest, StringReplaceWithEmptyOldReturnsOriginal);
    REGISTER_TEST(VmOpsStringTest, StringSplitBySeparator);
    REGISTER_TEST(VmOpsStringTest, StringSplitNoMatchReturnsWholeString);
    REGISTER_TEST(VmOpsStringTest, StringSplitByEmptySeparatorSplitsChars);
    REGISTER_TEST(VmOpsStringTest, StringIndexOfReturnsPositionWhenFound);
    REGISTER_TEST(VmOpsStringTest, StringIndexOfReturnsNegativeOneWhenNotFound);
    REGISTER_TEST(VmOpsStringTest, StringSubstrExtractsSlice);
    REGISTER_TEST(VmOpsStringTest, StringSubstrReturnsErrorForOutOfRange);
    REGISTER_TEST(VmOpsStringTest, StringBytesReturnsCorrectLength);
    REGISTER_TEST(VmOpsStringTest, StringCharAtReturnsCharacterInRange);
    REGISTER_TEST(VmOpsStringTest, StringCharAtReturnsErrorOutOfRange);
    REGISTER_TEST(VmOpsStringTest, StringTrimLeftAndTrimRight);
    REGISTER_TEST(VmOpsStringTest, StringReverseReversesCharacters);
    REGISTER_TEST(VmOpsStringTest, StringIsEmptyTrueAndFalse);
    REGISTER_TEST(VmOpsStringTest, StringCharCountCountsAsciiCharacters);
    REGISTER_TEST(VmOpsStringTest, StringRepeatReplicatesString);
    REGISTER_TEST(VmOpsStringTest, StringCountCountsOccurrences);
    REGISTER_TEST(VmOpsStringTest, StringLastIndexOfReturnsLastPosition);
    REGISTER_TEST(VmOpsStringTest, StringLastIndexOfReturnsFalseWhenNotFound);
    REGISTER_TEST(VmOpsStringTest, StringTrimPrefixAndSuffix);
    REGISTER_TEST(VmOpsStringTest, CharFromIntConvertsCodePoint);
    REGISTER_TEST(VmOpsStringTest, StringToCWrapsInQuotes);
    REGISTER_TEST(VmOpsStringTest, StringToCEscapesSpecialCharacters);
    REGISTER_TEST(VmOpsStringTest, StringFieldsSplitsOnWhitespace);
    REGISTER_TEST(VmOpsStringTest, StringEqualFoldMatchesCaseInsensitively);
    REGISTER_TEST(VmOpsStringTest, StringCutSplitsOnFirstSeparator);
    REGISTER_TEST(VmOpsStringTest, StringCutReturnsFalseWhenSeparatorAbsent);
    REGISTER_TEST(VmOpsStringTest, StringJoinCombinesArray);
    REGISTER_TEST(VmOpsStringTest, StringJoinEmptyArray);
}
