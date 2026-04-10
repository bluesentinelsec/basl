/* Minimal plugin registry for self-contained transpiled builds. */
#ifndef VIGIL_PLUGIN_REGISTRY_H
#define VIGIL_PLUGIN_REGISTRY_H

#include <string.h>
#include "vigil/export.h"
#include "vigil/native_module.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define VIGIL_PLUGIN_COUNT (0U)

typedef struct vigil_plugin_entry
{
    const char *name;
    size_t name_length;
    const vigil_native_module_t *module;
} vigil_plugin_entry_t;

static inline void vigil_plugin_fill_table_(vigil_plugin_entry_t *table)
{
    (void)table;
}

static inline int vigil_plugin_is_known_module(const char *name, size_t name_length)
{
    (void)name;
    (void)name_length;
    return 0;
}

static inline vigil_status_t vigil_plugin_register_all(vigil_native_registry_t *registry, vigil_error_t *error)
{
    (void)registry;
    (void)error;
    return VIGIL_STATUS_OK;
}

#ifdef __cplusplus
}
#endif

#endif
