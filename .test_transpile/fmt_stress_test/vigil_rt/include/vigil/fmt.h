#ifndef VIGIL_FMT_H
#define VIGIL_FMT_H

#include <stddef.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/status.h"
#include "vigil/token.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Format VIGIL source code.
     *
     * Takes an optional allocator (NULL for default), the original source text
     * and its token list (produced by vigil_lex_source).  Returns a newly
     * allocated formatted string in *out_text with length *out_length.
     * Caller must free *out_text with the same allocator.
     *
     * Comments are preserved by scanning the source text between token spans.
     */
    VIGIL_API vigil_status_t vigil_fmt(const vigil_allocator_t *allocator, const char *source_text,
                                       size_t source_length, const vigil_token_list_t *tokens, char **out_text,
                                       size_t *out_length, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
