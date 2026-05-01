#ifndef VIGIL_TRANSPILE_C_H
#define VIGIL_TRANSPILE_C_H

#include "vigil/runtime.h"
#include "vigil/status.h"
#include "vigil/string.h"
#include "vigil/value.h"
#include "vigil_regvm.h"

/* ── Internal transpiler context ─────────────────────────────────── */

typedef struct vigil_transpile_ctx
{
    vigil_runtime_t *runtime;
    vigil_string_t *output;
    vigil_error_t *error;
    const vigil_object_t *root_function;
} vigil_transpile_ctx_t;

/* Emit C code for a single register chunk (one function body). */
vigil_status_t vigil_transpile_emit_function(vigil_transpile_ctx_t *ctx, const vigil_reg_chunk_t *rc,
                                             const char *func_name, uint8_t arity, uint8_t max_regs, size_t func_index,
                                             size_t func_count);

#endif /* VIGIL_TRANSPILE_C_H */
