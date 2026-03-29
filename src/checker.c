#include "vigil/checker.h"

#include "internal/vigil_compiler_internal.h"

vigil_status_t vigil_check_source(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                  const vigil_native_registry_t *natives, vigil_diagnostic_list_t *diagnostics,
                                  vigil_error_t *error)
{
    return vigil_check_source_internal(registry, source_id, natives, diagnostics, error);
}
