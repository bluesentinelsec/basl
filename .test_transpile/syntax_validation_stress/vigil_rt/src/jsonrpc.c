#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/jsonrpc.h"

void vigil_jsonrpc_transport_init(vigil_jsonrpc_transport_t *transport, FILE *in, FILE *out,
                                  const vigil_allocator_t *allocator)
{
    if (transport == NULL)
        return;
    transport->in = in;
    transport->out = out;
    transport->allocator = allocator;
}

/* ── Read ────────────────────────────────────────────────────────── */

static int read_header_line(FILE *f, char *buf, size_t cap)
{
    size_t i = 0;
    for (;;)
    {
        int c = fgetc(f);
        if (c == EOF)
            return -1;
        if (i + 1 >= cap)
            return -1;
        buf[i++] = (char)c;
        if (i >= 2 && buf[i - 2] == '\r' && buf[i - 1] == '\n')
        {
            buf[i] = '\0';
            return (int)i;
        }
    }
}

vigil_status_t vigil_jsonrpc_read(vigil_jsonrpc_transport_t *transport, vigil_json_value_t **out, vigil_error_t *error)
{
    if (transport == NULL || transport->in == NULL || out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "jsonrpc: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Read headers until empty line. */
    size_t content_length = 0;
    int found_length = 0;
    char line[256];

    for (;;)
    {
        int n = read_header_line(transport->in, line, sizeof(line));
        if (n < 0)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "jsonrpc: failed to read header");
            return VIGIL_STATUS_INTERNAL;
        }
        /* Empty line (just \r\n) terminates headers. */
        if (n == 2 && line[0] == '\r' && line[1] == '\n')
            break;

        /* Parse Content-Length. */
        const char *prefix = "Content-Length: ";
        size_t prefix_len = 16;
        if ((size_t)n > prefix_len && memcmp(line, prefix, prefix_len) == 0)
        {
            content_length = (size_t)strtoul(line + prefix_len, NULL, 10);
            found_length = 1;
        }
    }

    if (!found_length || content_length == 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_SYNTAX_ERROR, "jsonrpc: missing Content-Length");
        return VIGIL_STATUS_SYNTAX_ERROR;
    }

    /* Read body. */
    vigil_allocator_t a = transport->allocator != NULL && vigil_allocator_is_valid(transport->allocator)
                              ? *transport->allocator
                              : vigil_default_allocator();

    char *body = (char *)a.allocate(a.user_data, content_length + 1);
    if (body == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "jsonrpc: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    size_t total = 0;
    while (total < content_length)
    {
        size_t n = fread(body + total, 1, content_length - total, transport->in);
        if (n == 0)
        {
            a.deallocate(a.user_data, body);
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "jsonrpc: unexpected EOF reading body");
            return VIGIL_STATUS_INTERNAL;
        }
        total += n;
    }
    body[content_length] = '\0';

    vigil_status_t s = vigil_json_parse(transport->allocator, body, content_length, out, error);
    a.deallocate(a.user_data, body);
    return s;
}

/* ── Write ───────────────────────────────────────────────────────── */

vigil_status_t vigil_jsonrpc_write(vigil_jsonrpc_transport_t *transport, const vigil_json_value_t *message,
                                   vigil_error_t *error)
{
    if (transport == NULL || transport->out == NULL || message == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "jsonrpc: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    char *json_str = NULL;
    size_t json_len = 0;
    vigil_status_t s = vigil_json_emit(message, &json_str, &json_len, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    /* Determine which allocator was used for the emitted string. */
    vigil_allocator_t a = transport->allocator != NULL && vigil_allocator_is_valid(transport->allocator)
                              ? *transport->allocator
                              : vigil_default_allocator();

    fprintf(transport->out, "Content-Length: %zu\r\n\r\n", json_len);
    size_t written = fwrite(json_str, 1, json_len, transport->out);
    fflush(transport->out);

    a.deallocate(a.user_data, json_str);

    if (written != json_len)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "jsonrpc: write failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}
