#include "vigil/checker.h"

#include "internal/vigil_compiler_internal.h"

vigil_status_t vigil_check_source(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                  const vigil_native_registry_t *natives, vigil_diagnostic_list_t *diagnostics,
                                  vigil_error_t *error)
{
    vigil_status_t status;
    vigil_program_state_t program;

    status = vigil_semantic_prepare_source_internal(registry, source_id, VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT, natives,
                                                    diagnostics, &program, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_semantic_validate_program_internal(&program);
    vigil_program_free(&program);
    return status;
}
