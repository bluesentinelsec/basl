#ifndef VIGIL_EDITOR_H
#define VIGIL_EDITOR_H

#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct vigil_editor_result
    {
        vigil_status_t status;
        char message[512];
    } vigil_editor_result_t;

    /* List of supported editor names (NULL-terminated). */
    VIGIL_API const char *const *vigil_editor_supported(void);

    /* Generate config file content for an editor. Caller provides the vigil binary path.
       Returns the file content in out_content (caller must free) and the target path
       in out_path (caller must free). Returns the number of files via out_count. */
    VIGIL_API int vigil_editor_is_supported(const char *name);

    /* Install editor integration. Writes config files and prints actions to stdout. */
    VIGIL_API vigil_editor_result_t vigil_editor_install(const char *name, const char *vigil_bin,
                                                         const char *config_home);

    /* Uninstall editor integration. Removes config files and prints actions to stdout. */
    VIGIL_API vigil_editor_result_t vigil_editor_uninstall(const char *name, const char *config_home);

    /* Check if an editor integration is installed. */
    VIGIL_API int vigil_editor_is_installed(const char *name, const char *config_home);

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_EDITOR_H */
