#ifndef VIGIL_COMPILER_INTERNAL_H
#define VIGIL_COMPILER_INTERNAL_H

#include "vigil/compiler.h"
#include "vigil/native_module.h"

typedef enum vigil_compile_mode
{
    VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT = 0,
    VIGIL_COMPILE_MODE_REPL = 1
} vigil_compile_mode_t;

vigil_status_t vigil_check_source_internal(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                           const vigil_native_registry_t *natives, vigil_diagnostic_list_t *diagnostics,
                                           vigil_error_t *error);

vigil_status_t vigil_compile_source_internal(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                             vigil_compile_mode_t mode, const vigil_native_registry_t *natives,
                                             vigil_object_t **out_function, int *out_repl_has_statements,
                                             vigil_diagnostic_list_t *diagnostics, vigil_error_t *error);

#endif
