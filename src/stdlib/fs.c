/* VIGIL standard library: fs module.
 *
 * Provides filesystem operations: path manipulation, file I/O,
 * directory operations, and standard locations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strtok_r strtok_s
#endif

#include "platform/platform.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* ── Allocator helpers ───────────────────────────────────────────── */

static const vigil_allocator_t *get_alloc(vigil_vm_t *vm)
{
    return vigil_runtime_allocator(vigil_vm_runtime(vm));
}

static void dealloc(const vigil_allocator_t *a, void *ptr)
{
    if (a)
        a->deallocate(a->user_data, ptr);
    else
        free(ptr);
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_string_arg(vigil_vm_t *vm, size_t base, size_t idx, const char **str, size_t *len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *str = vigil_string_object_c_str(obj);
    *len = vigil_string_object_length(obj);
    return true;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), str, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t push_bool(vigil_vm_t *vm, int b, vigil_error_t *error)
{
    vigil_value_t val;
    vigil_value_init_bool(&val, b);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t n, vigil_error_t *error)
{
    vigil_value_t val;
    vigil_value_init_int(&val, n);
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── Path operations ─────────────────────────────────────────────── */

static vigil_status_t fs_join(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char result[4096] = "";

    for (size_t i = 0; i < arg_count; i++)
    {
        const char *part;
        size_t part_len;
        if (!get_string_arg(vm, base, i, &part, &part_len))
            continue;
        if (part_len == 0)
            continue;

        if (result[0] != '\0')
        {
            size_t rlen = strlen(result);
            if (result[rlen - 1] != '/' && part[0] != '/')
            {
                if (rlen + 1 < sizeof(result))
                {
                    result[rlen] = '/';
                    result[rlen + 1] = '\0';
                }
            }
        }
        size_t space = sizeof(result) - strlen(result) - 1;
        size_t plen = strlen(part);
        if (plen > space)
            plen = space;
        size_t rlen = strlen(result);
        memcpy(result + rlen, part, plen);
        result[rlen + plen] = '\0';
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    return push_string(vm, result, strlen(result), error);
}

static vigil_status_t fs_clean(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    vigil_runtime_t *runtime;
    void *memory = NULL;
    char *copy;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, ".", 1, error);
    }

    char result[4096];
    char *parts[256];
    size_t part_count = 0;
    int is_abs = (path_len > 0 && path[0] == '/');

    /* Copy and split by / */
    runtime = vigil_vm_runtime(vm);
    if (vigil_runtime_alloc(runtime, path_len + 1, &memory, error) != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    copy = (char *)memory;
    memcpy(copy, path, path_len);
    copy[path_len] = '\0';

    char *tok = copy;
    char *next = NULL;
    while ((tok = strtok_r(tok, "/", &next)) != NULL)
    {
        if (strcmp(tok, ".") == 0)
        {
            /* skip */
        }
        else if (strcmp(tok, "..") == 0)
        {
            if (part_count > 0 && strcmp(parts[part_count - 1], "..") != 0)
            {
                part_count--;
            }
            else if (!is_abs)
            {
                parts[part_count++] = "..";
            }
        }
        else if (tok[0] != '\0')
        {
            parts[part_count++] = tok;
        }
        tok = NULL;
    }

    /* Rebuild */
    result[0] = '\0';
    size_t pos = 0;
    if (is_abs)
    {
        result[0] = '/';
        result[1] = '\0';
        pos = 1;
    }
    for (size_t i = 0; i < part_count; i++)
    {
        size_t plen = strlen(parts[i]);
        if (i > 0 || is_abs)
        {
            if (pos < sizeof(result) - 1)
                result[pos++] = '/';
        }
        if (pos + plen < sizeof(result))
        {
            memcpy(result + pos, parts[i], plen);
            pos += plen;
        }
        result[pos] = '\0';
    }
    if (result[0] == '\0')
    {
        result[0] = '.';
        result[1] = '\0';
    }
    /* Fix double slash at start */
    if (is_abs && result[1] == '/')
        memmove(result + 1, result + 2, strlen(result + 1));

    vigil_runtime_free(runtime, &memory);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_string(vm, result, strlen(result), error);
}

static vigil_status_t fs_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, ".", 1, error);
    }

    /* Find last / */
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++)
    {
        if (path[i] == '/')
            last_sep = path + i;
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    if (!last_sep)
        return push_string(vm, ".", 1, error);
    if (last_sep == path)
        return push_string(vm, "/", 1, error);
    return push_string(vm, path, (size_t)(last_sep - path), error);
}

static vigil_status_t fs_base(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    /* Find last / */
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++)
    {
        if (path[i] == '/')
            last_sep = path + i;
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    if (!last_sep)
        return push_string(vm, path, path_len, error);
    return push_string(vm, last_sep + 1, path_len - (size_t)(last_sep - path) - 1, error);
}

static vigil_status_t fs_ext(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    /* Find last . after last / */
    const char *last_dot = NULL;
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++)
    {
        if (path[i] == '/')
            last_sep = path + i;
        if (path[i] == '.')
            last_dot = path + i;
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    if (!last_dot || (last_sep && last_dot < last_sep))
    {
        return push_string(vm, "", 0, error);
    }
    return push_string(vm, last_dot, path_len - (size_t)(last_dot - path), error);
}

static vigil_status_t fs_is_abs(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    vigil_vm_stack_pop_n(vm, arg_count);
#ifdef _WIN32
    int is_abs = (path_len >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) ||
                 (path_len > 0 && (path[0] == '/' || path[0] == '\\'));
#else
    int is_abs = (path_len > 0 && path[0] == '/');
#endif
    return push_bool(vm, is_abs, error);
}

/* ── File operations ─────────────────────────────────────────────── */

static vigil_status_t fs_read(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    vigil_runtime_t *runtime;
    const vigil_allocator_t *allocator;
    void *memory = NULL;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    char *data = NULL;
    size_t data_len = 0;
    runtime = vigil_vm_runtime(vm);
    allocator = vigil_runtime_allocator(runtime);
    vigil_status_t s = vigil_platform_read_file(allocator, pathbuf, &data, &data_len, error);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);
    s = push_string(vm, data, data_len, error);
    memory = data;
    vigil_runtime_free(runtime, &memory);
    return s;
}

static vigil_status_t fs_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path, *data;
    size_t path_len, data_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len) || !get_string_arg(vm, base, 1, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_status_t s = vigil_platform_write_file(pathbuf, data, data_len, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_append(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path, *data;
    size_t path_len, data_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len) || !get_string_arg(vm, base, 1, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_status_t s = vigil_platform_append_file(pathbuf, data, data_len, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_copy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *src, *dst;
    size_t src_len, dst_len;

    if (!get_string_arg(vm, base, 0, &src, &src_len) || !get_string_arg(vm, base, 1, &dst, &dst_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char srcbuf[4096], dstbuf[4096];
    snprintf(srcbuf, sizeof(srcbuf), "%.*s", (int)src_len, src);
    snprintf(dstbuf, sizeof(dstbuf), "%.*s", (int)dst_len, dst);

    vigil_status_t s = vigil_platform_copy_file(srcbuf, dstbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_move(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *src, *dst;
    size_t src_len, dst_len;

    if (!get_string_arg(vm, base, 0, &src, &src_len) || !get_string_arg(vm, base, 1, &dst, &dst_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char srcbuf[4096], dstbuf[4096];
    snprintf(srcbuf, sizeof(srcbuf), "%.*s", (int)src_len, src);
    snprintf(dstbuf, sizeof(dstbuf), "%.*s", (int)dst_len, dst);

    vigil_status_t s = vigil_platform_rename(srcbuf, dstbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_remove(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_status_t s = vigil_platform_remove(pathbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_exists(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    int exists = 0;
    vigil_platform_file_exists(pathbuf, &exists);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, exists, error);
}

static vigil_status_t fs_is_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    int is_dir = 0;
    vigil_platform_is_directory(pathbuf, &is_dir);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, is_dir, error);
}

static vigil_status_t fs_is_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    int is_file = 0;
    vigil_platform_is_file(pathbuf, &is_file);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, is_file, error);
}

/* ── Directory operations ────────────────────────────────────────── */

static vigil_status_t fs_mkdir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_status_t s = vigil_platform_mkdir(pathbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_mkdir_all(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_status_t s = vigil_platform_mkdir_p(pathbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

/* Callback data for list_dir */
typedef struct
{
    vigil_vm_t *vm;
    vigil_object_t *array;
    vigil_error_t *error;
} list_ctx_t;

static vigil_status_t list_callback(const char *name, int is_dir, void *user_data)
{
    (void)is_dir;
    list_ctx_t *ctx = (list_ctx_t *)user_data;

    vigil_object_t *str = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(ctx->vm), name, strlen(name), &str, ctx->error);
    if (s != VIGIL_STATUS_OK)
        return s;

    vigil_value_t val;
    vigil_value_init_object(&val, &str);
    s = vigil_array_object_append(ctx->array, &val, ctx->error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t fs_list(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        /* Return empty array */
        vigil_object_t *arr = NULL;
        vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        vigil_status_t s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return s;
    }

    list_ctx_t ctx = {vm, arr, error};
    vigil_platform_list_dir(pathbuf, list_callback, &ctx, error);

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* Recursive walk helper */
typedef struct
{
    vigil_vm_t *vm;
    vigil_object_t *array;
    vigil_error_t *error;
    char base_path[4096];
} walk_ctx_t;

static vigil_status_t walk_dir(walk_ctx_t *ctx, const char *dir);

static vigil_status_t walk_callback(const char *name, int is_dir, void *user_data)
{
    walk_ctx_t *ctx = (walk_ctx_t *)user_data;

    char full_path[4096];
    size_t base_len = strlen(ctx->base_path);
    size_t name_len = strlen(name);
    if (base_len + 1 + name_len >= sizeof(full_path))
    {
        return VIGIL_STATUS_OK; /* Skip paths that are too long */
    }
    memcpy(full_path, ctx->base_path, base_len);
    full_path[base_len] = '/';
    memcpy(full_path + base_len + 1, name, name_len + 1);

    vigil_object_t *str = NULL;
    vigil_status_t s =
        vigil_string_object_new(vigil_vm_runtime(ctx->vm), full_path, strlen(full_path), &str, ctx->error);
    if (s != VIGIL_STATUS_OK)
        return s;

    vigil_value_t val;
    vigil_value_init_object(&val, &str);
    s = vigil_array_object_append(ctx->array, &val, ctx->error);
    vigil_value_release(&val);
    if (s != VIGIL_STATUS_OK)
        return s;

    if (is_dir)
    {
        char saved[4096];
        size_t full_len = strlen(full_path);
        memcpy(saved, ctx->base_path, base_len + 1);
        if (full_len < sizeof(ctx->base_path))
        {
            memcpy(ctx->base_path, full_path, full_len + 1);
        }
        walk_dir(ctx, full_path);
        memcpy(ctx->base_path, saved, base_len + 1);
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t walk_dir(walk_ctx_t *ctx, const char *dir)
{
    return vigil_platform_list_dir(dir, walk_callback, ctx, ctx->error);
}

static vigil_status_t fs_walk(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *arr = NULL;
        vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        vigil_status_t s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return s;
    }

    walk_ctx_t ctx = {vm, arr, error, ""};
    size_t copy_len = path_len < sizeof(ctx.base_path) - 1 ? path_len : sizeof(ctx.base_path) - 1;
    memcpy(ctx.base_path, pathbuf, copy_len);
    ctx.base_path[copy_len] = '\0';
    walk_dir(&ctx, pathbuf);

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* ── Metadata and locations ──────────────────────────────────────── */

static vigil_status_t fs_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    int64_t size = vigil_platform_file_size(pathbuf);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, size, error);
}

static vigil_status_t fs_mtime(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    int64_t mtime = vigil_platform_file_mtime(pathbuf);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, mtime, error);
}

static vigil_status_t fs_temp_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    char *path = NULL;
    vigil_status_t s = vigil_platform_temp_dir(get_alloc(vm), &path, error);
    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);

    s = push_string(vm, path, strlen(path), error);
    dealloc(get_alloc(vm), path);
    return s;
}

static vigil_status_t fs_temp_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *prefix = "tmp";
    size_t prefix_len = 3;

    if (arg_count > 0)
    {
        get_string_arg(vm, base, 0, &prefix, &prefix_len);
    }

    char prefixbuf[64];
    snprintf(prefixbuf, sizeof(prefixbuf), "%.*s", (int)prefix_len, prefix);

    vigil_vm_stack_pop_n(vm, arg_count);

    char *path = NULL;
    vigil_status_t s = vigil_platform_temp_file(get_alloc(vm), prefixbuf, &path, error);
    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);

    s = push_string(vm, path, strlen(path), error);
    dealloc(get_alloc(vm), path);
    return s;
}

static vigil_status_t fs_home_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    char *path = NULL;
    vigil_status_t s = vigil_platform_home_dir(get_alloc(vm), &path, error);
    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);

    s = push_string(vm, path, strlen(path), error);
    dealloc(get_alloc(vm), path);
    return s;
}

static vigil_status_t fs_config_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    char *path = NULL;
    vigil_status_t s = vigil_platform_config_dir(get_alloc(vm), &path, error);
    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);

    s = push_string(vm, path, strlen(path), error);
    dealloc(get_alloc(vm), path);
    return s;
}

static vigil_status_t fs_cache_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    char *path = NULL;
    vigil_status_t s = vigil_platform_cache_dir(get_alloc(vm), &path, error);
    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);

    s = push_string(vm, path, strlen(path), error);
    dealloc(get_alloc(vm), path);
    return s;
}

static vigil_status_t fs_data_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    char *path = NULL;
    vigil_status_t s = vigil_platform_data_dir(get_alloc(vm), &path, error);
    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);

    s = push_string(vm, path, strlen(path), error);
    dealloc(get_alloc(vm), path);
    return s;
}

static vigil_status_t fs_cwd(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    char *path = NULL;
    vigil_status_t s = vigil_platform_getcwd(get_alloc(vm), &path, error);
    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);

    s = push_string(vm, path, strlen(path), error);
    dealloc(get_alloc(vm), path);
    return s;
}

/* ── Symlink operations ──────────────────────────────────────────── */

static vigil_status_t fs_symlink(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *target, *linkpath;
    size_t target_len, link_len;

    if (!get_string_arg(vm, base, 0, &target, &target_len) || !get_string_arg(vm, base, 1, &linkpath, &link_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char tbuf[4096], lbuf[4096];
    snprintf(tbuf, sizeof(tbuf), "%.*s", (int)target_len, target);
    snprintf(lbuf, sizeof(lbuf), "%.*s", (int)link_len, linkpath);

    vigil_status_t s = vigil_platform_symlink(tbuf, lbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_readlink(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    char *target = NULL;
    vigil_status_t s = vigil_platform_readlink(get_alloc(vm), pathbuf, &target, error);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK)
        return push_string(vm, "", 0, error);
    s = push_string(vm, target, strlen(target), error);
    dealloc(get_alloc(vm), target);
    return s;
}

static vigil_status_t fs_is_symlink(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    int result = 0;
    vigil_platform_is_symlink(pathbuf, &result);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, result, error);
}

/* ── Recursive remove ────────────────────────────────────────────── */

static vigil_status_t fs_remove_all(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_status_t s = vigil_platform_remove_all(pathbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

/* ── Glob matching ───────────────────────────────────────────────── */

typedef struct
{
    vigil_vm_t *vm;
    vigil_object_t *array;
    vigil_error_t *error;
    const char *pattern;
    const char *dir_path;
} glob_ctx_t;

static vigil_status_t glob_callback(const char *name, int is_dir, void *user_data)
{
    glob_ctx_t *ctx = (glob_ctx_t *)user_data;
    (void)is_dir;

    if (!vigil_platform_glob_match(ctx->pattern, name))
        return VIGIL_STATUS_OK;

    char full[4096];
    snprintf(full, sizeof(full), "%s/%s", ctx->dir_path, name);

    vigil_object_t *str = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(ctx->vm), full, strlen(full), &str, ctx->error);
    if (s != VIGIL_STATUS_OK)
        return s;

    vigil_value_t val;
    vigil_value_init_object(&val, &str);
    s = vigil_array_object_append(ctx->array, &val, ctx->error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t fs_glob(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *dir, *pattern;
    size_t dir_len, pat_len;

    if (!get_string_arg(vm, base, 0, &dir, &dir_len) || !get_string_arg(vm, base, 1, &pattern, &pat_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *arr = NULL;
        vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        vigil_status_t s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    char dirbuf[4096], patbuf[4096];
    snprintf(dirbuf, sizeof(dirbuf), "%.*s", (int)dir_len, dir);
    snprintf(patbuf, sizeof(patbuf), "%.*s", (int)pat_len, pattern);

    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return s;
    }

    glob_ctx_t ctx = {vm, arr, error, patbuf, dirbuf};
    vigil_platform_list_dir(dirbuf, glob_callback, &ctx, error);

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int str_str_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const char *const fs_path_param_names[] = {"path"};
static const char *const fs_src_dst_param_names[] = {"src", "dst"};
static const char *const fs_target_link_param_names[] = {"target", "link"};
static const char *const fs_dir_pattern_param_names[] = {"dir", "pattern"};
static const char *const fs_prefix_param_names[] = {"prefix"};

static const vigil_native_symbol_doc_t vigil_fs_module_doc = {
    "Filesystem operations.",
    "Path helpers, file and directory operations, standard locations, and buffered Reader/Writer stream helpers.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_join_doc = {
    "Join path segments.", "Combines path segments with the platform separator.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_clean_doc = {"Normalize a path.",
                                                             "Removes redundant separators and dot segments.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_dir_doc = {"Get the directory part of a path.",
                                                           "Returns the parent directory portion of the path.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_base_doc = {"Get the final path element.",
                                                            "Returns the filename or last path segment.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_ext_doc = {
    "Get the file extension.", "Returns the extension including the leading dot when present.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_is_abs_doc = {
    "Check whether a path is absolute.", "Returns true when the path is absolute on the current platform.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_read_doc = {
    "Read a file into a string.", "Reads the entire file contents and returns them as a string.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_write_doc = {
    "Write a file.", "Creates or truncates a file and writes the supplied data.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_append_doc = {"Append to a file.",
                                                              "Appends the supplied data to the end of a file.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_copy_doc = {"Copy a file.", "Copies a file from one path to another.",
                                                            NULL};
static const vigil_native_symbol_doc_t vigil_fs_move_doc = {"Move or rename a path.",
                                                            "Moves or renames a file or directory.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_remove_doc = {
    "Remove a file or empty directory.", "Deletes the path when possible and returns success as a bool.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_remove_all_doc = {
    "Remove a path recursively.", "Deletes a file or directory tree recursively.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_exists_doc = {"Check whether a path exists.",
                                                              "Returns true when the target path exists.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_is_dir_doc = {
    "Check whether a path is a directory.", "Returns true when the target path exists and is a directory.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_is_file_doc = {
    "Check whether a path is a file.", "Returns true when the target path exists and is a regular file.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_is_symlink_doc = {
    "Check whether a path is a symbolic link.", "Returns true when the target path is a symbolic link.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_mkdir_doc = {"Create a directory.", "Creates a single directory.",
                                                             NULL};
static const vigil_native_symbol_doc_t vigil_fs_mkdir_all_doc = {"Create a directory tree.",
                                                                 "Creates a directory and any missing parents.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_list_doc = {"List directory entries.",
                                                            "Returns the immediate names in a directory.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_walk_doc = {
    "Walk a directory tree.", "Returns all descendant paths rooted at the target directory.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_glob_doc = {
    "Glob within a directory.", "Returns paths whose names match the provided glob pattern.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_symlink_doc = {
    "Create a symbolic link.", "Creates a symbolic link pointing at the target path.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_readlink_doc = {
    "Read a symbolic link target.", "Returns the target path referenced by a symbolic link.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_size_doc = {"Get file size.",
                                                            "Returns the file size in bytes, or -1 on failure.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_mtime_doc = {
    "Get modification time.", "Returns the last modification time as a Unix timestamp.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_temp_dir_doc = {"Get the temporary directory.",
                                                                "Returns the platform temporary directory path.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_temp_file_doc = {
    "Create a temporary file path.", "Creates a unique temporary file and returns its path.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_home_dir_doc = {"Get the home directory.",
                                                                "Returns the current user's home directory.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_config_dir_doc = {
    "Get the config directory.", "Returns the user-specific configuration directory.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_cache_dir_doc = {"Get the cache directory.",
                                                                 "Returns the user-specific cache directory.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_data_dir_doc = {
    "Get the data directory.", "Returns the user-specific application data directory.", NULL};
static const vigil_native_symbol_doc_t vigil_fs_cwd_doc = {"Get the current working directory.",
                                                           "Returns the current process working directory.", NULL};

static const vigil_native_module_function_t vigil_fs_functions[] = {
    /* Path operations */
    {"join", 4U, fs_join, 2U, str_str_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, fs_src_dst_param_names,
     NULL, NULL, &vigil_fs_join_doc},
    {"clean", 5U, fs_clean, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_clean_doc},
    {"dir", 3U, fs_dir, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL, NULL,
     &vigil_fs_dir_doc},
    {"base", 4U, fs_base, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_base_doc},
    {"ext", 3U, fs_ext, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL, NULL,
     &vigil_fs_ext_doc},
    {"is_abs", 6U, fs_is_abs, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_is_abs_doc},
    /* File operations */
    {"read", 4U, fs_read, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_read_doc},
    {"write", 5U, fs_write, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_src_dst_param_names,
     NULL, NULL, &vigil_fs_write_doc},
    {"append", 6U, fs_append, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_src_dst_param_names,
     NULL, NULL, &vigil_fs_append_doc},
    {"copy", 4U, fs_copy, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_src_dst_param_names,
     NULL, NULL, &vigil_fs_copy_doc},
    {"move", 4U, fs_move, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_src_dst_param_names,
     NULL, NULL, &vigil_fs_move_doc},
    {"remove", 6U, fs_remove, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_remove_doc},
    {"remove_all", 10U, fs_remove_all, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names,
     NULL, NULL, &vigil_fs_remove_all_doc},
    {"exists", 6U, fs_exists, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_exists_doc},
    {"is_dir", 6U, fs_is_dir, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_is_dir_doc},
    {"is_file", 7U, fs_is_file, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_is_file_doc},
    {"is_symlink", 10U, fs_is_symlink, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names,
     NULL, NULL, &vigil_fs_is_symlink_doc},
    /* Directory operations */
    {"mkdir", 5U, fs_mkdir, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL,
     NULL, &vigil_fs_mkdir_doc},
    {"mkdir_all", 9U, fs_mkdir_all, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names,
     NULL, NULL, &vigil_fs_mkdir_all_doc},
    {"list", 4U, fs_list, 1U, str_param, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL, NULL, 0U,
     fs_path_param_names, NULL, NULL, &vigil_fs_list_doc},
    {"walk", 4U, fs_walk, 1U, str_param, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL, NULL, 0U,
     fs_path_param_names, NULL, NULL, &vigil_fs_walk_doc},
    {"glob", 4U, fs_glob, 2U, str_str_params, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL, NULL, 0U,
     fs_dir_pattern_param_names, NULL, NULL, &vigil_fs_glob_doc},
    /* Symlink operations */
    {"symlink", 7U, fs_symlink, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     fs_target_link_param_names, NULL, NULL, &vigil_fs_symlink_doc},
    {"readlink", 8U, fs_readlink, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names,
     NULL, NULL, &vigil_fs_readlink_doc},
    /* Metadata */
    {"size", 4U, fs_size, 1U, str_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL, NULL,
     &vigil_fs_size_doc},
    {"mtime", 5U, fs_mtime, 1U, str_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, fs_path_param_names, NULL, NULL,
     &vigil_fs_mtime_doc},
    /* Locations */
    {"temp_dir", 8U, fs_temp_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_fs_temp_dir_doc},
    {"temp_file", 9U, fs_temp_file, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     fs_prefix_param_names, NULL, NULL, &vigil_fs_temp_file_doc},
    {"home_dir", 8U, fs_home_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_fs_home_dir_doc},
    {"config_dir", 10U, fs_config_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_fs_config_dir_doc},
    {"cache_dir", 9U, fs_cache_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_fs_cache_dir_doc},
    {"data_dir", 8U, fs_data_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_fs_data_dir_doc},
    {"cwd", 3U, fs_cwd, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, &vigil_fs_cwd_doc},
};

#define FS_FUNCTION_COUNT (sizeof(vigil_fs_functions) / sizeof(vigil_fs_functions[0]))

/* ── Streaming I/O: Reader and Writer classes ─────────────────────
 *
 * Reader: buffered line-by-line and block reading from a file.
 * Writer: buffered writing and appending to a file.
 *
 * Both use the handle-registry pattern from thread.c — opaque C structs
 * owned by a global registry, with an i64 handle stored as the sole
 * instance field.  This avoids exposing raw FILE* into the Vigil value
 * system and makes cleanup straightforward.
 */

/* ── Error-kind constants (from compiler.c vigil_builtin_error_kind_by_name) */
#define FS_ERR_NOT_FOUND 1
#define FS_ERR_IO 5
#define FS_ERR_EOF 4

/* ── Reader ──────────────────────────────────────────────────────── */

#define READER_LINE_BUF 65536 /* max line length for read_line() */

typedef struct
{
    FILE *fp;
    char *line_buf; /* heap buffer used by read_line()  */
    int eof_reached;
    int closed;
} fs_reader_t;

enum
{
    RF_HANDLE = 0,
    READER_FIELD_COUNT
};

/* ── Writer ──────────────────────────────────────────────────────── */

typedef struct
{
    FILE *fp;
    int closed;
} fs_writer_t;

enum
{
    WF_HANDLE = 0,
    WRITER_FIELD_COUNT
};

/* ── Shared registry (reuses thread.c handle_registry_t pattern) ── */

#define FS_REGISTRY_INITIAL 64

typedef struct
{
    void **items;
    int64_t count;
    int64_t capacity;
    vigil_platform_mutex_t *lock;
} fs_handle_registry_t;

static fs_handle_registry_t g_readers;
static fs_handle_registry_t g_writers;
static volatile int64_t g_stream_registries_state = 0;

static vigil_status_t fs_registry_init(fs_handle_registry_t *r, vigil_runtime_t *rt, vigil_error_t *error)
{
    vigil_status_t st;
    r->capacity = FS_REGISTRY_INITIAL;
    r->count = 0;
    st = vigil_runtime_alloc(rt, (size_t)r->capacity * sizeof(void *), (void **)&r->items, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = vigil_platform_mutex_create(&r->lock, error);
    if (st != VIGIL_STATUS_OK)
        vigil_runtime_free(rt, (void **)&r->items);
    return st;
}

static vigil_status_t fs_registry_alloc(fs_handle_registry_t *r, vigil_runtime_t *rt, int64_t *out_handle,
                                        vigil_error_t *error)
{
    vigil_platform_mutex_lock(r->lock);
    if (r->count == r->capacity)
    {
        int64_t new_cap = r->capacity * 2;
        void **new_items = NULL;
        vigil_status_t st = vigil_runtime_alloc(rt, (size_t)new_cap * sizeof(void *), (void **)&new_items, error);
        if (st != VIGIL_STATUS_OK)
        {
            vigil_platform_mutex_unlock(r->lock);
            return st;
        }
        memcpy(new_items, r->items, (size_t)r->capacity * sizeof(void *));
        vigil_runtime_free(rt, (void **)&r->items);
        r->items = new_items;
        r->capacity = new_cap;
    }
    *out_handle = r->count++;
    vigil_platform_mutex_unlock(r->lock);
    return VIGIL_STATUS_OK;
}

static void *fs_registry_get(fs_handle_registry_t *r, int64_t handle)
{
    void *val = NULL;
    vigil_platform_mutex_lock(r->lock);
    if (handle >= 0 && handle < r->count)
        val = r->items[handle];
    vigil_platform_mutex_unlock(r->lock);
    return val;
}

static void fs_registry_set(fs_handle_registry_t *r, int64_t handle, void *val)
{
    vigil_platform_mutex_lock(r->lock);
    if (handle >= 0 && handle < r->count)
        r->items[handle] = val;
    vigil_platform_mutex_unlock(r->lock);
}

static vigil_status_t ensure_stream_registries(vigil_runtime_t *rt, vigil_error_t *error)
{
    for (;;)
    {
        int64_t state = vigil_atomic_load(&g_stream_registries_state);
        if (state == 2)
            return VIGIL_STATUS_OK;
        if (state == 0 && vigil_atomic_cas(&g_stream_registries_state, 0, 1))
        {
            vigil_status_t st = fs_registry_init(&g_readers, rt, error);
            if (st == VIGIL_STATUS_OK)
                st = fs_registry_init(&g_writers, rt, error);
            if (st != VIGIL_STATUS_OK)
            {
                vigil_atomic_store(&g_stream_registries_state, 0);
                return st;
            }
            vigil_atomic_store(&g_stream_registries_state, 2);
            return VIGIL_STATUS_OK;
        }
        vigil_platform_thread_yield();
    }
}

/* ── Helpers shared by Reader and Writer ─────────────────────────── */

static vigil_object_t *get_self_obj(vigil_vm_t *vm, size_t base)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    return (vigil_object_t *)vigil_nanbox_decode_ptr(v);
}

static int64_t get_handle_field(vigil_object_t *self, size_t field_idx)
{
    vigil_value_t v;
    vigil_instance_object_get_field(self, field_idx, &v);
    return vigil_nanbox_decode_int(v);
}

/* Push a Vigil err value with the given kind onto the stack. */
static vigil_status_t push_err_kind(vigil_vm_t *vm, const char *msg, int64_t kind, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, kind, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    st = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return st;
}

/* Push an empty string followed by an error kind — for (string, err) fail paths. */
static vigil_status_t push_empty_str_and_err(vigil_vm_t *vm, const char *msg, int64_t kind, vigil_error_t *error)
{
    vigil_status_t st = push_string(vm, "", 0, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return push_err_kind(vm, msg, kind, error);
}

/* Push an i32 followed by an error kind — for (i32, err) fail paths. */
static vigil_status_t push_zero_and_err(vigil_vm_t *vm, const char *msg, int64_t kind, vigil_error_t *error)
{
    vigil_status_t st = push_i64(vm, 0, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return push_err_kind(vm, msg, kind, error);
}

/* Push a nil object followed by an error kind — for (Reader/Writer, err) fail paths. */
static vigil_status_t push_nil_and_err(vigil_vm_t *vm, const char *msg, int64_t kind, vigil_error_t *error)
{
    vigil_value_t nil;
    vigil_value_init_nil(&nil);
    vigil_status_t st = vigil_vm_stack_push(vm, &nil, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return push_err_kind(vm, msg, kind, error);
}

/* Push a string result followed by ok — success path for (string, err). */
static vigil_status_t push_str_and_ok(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_status_t st = push_string(vm, str, len, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* Push an i32 result followed by ok — success path for (i32, err). */
static vigil_status_t push_i32_and_ok(vigil_vm_t *vm, int64_t n, vigil_error_t *error)
{
    vigil_status_t st = push_i64(vm, n, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Reader implementation ───────────────────────────────────────── */

static vigil_status_t reader_open(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Stack (is_static=1): [class_index, path_string] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = (size_t)vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base));
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_status_t st;
    const char *path;
    size_t path_len;
    fs_reader_t *rd = NULL;
    int64_t handle;
    vigil_value_t field;
    vigil_object_t *inst = NULL;
    vigil_value_t result;

    st = ensure_stream_registries(rt, error);
    if (st != VIGIL_STATUS_OK)
        return st;

    if (!get_string_arg(vm, base, 1, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_reader: invalid path argument", FS_ERR_IO, error);
    }

    st = vigil_runtime_alloc(rt, sizeof(fs_reader_t), (void **)&rd, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_reader: out of memory", FS_ERR_IO, error);
    }

    st = vigil_runtime_alloc(rt, READER_LINE_BUF, (void **)&rd->line_buf, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_runtime_free(rt, (void **)&rd);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_reader: out of memory", FS_ERR_IO, error);
    }

#ifdef _WIN32
    {
        errno_t fe = fopen_s(&rd->fp, path, "r");
        if (fe != 0)
            rd->fp = NULL;
    }
#else
    rd->fp = fopen(path, "r");
#endif
    if (rd->fp == NULL)
    {
        vigil_runtime_free(rt, (void **)&rd->line_buf);
        vigil_runtime_free(rt, (void **)&rd);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_reader: file not found", FS_ERR_NOT_FOUND, error);
    }

    st = fs_registry_alloc(&g_readers, rt, &handle, error);
    if (st != VIGIL_STATUS_OK)
    {
        fclose(rd->fp);
        vigil_runtime_free(rt, (void **)&rd->line_buf);
        vigil_runtime_free(rt, (void **)&rd);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_reader: registry alloc failed", FS_ERR_IO, error);
    }
    fs_registry_set(&g_readers, handle, rd);

    vigil_value_init_int(&field, handle);
    st = vigil_instance_object_new(rt, ci, &field, READER_FIELD_COUNT, &inst, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (st != VIGIL_STATUS_OK)
        return st;

    vigil_value_init_object(&result, &inst);
    st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    if (st != VIGIL_STATUS_OK)
        return st;
    return vigil_runtime_push_ok_error(rt, vm, error);
}

static vigil_status_t reader_read_line(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Stack: [self] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self_obj(vm, base);
    int64_t handle;
    fs_reader_t *rd;
    char *got;
    size_t len;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (self == NULL)
        return push_empty_str_and_err(vm, "read_line: invalid reader", FS_ERR_IO, error);

    handle = get_handle_field(self, RF_HANDLE);
    rd = (fs_reader_t *)fs_registry_get(&g_readers, handle);
    if (rd == NULL || rd->closed)
        return push_empty_str_and_err(vm, "read_line: reader is closed", FS_ERR_IO, error);
    if (rd->eof_reached)
        return push_empty_str_and_err(vm, "", FS_ERR_EOF, error);

    got = fgets(rd->line_buf, READER_LINE_BUF, rd->fp);
    if (got == NULL)
    {
        rd->eof_reached = 1;
        return push_empty_str_and_err(vm, "", FS_ERR_EOF, error);
    }

    len = strlen(rd->line_buf);
    /* Strip trailing newline so callers receive clean lines. */
    if (len > 0 && rd->line_buf[len - 1] == '\n')
    {
        rd->line_buf[--len] = '\0';
        if (len > 0 && rd->line_buf[len - 1] == '\r')
            rd->line_buf[--len] = '\0';
    }

    return push_str_and_ok(vm, rd->line_buf, len, error);
}

static vigil_status_t reader_read_bytes(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Stack: [self, n_i32] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self_obj(vm, base);
    int64_t n = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base + 1));
    int64_t handle;
    fs_reader_t *rd;
    char *buf = NULL;
    size_t nread;
    vigil_status_t st;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (self == NULL || n <= 0)
        return push_empty_str_and_err(vm, "read_bytes: invalid argument", FS_ERR_IO, error);

    handle = get_handle_field(self, RF_HANDLE);
    rd = (fs_reader_t *)fs_registry_get(&g_readers, handle);
    if (rd == NULL || rd->closed)
        return push_empty_str_and_err(vm, "read_bytes: reader is closed", FS_ERR_IO, error);
    if (rd->eof_reached)
        return push_empty_str_and_err(vm, "", FS_ERR_EOF, error);

    {
        vigil_runtime_t *rt = vigil_vm_runtime(vm);
        st = vigil_runtime_alloc(rt, (size_t)n + 1, (void **)&buf, error);
        if (st != VIGIL_STATUS_OK)
            return push_empty_str_and_err(vm, "read_bytes: out of memory", FS_ERR_IO, error);

        nread = fread(buf, 1, (size_t)n, rd->fp);
        if (nread == 0)
        {
            vigil_runtime_free(rt, (void **)&buf);
            rd->eof_reached = 1;
            return push_empty_str_and_err(vm, "", FS_ERR_EOF, error);
        }
        buf[nread] = '\0';
        st = push_str_and_ok(vm, buf, nread, error);
        vigil_runtime_free(rt, (void **)&buf);
        return st;
    }
}

static vigil_status_t reader_read_all(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Stack: [self] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self_obj(vm, base);
    int64_t handle;
    fs_reader_t *rd;
    char *buf = NULL;
    size_t len = 0, cap = 4096;
    size_t nread;
    vigil_status_t st;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (self == NULL)
        return push_empty_str_and_err(vm, "read_all: invalid reader", FS_ERR_IO, error);

    handle = get_handle_field(self, RF_HANDLE);
    rd = (fs_reader_t *)fs_registry_get(&g_readers, handle);
    if (rd == NULL || rd->closed)
        return push_empty_str_and_err(vm, "read_all: reader is closed", FS_ERR_IO, error);
    if (rd->eof_reached)
        return push_str_and_ok(vm, "", 0, error);

    {
        vigil_runtime_t *rt = vigil_vm_runtime(vm);
        st = vigil_runtime_alloc(rt, cap, (void **)&buf, error);
        if (st != VIGIL_STATUS_OK)
            return push_empty_str_and_err(vm, "read_all: out of memory", FS_ERR_IO, error);

        for (;;)
        {
            nread = fread(buf + len, 1, cap - len, rd->fp);
            len += nread;
            if (nread == 0)
            {
                rd->eof_reached = 1;
                break;
            }
            if (len == cap)
            {
                size_t new_cap = cap * 2;
                st = vigil_runtime_realloc(rt, (void **)&buf, new_cap, error);
                if (st != VIGIL_STATUS_OK)
                {
                    vigil_runtime_free(rt, (void **)&buf);
                    return push_empty_str_and_err(vm, "read_all: out of memory", FS_ERR_IO, error);
                }
                cap = new_cap;
            }
        }

        st = push_str_and_ok(vm, buf, len, error);
        vigil_runtime_free(rt, (void **)&buf);
        return st;
    }
}

static vigil_status_t reader_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Stack: [self] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self_obj(vm, base);
    int64_t handle;
    fs_reader_t *rd;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (self == NULL)
        return push_err_kind(vm, "close: invalid reader", FS_ERR_IO, error);

    handle = get_handle_field(self, RF_HANDLE);
    rd = (fs_reader_t *)fs_registry_get(&g_readers, handle);
    if (rd == NULL)
        return push_err_kind(vm, "close: invalid reader handle", FS_ERR_IO, error);
    if (!rd->closed)
    {
        vigil_runtime_t *rt = vigil_vm_runtime(vm);
        fclose(rd->fp);
        vigil_runtime_free(rt, (void **)&rd->line_buf);
        rd->fp = NULL;
        rd->closed = 1;
        fs_registry_set(&g_readers, handle, rd);
    }
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Writer implementation ───────────────────────────────────────── */

static vigil_status_t writer_open_impl(vigil_vm_t *vm, size_t arg_count, const char *mode, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = (size_t)vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base));
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_status_t st;
    const char *path;
    size_t path_len;
    fs_writer_t *wr = NULL;
    int64_t handle;
    vigil_value_t field;
    vigil_object_t *inst = NULL;
    vigil_value_t result;

    st = ensure_stream_registries(rt, error);
    if (st != VIGIL_STATUS_OK)
        return st;

    if (!get_string_arg(vm, base, 1, &path, &path_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_writer: invalid path argument", FS_ERR_IO, error);
    }

    st = vigil_runtime_alloc(rt, sizeof(fs_writer_t), (void **)&wr, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_writer: out of memory", FS_ERR_IO, error);
    }

#ifdef _WIN32
    {
        errno_t fe = fopen_s(&wr->fp, path, mode);
        if (fe != 0)
            wr->fp = NULL;
    }
#else
    wr->fp = fopen(path, mode);
#endif
    if (wr->fp == NULL)
    {
        vigil_runtime_free(rt, (void **)&wr);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_writer: cannot open file", FS_ERR_IO, error);
    }

    st = fs_registry_alloc(&g_writers, rt, &handle, error);
    if (st != VIGIL_STATUS_OK)
    {
        fclose(wr->fp);
        vigil_runtime_free(rt, (void **)&wr);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_nil_and_err(vm, "open_writer: registry alloc failed", FS_ERR_IO, error);
    }
    fs_registry_set(&g_writers, handle, wr);

    vigil_value_init_int(&field, handle);
    st = vigil_instance_object_new(rt, ci, &field, WRITER_FIELD_COUNT, &inst, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (st != VIGIL_STATUS_OK)
        return st;

    vigil_value_init_object(&result, &inst);
    st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    if (st != VIGIL_STATUS_OK)
        return st;
    return vigil_runtime_push_ok_error(rt, vm, error);
}

static vigil_status_t writer_open(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return writer_open_impl(vm, arg_count, "wb", error);
}

static vigil_status_t writer_open_append(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return writer_open_impl(vm, arg_count, "ab", error);
}

static vigil_status_t writer_write_impl(vigil_vm_t *vm, size_t arg_count, int newline, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self_obj(vm, base);
    const char *str;
    size_t str_len;
    int64_t handle;
    fs_writer_t *wr;
    size_t written;

    if (self == NULL || !get_string_arg(vm, base, 1, &str, &str_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_zero_and_err(vm, "write: invalid argument", FS_ERR_IO, error);
    }

    handle = get_handle_field(self, WF_HANDLE);
    wr = (fs_writer_t *)fs_registry_get(&g_writers, handle);
    if (wr == NULL || wr->closed)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_zero_and_err(vm, "write: writer is closed", FS_ERR_IO, error);
    }

    written = fwrite(str, 1, str_len, wr->fp);
    if (newline && written == str_len)
    {
        fwrite("\n", 1, 1, wr->fp);
        written++;
    }

    if (ferror(wr->fp))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_zero_and_err(vm, "write: I/O error", FS_ERR_IO, error);
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i32_and_ok(vm, (int64_t)written, error);
}

static vigil_status_t writer_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return writer_write_impl(vm, arg_count, 0, error);
}

static vigil_status_t writer_write_line(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return writer_write_impl(vm, arg_count, 1, error);
}

static vigil_status_t writer_flush(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self_obj(vm, base);
    int64_t handle;
    fs_writer_t *wr;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (self == NULL)
        return push_err_kind(vm, "flush: invalid writer", FS_ERR_IO, error);

    handle = get_handle_field(self, WF_HANDLE);
    wr = (fs_writer_t *)fs_registry_get(&g_writers, handle);
    if (wr == NULL || wr->closed)
        return push_err_kind(vm, "flush: writer is closed", FS_ERR_IO, error);

    if (fflush(wr->fp) != 0)
        return push_err_kind(vm, "flush: I/O error", FS_ERR_IO, error);

    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t writer_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self_obj(vm, base);
    int64_t handle;
    fs_writer_t *wr;

    vigil_vm_stack_pop_n(vm, arg_count);

    if (self == NULL)
        return push_err_kind(vm, "close: invalid writer", FS_ERR_IO, error);

    handle = get_handle_field(self, WF_HANDLE);
    wr = (fs_writer_t *)fs_registry_get(&g_writers, handle);
    if (wr == NULL)
        return push_err_kind(vm, "close: invalid writer handle", FS_ERR_IO, error);
    if (!wr->closed)
    {
        fflush(wr->fp);
        fclose(wr->fp);
        wr->fp = NULL;
        wr->closed = 1;
        fs_registry_set(&g_writers, handle, wr);
    }
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static const int obj_err_returns[] = {VIGIL_TYPE_OBJECT, VIGIL_TYPE_ERR};
static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};
static const int i32_err_returns[] = {VIGIL_TYPE_I32, VIGIL_TYPE_ERR};
static const int str1_param[] = {VIGIL_TYPE_STRING};
static const int i32_1_param[] = {VIGIL_TYPE_I32};
static const char *const fs_reader_open_param_names[] = {"path"};
static const char *const fs_reader_read_bytes_param_names[] = {"n"};
static const char *const fs_writer_open_param_names[] = {"path"};
static const char *const fs_writer_write_param_names[] = {"s"};

static const vigil_native_symbol_doc_t vigil_fs_reader_doc = {
    "Buffered file reader.",
    "Reads files incrementally or all at once through an opaque runtime-backed handle.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_reader_handle_doc = {
    "Internal reader handle.",
    "Opaque runtime handle for the open reader.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_reader_open_doc = {
    "Open a file for reading.",
    "Returns a Reader instance and an error value.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_reader_read_line_doc = {
    "Read the next line.",
    "Returns the next line without its trailing newline, or an EOF-style error when no data remains.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_reader_read_bytes_doc = {
    "Read up to N bytes.",
    "Returns up to the requested number of bytes from the stream.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_reader_read_all_doc = {
    "Read the rest of the file.",
    "Returns all remaining bytes from the reader.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_reader_close_doc = {
    "Close the reader.",
    "Closes the underlying file handle.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_writer_doc = {
    "Buffered file writer.",
    "Writes strings to a file through an opaque runtime-backed handle.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_writer_handle_doc = {
    "Internal writer handle.",
    "Opaque runtime handle for the open writer.",
    NULL,
};

/* ── Class descriptors ───────────────────────────────────────────── */

static const vigil_native_class_field_t reader_fields[] = {
    {"handle", 6U, VIGIL_TYPE_I64, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &vigil_fs_reader_handle_doc},
};

static const vigil_native_class_field_t writer_fields[] = {
    {"handle", 6U, VIGIL_TYPE_I64, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &vigil_fs_writer_handle_doc},
};

static const vigil_native_symbol_doc_t vigil_fs_writer_open_doc = {
    "Open a file for writing.",
    "Creates or truncates a file and returns a Writer instance with an error value.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_writer_open_append_doc = {
    "Open a file for appending.",
    "Creates the file if needed and appends future writes to the end.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_writer_write_doc = {
    "Write a string.",
    "Writes the supplied string and returns the number of bytes written.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_writer_write_line_doc = {
    "Write a string plus newline.",
    "Writes the supplied string followed by a newline and returns the number of bytes written.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_writer_flush_doc = {
    "Flush buffered output.",
    "Flushes any buffered bytes to the underlying file.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fs_writer_close_doc = {
    "Close the writer.",
    "Flushes and closes the underlying file handle.",
    NULL,
};

// clang-format off
#define FS_STATIC(n, nl, fn, pc, pt, rt, rc, rts) \
    {n, nl, fn, pc, pt, rt, rc, rts, 1, NULL, 0U, 0, NULL, NULL, NULL, NULL}
#define FS_METHOD(n, nl, fn, pc, pt, rt, rc, rts) \
    {n, nl, fn, pc, pt, rt, rc, rts, 0, NULL, 0U, 0, NULL, NULL, NULL, NULL}
// clang-format on

static const vigil_native_class_method_t reader_methods[] = {
    {"open", 4U, reader_open, 1U, str1_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 1, "Reader", 6U, 0,
     fs_reader_open_param_names, NULL, "fs.Reader", &vigil_fs_reader_open_doc},
    {"read_line", 9U, reader_read_line, 0U, NULL, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, 0U, 0, NULL, NULL,
     NULL, &vigil_fs_reader_read_line_doc},
    {"read_bytes", 10U, reader_read_bytes, 1U, i32_1_param, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, 0U, 0,
     fs_reader_read_bytes_param_names, NULL, NULL, &vigil_fs_reader_read_bytes_doc},
    {"read_all", 8U, reader_read_all, 0U, NULL, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, 0U, 0, NULL, NULL,
     NULL, &vigil_fs_reader_read_all_doc},
    {"close", 5U, reader_close, 0U, NULL, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_fs_reader_close_doc},
};

static const vigil_native_class_method_t writer_methods[] = {
    {"open", 4U, writer_open, 1U, str1_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 1, "Writer", 6U, 0,
     fs_writer_open_param_names, NULL, "fs.Writer", &vigil_fs_writer_open_doc},
    {"open_append", 11U, writer_open_append, 1U, str1_param, VIGIL_TYPE_OBJECT, 2U, obj_err_returns, 1, "Writer", 6U, 0,
     fs_writer_open_param_names, NULL, "fs.Writer", &vigil_fs_writer_open_append_doc},
    {"write", 5U, writer_write, 1U, str1_param, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, NULL, 0U, 0,
     fs_writer_write_param_names, NULL, NULL, &vigil_fs_writer_write_doc},
    {"write_line", 10U, writer_write_line, 1U, str1_param, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, NULL, 0U, 0,
     fs_writer_write_param_names, NULL, NULL, &vigil_fs_writer_write_line_doc},
    {"flush", 5U, writer_flush, 0U, NULL, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_fs_writer_flush_doc},
    {"close", 5U, writer_close, 0U, NULL, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &vigil_fs_writer_close_doc},
};

#undef FS_STATIC
#undef FS_METHOD

static const vigil_native_class_t vigil_fs_classes[] = {
    {"Reader", 6U, reader_fields, READER_FIELD_COUNT, reader_methods,
     sizeof(reader_methods) / sizeof(reader_methods[0]), NULL, &vigil_fs_reader_doc},
    {"Writer", 6U, writer_fields, WRITER_FIELD_COUNT, writer_methods,
     sizeof(writer_methods) / sizeof(writer_methods[0]), NULL, &vigil_fs_writer_doc},
};

#define FS_CLASS_COUNT (sizeof(vigil_fs_classes) / sizeof(vigil_fs_classes[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_fs = {
    "fs", 2U, vigil_fs_functions, FS_FUNCTION_COUNT, vigil_fs_classes, FS_CLASS_COUNT, &vigil_fs_module_doc};
