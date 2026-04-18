/* VIGIL regex engine - Thompson NFA implementation (RE2-style)
 *
 * Guarantees linear time O(n*m) matching where n=pattern, m=input.
 * No backtracking, no pathological cases.
 */
#ifndef VIGIL_STDLIB_REGEX_H
#define VIGIL_STDLIB_REGEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vigil/allocator.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum number of capture groups */
#define VIGIL_REGEX_MAX_GROUPS 32

    /* Opaque regex handle */
    typedef struct vigil_regex vigil_regex_t;

    /* Match result for a single capture group */
    typedef struct vigil_regex_match
    {
        size_t start; /* Start offset in input (SIZE_MAX if not matched) */
        size_t end;   /* End offset in input */
    } vigil_regex_match_t;

    /* Full match result */
    typedef struct vigil_regex_result
    {
        bool matched;
        size_t group_count;
        vigil_regex_match_t groups[VIGIL_REGEX_MAX_GROUPS];
    } vigil_regex_result_t;

    /* Inline flags parsed from (?i), (?m), (?s) or the flags string API. */
    typedef struct
    {
        bool case_insensitive; /* (?i) */
        bool multiline;        /* (?m) */
        bool dotall;           /* (?s) */
    } regex_flags_t;

    /* Compile a regex pattern. Returns NULL on error, sets error message. */
    vigil_regex_t *vigil_regex_compile(const vigil_allocator_t *allocator, const char *pattern, size_t pattern_len,
                                       char *error_buf, size_t error_buf_size);

    /* Compile with explicit flags string (e.g. "ims"). */
    vigil_regex_t *vigil_regex_compile_with_flags(const vigil_allocator_t *allocator, const char *pattern,
                                                   size_t pattern_len, const char *flags, size_t flags_len,
                                                   char *error_buf, size_t error_buf_size);

    /* Free a compiled regex */
    void vigil_regex_free(vigil_regex_t *re);

    /* Check if input matches the pattern (anchored at start) */
    bool vigil_regex_match(const vigil_regex_t *re, const char *input, size_t input_len, vigil_regex_result_t *result);

    /* Find first match anywhere in input */
    bool vigil_regex_find(const vigil_regex_t *re, const char *input, size_t input_len, vigil_regex_result_t *result);

    /* Find all non-overlapping matches */
    size_t vigil_regex_find_all(const vigil_regex_t *re, const char *input, size_t input_len,
                                vigil_regex_result_t *results, size_t max_results);

    /* Replace first match with replacement string */
    vigil_status_t vigil_regex_replace(const vigil_regex_t *re, const char *input, size_t input_len,
                                       const char *replacement, size_t replacement_len, char **output,
                                       size_t *output_len);

    /* Replace all matches with replacement string */
    vigil_status_t vigil_regex_replace_all(const vigil_regex_t *re, const char *input, size_t input_len,
                                           const char *replacement, size_t replacement_len, char **output,
                                           size_t *output_len);

    /* Split input by pattern */
    vigil_status_t vigil_regex_split(const vigil_regex_t *re, const char *input, size_t input_len, char ***parts,
                                     size_t **part_lens, size_t *part_count);

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_STDLIB_REGEX_H */
