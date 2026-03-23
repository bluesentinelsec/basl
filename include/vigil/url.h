/* VIGIL URL parsing library.
 *
 * Implements RFC 3986 URI parsing with percent-encoding/decoding.
 * Pure C11, no external dependencies.
 */
#ifndef VIGIL_URL_H
#define VIGIL_URL_H

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Parsed URL structure.
     * All strings are owned by the structure and freed by vigil_url_free().
     */
    typedef struct vigil_url
    {
        char *scheme;                /* e.g. "https" */
        char *username;              /* userinfo username (decoded) */
        char *password;              /* userinfo password (decoded), NULL if not set */
        char *host;                  /* hostname or IP (decoded) */
        char *port;                  /* port string, NULL if not specified */
        char *path;                  /* path (decoded) */
        char *raw_query;             /* query string without '?' (encoded) */
        char *fragment;              /* fragment without '#' (decoded) */
        vigil_allocator_t allocator; /* allocator used for all owned strings */
    } vigil_url_t;

    /**
     * Parse a URL string into components.
     * Returns VIGIL_STATUS_OK on success.
     */
    VIGIL_API vigil_status_t vigil_url_parse(const vigil_allocator_t *allocator, const char *url_string,
                                             size_t url_length, vigil_url_t *out_url, vigil_error_t *error);

    /**
     * Free all memory owned by a parsed URL.
     */
    VIGIL_API void vigil_url_free(vigil_url_t *url);

    /**
     * Reconstruct a URL string from components.
     * Caller must free the returned string with the same allocator.
     */
    VIGIL_API vigil_status_t vigil_url_string(const vigil_url_t *url, char **out_string, size_t *out_length,
                                              vigil_error_t *error);

    /**
     * Percent-encode a string for use in a URL path.
     * Caller must free the returned string with the same allocator.
     */
    VIGIL_API vigil_status_t vigil_url_path_escape(const vigil_allocator_t *allocator, const char *input,
                                                   size_t input_length, char **out_escaped, size_t *out_length,
                                                   vigil_error_t *error);

    /**
     * Percent-encode a string for use in a URL query.
     * Caller must free the returned string with the same allocator.
     */
    VIGIL_API vigil_status_t vigil_url_query_escape(const vigil_allocator_t *allocator, const char *input,
                                                    size_t input_length, char **out_escaped, size_t *out_length,
                                                    vigil_error_t *error);

    /**
     * Decode a percent-encoded string.
     * Caller must free the returned string with the same allocator.
     */
    VIGIL_API vigil_status_t vigil_url_unescape(const vigil_allocator_t *allocator, const char *input,
                                                size_t input_length, char **out_decoded, size_t *out_length,
                                                vigil_error_t *error);

    /**
     * Get the hostname from a URL (without port).
     */
    VIGIL_API const char *vigil_url_hostname(const vigil_url_t *url);

    /**
     * Check if URL is absolute (has scheme).
     */
    VIGIL_API int vigil_url_is_absolute(const vigil_url_t *url);

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_URL_H */
