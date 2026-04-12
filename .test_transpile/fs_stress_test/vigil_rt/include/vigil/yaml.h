/* VIGIL YAML parsing library.
 *
 * Parses a subset of YAML 1.2 into vigil_json_value_t structures.
 * Supports: scalars, block mappings, block sequences, comments,
 * quoted strings, and block scalars (| and >).
 * Pure C11, no external dependencies.
 */
#ifndef VIGIL_YAML_H
#define VIGIL_YAML_H

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/json.h"
#include "vigil/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Parse a YAML string into a JSON value tree.
     * Returns VIGIL_STATUS_OK on success.
     * The returned value must be freed with vigil_json_free().
     */
    VIGIL_API vigil_status_t vigil_yaml_parse(const char *yaml, size_t length, const vigil_allocator_t *allocator,
                                              vigil_json_value_t **out, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_YAML_H */
