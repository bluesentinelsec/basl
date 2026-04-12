#ifndef VIGIL_LSP_H
#define VIGIL_LSP_H

#include <stdio.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/jsonrpc.h"
#include "vigil/semantic.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * LSP (Language Server Protocol) server.
     *
     * Provides IDE features for VIGIL:
     * - Diagnostics (errors/warnings)
     * - Document symbols (outline)
     * - Go to definition
     * - Completions
     * - Hover information
     *
     * Uses JSON-RPC transport (same as DAP).
     */

    typedef struct vigil_lsp_server vigil_lsp_server_t;

    /* ── Server Lifecycle ─────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_lsp_server_create(vigil_lsp_server_t **out, FILE *in, FILE *out_stream,
                                                     const vigil_allocator_t *allocator, vigil_error_t *error);

    VIGIL_API void vigil_lsp_server_destroy(vigil_lsp_server_t **server);

    /*
     * Run the LSP message loop.
     * Blocks until a "shutdown" request is received or an I/O error occurs.
     */
    VIGIL_API vigil_status_t vigil_lsp_server_process_one(vigil_lsp_server_t *server, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_lsp_server_run(vigil_lsp_server_t *server, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
