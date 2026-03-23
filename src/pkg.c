/*
 * VIGIL Package Manager
 *
 * Provides Go-style package management using git for distribution.
 * Packages are identified by git URLs (e.g. github.com/user/repo).
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"
#include "vigil/pkg.h"
#include "vigil/toml.h"

#ifdef _MSC_VER
#define pkg_strdup _strdup
#else
#define pkg_strdup strdup
#endif

/* ── Helpers ─────────────────────────────────────────────────────── */

static void set_error(vigil_error_t *error, vigil_status_t type, const char *msg)
{
    if (error == NULL)
        return;
    vigil_error_clear(error);
    error->type = type;
    error->value = msg;
    error->length = msg ? strlen(msg) : 0;
}

static char *trim_whitespace(char *str)
{
    char *end;
    while (isspace((unsigned char)*str))
        str++;
    if (*str == '\0')
        return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return str;
}

/* ── Package specifier ───────────────────────────────────────────── */

void vigil_pkg_spec_free(vigil_pkg_spec_t *spec)
{
    if (spec == NULL)
        return;
    free(spec->url);
    free(spec->version);
    spec->url = NULL;
    spec->version = NULL;
}

vigil_status_t vigil_pkg_parse_spec(const char *input, vigil_pkg_spec_t *out_spec, vigil_error_t *error)
{
    const char *at;
    size_t url_len;

    if (input == NULL || out_spec == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    memset(out_spec, 0, sizeof(*out_spec));
    at = strchr(input, '@');

    if (at != NULL)
    {
        url_len = (size_t)(at - input);
        out_spec->url = malloc(url_len + 1);
        if (out_spec->url == NULL)
        {
            set_error(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        memcpy(out_spec->url, input, url_len);
        out_spec->url[url_len] = '\0';

        out_spec->version = pkg_strdup(at + 1);
        if (out_spec->version == NULL)
        {
            free(out_spec->url);
            out_spec->url = NULL;
            set_error(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
    }
    else
    {
        out_spec->url = pkg_strdup(input);
        if (out_spec->url == NULL)
        {
            set_error(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        out_spec->version = NULL;
    }

    return VIGIL_STATUS_OK;
}

/* ── Lock file ───────────────────────────────────────────────────── */

void vigil_pkg_lock_init(vigil_pkg_lock_t *lock)
{
    if (lock == NULL)
        return;
    lock->entries = NULL;
    lock->count = 0;
    lock->capacity = 0;
}

void vigil_pkg_lock_free(vigil_pkg_lock_t *lock)
{
    size_t i;
    if (lock == NULL)
        return;
    for (i = 0; i < lock->count; i++)
    {
        free(lock->entries[i].name);
        free(lock->entries[i].version);
        free(lock->entries[i].commit);
    }
    free(lock->entries);
    lock->entries = NULL;
    lock->count = 0;
    lock->capacity = 0;
}

vigil_status_t vigil_pkg_lock_add(vigil_pkg_lock_t *lock, const char *name, const char *version, const char *commit,
                                  vigil_error_t *error)
{
    vigil_pkg_lock_entry_t *entry;
    size_t i;

    if (lock == NULL || name == NULL || commit == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Update existing entry if present */
    for (i = 0; i < lock->count; i++)
    {
        if (strcmp(lock->entries[i].name, name) == 0)
        {
            free(lock->entries[i].version);
            free(lock->entries[i].commit);
            lock->entries[i].version = version ? pkg_strdup(version) : NULL;
            lock->entries[i].commit = pkg_strdup(commit);
            return VIGIL_STATUS_OK;
        }
    }

    /* Grow capacity if needed */
    if (lock->count >= lock->capacity)
    {
        size_t new_cap = lock->capacity == 0 ? 8 : lock->capacity * 2;
        vigil_pkg_lock_entry_t *new_entries = realloc(lock->entries, new_cap * sizeof(vigil_pkg_lock_entry_t));
        if (new_entries == NULL)
        {
            set_error(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        lock->entries = new_entries;
        lock->capacity = new_cap;
    }

    entry = &lock->entries[lock->count];
    entry->name = pkg_strdup(name);
    entry->version = version ? pkg_strdup(version) : NULL;
    entry->commit = pkg_strdup(commit);

    if (entry->name == NULL || entry->commit == NULL)
    {
        free(entry->name);
        free(entry->version);
        free(entry->commit);
        set_error(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    lock->count++;
    return VIGIL_STATUS_OK;
}

const vigil_pkg_lock_entry_t *vigil_pkg_lock_find(const vigil_pkg_lock_t *lock, const char *name)
{
    size_t i;
    if (lock == NULL || name == NULL)
        return NULL;
    for (i = 0; i < lock->count; i++)
    {
        if (strcmp(lock->entries[i].name, name) == 0)
        {
            return &lock->entries[i];
        }
    }
    return NULL;
}

/* ── Lock file I/O ───────────────────────────────────────────────── */

vigil_status_t vigil_pkg_lock_read(const char *path, vigil_pkg_lock_t *out_lock, vigil_error_t *error)
{
    char *data = NULL;
    size_t length;
    vigil_toml_value_t *root = NULL;
    const vigil_toml_value_t *packages;
    size_t i, count;
    vigil_status_t status;

    if (path == NULL || out_lock == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_pkg_lock_init(out_lock);

    status = vigil_platform_read_file(NULL, path, &data, &length, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_toml_parse(NULL, data, length, &root, error);
    free(data);
    if (status != VIGIL_STATUS_OK)
        return status;

    packages = vigil_toml_table_get(root, "package");
    if (packages == NULL || vigil_toml_type(packages) != VIGIL_TOML_ARRAY)
    {
        vigil_toml_free(&root);
        return VIGIL_STATUS_OK; /* Empty lock file */
    }

    count = vigil_toml_array_count(packages);
    for (i = 0; i < count; i++)
    {
        const vigil_toml_value_t *pkg = vigil_toml_array_get(packages, i);
        const vigil_toml_value_t *name_val, *version_val, *commit_val;
        const char *name, *version, *commit;

        if (pkg == NULL || vigil_toml_type(pkg) != VIGIL_TOML_TABLE)
            continue;

        name_val = vigil_toml_table_get(pkg, "name");
        version_val = vigil_toml_table_get(pkg, "version");
        commit_val = vigil_toml_table_get(pkg, "commit");

        if (name_val == NULL || vigil_toml_type(name_val) != VIGIL_TOML_STRING)
            continue;
        if (commit_val == NULL || vigil_toml_type(commit_val) != VIGIL_TOML_STRING)
            continue;

        name = vigil_toml_string_value(name_val);
        commit = vigil_toml_string_value(commit_val);
        version = (version_val && vigil_toml_type(version_val) == VIGIL_TOML_STRING)
                      ? vigil_toml_string_value(version_val)
                      : NULL;

        status = vigil_pkg_lock_add(out_lock, name, version, commit, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_pkg_lock_free(out_lock);
            vigil_toml_free(&root);
            return status;
        }
    }

    vigil_toml_free(&root);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_pkg_lock_write(const char *path, const vigil_pkg_lock_t *lock, vigil_error_t *error)
{
    char *buf = NULL;
    size_t buf_size = 4096;
    size_t buf_len = 0;
    size_t i;
    vigil_status_t status;

    if (path == NULL || lock == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    buf = malloc(buf_size);
    if (buf == NULL)
    {
        set_error(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    buf_len = (size_t)snprintf(buf, buf_size, "# AUTO-GENERATED by vigil get - DO NOT EDIT\n\n");

    for (i = 0; i < lock->count; i++)
    {
        const vigil_pkg_lock_entry_t *e = &lock->entries[i];
        size_t needed = 128 + strlen(e->name) + strlen(e->commit) + (e->version ? strlen(e->version) : 0);

        if (buf_len + needed >= buf_size)
        {
            buf_size = buf_size * 2 + needed;
            char *new_buf = realloc(buf, buf_size);
            if (new_buf == NULL)
            {
                free(buf);
                set_error(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }
            buf = new_buf;
        }

        buf_len += (size_t)snprintf(buf + buf_len, buf_size - buf_len, "[[package]]\n");
        buf_len += (size_t)snprintf(buf + buf_len, buf_size - buf_len, "name = \"%s\"\n", e->name);
        if (e->version != NULL)
        {
            buf_len += (size_t)snprintf(buf + buf_len, buf_size - buf_len, "version = \"%s\"\n", e->version);
        }
        buf_len += (size_t)snprintf(buf + buf_len, buf_size - buf_len, "commit = \"%s\"\n\n", e->commit);
    }

    status = vigil_platform_write_file(path, buf, buf_len, error);
    free(buf);
    return status;
}

/* ── Git operations ──────────────────────────────────────────────── */

vigil_status_t vigil_pkg_git_available(vigil_error_t *error)
{
    const char *argv[] = {"git", "--version", NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    vigil_status_t status;

    status = vigil_platform_exec(NULL, argv, &out, &err_out, &exit_code, error);
    free(out);
    free(err_out);

    if (status != VIGIL_STATUS_OK || exit_code != 0)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "git is not installed. Install git and try again.");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_pkg_git_clone(const char *url, const char *dest, vigil_error_t *error)
{
    char git_url[1024];
    const char *argv[] = {"git", "clone", git_url, dest, NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    vigil_status_t status;

    if (url == NULL || dest == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Convert package URL to git SSH URL (e.g. github.com/user/repo -> git@github.com:user/repo.git) */
    {
        const char *slash = strchr(url, '/');
        if (slash != NULL)
        {
            size_t host_len = (size_t)(slash - url);
            snprintf(git_url, sizeof(git_url), "git@%.*s:%s.git", (int)host_len, url, slash + 1);
        }
        else
        {
            snprintf(git_url, sizeof(git_url), "git@%s.git", url);
        }
    }

    status = vigil_platform_exec(NULL, argv, &out, &err_out, &exit_code, error);

    if (status != VIGIL_STATUS_OK)
    {
        free(out);
        free(err_out);
        return status;
    }

    if (exit_code != 0)
    {
        free(out);
        free(err_out);
        set_error(error, VIGIL_STATUS_INTERNAL, "git clone failed");
        return VIGIL_STATUS_INTERNAL;
    }

    free(out);
    free(err_out);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_pkg_git_fetch(const char *repo_path, vigil_error_t *error)
{
    const char *argv[] = {"git", "-C", repo_path, "fetch", "--tags", NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    vigil_status_t status;

    if (repo_path == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_platform_exec(NULL, argv, &out, &err_out, &exit_code, error);
    free(out);
    free(err_out);

    if (status != VIGIL_STATUS_OK)
        return status;
    if (exit_code != 0)
    {
        set_error(error, VIGIL_STATUS_INTERNAL, "git fetch failed");
        return VIGIL_STATUS_INTERNAL;
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_pkg_git_checkout(const char *repo_path, const char *version, vigil_error_t *error)
{
    const char *argv[] = {"git", "-C", repo_path, "checkout", version, NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    vigil_status_t status;

    if (repo_path == NULL || version == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_platform_exec(NULL, argv, &out, &err_out, &exit_code, error);

    if (status != VIGIL_STATUS_OK)
    {
        free(out);
        free(err_out);
        return status;
    }

    if (exit_code != 0)
    {
        free(out);
        free(err_out);
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "version not found");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    free(out);
    free(err_out);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_pkg_git_head(const char *repo_path, char *out_commit, size_t commit_size, vigil_error_t *error)
{
    const char *argv[] = {"git", "-C", repo_path, "rev-parse", "HEAD", NULL};
    char *out = NULL, *err_out = NULL;
    int exit_code;
    vigil_status_t status;
    char *trimmed;

    if (repo_path == NULL || out_commit == NULL || commit_size == 0)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_platform_exec(NULL, argv, &out, &err_out, &exit_code, error);

    if (status != VIGIL_STATUS_OK)
    {
        free(out);
        free(err_out);
        return status;
    }

    if (exit_code != 0 || out == NULL)
    {
        set_error(error, VIGIL_STATUS_INTERNAL, "failed to get HEAD commit");
        free(out);
        free(err_out);
        return VIGIL_STATUS_INTERNAL;
    }

    trimmed = trim_whitespace(out);
    {
        size_t len = strlen(trimmed);
        if (len >= commit_size)
        {
            free(out);
            free(err_out);
            set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "commit buffer too small");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        memcpy(out_commit, trimmed, len + 1);
    }
    free(out);
    free(err_out);
    return VIGIL_STATUS_OK;
}

/* ── TOML helpers ────────────────────────────────────────────────── */

static vigil_status_t read_project_toml(const char *project_root, vigil_toml_value_t **out_root, vigil_error_t *error)
{
    char toml_path[4096];
    char *data = NULL;
    size_t length;
    vigil_status_t status;

    if (vigil_platform_path_join(project_root, "vigil.toml", toml_path, sizeof(toml_path), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }

    status = vigil_platform_read_file(NULL, toml_path, &data, &length, error);
    if (status != VIGIL_STATUS_OK)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "vigil.toml not found");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_toml_parse(NULL, data, length, out_root, error);
    free(data);
    return status;
}

static vigil_status_t write_project_toml(const char *project_root, vigil_toml_value_t *root, vigil_error_t *error)
{
    char toml_path[4096];
    char *output = NULL;
    size_t output_len;
    vigil_status_t status;

    if (vigil_platform_path_join(project_root, "vigil.toml", toml_path, sizeof(toml_path), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }

    status = vigil_toml_emit(root, &output, &output_len, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = vigil_platform_write_file(toml_path, output, output_len, error);
    free(output);
    return status;
}

static vigil_status_t ensure_deps_table(vigil_toml_value_t *root, vigil_toml_value_t **out_deps, vigil_error_t *error)
{
    const vigil_toml_value_t *existing;
    vigil_toml_value_t *deps = NULL;

    existing = vigil_toml_table_get(root, "deps");
    if (existing != NULL)
    {
        *out_deps = (vigil_toml_value_t *)existing;
        return VIGIL_STATUS_OK;
    }

    if (vigil_toml_table_new(NULL, &deps, error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }

    if (vigil_toml_table_set(root, "deps", 4, deps, error) != VIGIL_STATUS_OK)
    {
        vigil_toml_free(&deps);
        return error->type;
    }

    *out_deps = deps;
    return VIGIL_STATUS_OK;
}

/* Forward declaration for transitive deps */
static vigil_status_t install_transitive_deps(const char *project_root, const char *pkg_path, vigil_pkg_lock_t *lock,
                                              vigil_error_t *error);

/* Install a single package (used by both get and sync) */
static void ensure_parent_dir(const char *path, vigil_error_t *error)
{
    char parent[4096];
    size_t len = strlen(path);
    memcpy(parent, path, len + 1);
    while (len > 0 && parent[len - 1] != '/' && parent[len - 1] != '\\')
        len--;
    if (len > 0)
        parent[len - 1] = '\0';
    vigil_platform_mkdir_p(parent, error);
}

static vigil_status_t install_package(const char *project_root, const char *pkg_url, const char *version,
                                      vigil_pkg_lock_t *lock, char *out_commit, size_t commit_size,
                                      vigil_error_t *error)
{
    char deps_path[4096];
    char pkg_path[4096];
    int pkg_exists = 0;
    vigil_status_t status;

    /* Build paths */
    if (vigil_platform_path_join(project_root, "deps", deps_path, sizeof(deps_path), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }
    if (vigil_platform_path_join(deps_path, pkg_url, pkg_path, sizeof(pkg_path), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }

    /* Ensure deps/ directory exists */
    vigil_platform_mkdir_p(deps_path, error);

    /* Check if package already exists */
    vigil_platform_file_exists(pkg_path, &pkg_exists);

    if (pkg_exists)
    {
        status = vigil_pkg_git_fetch(pkg_path, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }
    else
    {
        ensure_parent_dir(pkg_path, error);
        status = vigil_pkg_git_clone(pkg_url, pkg_path, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    /* Checkout specific version if requested */
    if (version != NULL && strlen(version) > 0)
    {
        status = vigil_pkg_git_checkout(pkg_path, version, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    /* Get current commit */
    status = vigil_pkg_git_head(pkg_path, out_commit, commit_size, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Update lock */
    vigil_pkg_lock_add(lock, pkg_url, version, out_commit, error);

    /* Install transitive dependencies */
    install_transitive_deps(project_root, pkg_path, lock, error);

    return VIGIL_STATUS_OK;
}

/* Install transitive dependencies from a package's vigil.toml */
static vigil_status_t install_transitive_deps(const char *project_root, const char *pkg_path, vigil_pkg_lock_t *lock,
                                              vigil_error_t *error)
{
    char toml_path[4096];
    char *data = NULL;
    size_t length;
    vigil_toml_value_t *root = NULL;
    const vigil_toml_value_t *deps;
    size_t i, count;
    int exists = 0;

    /* Check if package has vigil.toml */
    if (vigil_platform_path_join(pkg_path, "vigil.toml", toml_path, sizeof(toml_path), error) != VIGIL_STATUS_OK)
    {
        return VIGIL_STATUS_OK; /* Not fatal */
    }

    vigil_platform_file_exists(toml_path, &exists);
    if (!exists)
        return VIGIL_STATUS_OK;

    if (vigil_platform_read_file(NULL, toml_path, &data, &length, error) != VIGIL_STATUS_OK)
    {
        return VIGIL_STATUS_OK; /* Not fatal */
    }

    if (vigil_toml_parse(NULL, data, length, &root, error) != VIGIL_STATUS_OK)
    {
        free(data);
        return VIGIL_STATUS_OK; /* Not fatal */
    }
    free(data);

    deps = vigil_toml_table_get(root, "deps");
    if (deps == NULL || vigil_toml_type(deps) != VIGIL_TOML_TABLE)
    {
        vigil_toml_free(&root);
        return VIGIL_STATUS_OK;
    }

    count = vigil_toml_table_count(deps);
    for (i = 0; i < count; i++)
    {
        const char *dep_url = NULL;
        size_t dep_url_len;
        const vigil_toml_value_t *version_val;
        const char *version = NULL;
        char commit[64];
        char url_copy[1024];

        if (vigil_toml_table_entry(deps, i, &dep_url, &dep_url_len, &version_val) != VIGIL_STATUS_OK)
            continue;

        /* Skip if already in lock (already installed) */
        snprintf(url_copy, sizeof(url_copy), "%.*s", (int)dep_url_len, dep_url);
        if (vigil_pkg_lock_find(lock, url_copy) != NULL)
            continue;

        if (version_val != NULL && vigil_toml_type(version_val) == VIGIL_TOML_STRING)
        {
            version = vigil_toml_string_value(version_val);
        }

        /* Install this transitive dependency */
        install_package(project_root, url_copy, version, lock, commit, sizeof(commit), error);
    }

    vigil_toml_free(&root);
    return VIGIL_STATUS_OK;
}

/* ── High-level: vigil_pkg_get ────────────────────────────────────── */

vigil_status_t vigil_pkg_get(const char *project_root, const char *specifier, vigil_error_t *error)
{
    vigil_pkg_spec_t spec = {0};
    vigil_pkg_lock_t lock = {0};
    vigil_toml_value_t *root = NULL;
    vigil_toml_value_t *deps = NULL;
    vigil_toml_value_t *version_val = NULL;
    char lock_path[4096];
    char commit[64];
    vigil_status_t status;
    const char *version_str;

    if (project_root == NULL || specifier == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Check git is available */
    status = vigil_pkg_git_available(error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Parse specifier */
    status = vigil_pkg_parse_spec(specifier, &spec, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Build lock path */
    if (vigil_platform_path_join(project_root, "vigil.lock", lock_path, sizeof(lock_path), error) != VIGIL_STATUS_OK)
    {
        goto cleanup;
    }

    /* Read existing lock file (ignore errors - may not exist) */
    vigil_pkg_lock_init(&lock);
    vigil_pkg_lock_read(lock_path, &lock, NULL);

    /* Install package and transitive deps */
    status = install_package(project_root, spec.url, spec.version, &lock, commit, sizeof(commit), error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    /* Write lock file */
    status = vigil_pkg_lock_write(lock_path, &lock, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    /* Update vigil.toml */
    status = read_project_toml(project_root, &root, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    status = ensure_deps_table(root, &deps, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    /* Add/update dependency in [deps] */
    version_str = spec.version ? spec.version : commit;
    status = vigil_toml_string_new(NULL, version_str, strlen(version_str), &version_val, error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    /* Remove existing entry first, then add new one */
    vigil_toml_table_remove(deps, spec.url, NULL);
    status = vigil_toml_table_set(deps, spec.url, strlen(spec.url), version_val, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_toml_free(&version_val);
        goto cleanup;
    }

    status = write_project_toml(project_root, root, error);

cleanup:
    vigil_pkg_spec_free(&spec);
    vigil_pkg_lock_free(&lock);
    vigil_toml_free(&root);
    return status;
}

/* ── High-level: vigil_pkg_sync ───────────────────────────────────── */

vigil_status_t vigil_pkg_sync(const char *project_root, vigil_error_t *error)
{
    vigil_toml_value_t *root = NULL;
    const vigil_toml_value_t *deps;
    vigil_pkg_lock_t lock = {0};
    char lock_path[4096];
    size_t i, count;
    vigil_status_t status;

    if (project_root == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_pkg_git_available(error);
    if (status != VIGIL_STATUS_OK)
        return status;

    status = read_project_toml(project_root, &root, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    deps = vigil_toml_table_get(root, "deps");
    if (deps == NULL || vigil_toml_type(deps) != VIGIL_TOML_TABLE)
    {
        vigil_toml_free(&root);
        return VIGIL_STATUS_OK; /* No deps to sync */
    }

    if (vigil_platform_path_join(project_root, "vigil.lock", lock_path, sizeof(lock_path), error) != VIGIL_STATUS_OK)
    {
        vigil_toml_free(&root);
        return error->type;
    }

    vigil_pkg_lock_init(&lock);
    vigil_pkg_lock_read(lock_path, &lock, NULL);

    count = vigil_toml_table_count(deps);
    for (i = 0; i < count; i++)
    {
        const char *pkg_url = NULL;
        size_t pkg_url_len;
        const vigil_toml_value_t *version_val;
        const char *version = NULL;
        char url_copy[1024];
        char commit[64];

        if (vigil_toml_table_entry(deps, i, &pkg_url, &pkg_url_len, &version_val) != VIGIL_STATUS_OK)
            continue;

        if (version_val != NULL && vigil_toml_type(version_val) == VIGIL_TOML_STRING)
        {
            version = vigil_toml_string_value(version_val);
        }

        snprintf(url_copy, sizeof(url_copy), "%.*s", (int)pkg_url_len, pkg_url);

        /* Install package (handles transitive deps too) */
        status = install_package(project_root, url_copy, version, &lock, commit, sizeof(commit), error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_toml_free(&root);
            vigil_pkg_lock_free(&lock);
            return status;
        }
    }

    vigil_pkg_lock_write(lock_path, &lock, error);
    vigil_pkg_lock_free(&lock);
    vigil_toml_free(&root);
    return VIGIL_STATUS_OK;
}

/* ── High-level: vigil_pkg_remove ─────────────────────────────────── */

static vigil_status_t remove_directory_recursive(const char *path, vigil_error_t *error);

static vigil_status_t remove_dir_callback(const char *name, int is_dir, void *user_data)
{
    char *parent = (char *)user_data;
    char child[4096];
    vigil_error_t err = {0};

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return VIGIL_STATUS_OK;

    snprintf(child, sizeof(child), "%s/%s", parent, name);

    if (is_dir)
    {
        remove_directory_recursive(child, &err);
    }
    else
    {
        vigil_platform_remove(child, &err);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t remove_directory_recursive(const char *path, vigil_error_t *error)
{
    vigil_platform_list_dir(path, remove_dir_callback, (void *)path, error);
    return vigil_platform_remove(path, error);
}

static void lock_remove_entry(vigil_pkg_lock_t *lock, const char *name)
{
    size_t i;
    for (i = 0; i < lock->count; i++)
    {
        if (strcmp(lock->entries[i].name, name) == 0)
        {
            free(lock->entries[i].name);
            free(lock->entries[i].version);
            free(lock->entries[i].commit);
            if (i + 1 < lock->count)
            {
                memmove(&lock->entries[i], &lock->entries[i + 1],
                        (lock->count - i - 1) * sizeof(vigil_pkg_lock_entry_t));
            }
            lock->count--;
            break;
        }
    }
}

vigil_status_t vigil_pkg_remove(const char *project_root, const char *package_url, vigil_error_t *error)
{
    vigil_toml_value_t *root = NULL;
    vigil_toml_value_t *deps = NULL;
    vigil_pkg_lock_t lock = {0};
    char deps_path[4096];
    char pkg_path[4096];
    char lock_path[4096];
    vigil_status_t status;
    int pkg_exists = 0;

    if (project_root == NULL || package_url == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Build paths */
    if (vigil_platform_path_join(project_root, "deps", deps_path, sizeof(deps_path), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }
    if (vigil_platform_path_join(deps_path, package_url, pkg_path, sizeof(pkg_path), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }
    if (vigil_platform_path_join(project_root, "vigil.lock", lock_path, sizeof(lock_path), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }

    /* Remove from vigil.toml */
    status = read_project_toml(project_root, &root, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    deps = (vigil_toml_value_t *)vigil_toml_table_get(root, "deps");
    if (deps != NULL && vigil_toml_type(deps) == VIGIL_TOML_TABLE)
    {
        vigil_toml_table_remove(deps, package_url, error);
    }

    status = write_project_toml(project_root, root, error);
    vigil_toml_free(&root);
    if (status != VIGIL_STATUS_OK)
        return status;

    /* Remove from vigil.lock */
    vigil_pkg_lock_init(&lock);
    if (vigil_pkg_lock_read(lock_path, &lock, NULL) == VIGIL_STATUS_OK)
    {
        lock_remove_entry(&lock, package_url);
        vigil_pkg_lock_write(lock_path, &lock, error);
    }
    vigil_pkg_lock_free(&lock);

    /* Remove from deps/ */
    vigil_platform_file_exists(pkg_path, &pkg_exists);
    if (pkg_exists)
    {
        remove_directory_recursive(pkg_path, error);
    }

    return VIGIL_STATUS_OK;
}

/* ── Import resolution ───────────────────────────────────────────── */

static int pkg_try_candidate(const char *candidate, char *out_path, size_t path_size, vigil_error_t *error)
{
    int exists = 0;
    if (vigil_platform_file_exists(candidate, &exists) == VIGIL_STATUS_OK && exists)
    {
        size_t len = strlen(candidate);
        if (len >= path_size)
        {
            set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "path buffer too small");
            return -1;
        }
        memcpy(out_path, candidate, len + 1);
        return 1;
    }
    return 0;
}

vigil_status_t vigil_pkg_resolve_import(const char *project_root, const char *import_path, char *out_path,
                                        size_t path_size, vigil_error_t *error)
{
    char deps_path[4096];
    char candidate[4096];

    if (project_root == NULL || import_path == NULL || out_path == NULL)
    {
        set_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Only handle imports that look like package URLs (contain a dot before first slash) */
    {
        const char *slash = strchr(import_path, '/');
        const char *dot = strchr(import_path, '.');
        if (dot == NULL || slash == NULL || dot > slash)
        {
            return VIGIL_STATUS_INVALID_ARGUMENT; /* Not a package import */
        }
    }

    /* Build candidate path: deps/<import_path>/lib/<last_segment>.vigil
       or deps/<import_path>.vigil */
    if (vigil_platform_path_join(project_root, "deps", deps_path, sizeof(deps_path), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }

    /* Try deps/<import_path>.vigil first */
    if (vigil_platform_path_join(deps_path, import_path, candidate, sizeof(candidate), error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }
    {
        size_t len = strlen(candidate);
        if (len + 6 < sizeof(candidate))
        {
            memcpy(candidate + len, ".vigil", 6);
        }
    }
    {
        int rc = pkg_try_candidate(candidate, out_path, path_size, error);
        if (rc != 0)
            return rc > 0 ? VIGIL_STATUS_OK : VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Try deps/<package>/lib/<module>.vigil */
    {
        /* Find the package root (first 3 segments: domain/user/repo) */
        const char *p = import_path;
        int slashes = 0;
        const char *pkg_end = NULL;
        const char *module_start = NULL;

        while (*p)
        {
            if (*p == '/')
            {
                slashes++;
                if (slashes == 3)
                {
                    pkg_end = p;
                    module_start = p + 1;
                    break;
                }
            }
            p++;
        }

        if (pkg_end != NULL && module_start != NULL && *module_start != '\0')
        {
            char pkg_root[4096];
            char lib_path[4096];
            char module_file[256];

            snprintf(pkg_root, sizeof(pkg_root), "%.*s", (int)(pkg_end - import_path), import_path);
            snprintf(module_file, sizeof(module_file), "%s.vigil", module_start);

            if (vigil_platform_path_join(deps_path, pkg_root, candidate, sizeof(candidate), error) == VIGIL_STATUS_OK &&
                vigil_platform_path_join(candidate, "lib", lib_path, sizeof(lib_path), error) == VIGIL_STATUS_OK &&
                vigil_platform_path_join(lib_path, module_file, candidate, sizeof(candidate), error) == VIGIL_STATUS_OK)
            {
                int rc = pkg_try_candidate(candidate, out_path, path_size, error);
                if (rc != 0)
                    return rc > 0 ? VIGIL_STATUS_OK : VIGIL_STATUS_INVALID_ARGUMENT;
            }
        }
    }

    /* Try deps/<import_path>/lib/mod.vigil (package entry point) */
    if (vigil_platform_path_join(deps_path, import_path, candidate, sizeof(candidate), error) == VIGIL_STATUS_OK)
    {
        char entry[4096];
        char mod_path[4096];
        if (vigil_platform_path_join(candidate, "lib", entry, sizeof(entry), error) == VIGIL_STATUS_OK &&
            vigil_platform_path_join(entry, "mod.vigil", mod_path, sizeof(mod_path), error) == VIGIL_STATUS_OK)
        {
            int rc = pkg_try_candidate(mod_path, out_path, path_size, error);
            if (rc != 0)
                return rc > 0 ? VIGIL_STATUS_OK : VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }

    return VIGIL_STATUS_INVALID_ARGUMENT;
}
