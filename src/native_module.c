#include "vigil/native_module.h"

#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"

void vigil_native_registry_init(vigil_native_registry_t *registry)
{
    if (registry == NULL)
    {
        return;
    }
    memset(registry, 0, sizeof(*registry));
    registry->allocator = vigil_default_allocator();
}

void vigil_native_registry_free(vigil_native_registry_t *registry)
{
    if (registry == NULL)
    {
        return;
    }
    registry->allocator.deallocate(registry->allocator.user_data, registry->modules);
    memset(registry, 0, sizeof(*registry));
}

vigil_status_t vigil_native_registry_add(vigil_native_registry_t *registry, const vigil_native_module_t *module,
                                         vigil_error_t *error)
{
    (void)error;
    if (registry == NULL || module == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (registry->module_count >= registry->module_capacity)
    {
        size_t new_cap;
        const vigil_native_module_t **new_buf;

        new_cap = registry->module_capacity < 8U ? 8U : registry->module_capacity * 2U;
        new_buf = (const vigil_native_module_t **)registry->allocator.reallocate(
            registry->allocator.user_data, registry->modules, new_cap * sizeof(*new_buf));
        if (new_buf == NULL)
        {
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        registry->modules = new_buf;
        registry->module_capacity = new_cap;
    }
    registry->modules[registry->module_count] = module;
    registry->module_count += 1U;
    return VIGIL_STATUS_OK;
}

const vigil_native_module_t *vigil_native_registry_find(const vigil_native_registry_t *registry, const char *name,
                                                        size_t name_length)
{
    size_t i;

    if (registry == NULL || name == NULL)
    {
        return NULL;
    }
    for (i = 0U; i < registry->module_count; i++)
    {
        if (registry->modules[i]->name_length == name_length &&
            memcmp(registry->modules[i]->name, name, name_length) == 0)
        {
            return registry->modules[i];
        }
    }
    return NULL;
}

int vigil_native_registry_find_index(const vigil_native_registry_t *registry, const char *name, size_t name_length,
                                     size_t *out_index)
{
    size_t i;

    if (registry == NULL || name == NULL || out_index == NULL)
    {
        return 0;
    }
    for (i = 0U; i < registry->module_count; i++)
    {
        if (registry->modules[i]->name_length == name_length &&
            memcmp(registry->modules[i]->name, name, name_length) == 0)
        {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}
