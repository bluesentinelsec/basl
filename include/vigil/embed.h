#ifndef VIGIL_EMBED_H
#define VIGIL_EMBED_H

#include <stddef.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Generate a VIGIL source module that embeds a single file as base64.
     * Caller must free *out_text with the same allocator.
     */
    VIGIL_API vigil_status_t vigil_embed_single(const vigil_allocator_t *allocator, const char *file_path,
                                                char **out_text, size_t *out_length, vigil_error_t *error);

    /*
     * Generate a VIGIL source module that embeds multiple files as base64.
     * Provides get(path), list(), and count.
     * Caller must free *out_text with the same allocator.
     */
    VIGIL_API vigil_status_t vigil_embed_multi(const vigil_allocator_t *allocator, const char **file_paths,
                                               const char **rel_paths, size_t file_count, char **out_text,
                                               size_t *out_length, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
