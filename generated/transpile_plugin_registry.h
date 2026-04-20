/* Auto-generated from plugin.toml manifests — do not edit. */
#ifndef VIGIL_TRANSPILE_PLUGIN_REGISTRY_H
#define VIGIL_TRANSPILE_PLUGIN_REGISTRY_H

#include <string.h>
#include "vigil/export.h"
#include "vigil/native_module.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C" {
#endif

extern VIGIL_API const vigil_native_module_t vigil_plugin_audio;
extern VIGIL_API const vigil_native_module_t vigil_plugin_gui;
extern VIGIL_API const vigil_native_module_t vigil_plugin_sdl;
extern VIGIL_API const vigil_native_module_t vigil_plugin_sysquery;
extern VIGIL_API const vigil_native_module_t vigil_plugin_test_plugin;
extern VIGIL_API const vigil_native_module_t vigil_plugin_tiled;

#define VIGIL_PLUGIN_COUNT_MAX (6U)

typedef struct vigil_plugin_entry {
    const char *name;
    size_t name_length;
    const vigil_native_module_t *module;
} vigil_plugin_entry_t;

static inline size_t vigil_plugin_fill_table_(vigil_plugin_entry_t *table) {
    size_t i = 0;
    table[i].name = "sysquery"; table[i].name_length = 8U; table[i].module = &vigil_plugin_sysquery; i++;
    table[i].name = "test_plugin"; table[i].name_length = 11U; table[i].module = &vigil_plugin_test_plugin; i++;
    table[i].name = "tiled"; table[i].name_length = 5U; table[i].module = &vigil_plugin_tiled; i++;
#ifdef VIGIL_PLUGIN_AUDIO_ENABLED
    table[i].name = "audio"; table[i].name_length = 5U; table[i].module = &vigil_plugin_audio; i++;
#endif
#ifdef VIGIL_PLUGIN_SDL_ENABLED
    table[i].name = "gui"; table[i].name_length = 3U; table[i].module = &vigil_plugin_gui; i++;
#endif
#ifdef VIGIL_PLUGIN_SDL_ENABLED
    table[i].name = "sdl"; table[i].name_length = 3U; table[i].module = &vigil_plugin_sdl; i++;
#endif
    return i;
}

static inline int vigil_plugin_is_known_module(const char *name, size_t name_length) {
    vigil_plugin_entry_t table[VIGIL_PLUGIN_COUNT_MAX + 1U];
    size_t count = vigil_plugin_fill_table_(table);
    for (size_t i = 0U; i < count; i++) {
        if (table[i].name_length == name_length && memcmp(table[i].name, name, name_length) == 0)
            return 1;
    }
    return 0;
}

static inline vigil_status_t vigil_plugin_register_all(vigil_native_registry_t *registry, vigil_error_t *error) {
    vigil_plugin_entry_t table[VIGIL_PLUGIN_COUNT_MAX + 1U];
    size_t count = vigil_plugin_fill_table_(table);
    for (size_t i = 0U; i < count; i++) {
        vigil_status_t s = vigil_native_registry_add(registry, table[i].module, error);
        if (s != VIGIL_STATUS_OK) return s;
    }
    return VIGIL_STATUS_OK;
}

/* For compatibility — use vigil_plugin_fill_table_ for actual count. */
#define VIGIL_PLUGIN_COUNT VIGIL_PLUGIN_COUNT_MAX

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_TRANSPILE_PLUGIN_REGISTRY_H */
