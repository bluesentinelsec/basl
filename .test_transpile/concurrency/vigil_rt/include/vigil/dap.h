#ifndef VIGIL_DAP_H
#define VIGIL_DAP_H

#include <stdio.h>

#include "vigil/allocator.h"
#include "vigil/debugger.h"
#include "vigil/export.h"
#include "vigil/json.h"
#include "vigil/jsonrpc.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/vm.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * DAP (Debug Adapter Protocol) server.
     *
     * Bridges a JSON-RPC transport to the vigil_debugger API.
     * The server reads DAP requests from stdin, translates them to
     * debugger API calls, and writes DAP events/responses to stdout.
     *
     * Typical usage:
     *   vigil_dap_server_t server;
     *   vigil_dap_server_init(&server, stdin, stdout, allocator);
     *   vigil_dap_server_set_runtime(&server, vm, sources);
     *   vigil_dap_server_run(&server, &error);  // blocks until disconnect
     */

    typedef struct vigil_dap_server vigil_dap_server_t;

    VIGIL_API vigil_status_t vigil_dap_server_create(vigil_dap_server_t **out, FILE *in, FILE *out_stream,
                                                     const vigil_allocator_t *allocator, vigil_error_t *error);

    VIGIL_API void vigil_dap_server_destroy(vigil_dap_server_t **server);

    /*
     * Attach the DAP server to a VM and source registry.
     * Must be called before run.  The server creates its own debugger internally.
     */
    VIGIL_API vigil_status_t vigil_dap_server_set_runtime(vigil_dap_server_t *server, vigil_vm_t *vm,
                                                          const vigil_source_registry_t *sources, vigil_error_t *error);

    /*
     * Set the compiled function to execute when a "launch" request arrives.
     */
    VIGIL_API void vigil_dap_server_set_program(vigil_dap_server_t *server, vigil_object_t *function,
                                                vigil_source_id_t source_id);

    /*
     * Run the DAP message loop.  Blocks until a "disconnect" request
     * is received or an I/O error occurs.
     */
    VIGIL_API vigil_status_t vigil_dap_server_run(vigil_dap_server_t *server, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
