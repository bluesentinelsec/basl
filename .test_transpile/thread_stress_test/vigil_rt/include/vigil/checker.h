#ifndef VIGIL_CHECKER_H
#define VIGIL_CHECKER_H

#include "vigil/diagnostic.h"
#include "vigil/export.h"
#include "vigil/native_module.h"
#include "vigil/source.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Validates a VIGIL source file without returning an executable entrypoint.
     * Diagnostics describe syntax and semantic errors discovered during lexing,
     * declaration parsing, and the semantic function-body validation pass.
     * If |natives| is non-NULL, native stdlib modules are recognized during
     * type checking; otherwise imports of native modules will be rejected.
     */
    VIGIL_API vigil_status_t vigil_check_source(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                                const vigil_native_registry_t *natives,
                                                vigil_diagnostic_list_t *diagnostics, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
