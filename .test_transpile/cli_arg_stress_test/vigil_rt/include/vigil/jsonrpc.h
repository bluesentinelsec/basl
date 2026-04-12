#ifndef VIGIL_JSONRPC_H
#define VIGIL_JSONRPC_H

#include <stdio.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/json.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * JSON-RPC message transport using Content-Length framing.
     *
     * Messages are delimited by HTTP-style headers:
     *   Content-Length: <n>\r\n
     *   \r\n
     *   <n bytes of JSON>
     *
     * This is the wire format used by both DAP and LSP.
     */

    typedef struct vigil_jsonrpc_transport
    {
        FILE *in;
        FILE *out;
        const vigil_allocator_t *allocator;
    } vigil_jsonrpc_transport_t;

    VIGIL_API void vigil_jsonrpc_transport_init(vigil_jsonrpc_transport_t *transport, FILE *in, FILE *out,
                                                const vigil_allocator_t *allocator);

    /*
     * Read one Content-Length framed JSON message.
     * Blocks until a complete message is available or EOF/error.
     * Returns VIGIL_STATUS_OK on success, with *out set to the parsed JSON.
     * Returns VIGIL_STATUS_INTERNAL on EOF or I/O error.
     */
    VIGIL_API vigil_status_t vigil_jsonrpc_read(vigil_jsonrpc_transport_t *transport, vigil_json_value_t **out,
                                                vigil_error_t *error);

    /*
     * Write one Content-Length framed JSON message.
     */
    VIGIL_API vigil_status_t vigil_jsonrpc_write(vigil_jsonrpc_transport_t *transport,
                                                 const vigil_json_value_t *message, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
