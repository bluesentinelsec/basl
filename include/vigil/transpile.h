#ifndef VIGIL_TRANSPILE_H
#define VIGIL_TRANSPILE_H

#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/status.h"
#include "vigil/string.h"
#include "vigil/value.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Transpile a compiled function (and its siblings) to C11 source.
     *
     * Walks the register IR in each function's chunk and emits C code into
     * `out_source`.  The caller must have compiled the program first via
     * vigil_compile_source().
     *
     * @param runtime   Active runtime (used for allocations).
     * @param function  Top-level compiled function object.
     * @param out_source  Receives the generated C source text.
     * @param error     Receives error details on failure.
     * @return VIGIL_STATUS_OK on success.
     */
    VIGIL_API vigil_status_t vigil_transpile_to_c(vigil_runtime_t *runtime, const vigil_object_t *function,
                                                  vigil_string_t *out_source, vigil_error_t *error);

    /**
     * Return the sibling index of the entry function (main) within the
     * compiled function's sibling table.  Used by the CLI to generate the
     * correct header and main wrapper.
     */
    VIGIL_API size_t vigil_transpile_entry_index(const vigil_object_t *function);

#ifdef __cplusplus
}
#endif

#endif
