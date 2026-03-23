/* VIGIL URL parsing library implementation.
 *
 * Implements RFC 3986 URI parsing.
 */
#include "vigil/url.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static char *str_ndup(const char *s, size_t n)
{
    char *r = malloc(n + 1);
    if (r)
    {
        memcpy(r, s, n);
        r[n] = '\0';
    }
    return r;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int is_unreserved(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '.' || c == '_' || c == '~';
}

/* ── Percent Encoding/Decoding ───────────────────────────────────── */

vigil_status_t vigil_url_unescape(const char *input, size_t input_length, char **out_decoded, size_t *out_length,
                                  vigil_error_t *error)
{
    char *result;
    size_t i, j;

    if (!input || !out_decoded)
    {
        if (error)
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "null argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    result = malloc(input_length + 1);
    if (!result)
    {
        if (error)
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    for (i = 0, j = 0; i < input_length; i++)
    {
        if (input[i] == '%' && i + 2 < input_length)
        {
            int h1 = hex_digit(input[i + 1]);
            int h2 = hex_digit(input[i + 2]);
            if (h1 >= 0 && h2 >= 0)
            {
                result[j++] = (char)((h1 << 4) | h2);
                i += 2;
                continue;
            }
        }
        if (input[i] == '+')
        {
            result[j++] = ' '; /* Query string convention */
        }
        else
        {
            result[j++] = input[i];
        }
    }
    result[j] = '\0';

    *out_decoded = result;
    if (out_length)
        *out_length = j;
    return VIGIL_STATUS_OK;
}

static vigil_status_t percent_encode(const char *input, size_t input_length, int encode_slash, int encode_plus,
                                     char **out_escaped, size_t *out_length, vigil_error_t *error)
{
    static const char hex[] = "0123456789ABCDEF";
    char *result;
    size_t i, j, needed;

    if (!input || !out_escaped)
    {
        if (error)
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "null argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Calculate needed size */
    needed = 0;
    for (i = 0; i < input_length; i++)
    {
        unsigned char c = (unsigned char)input[i];
        if (is_unreserved((char)c) || (!encode_slash && c == '/') || (!encode_plus && c == ' '))
            needed++;
        else
            needed += 3;
    }

    result = malloc(needed + 1);
    if (!result)
    {
        if (error)
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    for (i = 0, j = 0; i < input_length; i++)
    {
        unsigned char c = (unsigned char)input[i];
        if (is_unreserved((char)c) || (!encode_slash && c == '/'))
        {
            result[j++] = (char)c;
        }
        else if (!encode_plus && c == ' ')
        {
            result[j++] = '+';
        }
        else
        {
            result[j++] = '%';
            result[j++] = hex[c >> 4];
            result[j++] = hex[c & 0x0F];
        }
    }
    result[j] = '\0';

    *out_escaped = result;
    if (out_length)
        *out_length = j;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_url_path_escape(const char *input, size_t input_length, char **out_escaped, size_t *out_length,
                                     vigil_error_t *error)
{
    return percent_encode(input, input_length, 1, 1, out_escaped, out_length, error);
}

vigil_status_t vigil_url_query_escape(const char *input, size_t input_length, char **out_escaped, size_t *out_length,
                                      vigil_error_t *error)
{
    return percent_encode(input, input_length, 1, 0, out_escaped, out_length, error);
}

/* ── URL Parsing ─────────────────────────────────────────────────── */

static vigil_status_t url_unescape_into(const char *input, size_t length, char **out_field)
{
    *out_field = NULL;
    return vigil_url_unescape(input, length, out_field, NULL, NULL);
}

static vigil_status_t url_escape_append(char *buf, size_t cap, size_t *len, const char *prefix, const char *input,
                                        size_t input_len)
{
    char *escaped = NULL;
    vigil_status_t s = vigil_url_path_escape(input, input_len, &escaped, NULL, NULL);
    if (s != VIGIL_STATUS_OK)
        return s;
    *len += (size_t)snprintf(buf + *len, cap - *len, "%s%s", prefix, escaped);
    free(escaped);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_url_parse(const char *url_string, size_t url_length, vigil_url_t *out_url, vigil_error_t *error)
{
    const char *p, *end, *scheme_end, *authority_start, *authority_end;
    const char *userinfo_end, *host_start, *host_end, *port_start;
    const char *path_start, *path_end, *query_start, *query_end;
    const char *fragment_start;

    if (!url_string || !out_url)
    {
        if (error)
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "null argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    memset(out_url, 0, sizeof(*out_url));
    p = url_string;
    end = url_string + url_length;

    /* Parse scheme (if present) */
    scheme_end = NULL;
    for (const char *s = p; s < end; s++)
    {
        if (*s == ':')
        {
            scheme_end = s;
            break;
        }
        if (*s == '/' || *s == '?' || *s == '#')
            break;
        if (s == p && !isalpha((unsigned char)*s))
            break;
        if (s > p && !isalnum((unsigned char)*s) && *s != '+' && *s != '-' && *s != '.')
            break;
    }

    if (scheme_end)
    {
        out_url->scheme = str_ndup(p, (size_t)(scheme_end - p));
        p = scheme_end + 1;
    }

    /* Parse authority (if present) */
    authority_start = NULL;
    authority_end = NULL;
    if (p + 1 < end && p[0] == '/' && p[1] == '/')
    {
        p += 2;
        authority_start = p;
        /* Find end of authority */
        for (authority_end = p; authority_end < end; authority_end++)
        {
            if (*authority_end == '/' || *authority_end == '?' || *authority_end == '#')
                break;
        }

        /* Parse userinfo (if present) */
        userinfo_end = NULL;
        for (const char *s = authority_start; s < authority_end; s++)
        {
            if (*s == '@')
            {
                userinfo_end = s;
                break;
            }
        }

        if (userinfo_end)
        {
            /* Parse username:password */
            const char *colon = NULL;
            for (const char *s = authority_start; s < userinfo_end; s++)
            {
                if (*s == ':')
                {
                    colon = s;
                    break;
                }
            }
            if (colon)
            {
                url_unescape_into(authority_start, (size_t)(colon - authority_start), &out_url->username);
                url_unescape_into(colon + 1, (size_t)(userinfo_end - colon - 1), &out_url->password);
            }
            else
            {
                url_unescape_into(authority_start, (size_t)(userinfo_end - authority_start), &out_url->username);
            }
            host_start = userinfo_end + 1;
        }
        else
        {
            host_start = authority_start;
        }

        /* Parse host:port */
        host_end = authority_end;
        port_start = NULL;

        /* Handle IPv6 addresses */
        if (host_start < authority_end && *host_start == '[')
        {
            const char *bracket = memchr(host_start, ']', (size_t)(authority_end - host_start));
            if (bracket)
            {
                host_end = bracket + 1;
                if (host_end < authority_end && *host_end == ':')
                {
                    port_start = host_end + 1;
                }
            }
        }
        else
        {
            /* Find port separator */
            for (const char *s = host_start; s < authority_end; s++)
            {
                if (*s == ':')
                {
                    host_end = s;
                    port_start = s + 1;
                    break;
                }
            }
        }

        if (host_start < host_end)
        {
            /* Remove brackets from IPv6 */
            if (*host_start == '[' && *(host_end - 1) == ']')
            {
                out_url->host = str_ndup(host_start + 1, (size_t)(host_end - host_start - 2));
            }
            else
            {
                url_unescape_into(host_start, (size_t)(host_end - host_start), &out_url->host);
            }
        }

        if (port_start && port_start < authority_end)
        {
            out_url->port = str_ndup(port_start, (size_t)(authority_end - port_start));
        }

        p = authority_end;
    }

    /* Parse path */
    path_start = p;
    path_end = p;
    for (; path_end < end; path_end++)
    {
        if (*path_end == '?' || *path_end == '#')
            break;
    }
    if (path_start < path_end)
    {
        url_unescape_into(path_start, (size_t)(path_end - path_start), &out_url->path);
    }
    p = path_end;

    /* Parse query */
    query_start = NULL;
    query_end = NULL;
    if (p < end && *p == '?')
    {
        p++;
        query_start = p;
        for (query_end = p; query_end < end; query_end++)
        {
            if (*query_end == '#')
                break;
        }
        out_url->raw_query = str_ndup(query_start, (size_t)(query_end - query_start));
        p = query_end;
    }

    /* Parse fragment */
    if (p < end && *p == '#')
    {
        p++;
        fragment_start = p;
        url_unescape_into(fragment_start, (size_t)(end - fragment_start), &out_url->fragment);
    }

    return VIGIL_STATUS_OK;
}

void vigil_url_free(vigil_url_t *url)
{
    if (!url)
        return;
    free(url->scheme);
    free(url->username);
    free(url->password);
    free(url->host);
    free(url->port);
    free(url->path);
    free(url->raw_query);
    free(url->fragment);
    memset(url, 0, sizeof(*url));
}

/* ── URL String Building ─────────────────────────────────────────── */

vigil_status_t vigil_url_string(const vigil_url_t *url, char **out_string, size_t *out_length, vigil_error_t *error)
{
    char *result;
    size_t len, cap;

    if (!url || !out_string)
    {
        if (error)
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "null argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    cap = 256;
    result = malloc(cap);
    if (!result)
    {
        if (error)
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    len = 0;

    /* Scheme */
    if (url->scheme && url->scheme[0])
    {
        len += (size_t)snprintf(result + len, cap - len, "%s:", url->scheme);
    }

    /* Authority */
    if (url->host && url->host[0])
    {
        len += (size_t)snprintf(result + len, cap - len, "//");

        /* Userinfo */
        if (url->username && url->username[0])
        {
            url_escape_append(result, cap, &len, "", url->username, strlen(url->username));
            if (url->password)
                url_escape_append(result, cap, &len, ":", url->password, strlen(url->password));
            len += (size_t)snprintf(result + len, cap - len, "@");
        }

        /* Host (check for IPv6) */
        if (strchr(url->host, ':'))
        {
            len += (size_t)snprintf(result + len, cap - len, "[%s]", url->host);
        }
        else
        {
            len += (size_t)snprintf(result + len, cap - len, "%s", url->host);
        }

        /* Port */
        if (url->port && url->port[0])
        {
            len += (size_t)snprintf(result + len, cap - len, ":%s", url->port);
        }
    }

    /* Path */
    if (url->path && url->path[0])
    {
        /* Ensure path starts with / if we have authority */
        if (url->host && url->host[0] && url->path[0] != '/')
            len += (size_t)snprintf(result + len, cap - len, "/");
        url_escape_append(result, cap, &len, "", url->path, strlen(url->path));
    }

    /* Query */
    if (url->raw_query && url->raw_query[0])
    {
        len += (size_t)snprintf(result + len, cap - len, "?%s", url->raw_query);
    }

    /* Fragment */
    if (url->fragment && url->fragment[0])
    {
        url_escape_append(result, cap, &len, "#", url->fragment, strlen(url->fragment));
    }

    *out_string = result;
    if (out_length)
        *out_length = len;
    return VIGIL_STATUS_OK;
}

const char *vigil_url_hostname(const vigil_url_t *url)
{
    return url ? url->host : NULL;
}

int vigil_url_is_absolute(const vigil_url_t *url)
{
    return url && url->scheme && url->scheme[0];
}
