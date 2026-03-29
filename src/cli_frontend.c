#include <stdlib.h>
#include <string.h>

#include "internal/vigil_cli_frontend.h"
#include "platform/platform.h"
#include "vigil/lexer.h"
#include "vigil/pkg.h"
#include "vigil/stdlib.h"
#include "vigil/token.h"

#include "plugin_registry.h"

static void set_cli_frontend_error(vigil_error_t *error, vigil_status_t type, const char *message)
{
    if (error == NULL)
        return;
    vigil_error_clear(error);
    error->type = type;
    error->value = message;
    error->length = message == NULL ? 0U : strlen(message);
}

int cli_path_has_vigil_extension(const char *path, size_t length)
{
    return path != NULL && length >= 6U && memcmp(path + length - 6U, ".vigil", 6U) == 0;
}

int cli_path_is_absolute(const char *path, size_t length)
{
    if (path == NULL || length == 0U)
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
    return length >= 2U && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
}

static int registry_find_source_path(const vigil_source_registry_t *registry, const char *path,
                                     vigil_source_id_t *out_source_id)
{
    size_t index;

    if (out_source_id != NULL)
        *out_source_id = 0U;
    if (registry == NULL || path == NULL)
        return 0;
    for (index = 1U; index <= vigil_source_registry_count(registry); index += 1U)
    {
        const vigil_source_file_t *source;

        source = vigil_source_registry_get(registry, (vigil_source_id_t)index);
        if (source == NULL)
            continue;
        if (strcmp(vigil_string_c_str(&source->path), path) == 0)
        {
            if (out_source_id != NULL)
                *out_source_id = source->id;
            return 1;
        }
    }
    return 0;
}

const char *cli_source_token_text(const vigil_source_file_t *source, const vigil_token_t *token, size_t *out_length)
{
    size_t length;

    if (out_length != NULL)
        *out_length = 0U;
    if (source == NULL || token == NULL)
        return NULL;
    length = token->span.end_offset - token->span.start_offset;
    if (out_length != NULL)
        *out_length = length;
    return vigil_string_c_str(&source->text) + token->span.start_offset;
}

static size_t cli_path_dir_length(const char *path, size_t length)
{
    size_t prefix = length;
    while (prefix > 0U)
    {
        char current = path[prefix - 1U];
        if (current == '/' || current == '\\')
            break;
        prefix -= 1U;
    }
    return prefix;
}

vigil_status_t resolve_import_path(vigil_runtime_t *runtime, const char *base_path, const char *import_text,
                                   size_t import_length, vigil_string_t *out_path, vigil_error_t *error)
{
    size_t prefix_length;

    vigil_string_clear(out_path);
    if (runtime == NULL || base_path == NULL || import_text == NULL || out_path == NULL)
    {
        set_cli_frontend_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "import path inputs must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (cli_path_is_absolute(import_text, import_length))
        return vigil_string_assign(out_path, import_text, import_length, error);

    prefix_length = cli_path_dir_length(base_path, strlen(base_path));
    if (prefix_length != 0U)
    {
        if (vigil_string_assign(out_path, base_path, prefix_length, error) != VIGIL_STATUS_OK)
            return error->type;
        if (vigil_string_append(out_path, import_text, import_length, error) != VIGIL_STATUS_OK)
            return error->type;
    }
    else if (vigil_string_assign(out_path, import_text, import_length, error) != VIGIL_STATUS_OK)
    {
        return error->type;
    }
    if (!cli_path_has_vigil_extension(vigil_string_c_str(out_path), vigil_string_length(out_path)))
    {
        if (vigil_string_append_cstr(out_path, ".vigil", error) != VIGIL_STATUS_OK)
            return error->type;
    }
    (void)runtime;
    return VIGIL_STATUS_OK;
}

int find_project_root(const char *start_path, char *out_buf, size_t buf_size)
{
    char dir[4096];
    size_t len;

    if (start_path == NULL || buf_size == 0U)
        return 0;

    len = strlen(start_path);
    if (len >= sizeof(dir))
        return 0;
    memcpy(dir, start_path, len + 1U);
    while (len > 0U && dir[len - 1U] != '/' && dir[len - 1U] != '\\')
        len -= 1U;
    if (len > 0U)
        len -= 1U;
    if (len == 0U)
    {
        dir[0] = '.';
        len = 1U;
    }
    dir[len] = '\0';

    for (;;)
    {
        char candidate[4096];
        int exists;
        vigil_error_t error;

        exists = 0;
        memset(&error, 0, sizeof(error));
        if (vigil_platform_path_join(dir, "vigil.toml", candidate, sizeof(candidate), &error) != VIGIL_STATUS_OK)
            return 0;
        if (vigil_platform_file_exists(candidate, &exists) == VIGIL_STATUS_OK && exists)
        {
            if (len + 1U > buf_size)
                return 0;
            memcpy(out_buf, dir, len);
            out_buf[len] = '\0';
            return 1;
        }
        while (len > 0U && dir[len - 1U] != '/' && dir[len - 1U] != '\\')
            len -= 1U;
        if (len == 0U)
        {
            if (dir[0] != '.')
            {
                dir[0] = '.';
                dir[1] = '\0';
                len = 1U;
                continue;
            }
            return 0;
        }
        len -= 1U;
        dir[len] = '\0';
    }
}

static int read_imported_source_from_project(const char *project_root, const char *path, char **out_text,
                                             size_t *out_length, vigil_error_t *error)
{
    const char *base;
    const char *cursor_path;
    char lib_candidate[4096];
    vigil_error_t lib_error;

    if (project_root == NULL)
        return 0;

    base = path;
    memset(&lib_error, 0, sizeof(lib_error));
    for (cursor_path = path; *cursor_path != '\0'; cursor_path += 1)
    {
        if (*cursor_path == '/' || *cursor_path == '\\')
            base = cursor_path + 1;
    }

    {
        char lib_dir[4096];

        if (vigil_platform_path_join(project_root, "lib", lib_dir, sizeof(lib_dir), &lib_error) == VIGIL_STATUS_OK &&
            vigil_platform_path_join(lib_dir, base, lib_candidate, sizeof(lib_candidate), &lib_error) ==
                VIGIL_STATUS_OK)
        {
            vigil_error_clear(error);
            if (vigil_platform_read_file(NULL, lib_candidate, out_text, out_length, error) == VIGIL_STATUS_OK)
                return 1;
        }
    }

    {
        char deps_candidate[4096];

        vigil_error_clear(&lib_error);
        if (vigil_pkg_resolve_import(project_root, path, deps_candidate, sizeof(deps_candidate), &lib_error) ==
            VIGIL_STATUS_OK)
        {
            vigil_error_clear(error);
            if (vigil_platform_read_file(NULL, deps_candidate, out_text, out_length, error) == VIGIL_STATUS_OK)
                return 1;
        }
    }

    return 0;
}

static int read_source_for_registration(const char *path, const char *project_root, char **out_text, size_t *out_length,
                                        vigil_error_t *error)
{
    if (vigil_platform_read_file(NULL, path, out_text, out_length, error) == VIGIL_STATUS_OK)
        return 1;
    if (read_imported_source_from_project(project_root, path, out_text, out_length, error))
        return 1;

    set_cli_frontend_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "failed to read imported source");
    return 0;
}

typedef struct import_register_context
{
    vigil_source_registry_t *registry;
    const vigil_source_file_t **source;
    vigil_runtime_t *runtime;
    const char *project_root;
    vigil_error_t *error;
} import_register_context_t;

static int advance_brace_depth(const vigil_token_t *token, size_t *brace_depth, size_t *cursor)
{
    if (token == NULL)
        return 0;
    if (token->kind == VIGIL_TOKEN_LBRACE)
    {
        *brace_depth += 1U;
        *cursor += 1U;
        return 1;
    }
    if (token->kind == VIGIL_TOKEN_RBRACE)
    {
        if (*brace_depth != 0U)
            *brace_depth -= 1U;
        *cursor += 1U;
        return 1;
    }
    return 0;
}

static int register_single_import(import_register_context_t *context, const vigil_token_t *path_token)
{
    vigil_string_t import_path;
    const char *import_text;
    size_t import_length;

    import_text = cli_source_token_text(*context->source, path_token, &import_length);
    if (import_text == NULL || import_length < 2U)
        return 1;
    if (vigil_stdlib_is_known_module(import_text + 1U, import_length - 2U))
        return 1;
    if (vigil_plugin_is_known_module(import_text + 1U, import_length - 2U))
        return 1;

    vigil_string_init(&import_path, context->runtime);
    if (resolve_import_path(context->runtime, vigil_string_c_str(&(*context->source)->path), import_text + 1U,
                            import_length - 2U, &import_path, context->error) != VIGIL_STATUS_OK)
    {
        vigil_string_free(&import_path);
        return 0;
    }
    if (!register_source_tree(context->registry, vigil_string_c_str(&import_path), context->project_root, NULL,
                              context->error))
    {
        vigil_string_free(&import_path);
        return 0;
    }
    vigil_string_free(&import_path);
    *context->source = vigil_source_registry_get(context->registry, (*context->source)->id);
    return 1;
}

static int register_import_at_cursor(import_register_context_t *context, vigil_token_list_t *tokens, size_t *cursor,
                                     size_t brace_depth)
{
    const vigil_token_t *path_token;

    if (brace_depth != 0U)
        return 1;

    *cursor += 1U;
    path_token = vigil_token_list_get(tokens, *cursor);
    if (path_token == NULL ||
        (path_token->kind != VIGIL_TOKEN_STRING_LITERAL && path_token->kind != VIGIL_TOKEN_RAW_STRING_LITERAL))
    {
        return 1;
    }
    return register_single_import(context, path_token);
}

static int register_source_imports(vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                   const char *project_root, vigil_error_t *error)
{
    import_register_context_t context;
    vigil_runtime_t *runtime;
    const vigil_source_file_t *source;
    vigil_token_list_t tokens;
    vigil_diagnostic_list_t diagnostics;
    size_t cursor;
    size_t brace_depth;

    runtime = registry == NULL ? NULL : registry->runtime;
    source = vigil_source_registry_get(registry, source_id);
    context.registry = registry;
    context.source = &source;
    context.runtime = runtime;
    context.project_root = project_root;
    context.error = error;
    if (source == NULL)
    {
        set_cli_frontend_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "registered source was not found");
        return 0;
    }

    vigil_token_list_init(&tokens, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    if (vigil_lex_source(registry, source_id, &tokens, &diagnostics, error) != VIGIL_STATUS_OK)
    {
        vigil_error_clear(error);
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        return 1;
    }

    cursor = 0U;
    brace_depth = 0U;
    while (1)
    {
        const vigil_token_t *token;

        token = vigil_token_list_get(&tokens, cursor);
        if (token == NULL || token->kind == VIGIL_TOKEN_EOF)
            break;
        if (advance_brace_depth(token, &brace_depth, &cursor))
        {
            continue;
        }
        if (token->kind == VIGIL_TOKEN_IMPORT)
        {
            if (!register_import_at_cursor(&context, &tokens, &cursor, brace_depth))
            {
                vigil_token_list_free(&tokens);
                vigil_diagnostic_list_free(&diagnostics);
                return 0;
            }
        }
        cursor += 1U;
    }

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_error_clear(error);
    return 1;
}

int register_source_tree(vigil_source_registry_t *registry, const char *path, const char *project_root,
                         vigil_source_id_t *out_source_id, vigil_error_t *error)
{
    vigil_source_id_t source_id;
    char *file_text;
    size_t file_length;

    source_id = 0U;
    file_text = NULL;
    file_length = 0U;
    if (registry_find_source_path(registry, path, &source_id))
    {
        if (out_source_id != NULL)
            *out_source_id = source_id;
        vigil_error_clear(error);
        return 1;
    }
    if (!read_source_for_registration(path, project_root, &file_text, &file_length, error))
        return 0;
    if (vigil_source_registry_register(registry, path, strlen(path), file_text, file_length, &source_id, error) !=
        VIGIL_STATUS_OK)
    {
        free(file_text);
        return 0;
    }
    free(file_text);
    if (out_source_id != NULL)
        *out_source_id = source_id;
    return register_source_imports(registry, source_id, project_root, error);
}
