#ifndef VIGIL_PKG_H
#define VIGIL_PKG_H

#include <stddef.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Package specifier ───────────────────────────────────────────── */

    typedef struct vigil_pkg_spec
    {
        char *url;     /* e.g. "github.com/user/repo" */
        char *version; /* e.g. "v1.0.0" or NULL for latest */
        vigil_allocator_t allocator;
    } vigil_pkg_spec_t;

    VIGIL_API void vigil_pkg_spec_free(vigil_pkg_spec_t *spec);

    VIGIL_API vigil_status_t vigil_pkg_parse_spec(const vigil_allocator_t *allocator, const char *input,
                                                  vigil_pkg_spec_t *out_spec, vigil_error_t *error);

    /* ── Lock file ───────────────────────────────────────────────────── */

    typedef struct vigil_pkg_lock_entry
    {
        char *name;
        char *version;
        char *commit;
    } vigil_pkg_lock_entry_t;

    typedef struct vigil_pkg_lock
    {
        vigil_pkg_lock_entry_t *entries;
        size_t count;
        size_t capacity;
        vigil_allocator_t allocator;
    } vigil_pkg_lock_t;

    VIGIL_API void vigil_pkg_lock_init(vigil_pkg_lock_t *lock, const vigil_allocator_t *allocator);
    VIGIL_API void vigil_pkg_lock_free(vigil_pkg_lock_t *lock);

    VIGIL_API vigil_status_t vigil_pkg_lock_add(vigil_pkg_lock_t *lock, const char *name, const char *version,
                                                const char *commit, vigil_error_t *error);

    VIGIL_API const vigil_pkg_lock_entry_t *vigil_pkg_lock_find(const vigil_pkg_lock_t *lock, const char *name);

    VIGIL_API vigil_status_t vigil_pkg_lock_read(const vigil_allocator_t *allocator, const char *path,
                                                 vigil_pkg_lock_t *out_lock, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_pkg_lock_write(const char *path, const vigil_pkg_lock_t *lock, vigil_error_t *error);

    /* ── Git operations ──────────────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_pkg_git_available(vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_pkg_git_clone(const vigil_allocator_t *allocator, const char *url, const char *dest,
                                                 vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_pkg_git_fetch(const vigil_allocator_t *allocator, const char *repo_path,
                                                 vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_pkg_git_checkout(const vigil_allocator_t *allocator, const char *repo_path,
                                                    const char *version, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_pkg_git_head(const vigil_allocator_t *allocator, const char *repo_path,
                                                char *out_commit, size_t commit_size, vigil_error_t *error);

    /* ── High-level operations ───────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_pkg_get(const vigil_allocator_t *allocator, const char *project_root,
                                           const char *specifier, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_pkg_sync(const vigil_allocator_t *allocator, const char *project_root,
                                            vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_pkg_remove(const vigil_allocator_t *allocator, const char *project_root,
                                              const char *package_url, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_pkg_resolve_import(const char *project_root, const char *import_path, char *out_path,
                                                      size_t path_size, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
