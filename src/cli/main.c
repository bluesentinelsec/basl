#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#define cli_strdup _strdup
#else
#define cli_strdup strdup
#endif

#include "internal/vigil_cli_frontend.h"
#include "platform/platform.h"
#include "vigil/cli_lib.h"
#include "vigil/dap.h"
#include "vigil/doc.h"
#include "vigil/doc_registry.h"
#include "vigil/embed.h"
#include "vigil/fmt.h"
#include "vigil/lsp.h"
#include "vigil/package.h"
#include "vigil/pkg.h"
#include "vigil/stdlib.h"
#include "vigil/toml.h"
#include "vigil/vigil.h"

/* ── Shared helpers ──────────────────────────────────────────────── */

static void log_cli_message(vigil_runtime_t *runtime, vigil_log_level_t level, const char *message,
                            const char *field_key, const char *field_value)
{
    vigil_log_field_t field;
    const vigil_logger_t *logger;

    logger = vigil_runtime_logger(runtime);
    if (field_key != NULL && field_value != NULL)
    {
        field.key = field_key;
        field.value = field_value;
        (void)vigil_logger_log(logger, level, message, &field, 1U, NULL);
        return;
    }
    (void)vigil_logger_log(logger, level, message, NULL, 0U, NULL);
}

static void set_cli_error(vigil_error_t *error, vigil_status_t type, const char *message)
{
    if (error == NULL)
        return;
    vigil_error_clear(error);
    error->type = type;
    error->value = message;
    error->length = message == NULL ? 0U : strlen(message);
}

static int print_diagnostics(const vigil_source_registry_t *registry, const vigil_diagnostic_list_t *diagnostics)
{
    size_t index;
    vigil_string_t line;
    vigil_runtime_t *runtime;
    vigil_error_t error;

    runtime = registry == NULL ? NULL : registry->runtime;
    vigil_string_init(&line, runtime);
    memset(&error, 0, sizeof(error));
    for (index = 0U; index < vigil_diagnostic_list_count(diagnostics); index += 1U)
    {
        const vigil_diagnostic_t *diagnostic;
        diagnostic = vigil_diagnostic_list_get(diagnostics, index);
        if (diagnostic == NULL)
            continue;
        if (vigil_diagnostic_format(registry, diagnostic, &line, &error) == VIGIL_STATUS_OK)
        {
            fprintf(stderr, "%s\n", vigil_string_c_str(&line));
        }
    }
    vigil_string_free(&line);
    return 1;
}

static void print_error(const vigil_source_registry_t *registry, const char *prefix, const vigil_error_t *error)
{
    vigil_source_location_t location;
    const vigil_source_file_t *source;

    if (error == NULL)
    {
        fprintf(stderr, "%s: unknown error\n", prefix);
        return;
    }
    if (registry != NULL && error->location.source_id != 0U)
    {
        location = error->location;
        if (vigil_source_registry_resolve_location(registry, &location, NULL) == VIGIL_STATUS_OK)
        {
            source = vigil_source_registry_get(registry, location.source_id);
            fprintf(stderr, "%s: %s:%u:%u: %s\n", prefix,
                    source == NULL ? "<unknown>" : vigil_string_c_str(&source->path), location.line, location.column,
                    vigil_error_message(error));
            return;
        }
    }
    fprintf(stderr, "%s: %s\n", prefix, vigil_error_message(error));
}

/* ── Source loading ──────────────────────────────────────────────── */

/* ── run command ─────────────────────────────────────────────────── */

static int cmd_run(const char *script_path, const char *const *script_argv, size_t script_argc)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_value_t result;
    vigil_source_id_t source_id = 0U;
    vigil_object_t *function = NULL;
    vigil_status_t status;
    int exit_code = 0;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize runtime: %s\n", vigil_error_message(&error));
        return 1;
    }
    if (vigil_vm_open(&vm, runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        log_cli_message(runtime, VIGIL_LOG_ERROR, "failed to initialize vm", "error", vigil_error_message(&error));
        vigil_runtime_close(&runtime);
        return 1;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_value_init_nil(&result);

    {
        char proj_root[4096];
        const char *root = find_project_root(script_path, proj_root, sizeof(proj_root)) ? proj_root : NULL;
        if (!register_source_tree(&registry, script_path, root, &source_id, &error))
        {
            log_cli_message(runtime, VIGIL_LOG_ERROR, "failed to register source", "error",
                            vigil_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
    }

    {
        vigil_native_registry_t natives;
        vigil_native_registry_init(&natives);
        vigil_stdlib_register_all(&natives, &error);
        status = vigil_compile_source_with_natives(&registry, source_id, &natives, &function, &diagnostics, &error);
        vigil_native_registry_free(&natives);
    }
    if (status != VIGIL_STATUS_OK)
    {
        if (vigil_diagnostic_list_count(&diagnostics) != 0U)
        {
            exit_code = print_diagnostics(&registry, &diagnostics);
        }
        else
        {
            print_error(&registry, "compile failed", &error);
            exit_code = 1;
        }
        vigil_object_release(&function);
        goto cleanup;
    }

    vigil_vm_set_args(vm, script_argv, script_argc);
    status = vigil_vm_execute_function(vm, function, &result, &error);
    vigil_object_release(&function);
    if (status != VIGIL_STATUS_OK)
    {
        print_error(&registry, "execution failed", &error);
        exit_code = 1;
        goto cleanup;
    }
    if (vigil_value_kind(&result) != VIGIL_VALUE_INT)
    {
        log_cli_message(runtime, VIGIL_LOG_ERROR, "compiled entrypoint did not return i32", NULL, NULL);
        exit_code = 1;
        goto cleanup;
    }
    exit_code = (int)vigil_value_as_int(&result);

cleanup:
    vigil_value_release(&result);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return exit_code;
}

/* ── check command ───────────────────────────────────────────────── */

static int cmd_check(const char *script_path)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id = 0U;
    vigil_status_t status;
    int exit_code = 0;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize runtime: %s\n", vigil_error_message(&error));
        return 1;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    {
        char proj_root[4096];
        const char *root = find_project_root(script_path, proj_root, sizeof(proj_root)) ? proj_root : NULL;
        if (!register_source_tree(&registry, script_path, root, &source_id, &error))
        {
            log_cli_message(runtime, VIGIL_LOG_ERROR, "failed to register source", "error",
                            vigil_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
    }

    {
        vigil_native_registry_t natives;
        vigil_native_registry_init(&natives);
        vigil_stdlib_register_all(&natives, &error);
        status = vigil_check_source(&registry, source_id, &natives, &diagnostics, &error);
        vigil_native_registry_free(&natives);
    }
    if (status != VIGIL_STATUS_OK)
    {
        if (vigil_diagnostic_list_count(&diagnostics) != 0U)
        {
            exit_code = print_diagnostics(&registry, &diagnostics);
        }
        else
        {
            print_error(&registry, "check failed", &error);
            exit_code = 1;
        }
    }

cleanup:
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
    return exit_code;
}

/* ── new command ─────────────────────────────────────────────────── */

static vigil_status_t write_text_file(const char *base, const char *name, const char *content, vigil_error_t *error)
{
    char path[4096];
    vigil_status_t s = vigil_platform_path_join(base, name, path, sizeof(path), error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_platform_write_file(path, content, strlen(content), error);
}

static vigil_status_t make_subdir(const char *base, const char *name, vigil_error_t *error)
{
    char path[4096];
    vigil_status_t s = vigil_platform_path_join(base, name, path, sizeof(path), error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_platform_mkdir(path, error);
}

static const char *new_resolve_project_name(const char *name, char *project_name, size_t project_name_size,
                                            vigil_error_t *error)
{
    if (name != NULL && name[0] != '\0')
    {
        return name;
    }
    if (vigil_platform_readline("Project name: ", project_name, project_name_size, error) != VIGIL_STATUS_OK)
    {
        return NULL;
    }
    if (project_name[0] == '\0')
    {
        set_cli_error(error, VIGIL_STATUS_INVALID_ARGUMENT, "project name cannot be empty");
        return NULL;
    }
    return project_name;
}

static const char *new_resolve_project_dir(const char *name, const char *output_dir, char *project_path,
                                           size_t project_path_size)
{
    if (output_dir != NULL && output_dir[0] != '\0')
    {
        snprintf(project_path, project_path_size, "%s/%s", output_dir, name);
        return project_path;
    }
    return name;
}

static int new_create_project_tree(const char *dir, vigil_error_t *error)
{
    if (vigil_platform_mkdir_p(dir, error) != VIGIL_STATUS_OK)
        return 0;
    if (make_subdir(dir, "lib", error) != VIGIL_STATUS_OK)
        return 0;
    if (make_subdir(dir, "test", error) != VIGIL_STATUS_OK)
        return 0;
    return 1;
}

static int new_write_manifest(const char *dir, const char *name, vigil_error_t *error)
{
    vigil_toml_value_t *root = NULL;
    vigil_toml_value_t *str_val = NULL;
    char *toml_str = NULL;
    size_t toml_len = 0;
    int success = 0;

    if (vigil_toml_table_new(NULL, &root, error) != VIGIL_STATUS_OK)
        goto cleanup;
    if (vigil_toml_string_new(NULL, name, strlen(name), &str_val, error) != VIGIL_STATUS_OK)
        goto cleanup;
    if (vigil_toml_table_set(root, "name", 4, str_val, error) != VIGIL_STATUS_OK)
        goto cleanup;
    str_val = NULL;
    if (vigil_toml_string_new(NULL, "0.1.0", 5, &str_val, error) != VIGIL_STATUS_OK)
        goto cleanup;
    if (vigil_toml_table_set(root, "version", 7, str_val, error) != VIGIL_STATUS_OK)
        goto cleanup;
    str_val = NULL;
    if (vigil_toml_emit(root, &toml_str, &toml_len, error) != VIGIL_STATUS_OK)
        goto cleanup;
    if (write_text_file(dir, "vigil.toml", toml_str, error) != VIGIL_STATUS_OK)
        goto cleanup;
    success = 1;

cleanup:
    free(toml_str);
    vigil_toml_free(&str_val);
    vigil_toml_free(&root);
    return success;
}

static int new_write_lib_scaffold(const char *dir, const char *name, vigil_error_t *error)
{
    char lib_file[512];
    char test_file[512];
    char lib_content[1024];
    char test_content[1024];

    snprintf(lib_file, sizeof(lib_file), "lib/%s.vigil", name);
    snprintf(test_file, sizeof(test_file), "test/%s_test.vigil", name);

    snprintf(lib_content, sizeof(lib_content),
             "/// %s library module.\n"
             "\n"
             "pub fn hello() -> string {\n"
             "    return \"hello from %s\";\n"
             "}\n",
             name, name);

    snprintf(test_content, sizeof(test_content),
             "import \"test\";\n"
             "import \"%s\";\n"
             "\n"
             "fn test_hello(test.T t) -> void {\n"
             "    t.assert(%s.hello() == \"hello from %s\", \"hello should match\");\n"
             "}\n",
             name, name, name);

    return write_text_file(dir, lib_file, lib_content, error) == VIGIL_STATUS_OK &&
           write_text_file(dir, test_file, test_content, error) == VIGIL_STATUS_OK;
}

static int new_write_app_scaffold(const char *dir, const char *name, int scaffold, vigil_error_t *error)
{
    if (scaffold)
    {
        char lib_file[512];
        char test_file[512];
        char lib_content[1024];
        char test_content[1024];
        char main_content[1024];

        snprintf(lib_file, sizeof(lib_file), "lib/%s.vigil", name);
        snprintf(test_file, sizeof(test_file), "test/%s_test.vigil", name);

        snprintf(lib_content, sizeof(lib_content),
                 "/// %s module.\n"
                 "\n"
                 "pub fn greet(string name) -> string {\n"
                 "    return \"hello, \" + name;\n"
                 "}\n",
                 name);

        snprintf(test_content, sizeof(test_content),
                 "import \"test\";\n"
                 "import \"%s\";\n"
                 "\n"
                 "fn test_greet(test.T t) -> void {\n"
                 "    t.assert(%s.greet(\"world\") == \"hello, world\", \"greet should work\");\n"
                 "}\n",
                 name, name);

        snprintf(main_content, sizeof(main_content),
                 "import \"fmt\";\n"
                 "import \"%s\";\n"
                 "\n"
                 "fn main() -> i32 {\n"
                 "    fmt.println(%s.greet(\"world\"));\n"
                 "    return 0;\n"
                 "}\n",
                 name, name);

        return write_text_file(dir, lib_file, lib_content, error) == VIGIL_STATUS_OK &&
               write_text_file(dir, test_file, test_content, error) == VIGIL_STATUS_OK &&
               write_text_file(dir, "main.vigil", main_content, error) == VIGIL_STATUS_OK;
    }

    return write_text_file(dir, "main.vigil",
                           "import \"fmt\";\n"
                           "\n"
                           "fn main() -> i32 {\n"
                           "    fmt.println(\"hello, world!\");\n"
                           "    return 0;\n"
                           "}\n",
                           error) == VIGIL_STATUS_OK;
}

static void new_print_summary(const char *dir, const char *name, int is_lib, int scaffold)
{
    printf("created %s\n", dir);
    printf("  vigil.toml\n");
    if (is_lib)
    {
        printf("  lib/%s.vigil\n", name);
        printf("  test/%s_test.vigil\n", name);
    }
    else if (scaffold)
    {
        printf("  main.vigil\n");
        printf("  lib/%s.vigil\n", name);
        printf("  test/%s_test.vigil\n", name);
    }
    else
    {
        printf("  main.vigil\n");
    }
    printf("  lib/\n");
    printf("  test/\n");
    printf("  .gitignore\n");
}

/* Maximum project name length. Scaffold templates embed the name into fixed
   buffers, so we enforce a safe upper bound here rather than silently truncating. */
#define NEW_MAX_PROJECT_NAME 100

static int cmd_new(const char *name, int is_lib, int scaffold, const char *output_dir)
{
    vigil_error_t error = {0};
    int exists = 0;
    char project_name[256];
    char project_path[512];
    const char *dir;

    name = new_resolve_project_name(name, project_name, sizeof(project_name), &error);
    if (name == NULL)
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        return 1;
    }
    if (strlen(name) > NEW_MAX_PROJECT_NAME)
    {
        fprintf(stderr, "error: project name too long (max %d characters)\n", NEW_MAX_PROJECT_NAME);
        return 1;
    }
    dir = new_resolve_project_dir(name, output_dir, project_path, sizeof(project_path));

    /* Check if directory already exists. */
    if (vigil_platform_file_exists(dir, &exists) == VIGIL_STATUS_OK && exists)
    {
        fprintf(stderr, "error: '%s' already exists\n", dir);
        return 1;
    }

    if (!new_create_project_tree(dir, &error) || !new_write_manifest(dir, name, &error) ||
        write_text_file(dir, ".gitignore", "deps/\n", &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        return 1;
    }
    if (is_lib ? !new_write_lib_scaffold(dir, name, &error) : !new_write_app_scaffold(dir, name, scaffold, &error))
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        return 1;
    }

    new_print_summary(dir, name, is_lib, scaffold);
    return 0;
}

/* ── debug command ────────────────────────────────────────────────── */

static int cmd_debug(const char *script_path)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id = 0U;
    vigil_object_t *function = NULL;
    vigil_status_t status;
    vigil_dap_server_t *dap = NULL;
    int exit_code = 0;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize runtime: %s\n", vigil_error_message(&error));
        return 1;
    }
    if (vigil_vm_open(&vm, runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize vm: %s\n", vigil_error_message(&error));
        vigil_runtime_close(&runtime);
        return 1;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    {
        char proj_root[4096];
        const char *root = find_project_root(script_path, proj_root, sizeof(proj_root)) ? proj_root : NULL;
        if (!register_source_tree(&registry, script_path, root, &source_id, &error))
        {
            fprintf(stderr, "failed to register source: %s\n", vigil_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
    }

    /* Compile. */
    {
        vigil_native_registry_t natives;
        vigil_native_registry_init(&natives);
        vigil_stdlib_register_all(&natives, &error);
        status = vigil_compile_source_with_natives(&registry, source_id, &natives, &function, &diagnostics, &error);
        vigil_native_registry_free(&natives);
    }
    if (status != VIGIL_STATUS_OK)
    {
        if (vigil_diagnostic_list_count(&diagnostics) != 0U)
        {
            print_diagnostics(&registry, &diagnostics);
        }
        else
        {
            print_error(&registry, "compile failed", &error);
        }
        exit_code = 1;
        goto cleanup;
    }

    /* Create DAP server on stdio. */
    if (vigil_dap_server_create(&dap, stdin, stdout, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to create DAP server: %s\n", vigil_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    if (vigil_dap_server_set_runtime(dap, vm, &registry, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to attach DAP server: %s\n", vigil_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    vigil_dap_server_set_program(dap, function, source_id);

    /* Run the DAP message loop (blocks until disconnect). */
    status = vigil_dap_server_run(dap, &error);
    if (status != VIGIL_STATUS_OK && status != VIGIL_STATUS_INTERNAL)
    {
        fprintf(stderr, "DAP server error: %s\n", vigil_error_message(&error));
        exit_code = 1;
    }

cleanup:
    vigil_dap_server_destroy(&dap);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return exit_code;
}

/* ── interactive debug command ────────────────────────────────────── */

typedef struct
{
    vigil_debugger_t *debugger;
    const vigil_source_registry_t *sources;
    vigil_line_history_t *history;
    int quit_requested;
} debug_cli_state_t;

static void debug_cli_print_location(debug_cli_state_t *state)
{
    vigil_source_id_t source_id;
    uint32_t line, column;
    const vigil_source_file_t *source;

    if (vigil_debugger_current_location(state->debugger, &source_id, &line, &column) != VIGIL_STATUS_OK)
    {
        printf("  (unknown location)\n");
        return;
    }

    source = vigil_source_registry_get(state->sources, source_id);
    if (source)
    {
        printf("  %s:%u:%u\n", vigil_string_c_str(&source->path), line, column);
        /* Print the source line */
        const char *text = vigil_string_c_str(&source->text);
        const char *line_start = text;
        uint32_t current_line = 1;
        while (*line_start && current_line < line)
        {
            if (*line_start == '\n')
                current_line++;
            line_start++;
        }
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n')
            line_end++;
        printf("  %u | %.*s\n", line, (int)(line_end - line_start), line_start);
    }
}

static void debug_cli_print_backtrace(debug_cli_state_t *state)
{
    size_t frame_count = vigil_debugger_frame_count(state->debugger);
    for (size_t i = 0; i < frame_count; i++)
    {
        const char *name;
        size_t name_len;
        vigil_source_id_t source_id;
        uint32_t line, column;
        const vigil_source_file_t *source;

        if (vigil_debugger_frame_info(state->debugger, i, &name, &name_len, &source_id, &line, &column) !=
            VIGIL_STATUS_OK)
        {
            printf("  #%zu (unknown)\n", i);
            continue;
        }

        source = vigil_source_registry_get(state->sources, source_id);
        const char *path = source ? vigil_string_c_str(&source->path) : "?";
        printf("  #%zu %.*s at %s:%u\n", i, (int)name_len, name, path, line);
    }
}

static void debug_cli_print_value(const vigil_value_t *val)
{
    vigil_value_kind_t kind = vigil_value_kind(val);
    switch (kind)
    {
    case VIGIL_VALUE_NIL:
        printf("nil\n");
        break;
    case VIGIL_VALUE_BOOL:
        printf("%s\n", vigil_value_as_bool(val) ? "true" : "false");
        break;
    case VIGIL_VALUE_INT:
        printf("%lld\n", (long long)vigil_value_as_int(val));
        break;
    case VIGIL_VALUE_UINT:
        printf("%llu\n", (unsigned long long)vigil_value_as_uint(val));
        break;
    case VIGIL_VALUE_FLOAT:
        printf("%g\n", vigil_value_as_float(val));
        break;
    case VIGIL_VALUE_OBJECT: {
        vigil_object_t *obj = vigil_value_as_object(val);
        if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            printf("\"%s\"\n", vigil_string_object_c_str(obj));
        }
        else if (obj && vigil_object_type(obj) == VIGIL_OBJECT_ARRAY)
        {
            printf("<array[%zu]>\n", vigil_array_object_length(obj));
        }
        else
        {
            printf("<object>\n");
        }
        break;
    }
    default:
        printf("<unknown>\n");
        break;
    }
}

static void debug_cli_print_locals(debug_cli_state_t *state, size_t frame_idx)
{
    const char *names[32];
    size_t name_lens[32];
    vigil_value_t values[32];
    size_t count = vigil_debugger_frame_locals(state->debugger, frame_idx, names, name_lens, values, 32);
    if (count == 0)
    {
        printf("  (no locals)\n");
        return;
    }
    for (size_t i = 0; i < count; i++)
    {
        printf("  %.*s = ", (int)name_lens[i], names[i]);
        vigil_value_kind_t kind = vigil_value_kind(&values[i]);
        switch (kind)
        {
        case VIGIL_VALUE_NIL:
            printf("nil\n");
            break;
        case VIGIL_VALUE_BOOL:
            printf("%s\n", vigil_value_as_bool(&values[i]) ? "true" : "false");
            break;
        case VIGIL_VALUE_INT:
            printf("%lld\n", (long long)vigil_value_as_int(&values[i]));
            break;
        case VIGIL_VALUE_UINT:
            printf("%llu\n", (unsigned long long)vigil_value_as_uint(&values[i]));
            break;
        case VIGIL_VALUE_FLOAT:
            printf("%g\n", vigil_value_as_float(&values[i]));
            break;
        case VIGIL_VALUE_OBJECT: {
            vigil_object_t *obj = vigil_value_as_object(&values[i]);
            if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
            {
                printf("\"%s\"\n", vigil_string_object_c_str(obj));
            }
            else
            {
                printf("<object>\n");
            }
            break;
        }
        default:
            printf("<unknown>\n");
            break;
        }
        vigil_value_release(&values[i]);
    }
}

static const char *skip_to_line(const char *text, int target_line)
{
    const char *p = text;
    int cur = 1;
    while (*p && cur < target_line)
    {
        if (*p == '\n')
            cur++;
        p++;
    }
    return p;
}

static void debug_cli_print_source_range(const char *p, int start, int end, uint32_t current_line)
{
    int cur = start;
    while (*p && cur <= end)
    {
        const char *eol = p;
        while (*eol && *eol != '\n')
            eol++;
        char marker = (cur == (int)current_line) ? '>' : ' ';
        printf("%c%4d | %.*s\n", marker, cur, (int)(eol - p), p);
        if (*eol)
            eol++;
        p = eol;
        cur++;
    }
}

static void debug_cli_list_source(debug_cli_state_t *state, int around_line)
{
    vigil_source_id_t source_id;
    uint32_t line, column;
    const vigil_source_file_t *source;

    if (vigil_debugger_current_location(state->debugger, &source_id, &line, &column) != VIGIL_STATUS_OK)
        return;
    source = vigil_source_registry_get(state->sources, source_id);
    if (!source)
        return;

    const char *text = vigil_string_c_str(&source->text);
    int center = (around_line > 0) ? around_line : (int)line;
    int start = center - 5;
    if (start < 1)
        start = 1;

    debug_cli_print_source_range(skip_to_line(text, start), start, center + 5, line);
}

static void debug_cli_help(void)
{
    printf("Debugger commands:\n");
    printf("  c, continue       Resume execution\n");
    printf("  s, step           Step into\n");
    printf("  n, next           Step over\n");
    printf("  o, out            Step out\n");
    printf("  b <line>          Set breakpoint at line\n");
    printf("  b <function>      Set breakpoint on function\n");
    printf("  d <id>            Delete breakpoint\n");
    printf("  bt, backtrace     Show call stack\n");
    printf("  l, locals         Show local variables\n");
    printf("  p <var>           Print variable value\n");
    printf("  list [line]       Show source around line\n");
    printf("  w, where          Show current location\n");
    printf("  q, quit           Stop debugging\n");
    printf("  h, help           Show this help\n");
}

typedef enum
{
    DEBUG_CLI_COMMAND_UNHANDLED = 0,
    DEBUG_CLI_COMMAND_HANDLED = 1,
    DEBUG_CLI_COMMAND_RETURN = 2
} debug_cli_command_result_t;

static void debug_cli_print_stop_reason(vigil_debug_stop_reason_t reason)
{
    switch (reason)
    {
    case VIGIL_DEBUG_STOP_BREAKPOINT:
        printf("Breakpoint hit:\n");
        break;
    case VIGIL_DEBUG_STOP_STEP:
        printf("Stepped:\n");
        break;
    case VIGIL_DEBUG_STOP_ENTRY:
        printf("Program entry:\n");
        break;
    }
}

static const char *debug_cli_skip_whitespace(const char *text)
{
    while (*text == ' ' || *text == '\t')
        text++;
    return text;
}

static int debug_cli_command_matches(const char *command, const char *short_name, const char *long_name)
{
    return strcmp(command, short_name) == 0 || strcmp(command, long_name) == 0;
}

static int debug_cli_is_list_command(const char *command)
{
    return strncmp(command, "list", 4) == 0 && (command[4] == '\0' || command[4] == ' ');
}

static debug_cli_command_result_t debug_cli_handle_resume_command(debug_cli_state_t *state, const char *command,
                                                                  vigil_debug_action_t *out_action)
{
    if (debug_cli_command_matches(command, "c", "continue"))
    {
        vigil_debugger_continue(state->debugger);
        *out_action = VIGIL_DEBUG_CONTINUE;
        return DEBUG_CLI_COMMAND_RETURN;
    }
    if (debug_cli_command_matches(command, "s", "step"))
    {
        vigil_debugger_step_into(state->debugger);
        *out_action = VIGIL_DEBUG_CONTINUE;
        return DEBUG_CLI_COMMAND_RETURN;
    }
    if (debug_cli_command_matches(command, "n", "next"))
    {
        vigil_debugger_step_over(state->debugger);
        *out_action = VIGIL_DEBUG_CONTINUE;
        return DEBUG_CLI_COMMAND_RETURN;
    }
    if (debug_cli_command_matches(command, "o", "out"))
    {
        vigil_debugger_step_out(state->debugger);
        *out_action = VIGIL_DEBUG_CONTINUE;
        return DEBUG_CLI_COMMAND_RETURN;
    }
    if (debug_cli_command_matches(command, "q", "quit"))
    {
        state->quit_requested = 1;
        *out_action = VIGIL_DEBUG_CONTINUE;
        return DEBUG_CLI_COMMAND_RETURN;
    }
    return DEBUG_CLI_COMMAND_UNHANDLED;
}

static debug_cli_command_result_t debug_cli_handle_info_command(debug_cli_state_t *state, const char *command)
{
    if (debug_cli_command_matches(command, "bt", "backtrace"))
    {
        debug_cli_print_backtrace(state);
        return DEBUG_CLI_COMMAND_HANDLED;
    }
    if (debug_cli_command_matches(command, "l", "locals"))
    {
        debug_cli_print_locals(state, 0);
        return DEBUG_CLI_COMMAND_HANDLED;
    }
    if (debug_cli_is_list_command(command))
    {
        int around;

        around = 0;
        if (command[4] == ' ')
            around = atoi(command + 5);
        debug_cli_list_source(state, around);
        return DEBUG_CLI_COMMAND_HANDLED;
    }
    if (debug_cli_command_matches(command, "w", "where"))
    {
        debug_cli_print_location(state);
        return DEBUG_CLI_COMMAND_HANDLED;
    }
    if (debug_cli_command_matches(command, "h", "help"))
    {
        debug_cli_help();
        return DEBUG_CLI_COMMAND_HANDLED;
    }
    return DEBUG_CLI_COMMAND_UNHANDLED;
}

static void debug_cli_set_line_breakpoint(debug_cli_state_t *state, const char *arg, vigil_error_t *error)
{
    size_t bp_id;
    uint32_t bp_line;
    vigil_source_id_t source_id;
    uint32_t line;
    uint32_t column;

    bp_line = (uint32_t)atoi(arg);
    if (vigil_debugger_current_location(state->debugger, &source_id, &line, &column) != VIGIL_STATUS_OK)
        return;
    if (vigil_debugger_set_breakpoint(state->debugger, source_id, bp_line, &bp_id, error) == VIGIL_STATUS_OK)
    {
        printf("Breakpoint %zu set at line %u\n", bp_id, bp_line);
        return;
    }
    printf("Failed to set breakpoint\n");
}

static void debug_cli_set_function_breakpoint(debug_cli_state_t *state, const char *arg, vigil_error_t *error)
{
    size_t bp_id;

    if (vigil_debugger_set_breakpoint_function(state->debugger, arg, &bp_id, error) == VIGIL_STATUS_OK)
    {
        printf("Breakpoint %zu set on function '%s'\n", bp_id, arg);
        return;
    }
    printf("Function '%s' not found\n", arg);
}

static debug_cli_command_result_t debug_cli_handle_breakpoint_command(debug_cli_state_t *state, const char *command,
                                                                      vigil_error_t *error)
{
    const char *arg;

    if (command[0] != 'b' || (command[1] != ' ' && command[1] != '\t'))
        return DEBUG_CLI_COMMAND_UNHANDLED;

    arg = debug_cli_skip_whitespace(command + 2);
    if (*arg == '\0')
    {
        printf("Usage: b <line> or b <function>\n");
        return DEBUG_CLI_COMMAND_HANDLED;
    }
    if (*arg >= '0' && *arg <= '9')
    {
        debug_cli_set_line_breakpoint(state, arg, error);
        return DEBUG_CLI_COMMAND_HANDLED;
    }

    debug_cli_set_function_breakpoint(state, arg, error);
    return DEBUG_CLI_COMMAND_HANDLED;
}

static debug_cli_command_result_t debug_cli_handle_print_command(debug_cli_state_t *state, const char *command)
{
    const char *var_name;

    if (command[0] != 'p' || (command[1] != ' ' && command[1] != '\t'))
        return DEBUG_CLI_COMMAND_UNHANDLED;

    var_name = debug_cli_skip_whitespace(command + 2);
    if (*var_name == '\0')
    {
        printf("Usage: p <variable>\n");
        return DEBUG_CLI_COMMAND_HANDLED;
    }

    {
        vigil_value_t value;

        if (vigil_debugger_get_local(state->debugger, 0, var_name, &value) == VIGIL_STATUS_OK)
        {
            debug_cli_print_value(&value);
            vigil_value_release(&value);
        }
        else
        {
            printf("Variable '%s' not found in current scope\n", var_name);
        }
    }
    return DEBUG_CLI_COMMAND_HANDLED;
}

static debug_cli_command_result_t debug_cli_handle_delete_command(debug_cli_state_t *state, const char *command)
{
    size_t breakpoint_id;

    if (command[0] != 'd' || (command[1] != ' ' && command[1] != '\t'))
        return DEBUG_CLI_COMMAND_UNHANDLED;

    breakpoint_id = (size_t)atoi(command + 2);
    if (vigil_debugger_clear_breakpoint(state->debugger, breakpoint_id) == VIGIL_STATUS_OK)
    {
        printf("Breakpoint %zu deleted\n", breakpoint_id);
    }
    else
    {
        printf("No such breakpoint\n");
    }
    return DEBUG_CLI_COMMAND_HANDLED;
}

static vigil_debug_action_t debug_cli_callback(vigil_debugger_t *debugger, vigil_debug_stop_reason_t reason,
                                               void *userdata)
{
    debug_cli_state_t *state = (debug_cli_state_t *)userdata;
    char line[256];
    vigil_error_t error = {0};
    vigil_debug_action_t action;
    const char *command;

    (void)debugger;
    action = VIGIL_DEBUG_CONTINUE;

    debug_cli_print_stop_reason(reason);
    debug_cli_print_location(state);

    for (;;)
    {
        if (vigil_line_editor_readline("(debug) ", line, sizeof(line), state->history, &error) != VIGIL_STATUS_OK)
        {
            state->quit_requested = 1;
            return VIGIL_DEBUG_CONTINUE;
        }

        command = debug_cli_skip_whitespace(line);
        if (*command == '\0')
            continue;

        vigil_line_history_add(state->history, line);
        if (debug_cli_handle_resume_command(state, command, &action) == DEBUG_CLI_COMMAND_RETURN)
            return action;
        if (debug_cli_handle_info_command(state, command) == DEBUG_CLI_COMMAND_HANDLED)
            continue;
        if (debug_cli_handle_breakpoint_command(state, command, &error) == DEBUG_CLI_COMMAND_HANDLED)
            continue;
        if (debug_cli_handle_print_command(state, command) == DEBUG_CLI_COMMAND_HANDLED)
            continue;
        if (debug_cli_handle_delete_command(state, command) == DEBUG_CLI_COMMAND_HANDLED)
            continue;

        printf("Unknown command: %s (type 'help' for commands)\n", command);
    }
}

/* ── LSP Server ───────────────────────────────────────────── */

static int cmd_lsp(void)
{
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};
    vigil_status_t status;

    status = vigil_lsp_server_create(&server, stdin, stdout, NULL, &error);
    if (status != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to create LSP server: %s\n", vigil_error_message(&error));
        return 1;
    }

    status = vigil_lsp_server_run(server, &error);
    vigil_lsp_server_destroy(&server);

    if (status != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "LSP server error: %s\n", vigil_error_message(&error));
        return 1;
    }

    return 0;
}

static int debug_register_sources(vigil_source_registry_t *registry, const char *script_path,
                                  vigil_source_id_t *out_source_id, vigil_error_t *error)
{
    char proj_root[4096];
    const char *root = find_project_root(script_path, proj_root, sizeof(proj_root)) ? proj_root : NULL;

    if (register_source_tree(registry, script_path, root, out_source_id, error))
    {
        return 1;
    }

    fprintf(stderr, "failed to register source: %s\n", vigil_error_message(error));
    return 0;
}

static vigil_status_t debug_compile_source(vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                           vigil_object_t **out_function, vigil_diagnostic_list_t *diagnostics,
                                           vigil_debug_symbol_table_t *symbols, vigil_error_t *error)
{
    vigil_native_registry_t natives;
    vigil_status_t status;

    vigil_native_registry_init(&natives);
    vigil_stdlib_register_all(&natives, error);
    status =
        vigil_compile_source_with_debug_info(registry, source_id, &natives, out_function, diagnostics, symbols, error);
    vigil_native_registry_free(&natives);
    return status;
}

static int cmd_debug_interactive(const char *script_path)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_debugger_t *debugger = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_debug_symbol_table_t symbols;
    vigil_source_id_t source_id = 0U;
    vigil_object_t *function = NULL;
    vigil_status_t status;
    vigil_line_history_t history;
    debug_cli_state_t cli_state = {0};
    int exit_code = 0;
    int symbols_initialized = 0;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize runtime: %s\n", vigil_error_message(&error));
        return 1;
    }
    if (vigil_vm_open(&vm, runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize vm: %s\n", vigil_error_message(&error));
        vigil_runtime_close(&runtime);
        return 1;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_debug_symbol_table_init(&symbols, runtime);
    symbols_initialized = 1;
    vigil_line_history_init(&history, 100);

    if (!debug_register_sources(&registry, script_path, &source_id, &error))
    {
        exit_code = 1;
        goto cleanup;
    }

    status = debug_compile_source(&registry, source_id, &function, &diagnostics, &symbols, &error);
    if (status != VIGIL_STATUS_OK)
    {
        if (vigil_diagnostic_list_count(&diagnostics) != 0U)
        {
            print_diagnostics(&registry, &diagnostics);
        }
        else
        {
            print_error(&registry, "compile failed", &error);
        }
        exit_code = 1;
        goto cleanup;
    }

    /* Create debugger. */
    if (vigil_debugger_create(&debugger, vm, &registry, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to create debugger: %s\n", vigil_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Attach symbol table for function breakpoints. */
    vigil_debugger_set_symbols(debugger, &symbols);

    /* Set up CLI state. */
    cli_state.debugger = debugger;
    cli_state.sources = &registry;
    cli_state.history = &history;
    cli_state.quit_requested = 0;

    vigil_debugger_set_callback(debugger, debug_cli_callback, &cli_state);
    vigil_debugger_attach(debugger);

    /* Start paused at entry. */
    vigil_debugger_pause(debugger);

    printf("VIGIL Interactive Debugger\n");
    printf("Type 'help' for commands.\n\n");

    /* Execute. */
    {
        vigil_value_t result;
        vigil_value_init_nil(&result);
        status = vigil_vm_execute_function(vm, function, &result, &error);
        if (!cli_state.quit_requested)
        {
            if (status == VIGIL_STATUS_OK)
            {
                printf("\nProgram finished normally.\n");
            }
            else
            {
                printf("\nProgram error: %s\n", vigil_error_message(&error));
            }
        }
        vigil_value_release(&result);
    }

cleanup:
    vigil_debugger_destroy(&debugger);
    vigil_line_history_free(&history);
    vigil_object_release(&function);
    if (symbols_initialized)
        vigil_debug_symbol_table_free(&symbols);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return exit_code;
}

/* ── doc command ──────────────────────────────────────────────────── */

/* Show list of all documented modules */
static int cmd_doc_list_modules(void)
{
    size_t count, i;
    const char **modules = vigil_doc_list_modules(&count);

    printf("Available modules:\n\n");
    for (i = 0; i < count; i++)
    {
        const vigil_doc_entry_t *entry = vigil_doc_lookup(modules[i]);
        if (entry != NULL && entry->summary != NULL)
        {
            printf("  %-12s %s\n", modules[i], entry->summary);
        }
        else
        {
            printf("  %s\n", modules[i]);
        }
    }
    printf("\nUse 'vigil doc <module>' for module details.\n");
    printf("Use 'vigil doc <file.vigil>' for user code documentation.\n");
    return 0;
}

/* Show documentation for a builtin/stdlib symbol or module */
static int cmd_doc_builtin(const char *name)
{
    const vigil_doc_entry_t *entry;
    char *output = NULL;
    size_t output_len = 0;
    vigil_error_t error = {0};

    /* Try direct lookup first */
    entry = vigil_doc_lookup(name);
    if (entry != NULL)
    {
        if (vigil_doc_entry_render(entry, &output, &output_len, &error) == VIGIL_STATUS_OK)
        {
            fwrite(output, 1, output_len, stdout);
            free(output);
        }

        /* If it's a module, also list its contents */
        if (entry->signature == NULL)
        {
            size_t count, i;
            const vigil_doc_entry_t *entries = vigil_doc_list_module(name, &count);
            if (entries != NULL && count > 1)
            {
                printf("\nSymbols:\n");
                for (i = 1; i < count; i++)
                { /* Skip module entry itself */
                    printf("  %-20s %s\n", entries[i].name, entries[i].summary);
                }
            }
        }
        return 0;
    }

    return -1; /* Not found in registry */
}

static int cmd_doc(const char *file_path, const char *symbol)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_source_id_t source_id = 0U;
    vigil_token_list_t tokens;
    vigil_diagnostic_list_t diagnostics;
    const vigil_source_file_t *source;
    vigil_doc_module_t doc_module;
    char *output = NULL;
    size_t output_len = 0;
    int exit_code = 0;

    /* No arguments: list all modules */
    if (file_path == NULL)
    {
        return cmd_doc_list_modules();
    }

    /* Check if it's a builtin/stdlib name first */
    if (cmd_doc_builtin(file_path) == 0)
    {
        return 0;
    }

    /* If no file extension and not found in registry, error */
    if (strchr(file_path, '/') == NULL &&
        (strlen(file_path) < 6 || strcmp(file_path + strlen(file_path) - 6, ".vigil") != 0))
    {
        fprintf(stderr, "error: '%s' not found. Use 'vigil doc' to list available modules.\n", file_path);
        return 1;
    }

    /* File-based documentation */
    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize runtime: %s\n", vigil_error_message(&error));
        return 1;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_token_list_init(&tokens, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    /* Read and register source. */
    {
        char *file_text = NULL;
        size_t file_length = 0;
        if (vigil_platform_read_file(NULL, file_path, &file_text, &file_length, &error) != VIGIL_STATUS_OK)
        {
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
        if (vigil_source_registry_register(&registry, file_path, strlen(file_path), file_text, file_length, &source_id,
                                           &error) != VIGIL_STATUS_OK)
        {
            free(file_text);
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
        free(file_text);
    }

    source = vigil_source_registry_get(&registry, source_id);
    if (source == NULL)
    {
        fprintf(stderr, "error: failed to retrieve source\n");
        exit_code = 1;
        goto cleanup;
    }

    /* Lex. */
    if (vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Extract. */
    if (vigil_doc_extract(NULL, file_path, strlen(file_path), vigil_string_c_str(&source->text),
                          vigil_string_length(&source->text), &tokens, &doc_module, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Render. */
    if (vigil_doc_render(&doc_module, symbol, &output, &output_len, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error[doc]: %s\n", vigil_error_message(&error));
        vigil_doc_module_free(&doc_module);
        exit_code = 1;
        goto cleanup;
    }

    if (output != NULL)
    {
        fwrite(output, 1, output_len, stdout);
        if (output_len > 0 && output[output_len - 1] != '\n')
            fputc('\n', stdout);
        free(output);
    }

    vigil_doc_module_free(&doc_module);

cleanup:
    vigil_diagnostic_list_free(&diagnostics);
    vigil_token_list_free(&tokens);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
    return exit_code;
}

/* ── packaged binary runner ───────────────────────────────────────── */

/* Register all bundled files as sources and identify the entry source. */
static void packaged_register_sources(vigil_source_registry_t *registry, const vigil_package_bundle_t *bundle,
                                      vigil_source_id_t *out_source_id, vigil_error_t *error)
{
    for (size_t i = 0; i < bundle->file_count; i++)
    {
        vigil_source_id_t sid = 0;
        vigil_source_registry_register(registry, bundle->paths[i], strlen(bundle->paths[i]), bundle->contents[i],
                                       bundle->content_lengths[i], &sid, error);
        if (strcmp(bundle->paths[i], "entry.vigil") == 0)
            *out_source_id = sid;
    }
}

/* Compile and execute a packaged program. Returns exit code. */
static int packaged_compile_and_run(vigil_runtime_t *runtime, vigil_vm_t *vm, vigil_source_registry_t *registry,
                                    vigil_source_id_t source_id, int argc, char **argv)
{
    vigil_error_t error = {0};
    vigil_diagnostic_list_t diagnostics;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_status_t status;
    int exit_code = 0;

    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_value_init_nil(&result);

    {
        vigil_native_registry_t natives;
        vigil_native_registry_init(&natives);
        vigil_stdlib_register_all(&natives, &error);
        status = vigil_compile_source_with_natives(registry, source_id, &natives, &function, &diagnostics, &error);
        vigil_native_registry_free(&natives);
    }
    if (status != VIGIL_STATUS_OK)
    {
        if (vigil_diagnostic_list_count(&diagnostics) != 0U)
            print_diagnostics(registry, &diagnostics);
        else
            print_error(registry, "compile failed", &error);
        exit_code = 1;
    }
    else
    {
        if (argc > 1)
            vigil_vm_set_args(vm, (const char *const *)&argv[1], (size_t)(argc - 1));
        else
            vigil_vm_set_args(vm, NULL, 0);

        status = vigil_vm_execute_function(vm, function, &result, &error);
        if (status != VIGIL_STATUS_OK)
        {
            print_error(registry, "execution failed", &error);
            exit_code = 1;
        }
        else if (vigil_value_kind(&result) == VIGIL_VALUE_INT)
        {
            exit_code = (int)vigil_value_as_int(&result);
        }
    }

    vigil_object_release(&function);
    vigil_value_release(&result);
    vigil_diagnostic_list_free(&diagnostics);
    return exit_code;
}

static int try_run_packaged(int argc, char **argv)
{
    vigil_package_bundle_t bundle;
    vigil_error_t error = {0};
    vigil_status_t status;
    const char *entry_src = NULL;

    status = vigil_package_read_self(&bundle, &error);
    if (status != VIGIL_STATUS_OK)
    {
        if (error.value != NULL && (strcmp(vigil_error_message(&error), "not a packaged binary") == 0 ||
                                    strcmp(vigil_error_message(&error), "no bundle trailer") == 0))
            return -1;
        fprintf(stderr, "error[package]: %s\n", vigil_error_message(&error));
        return 1;
    }

    /* Find entry.vigil. */
    for (size_t i = 0; i < bundle.file_count; i++)
    {
        if (strcmp(bundle.paths[i], "entry.vigil") == 0)
        {
            entry_src = bundle.contents[i];
            break;
        }
    }
    if (entry_src == NULL)
    {
        fprintf(stderr, "error[package]: entry.vigil not found in bundle\n");
        vigil_package_bundle_free(&bundle);
        return 1;
    }

    {
        vigil_runtime_t *runtime = NULL;
        vigil_vm_t *vm = NULL;
        vigil_source_registry_t registry;
        vigil_source_id_t source_id = 0;
        int exit_code;

        if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
        {
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            vigil_package_bundle_free(&bundle);
            return 1;
        }
        if (vigil_vm_open(&vm, runtime, NULL, &error) != VIGIL_STATUS_OK)
        {
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            vigil_runtime_close(&runtime);
            vigil_package_bundle_free(&bundle);
            return 1;
        }

        vigil_source_registry_init(&registry, runtime);
        packaged_register_sources(&registry, &bundle, &source_id, &error);
        exit_code = packaged_compile_and_run(runtime, vm, &registry, source_id, argc, argv);

        vigil_source_registry_free(&registry);
        vigil_vm_close(&vm);
        vigil_runtime_close(&runtime);
        vigil_package_bundle_free(&bundle);
        return exit_code;
    }
}

/* ── fmt command ─────────────────────────────────────────────────── */

static int fmt_one_file(const char *file_path, int check_only)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_source_id_t source_id = 0U;
    vigil_token_list_t tokens;
    vigil_diagnostic_list_t diagnostics;
    const vigil_source_file_t *source;
    char *formatted = NULL;
    size_t formatted_len = 0;
    int exit_code = 0;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize runtime: %s\n", vigil_error_message(&error));
        return 1;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_token_list_init(&tokens, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    /* Read and register source. */
    {
        char *file_text = NULL;
        size_t file_length = 0;
        if (vigil_platform_read_file(NULL, file_path, &file_text, &file_length, &error) != VIGIL_STATUS_OK)
        {
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
        if (vigil_source_registry_register(&registry, file_path, strlen(file_path), file_text, file_length, &source_id,
                                           &error) != VIGIL_STATUS_OK)
        {
            free(file_text);
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
        free(file_text);
    }

    source = vigil_source_registry_get(&registry, source_id);
    if (source == NULL)
    {
        fprintf(stderr, "error: failed to retrieve source\n");
        exit_code = 1;
        goto cleanup;
    }

    /* Lex. */
    if (vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error[fmt]: %s: %s\n", file_path, vigil_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Format. */
    if (vigil_fmt(vigil_string_c_str(&source->text), vigil_string_length(&source->text), &tokens, &formatted,
                  &formatted_len, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error[fmt]: %s: %s\n", file_path, vigil_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Compare. */
    {
        const char *orig = vigil_string_c_str(&source->text);
        size_t orig_len = vigil_string_length(&source->text);
        if (formatted_len == orig_len && memcmp(orig, formatted, orig_len) == 0)
        {
            /* Already formatted. */
            goto cleanup;
        }
    }

    if (check_only)
    {
        /* In check mode, print the filename and return failure. */
        fprintf(stderr, "%s\n", file_path);
        exit_code = 1;
    }
    else
    {
        /* Write back. */
        if (vigil_platform_write_file(file_path, formatted, formatted_len, &error) != VIGIL_STATUS_OK)
        {
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            exit_code = 1;
        }
    }

cleanup:
    free(formatted);
    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
    return exit_code;
}

static int cmd_fmt(const char *file_path, int check_only)
{
    return fmt_one_file(file_path, check_only);
}

/* ── shared directory listing helpers ─────────────────────────────── */

typedef struct
{
    char name[256];
    int is_dir;
} dir_entry_t;
typedef struct
{
    dir_entry_t *items;
    size_t count;
    size_t cap;
} dir_list_t;

static vigil_status_t dir_list_cb(const char *name, int is_dir, void *ud)
{
    dir_list_t *dl = ud;
    if (dl->count == dl->cap)
    {
        dl->cap = dl->cap ? dl->cap * 2 : 32;
        dl->items = realloc(dl->items, dl->cap * sizeof(dir_entry_t));
    }
    snprintf(dl->items[dl->count].name, sizeof(dl->items[0].name), "%s", name);
    dl->items[dl->count].is_dir = is_dir;
    dl->count++;
    return VIGIL_STATUS_OK;
}

/* ── embed command ───────────────────────────────────────────────── */

typedef struct
{
    char **paths;
    char **rels;
    size_t count;
    size_t cap;
} file_list_t;

static void fl_add(file_list_t *fl, const char *path, const char *rel)
{
    if (fl->count == fl->cap)
    {
        fl->cap = fl->cap ? fl->cap * 2 : 16;
        fl->paths = realloc(fl->paths, fl->cap * sizeof(char *));
        fl->rels = realloc(fl->rels, fl->cap * sizeof(char *));
    }
    fl->paths[fl->count] = cli_strdup(path);
    fl->rels[fl->count] = cli_strdup(rel);
    fl->count++;
}

static void fl_free(file_list_t *fl)
{
    for (size_t i = 0; i < fl->count; i++)
    {
        free(fl->paths[i]);
        free(fl->rels[i]);
    }
    free(fl->paths);
    free(fl->rels);
}

static void collect_dir(file_list_t *fl, const char *dir, const char *rel_prefix)
{
    vigil_error_t err = {0};
    dir_list_t dl = {NULL, 0, 0};
    if (vigil_platform_list_dir(dir, dir_list_cb, &dl, &err) != VIGIL_STATUS_OK)
    {
        free(dl.items);
        return;
    }
    for (size_t i = 0; i < dl.count; i++)
    {
        char full[4096], rel[4096];
        vigil_platform_path_join(dir, dl.items[i].name, full, sizeof(full), &err);
        if (rel_prefix[0])
            snprintf(rel, sizeof(rel), "%s/%s", rel_prefix, dl.items[i].name);
        else
            snprintf(rel, sizeof(rel), "%s", dl.items[i].name);
        if (dl.items[i].is_dir)
            collect_dir(fl, full, rel);
        else
            fl_add(fl, full, rel);
    }
    free(dl.items);
}

/* Parse embed command arguments into targets and output path. */
static void cmd_embed_parse_args(int argc, char **argv, const char **out_output, const char ***out_targets,
                                 size_t *out_count)
{
    *out_output = NULL;
    *out_targets = NULL;
    *out_count = 0;
    for (int i = 2; i < argc; i++)
    {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc)
            *out_output = argv[++i];
        else
        {
            *out_targets = realloc(*out_targets, (*out_count + 1) * sizeof(char *));
            (*out_targets)[*out_count] = argv[i];
            (*out_count)++;
        }
    }
}

/* Collect files from targets, expanding directories. */
static void cmd_embed_collect_files(file_list_t *fl, const char **targets, size_t target_count)
{
    for (size_t i = 0; i < target_count; i++)
    {
        int is_dir = 0;
        vigil_platform_is_directory(targets[i], &is_dir);
        if (is_dir)
        {
            collect_dir(fl, targets[i], "");
        }
        else
        {
            const char *base = targets[i];
            for (const char *p = targets[i]; *p; p++)
                if (*p == '/' || *p == '\\')
                    base = p + 1;
            fl_add(fl, targets[i], base);
        }
    }
}

/* Determine the output path for embed. */
static void cmd_embed_resolve_output(const char *output, const file_list_t *fl, size_t target_count, char *out_path,
                                     size_t out_path_size)
{
    if (output)
    {
        snprintf(out_path, out_path_size, "%s", output);
    }
    else if (fl->count == 1 && target_count == 1)
    {
        const char *base = fl->rels[0];
        const char *dot = strrchr(base, '.');
        size_t nlen = dot ? (size_t)(dot - base) : strlen(base);
        snprintf(out_path, out_path_size, "%.*s.vigil", (int)nlen, base);
    }
    else
    {
        snprintf(out_path, out_path_size, "assets.vigil");
    }
}

static int cmd_embed(int argc, char **argv)
{
    const char *output = NULL;
    const char **targets = NULL;
    size_t target_count = 0;
    vigil_error_t error = {0};

    cmd_embed_parse_args(argc, argv, &output, &targets, &target_count);
    if (target_count == 0)
    {
        fprintf(stderr, "usage: vigil embed <file|dir...> [-o output.vigil]\n");
        free(targets);
        return 2;
    }

    file_list_t fl = {NULL, NULL, 0, 0};
    cmd_embed_collect_files(&fl, targets, target_count);

    if (fl.count == 0)
    {
        fprintf(stderr, "error: no files found\n");
        fl_free(&fl);
        free(targets);
        return 1;
    }

    char *text = NULL;
    size_t text_len = 0;
    vigil_status_t status;

    if (fl.count == 1 && target_count == 1)
    {
        int is_dir = 0;
        vigil_platform_is_directory(targets[0], &is_dir);
        if (!is_dir)
            status = vigil_embed_single(fl.paths[0], &text, &text_len, &error);
        else
            status =
                vigil_embed_multi((const char **)fl.paths, (const char **)fl.rels, fl.count, &text, &text_len, &error);
    }
    else
    {
        status = vigil_embed_multi((const char **)fl.paths, (const char **)fl.rels, fl.count, &text, &text_len, &error);
    }

    if (status != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        fl_free(&fl);
        free(targets);
        return 1;
    }

    char out_path[4096];
    cmd_embed_resolve_output(output, &fl, target_count, out_path, sizeof(out_path));

    if (vigil_platform_write_file(out_path, text, text_len, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        free(text);
        fl_free(&fl);
        free(targets);
        return 1;
    }

    printf("embedded %zu file(s) -> %s\n", fl.count, out_path);
    free(text);
    fl_free(&fl);
    free(targets);
    return 0;
}

/* ── repl command ────────────────────────────────────────────────── */

/* Simple growable string buffer for the REPL preamble. */
typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} repl_buf_t;

static void repl_buf_init(repl_buf_t *buf)
{
    buf->data = calloc(1, 1); /* empty string so data is never NULL */
    buf->length = 0;
    buf->capacity = 1;
}

static void repl_buf_free(repl_buf_t *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

static int repl_buf_append(repl_buf_t *buf, const char *text, size_t len)
{
    if (buf->length + len + 1U > buf->capacity)
    {
        size_t new_cap = buf->capacity == 0 ? 256 : buf->capacity;
        while (new_cap < buf->length + len + 1U)
            new_cap *= 2;
        char *new_data = realloc(buf->data, new_cap);
        if (!new_data)
            return 0;
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->length, text, len);
    buf->length += len;
    buf->data[buf->length] = '\0';
    return 1;
}

static int repl_buf_append_cstr(repl_buf_t *buf, const char *text)
{
    return repl_buf_append(buf, text, strlen(text));
}

static void repl_buf_clear(repl_buf_t *buf)
{
    buf->length = 0;
    if (buf->data)
        buf->data[0] = '\0';
}

/* Named preamble entry for redefinition support. */
typedef struct
{
    char *name;   /* declaration name (NULL for imports) */
    char *source; /* full source text including trailing newline */
} repl_decl_t;

typedef struct
{
    repl_decl_t *entries;
    size_t count;
    size_t capacity;
} repl_decl_list_t;

static void repl_decl_list_init(repl_decl_list_t *list)
{
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void repl_decl_list_free(repl_decl_list_t *list)
{
    for (size_t i = 0; i < list->count; i++)
    {
        free(list->entries[i].name);
        free(list->entries[i].source);
    }
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* Extract the declaration name from input. Recognizes:
   fn <name>, class <name>, enum <name>, interface <name>,
   const <type> <name>, <type> <name> = ... (global var) */
/* Allocate a copy of [start, end). Returns NULL if empty or allocation fails. */
static char *repl_dup_span(const char *start, const char *end)
{
    if (end <= start)
        return NULL;
    char *name = malloc((size_t)(end - start) + 1);
    if (name)
    {
        memcpy(name, start, (size_t)(end - start));
        name[end - start] = '\0';
    }
    return name;
}

/* Check if character terminates a keyword name token. */
static int repl_is_name_terminator(char c)
{
    return c == '\0' || strchr("( {\t<", c) != NULL;
}

/* Try to extract the name token after a keyword like "fn ", "class ", etc. */
static char *repl_extract_keyword_name(const char *p)
{
    const char *keywords[] = {"fn ", "class ", "enum ", "interface ", NULL};
    for (int i = 0; keywords[i]; i++)
    {
        size_t klen = strlen(keywords[i]);
        if (strncmp(p, keywords[i], klen) == 0)
        {
            const char *start = p + klen;
            while (*start == ' ' || *start == '\t')
                start++;
            const char *end = start;
            while (!repl_is_name_terminator(*end))
                end++;
            return repl_dup_span(start, end);
        }
    }
    return NULL;
}

/* Skip whitespace, returning pointer to first non-whitespace char. */
static const char *repl_skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* Skip a non-whitespace token, returning pointer past it. */
static const char *repl_skip_token(const char *p)
{
    while (*p && *p != ' ' && *p != '\t')
        p++;
    return p;
}

/* Check if character terminates a variable name. */
static int repl_is_var_terminator(char c)
{
    return c == '\0' || strchr(" \t=;\n", c) != NULL;
}

/* Try to extract a variable name from "const <type> <name>" or "<type> <name> = ...". */
static char *repl_extract_variable_name(const char *p)
{
    if (strncmp(p, "const ", 6) == 0)
        p += 6;
    p = repl_skip_ws(p);
    const char *type_start = p;
    p = repl_skip_token(p);
    if (p == type_start)
        return NULL;
    p = repl_skip_ws(p);
    const char *nstart = p;
    while (!repl_is_var_terminator(*p))
        p++;
    return repl_dup_span(nstart, p);
}

static char *repl_extract_decl_name(const char *input)
{
    const char *p = input;
    while (*p == ' ' || *p == '\t')
        p++;
    char *name = repl_extract_keyword_name(p);
    if (name)
        return name;
    return repl_extract_variable_name(p);
}

/* Add or replace a declaration in the list. */
static void repl_decl_list_put(repl_decl_list_t *list, const char *name, const char *source)
{
    /* If name is non-NULL, look for existing entry to replace. */
    if (name)
    {
        for (size_t i = 0; i < list->count; i++)
        {
            if (list->entries[i].name && strcmp(list->entries[i].name, name) == 0)
            {
                free(list->entries[i].source);
                list->entries[i].source = cli_strdup(source);
                return;
            }
        }
    }
    if (list->count >= list->capacity)
    {
        size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        repl_decl_t *new_entries = realloc(list->entries, new_cap * sizeof(repl_decl_t));
        if (!new_entries)
            return;
        list->entries = new_entries;
        list->capacity = new_cap;
    }
    list->entries[list->count].name = name ? cli_strdup(name) : NULL;
    list->entries[list->count].source = cli_strdup(source);
    list->count++;
}

/* Rebuild the preamble buffer from the declaration list. */
static void repl_rebuild_preamble(repl_buf_t *preamble, const repl_decl_list_t *list)
{
    repl_buf_clear(preamble);
    for (size_t i = 0; i < list->count; i++)
    {
        repl_buf_append_cstr(preamble, list->entries[i].source);
    }
}

/* Print a runtime error with source location when available. */
static void repl_print_error(const vigil_error_t *err)
{
    if (err->location.line > 0)
    {
        fprintf(stderr, "error: <repl>:%u:%u: %s\n", err->location.line, err->location.column,
                vigil_error_message(err));
    }
    else
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(err));
    }
}

/* Check if text has an unterminated string literal (odd number of unescaped quotes). */
static int repl_has_unterminated_string(const char *text)
{
    int quotes = 0;
    for (const char *p = text; *p; p++)
    {
        if (*p == '\\' && p[1])
        {
            p++;
            continue;
        }
        if (*p == '"')
            quotes++;
    }
    return quotes % 2 != 0;
}

/* Find the last non-whitespace character in text. Returns pointer past it, or text if empty. */
static const char *repl_find_last_nonws(const char *text)
{
    const char *p = text + strlen(text);
    while (p > text && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r'))
        p--;
    return p;
}

/* Check if a single trailing character is a continuation operator. */
static int repl_is_trailing_operator(char c)
{
    return strchr(",+*/%^<>|&=", c) != NULL;
}

/* Check if the last two characters form a two-char continuation operator. */
static const char *repl_two_char_ops[] = {"->", "&&", "||", "==", "!=", "<=", ">=", NULL};

static int repl_is_trailing_two_char_op(char a, char b)
{
    char pair[3] = {a, b, '\0'};
    for (const char **op = repl_two_char_ops; *op; op++)
    {
        if (pair[0] == (*op)[0] && pair[1] == (*op)[1])
            return 1;
    }
    return 0;
}

/* Check if input needs continuation (trailing operator, comma, arrow, or unterminated string). */
static int repl_needs_continuation(const char *text)
{
    if (repl_has_unterminated_string(text))
        return 1;

    const char *p = repl_find_last_nonws(text);
    if (p == text)
        return 0;

    char last = p[-1];

    /* Two-char trailing tokens. */
    if (p - text >= 2 && repl_is_trailing_two_char_op(p[-2], p[-1]))
        return 1;

    /* Trailing - but not -- */
    if (last == '-' && (p - 1 == text || p[-2] != '-'))
        return 1;

    return repl_is_trailing_operator(last);
}

/* Count net open brackets in text. */
static const char *skip_string_literal(const char *text, char quote)
{
    for (text++; *text; text++)
    {
        if (*text == '\\')
        {
            if (text[1])
                text++;
            continue;
        }
        if (*text == quote)
            return text;
    }
    return text;
}

static int repl_bracket_depth(const char *text)
{
    int depth = 0;
    for (; *text; text++)
    {
        if (*text == '"' || *text == '\'')
        {
            text = skip_string_literal(text, *text);
            if (!*text)
                break;
            continue;
        }
        if (*text == '{' || *text == '(' || *text == '[')
            depth++;
        else if (*text == '}' || *text == ')' || *text == ']')
            depth--;
    }
    return depth;
}

/* Try to compile source in REPL mode.  If out_function is non-NULL after
   a successful call, the source contained executable statements.  If NULL,
   it contained only declarations.  Returns 1 on success. */
static int repl_compile_and_run(vigil_runtime_t *runtime, const char *source_text, const char *project_root,
                                vigil_object_t **out_function, int *out_has_statements, int print_errors)
{
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id = 0;
    vigil_status_t status;
    int ok = 0;

    if (out_function)
        *out_function = NULL;

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);

    if (vigil_source_registry_register_cstr(&registry, "<repl>", source_text, &source_id, &error) != VIGIL_STATUS_OK)
    {
        if (print_errors)
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        goto done;
    }

    /* Register non-native imports. */
    {
        const vigil_source_file_t *source = vigil_source_registry_get(&registry, source_id);
        vigil_token_list_t tokens;
        size_t cursor = 0, brace_depth = 0;

        vigil_token_list_init(&tokens, runtime);
        if (vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error) == VIGIL_STATUS_OK)
        {
            while (1)
            {
                const vigil_token_t *token = vigil_token_list_get(&tokens, cursor);
                if (!token || token->kind == VIGIL_TOKEN_EOF)
                    break;
                if (token->kind == VIGIL_TOKEN_LBRACE)
                {
                    brace_depth++;
                    cursor++;
                    continue;
                }
                if (token->kind == VIGIL_TOKEN_RBRACE)
                {
                    if (brace_depth)
                        brace_depth--;
                    cursor++;
                    continue;
                }
                if (brace_depth == 0 && token->kind == VIGIL_TOKEN_IMPORT)
                {
                    const vigil_token_t *path_token;
                    const char *import_text;
                    size_t import_length;
                    cursor++;
                    path_token = vigil_token_list_get(&tokens, cursor);
                    if (!path_token || (path_token->kind != VIGIL_TOKEN_STRING_LITERAL &&
                                        path_token->kind != VIGIL_TOKEN_RAW_STRING_LITERAL))
                        break;
                    import_text = cli_source_token_text(source, path_token, &import_length);
                    if (!import_text || import_length < 2)
                        break;
                    if (!vigil_stdlib_is_known_module(import_text + 1, import_length - 2))
                    {
                        vigil_string_t import_path;
                        vigil_string_init(&import_path, runtime);
                        if (resolve_import_path(runtime, "<repl>", import_text + 1, import_length - 2, &import_path,
                                                &error) == VIGIL_STATUS_OK)
                        {
                            register_source_tree(&registry, vigil_string_c_str(&import_path), project_root, NULL,
                                                 &error);
                            source = vigil_source_registry_get(&registry, source_id);
                        }
                        vigil_string_free(&import_path);
                    }
                }
                cursor++;
            }
        }
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        vigil_diagnostic_list_init(&diagnostics, runtime);
    }

    {
        vigil_native_registry_t natives;
        vigil_object_t *function = NULL;
        vigil_native_registry_init(&natives);
        vigil_stdlib_register_all(&natives, &error);
        status = vigil_compile_source_repl(&registry, source_id, &natives, &function, out_has_statements, &diagnostics,
                                           &error);
        vigil_native_registry_free(&natives);
        if (status != VIGIL_STATUS_OK)
        {
            if (print_errors)
            {
                if (vigil_diagnostic_list_count(&diagnostics))
                    print_diagnostics(&registry, &diagnostics);
                else
                    fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            }
            vigil_object_release(&function);
            goto done;
        }
        if (out_function)
            *out_function = function;
        else
            vigil_object_release(&function);
    }
    ok = 1;

done:
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    return ok;
}

/* Read a complete (possibly multi-line) input from the REPL.
   Returns 1 on success, 0 on EOF/error. */
static int repl_read_input(repl_buf_t *input, vigil_line_history_t *history, vigil_error_t *error)
{
    char line[4096];
    vigil_status_t rs;

    repl_buf_clear(input);
    rs = vigil_line_editor_readline(">>> ", line, sizeof(line), history, error);
    if (rs != VIGIL_STATUS_OK)
        return 0;

    /* Skip empty lines. */
    {
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            return -1; /* empty, caller should continue */
    }

    repl_buf_append_cstr(input, line);

    if (line[0] != ':' && strcmp(line, "exit()") != 0)
        vigil_line_history_add(history, line);

    /* Multi-line: accumulate while brackets are unbalanced or line needs continuation. */
    while (repl_bracket_depth(input->data) > 0 || repl_needs_continuation(input->data))
    {
        rs = vigil_line_editor_readline("... ", line, sizeof(line), history, error);
        if (rs != VIGIL_STATUS_OK)
            return 0;
        repl_buf_append_cstr(input, "\n");
        repl_buf_append_cstr(input, line);
    }
    return 1;
}

/* Update the declaration list and preamble after a successful declaration-only compile. */
static void repl_update_declarations(repl_decl_list_t *decls, const char *input_data, int needs_semi)
{
    repl_buf_t decl_src;
    char *dname;
    repl_buf_init(&decl_src);
    repl_buf_append_cstr(&decl_src, input_data);
    if (needs_semi)
        repl_buf_append_cstr(&decl_src, ";");
    repl_buf_append_cstr(&decl_src, "\n");
    dname = repl_extract_decl_name(input_data);
    repl_decl_list_put(decls, dname, decl_src.data);
    free(dname);
    repl_buf_free(&decl_src);
}

/* Handle :doc command. */
static void repl_handle_doc(const char *input_data, vigil_error_t *error)
{
    const char *arg;
    if (strlen(input_data) < 4)
        return;
    arg = input_data + 4;
    while (*arg == ' ')
        arg++;
    if (*arg == '\0')
    {
        size_t count, i;
        const char **modules = vigil_doc_list_modules(&count);
        printf("Available: ");
        for (i = 0; i < count; i++)
            printf("%s%s", modules[i], i < count - 1 ? ", " : "\n");
        return;
    }
    const vigil_doc_entry_t *entry = vigil_doc_lookup(arg);
    if (entry != NULL)
    {
        char *text = NULL;
        size_t len = 0;
        if (vigil_doc_entry_render(entry, &text, &len, error) == VIGIL_STATUS_OK)
        {
            printf("%s", text);
            free(text);
        }
    }
    else
    {
        printf("Not found: %s\n", arg);
    }
}

/* Handle special REPL commands. Returns: 1=handled (continue loop), -1=quit, 0=not a command. */
static int repl_handle_special_command(const char *input_data, repl_decl_list_t *decls, repl_buf_t *preamble,
                                       vigil_error_t *error)
{
    if (!input_data)
        return 0;
    if (strcmp(input_data, ":quit") == 0 || strcmp(input_data, ":q") == 0 || strcmp(input_data, "exit()") == 0)
        return -1;
    if (strcmp(input_data, ":help") == 0 || strcmp(input_data, ":h") == 0)
    {
        printf("  :help    Show this message\n");
        printf("  :quit    Exit the REPL (also Ctrl-D or exit())\n");
        printf("  :clear   Reset all state\n");
        printf("  :doc     Show documentation (e.g. :doc len, :doc math)\n");
        printf("  __ans    Last expression result (string)\n");
        return 1;
    }
    if (strncmp(input_data, ":doc", 4) == 0)
    {
        repl_handle_doc(input_data, error);
        return 1;
    }
    if (strcmp(input_data, ":clear") == 0)
    {
        repl_decl_list_free(decls);
        repl_decl_list_init(decls);
        repl_decl_list_put(decls, NULL, "import \"fmt\";\n");
        repl_rebuild_preamble(preamble, decls);
        printf("State cleared.\n");
        return 1;
    }
    return 0;
}

/* Check if input needs a trailing semicolon. */
static int repl_input_needs_semi(const repl_buf_t *input)
{
    const char *tail = input->data + input->length;
    while (tail > input->data && (tail[-1] == ' ' || tail[-1] == '\t' || tail[-1] == '\n'))
        tail--;
    return tail > input->data && tail[-1] != ';' && tail[-1] != '}';
}

/* Bundled REPL session state to reduce parameter counts. */
typedef struct
{
    vigil_runtime_t *runtime;
    vigil_vm_t *vm;
    repl_buf_t *input;
    repl_buf_t *preamble;
    repl_decl_list_t *decls;
    const char *project_root;
    vigil_error_t *error;
} repl_state_t;

/* Try to evaluate input as a printable expression. Returns 1 if successful. */
static int repl_try_expression(repl_state_t *s)
{
    repl_buf_t src;
    vigil_object_t *function = NULL;
    int handled = 0;

    repl_buf_init(&src);
    repl_buf_append_cstr(&src, s->preamble->data ? s->preamble->data : "");
    repl_buf_append_cstr(&src, "fmt.println(string(");
    repl_buf_append_cstr(&src, s->input->data);
    repl_buf_append_cstr(&src, "));\n");

    if (repl_compile_and_run(s->runtime, src.data, s->project_root, &function, NULL, 0) && function)
    {
        vigil_value_t result;
        vigil_value_init_nil(&result);
        if (vigil_vm_execute_function(s->vm, function, &result, s->error) != VIGIL_STATUS_OK)
        {
            repl_print_error(s->error);
        }
        else
        {
            repl_buf_t ans;
            repl_buf_init(&ans);
            repl_buf_append_cstr(&ans, "string __ans = string(");
            repl_buf_append_cstr(&ans, s->input->data);
            repl_buf_append_cstr(&ans, ");\n");
            repl_decl_list_put(s->decls, "__ans", ans.data);
            repl_buf_free(&ans);
        }
        vigil_value_release(&result);
        vigil_object_release(&function);
        handled = 1;
    }
    else
    {
        vigil_object_release(&function);
    }
    repl_buf_free(&src);
    return handled;
}

/* Try to evaluate input as bare code (declarations, statements, or mix). */
static void repl_try_bare_code(repl_state_t *s, int needs_semi)
{
    repl_buf_t src;
    vigil_object_t *function = NULL;
    int has_stmts = 0;

    repl_buf_init(&src);
    repl_buf_append_cstr(&src, s->preamble->data ? s->preamble->data : "");
    repl_buf_append_cstr(&src, s->input->data);
    if (needs_semi)
        repl_buf_append_cstr(&src, ";");
    repl_buf_append_cstr(&src, "\n");

    if (repl_compile_and_run(s->runtime, src.data, s->project_root, &function, &has_stmts, 1))
    {
        if (function)
        {
            vigil_value_t result;
            vigil_value_init_nil(&result);
            if (vigil_vm_execute_function(s->vm, function, &result, s->error) != VIGIL_STATUS_OK)
                repl_print_error(s->error);
            vigil_value_release(&result);
            vigil_object_release(&function);
        }
        if (!has_stmts)
            repl_update_declarations(s->decls, s->input->data, needs_semi);
    }
    else
    {
        vigil_object_release(&function);
    }
    repl_buf_free(&src);
}

/* Evaluate a single REPL input line (expression or bare code). */
static void repl_eval_input(repl_state_t *s)
{
    int needs_semi = repl_input_needs_semi(s->input);

    if (!repl_try_expression(s))
        repl_try_bare_code(s, needs_semi);

    repl_rebuild_preamble(s->preamble, s->decls);
}

static int cmd_repl(void)
{
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    repl_buf_t preamble;
    repl_buf_t input;
    repl_decl_list_t decls;
    vigil_line_history_t history;
    char proj_root[4096];
    const char *project_root = NULL;
    int exit_code = 0;

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize runtime: %s\n", vigil_error_message(&error));
        return 1;
    }
    if (vigil_vm_open(&vm, runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "failed to initialize vm: %s\n", vigil_error_message(&error));
        vigil_runtime_close(&runtime);
        return 1;
    }

    repl_buf_init(&preamble);
    repl_buf_init(&input);
    repl_decl_list_init(&decls);
    vigil_line_history_init(&history, 1000);

    repl_decl_list_put(&decls, NULL, "import \"fmt\";\n");
    repl_rebuild_preamble(&preamble, &decls);

    if (find_project_root("./dummy.vigil", proj_root, sizeof(proj_root)))
        project_root = proj_root;

    printf("vigil %s\n", VIGIL_VERSION);
    printf("Type :help for help, :quit to exit.\n");

    for (;;)
    {
        int rc = repl_read_input(&input, &history, &error);
        if (rc == 0)
            break;
        if (rc == -1)
            continue;

        rc = repl_handle_special_command(input.data, &decls, &preamble, &error);
        if (rc == -1)
            break;
        if (rc == 1)
            continue;

        repl_eval_input(&(repl_state_t){runtime, vm, &input, &preamble, &decls, project_root, &error});
    }

    repl_buf_free(&preamble);
    repl_decl_list_free(&decls);
    vigil_line_history_free(&history);
    repl_buf_free(&input);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    return exit_code;
}

/* ── get command ─────────────────────────────────────────────────── */

static int cmd_get(int argc, char **argv)
{
    vigil_error_t error = {0};
    char project_root[4096];
    char *cwd = NULL;
    int remove_mode = 0;
    int i;
    int found_pkg = 0;

    /* Parse flags */
    for (i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "-remove") == 0 || strcmp(argv[i], "--remove") == 0)
        {
            remove_mode = 1;
        }
    }

    /* Get current directory */
    if (vigil_platform_getcwd(&cwd, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error: failed to get current directory\n");
        return 1;
    }

    /* Find project root */
    {
        char toml_path[4096];
        int exists = 0;
        if (vigil_platform_path_join(cwd, "vigil.toml", toml_path, sizeof(toml_path), &error) == VIGIL_STATUS_OK &&
            vigil_platform_file_exists(toml_path, &exists) == VIGIL_STATUS_OK && exists)
        {
            snprintf(project_root, sizeof(project_root), "%s", cwd);
        }
        else
        {
            fprintf(stderr, "error: not in a VIGIL project (no vigil.toml found)\n");
            free(cwd);
            return 1;
        }
    }
    free(cwd);

    /* No package specified - sync all deps */
    found_pkg = 0;
    for (i = 2; i < argc; i++)
    {
        if (argv[i][0] != '-')
        {
            found_pkg = 1;
            break;
        }
    }

    if (!found_pkg)
    {
        printf("syncing dependencies...\n");
        if (vigil_pkg_sync(project_root, &error) != VIGIL_STATUS_OK)
        {
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            return 1;
        }
        printf("done\n");
        return 0;
    }

    /* Process each package argument */
    for (i = 2; i < argc; i++)
    {
        if (argv[i][0] == '-')
            continue;

        if (remove_mode)
        {
            printf("removing %s...\n", argv[i]);
            if (vigil_pkg_remove(project_root, argv[i], &error) != VIGIL_STATUS_OK)
            {
                fprintf(stderr, "error: %s\n", vigil_error_message(&error));
                return 1;
            }
        }
        else
        {
            printf("getting %s...\n", argv[i]);
            if (vigil_pkg_get(project_root, argv[i], &error) != VIGIL_STATUS_OK)
            {
                fprintf(stderr, "error: %s\n", vigil_error_message(&error));
                return 1;
            }
            printf("  installed %s\n", argv[i]);
        }
    }

    return 0;
}

/* ── package command ─────────────────────────────────────────────── */

static int package_inspect_mode(const char *entry_path, const char *output_path, vigil_error_t *error)
{
    const char *target = entry_path != NULL ? entry_path : output_path;
    vigil_package_bundle_t bundle;
    size_t index;

    if (target == NULL)
    {
        fprintf(stderr, "error: --inspect requires a binary path\n");
        return 2;
    }
    if (vigil_package_read(target, &bundle, error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error[package]: %s\n", vigil_error_message(error));
        return 1;
    }

    printf("ENTRY\n  entry.vigil\n\nFILES\n");
    for (index = 0; index < bundle.file_count; index++)
    {
        printf("  %s\n", bundle.paths[index]);
    }
    vigil_package_bundle_free(&bundle);
    return 0;
}

static void package_default_output_path(const char *script_path, char *out_path, size_t out_path_size)
{
    const char *base = script_path;
    const char *cursor;
    size_t base_length;

    for (cursor = script_path; *cursor != '\0'; cursor++)
    {
        if (*cursor == '/' || *cursor == '\\')
        {
            base = cursor + 1;
        }
    }

    base_length = strlen(base);
    if (base_length > 6 && memcmp(base + base_length - 6, ".vigil", 6) == 0)
    {
        base_length -= 6;
    }
    snprintf(out_path, out_path_size, "%.*s", (int)base_length, base);
}

static vigil_package_file_t *package_collect_files(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                                   size_t *out_count)
{
    vigil_package_file_t *pkg_files;
    size_t pkg_count = 0;
    size_t pkg_cap;
    size_t index;

    *out_count = 0U;
    pkg_cap = vigil_source_registry_count(registry) + 1;
    pkg_files = (vigil_package_file_t *)calloc(pkg_cap, sizeof(vigil_package_file_t));
    if (pkg_files == NULL)
    {
        return NULL;
    }

    for (index = 1; index <= vigil_source_registry_count(registry); index++)
    {
        const vigil_source_file_t *src = vigil_source_registry_get(registry, (vigil_source_id_t)index);
        if (src == NULL)
        {
            continue;
        }

        if ((vigil_source_id_t)index == source_id)
        {
            pkg_files[pkg_count].path = "entry.vigil";
            pkg_files[pkg_count].path_length = 11;
        }
        else
        {
            pkg_files[pkg_count].path = vigil_string_c_str(&src->path);
            pkg_files[pkg_count].path_length = vigil_string_length(&src->path);
        }
        pkg_files[pkg_count].data = vigil_string_c_str(&src->text);
        pkg_files[pkg_count].data_length = vigil_string_length(&src->text);
        pkg_count++;
    }

    *out_count = pkg_count;
    return pkg_files;
}

static int cmd_package(const char *entry_path, const char *output_path, const char *key, int inspect)
{
    vigil_error_t error = {0};

    if (inspect)
    {
        return package_inspect_mode(entry_path, output_path, &error);
    }

    /* Build mode. */
    {
        vigil_runtime_t *runtime = NULL;
        vigil_source_registry_t registry;
        vigil_source_id_t source_id = 0;
        const char *script_path;
        char out_path[4096];
        vigil_package_file_t *pkg_files = NULL;
        size_t pkg_count = 0;

        script_path = entry_path ? entry_path : "main.vigil";

        if (output_path != NULL)
        {
            snprintf(out_path, sizeof(out_path), "%s", output_path);
        }
        else
        {
            package_default_output_path(script_path, out_path, sizeof(out_path));
        }

        if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
        {
            fprintf(stderr, "error: %s\n", vigil_error_message(&error));
            return 1;
        }

        vigil_source_registry_init(&registry, runtime);

        /* Register source tree (walks imports). */
        {
            char proj_root[4096];
            const char *root = find_project_root(script_path, proj_root, sizeof(proj_root)) ? proj_root : NULL;
            if (!register_source_tree(&registry, script_path, root, &source_id, &error))
            {
                fprintf(stderr, "error: %s\n", vigil_error_message(&error));
                vigil_source_registry_free(&registry);
                vigil_runtime_close(&runtime);
                return 1;
            }
        }

        pkg_files = package_collect_files(&registry, source_id, &pkg_count);
        if (pkg_files == NULL)
        {
            vigil_source_registry_free(&registry);
            vigil_runtime_close(&runtime);
            return 1;
        }

        if (vigil_package_build(out_path, pkg_files, pkg_count, key, key ? strlen(key) : 0, &error) != VIGIL_STATUS_OK)
        {
            fprintf(stderr, "error[package]: %s\n", vigil_error_message(&error));
            free(pkg_files);
            vigil_source_registry_free(&registry);
            vigil_runtime_close(&runtime);
            return 1;
        }

        printf("packaged %zu file(s) -> %s\n", pkg_count, out_path);
        free(pkg_files);
        vigil_source_registry_free(&registry);
        vigil_runtime_close(&runtime);
        return 0;
    }
}

/* ── main ────────────────────────────────────────────────────────── */

static void normalize_format_arg(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "format") == 0)
        argv[1] = (char *)"fmt";
}

/* ── Early command dispatch (before CLI parser) ──────────────────── */

static int early_dispatch_run(int argc, char **argv)
{
    if (argc < 3 || strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)
        return -1;
    const char *const *script_argv = argc > 3 ? (const char *const *)&argv[3] : NULL;
    size_t script_argc = argc > 3 ? (size_t)(argc - 3) : 0;
    return cmd_run(argv[2], script_argv, script_argc);
}

static int early_dispatch_embed(int argc, char **argv)
{
    if (argc == 2 || strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)
    {
        printf("Usage: vigil embed <file|dir...> [-o output.vigil]\n\n");
        printf("Embed files as VIGIL source code\n\nOptions:\n");
        printf("  -o, --output         Output file (default: embed.vigil)\n");
        return 0;
    }
    return cmd_embed(argc, argv);
}

static int early_dispatch_get(int argc, char **argv)
{
    if (argc >= 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0))
    {
        printf("Usage: vigil get [package[@version]...]\n\n");
        printf("Manage dependencies using git for distribution.\n\n");
        printf("Examples:\n");
        printf("  vigil get                              Sync all deps from vigil.toml\n");
        printf("  vigil get github.com/user/repo         Install latest\n");
        printf("  vigil get github.com/user/repo@v1.0.0  Install specific version\n");
        printf("  vigil get github.com/user/repo@main    Install branch\n");
        printf("\nOptions:\n  --remove   Remove a package\n");
        return 0;
    }
    return cmd_get(argc, argv);
}

typedef int (*early_handler_t)(int argc, char **argv);

typedef struct
{
    const char *name;
    early_handler_t handler;
} early_command_t;

static const early_command_t early_commands[] = {
    {"run", early_dispatch_run},
    {"embed", early_dispatch_embed},
    {"test", (early_handler_t)vigil_cli_run_test_command},
    {"get", early_dispatch_get},
    {NULL, NULL},
};

/* Try to dispatch a command before the CLI parser. Returns >= 0 if handled. */
static int dispatch_early_command(int argc, char **argv)
{
    if (argc < 2)
        return -1;
    if (strcmp(argv[1], "repl") == 0)
        return cmd_repl();
    if (strcmp(argv[1], "lsp") == 0 && argc == 2)
        return cmd_lsp();
    if (strcmp(argv[1], "version") == 0)
    {
        printf("vigil %s\n", VIGIL_VERSION);
        return 0;
    }
    for (const early_command_t *ec = early_commands; ec->name; ec++)
    {
        if (strcmp(argv[1], ec->name) == 0)
            return ec->handler(argc, argv);
    }
    return -1;
}

/* ── Post-parse command dispatch ─────────────────────────────────── */

typedef struct
{
    const char *check_file;
    const char *new_name;
    const char *new_output;
    const char *debug_file;
    int debug_interactive;
    const char *doc_file;
    const char *doc_symbol;
    const char *fmt_file;
    int fmt_check;
    const char *pkg_entry;
    const char *pkg_output;
    const char *pkg_key;
    int pkg_inspect;
    int new_lib;
    int new_scaffold;
} parsed_args_t;

static int dispatch_check(const parsed_args_t *args)
{
    if (args->check_file == NULL)
    {
        fprintf(stderr, "error: missing file argument\n");
        return 2;
    }
    return cmd_check(args->check_file);
}

static int dispatch_new(const parsed_args_t *args)
{
    return cmd_new(args->new_name, args->new_lib, args->new_scaffold, args->new_output);
}

static int dispatch_debug(const parsed_args_t *args)
{
    if (args->debug_file == NULL)
    {
        fprintf(stderr, "error: missing file argument\n");
        return 2;
    }
    if (args->debug_interactive)
        return cmd_debug_interactive(args->debug_file);
    return cmd_debug(args->debug_file);
}

static int dispatch_doc(const parsed_args_t *args)
{
    return cmd_doc(args->doc_file, args->doc_symbol);
}

static int dispatch_fmt(const parsed_args_t *args)
{
    if (args->fmt_file == NULL)
    {
        fprintf(stderr, "error: missing file argument\n");
        return 2;
    }
    return cmd_fmt(args->fmt_file, args->fmt_check);
}

static int dispatch_package(const parsed_args_t *args)
{
    return cmd_package(args->pkg_entry, args->pkg_output, args->pkg_key, args->pkg_inspect);
}

typedef struct
{
    const char *name;
    int (*handler)(const parsed_args_t *args);
} post_parse_command_t;

static const post_parse_command_t post_parse_commands[] = {
    {"check", dispatch_check},
    {"new", dispatch_new},
    {"debug", dispatch_debug},
    {"doc", dispatch_doc},
    {"fmt", dispatch_fmt},
    {"package", dispatch_package},
    {NULL, NULL},
};

static int dispatch_post_parse(const char *name, const parsed_args_t *args)
{
    for (const post_parse_command_t *pc = post_parse_commands; pc->name; pc++)
    {
        if (strcmp(name, pc->name) == 0)
            return pc->handler(args);
    }
    return 0;
}

/* ── CLI registration ────────────────────────────────────────────── */

static void register_cli_commands(vigil_cli_t *cli, parsed_args_t *args)
{
    vigil_cli_command_t *cmd;

    cmd = vigil_cli_add_command(cli, "run", "Run a VIGIL script");
    vigil_cli_add_positional(cmd, "file", "Script file to run", NULL);

    cmd = vigil_cli_add_command(cli, "check", "Type-check a VIGIL script");
    vigil_cli_add_positional(cmd, "file", "Script file to check", &args->check_file);

    cmd = vigil_cli_add_command(cli, "new", "Create a new VIGIL project");
    vigil_cli_add_positional(cmd, "name", "Project name", &args->new_name);
    vigil_cli_add_bool_flag(cmd, "lib", 'l', "Create a library project", &args->new_lib);
    vigil_cli_add_bool_flag(cmd, "scaffold", 's', "Include example module and test", &args->new_scaffold);
    vigil_cli_add_string_flag(cmd, "output", 'o', "Output directory", &args->new_output);

    cmd = vigil_cli_add_command(cli, "debug", "Debug a VIGIL script");
    vigil_cli_add_positional(cmd, "file", "Script file to debug", &args->debug_file);
    vigil_cli_add_bool_flag(cmd, "interactive", 'i', "Use interactive CLI debugger (default: DAP server)",
                            &args->debug_interactive);

    cmd = vigil_cli_add_command(cli, "doc", "Show documentation for modules, builtins, or source files");
    vigil_cli_add_positional(cmd, "target", "Module name (e.g. math) or source file path", &args->doc_file);
    vigil_cli_add_positional(cmd, "symbol", "Symbol to look up (e.g. sqrt or Point.x)", &args->doc_symbol);

    cmd = vigil_cli_add_command(cli, "fmt", "Format VIGIL source files");
    vigil_cli_add_positional(cmd, "file", "Source file to format", &args->fmt_file);
    vigil_cli_add_bool_flag(cmd, "check", 'c', "Check formatting without rewriting", &args->fmt_check);

    (void)vigil_cli_add_command(cli, "repl", "Start interactive REPL");
    (void)vigil_cli_add_command(cli, "lsp", "Start Language Server Protocol server");
    (void)vigil_cli_add_command(cli, "version", "Print version information");
    (void)vigil_cli_add_command(cli, "embed", "Embed files as VIGIL source code");
    (void)vigil_cli_add_command(cli, "test", "Run tests");
    (void)vigil_cli_add_command(cli, "get", "Manage dependencies");

    cmd = vigil_cli_add_command(cli, "package", "Package a VIGIL program as a standalone binary");
    vigil_cli_add_positional(cmd, "entry", "Entry script or project directory", &args->pkg_entry);
    vigil_cli_add_string_flag(cmd, "output", 'o', "Output path", &args->pkg_output);
    vigil_cli_add_string_flag(cmd, "key", 'k', "XOR encryption key for obfuscation", &args->pkg_key);
    vigil_cli_add_bool_flag(cmd, "inspect", 'i', "Inspect a packaged binary", &args->pkg_inspect);
}

int main(int argc, char **argv)
{
    vigil_cli_t cli;
    parsed_args_t args = {0};
    vigil_error_t error = {0};
    const vigil_cli_command_t *matched;

    /* Check if this is a packaged binary. */
    {
        int rc = try_run_packaged(argc, argv);
        if (rc >= 0)
            return rc;
    }

    /* Handle "vigil <file.vigil> [args...]" as shorthand for "vigil run". */
    if (argc >= 2 && argv[1][0] != '-')
    {
        size_t len = strlen(argv[1]);
        if (len > 6 && strcmp(argv[1] + len - 6, ".vigil") == 0)
        {
            const char *const *script_argv = argc > 2 ? (const char *const *)&argv[2] : NULL;
            size_t script_argc = argc > 2 ? (size_t)(argc - 2) : 0;
            return cmd_run(argv[1], script_argv, script_argc);
        }
    }

    /* Dispatch commands that need special arg handling before CLI parser. */
    {
        int rc = dispatch_early_command(argc, argv);
        if (rc >= 0)
            return rc;
    }

    normalize_format_arg(argc, argv);
    vigil_cli_init(&cli, "vigil", "The VIGIL Scripting Language");
    register_cli_commands(&cli, &args);

    if (vigil_cli_parse(&cli, argc, argv, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stderr, "error: %s\n", vigil_error_message(&error));
        vigil_cli_free(&cli);
        return 2;
    }

    matched = vigil_cli_matched_command(&cli);
    if (matched == NULL)
    {
        if (!cli.help_shown)
            vigil_cli_print_help(&cli);
        vigil_cli_free(&cli);
        return 0;
    }

    {
        const char *matched_name = matched->name;
        vigil_cli_free(&cli);
        return dispatch_post_parse(matched_name, &args);
    }
}
