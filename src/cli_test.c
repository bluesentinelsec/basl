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
#include "internal/vigil_coverage.h"
#include "platform/platform.h"
#include "vigil/stdlib.h"
#include "vigil/vigil.h"

#include "plugin_registry.h"

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

typedef struct
{
    char **items;
    size_t count;
    size_t cap;
} str_list_t;

typedef struct
{
    int verbose;
    int coverage;
    int include_deps;
    int min_coverage_set;
    double min_coverage;
    vigil_coverage_output_format_t coverage_format;
    const char *filter;
    int transpile;
} test_options_t;

typedef struct
{
    const char *test_file_path;
    const char *original_source;
    size_t original_length;
    const char *test_name;
    const test_options_t *options;
    vigil_coverage_session_t *coverage;
} test_run_request_t;

typedef struct
{
    vigil_source_registry_t *registry;
    vigil_object_t *function;
    const char *scope_root;
    const char *wrapper_path;
    char *err_msg;
    size_t err_msg_size;
    vigil_error_t *error;
} test_coverage_state_t;

typedef struct
{
    const test_run_request_t *request;
    vigil_vm_t *vm;
    vigil_source_registry_t *registry;
    vigil_object_t *function;
    vigil_value_t *result;
    const char *scope_root;
    const char *wrapper_path;
    char *err_msg;
    size_t err_msg_size;
    vigil_error_t *error;
} test_execution_state_t;

typedef struct
{
    const char *test_file_path;
    const test_options_t *options;
    FILE *stream;
    int *total_pass;
    int *total_fail;
    int *file_failed;
} test_result_state_t;

typedef struct
{
    vigil_source_registry_t *registry;
    vigil_diagnostic_list_t *diagnostics;
    const char *test_file_path;
    const char *project_root;
    const char *wrapper_path;
    const char *combined;
    size_t total_length;
    vigil_object_t **out_function;
    char *err_msg;
    size_t err_msg_size;
} test_compile_state_t;

typedef struct
{
    const test_run_request_t *request;
    char *project_root;
    size_t project_root_size;
    char *scope_root_buffer;
    size_t scope_root_size;
    const char **out_root;
    const char **out_scope_root;
    char **out_combined;
    char **out_wrapper_path;
    size_t *out_total_length;
} test_runtime_input_state_t;

static FILE *test_output_stream(const test_options_t *options)
{
    if (options != NULL && options->coverage && options->coverage_format == VIGIL_COVERAGE_OUTPUT_JSON)
        return stderr;
    return stdout;
}

static vigil_status_t dir_list_cb(const char *name, int is_dir, void *ud)
{
    dir_list_t *dl;

    dl = ud;
    if (dl->count == dl->cap)
    {
        size_t next_cap;

        next_cap = dl->cap == 0U ? 32U : dl->cap * 2U;
        dl->items = realloc(dl->items, next_cap * sizeof(dir_entry_t));
        if (dl->items == NULL)
        {
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        dl->cap = next_cap;
    }
    snprintf(dl->items[dl->count].name, sizeof(dl->items[0].name), "%s", name);
    dl->items[dl->count].is_dir = is_dir;
    dl->count += 1U;
    return VIGIL_STATUS_OK;
}

static void sl_add(str_list_t *sl, const char *s)
{
    if (sl->count == sl->cap)
    {
        size_t next_cap;

        next_cap = sl->cap == 0U ? 16U : sl->cap * 2U;
        sl->items = realloc(sl->items, next_cap * sizeof(char *));
        if (sl->items == NULL)
            return;
        sl->cap = next_cap;
    }
    sl->items[sl->count] = cli_strdup(s);
    if (sl->items[sl->count] != NULL)
        sl->count += 1U;
}

static void sl_free(str_list_t *sl)
{
    size_t index;

    for (index = 0U; index < sl->count; index += 1U)
        free(sl->items[index]);
    free(sl->items);
    memset(sl, 0, sizeof(*sl));
}

static void collect_test_files(str_list_t *out, const char *dir)
{
    vigil_error_t err;
    dir_list_t dl;
    size_t index;

    memset(&err, 0, sizeof(err));
    memset(&dl, 0, sizeof(dl));
    if (vigil_platform_list_dir(dir, dir_list_cb, &dl, &err) != VIGIL_STATUS_OK)
    {
        free(dl.items);
        return;
    }

    for (index = 0U; index < dl.count; index += 1U)
    {
        char full[4096];

        if (vigil_platform_path_join(dir, dl.items[index].name, full, sizeof(full), &err) != VIGIL_STATUS_OK)
            continue;
        if (dl.items[index].is_dir)
        {
            collect_test_files(out, full);
            continue;
        }
        if (strlen(dl.items[index].name) > 11U &&
            strcmp(dl.items[index].name + strlen(dl.items[index].name) - 11U, "_test.vigil") == 0)
        {
            sl_add(out, full);
        }
    }
    free(dl.items);
}

static void scan_test_functions(vigil_runtime_t *runtime, vigil_source_registry_t *registry,
                                vigil_source_id_t source_id, str_list_t *out)
{
    vigil_token_list_t tokens;
    vigil_diagnostic_list_t diagnostics;
    const vigil_source_file_t *source;
    size_t cursor;
    size_t brace_depth;

    source = vigil_source_registry_get(registry, source_id);
    if (source == NULL)
        return;

    cursor = 0U;
    brace_depth = 0U;
    vigil_token_list_init(&tokens, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    if (vigil_lex_source(registry, source_id, &tokens, &diagnostics, NULL) != VIGIL_STATUS_OK)
    {
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        return;
    }

    while (1)
    {
        const vigil_token_t *token;

        token = vigil_token_list_get(&tokens, cursor);
        if (token == NULL || token->kind == VIGIL_TOKEN_EOF)
            break;
        if (token->kind == VIGIL_TOKEN_LBRACE)
        {
            brace_depth += 1U;
            cursor += 1U;
            continue;
        }
        if (token->kind == VIGIL_TOKEN_RBRACE)
        {
            if (brace_depth != 0U)
                brace_depth -= 1U;
            cursor += 1U;
            continue;
        }
        if (brace_depth == 0U && token->kind == VIGIL_TOKEN_FN)
        {
            const vigil_token_t *name_token;

            name_token = vigil_token_list_get(&tokens, cursor + 1U);
            if (name_token != NULL && name_token->kind == VIGIL_TOKEN_IDENTIFIER)
            {
                char name[256];
                size_t name_length;
                const char *name_text;

                name_text = cli_source_token_text(source, name_token, &name_length);
                if (name_text != NULL && name_length > 5U && memcmp(name_text, "test_", 5U) == 0 &&
                    name_length < sizeof(name))
                {
                    memcpy(name, name_text, name_length);
                    name[name_length] = '\0';
                    sl_add(out, name);
                }
            }
        }
        cursor += 1U;
    }

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
}

static int test_matches_filter(const char *name, const char *filter)
{
    const char *cursor;

    cursor = filter;
    while (*cursor != '\0')
    {
        char part[256];
        const char *end;
        size_t length;

        end = strchr(cursor, '|');
        length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        while (length > 0U && cursor[0] == ' ')
        {
            cursor += 1U;
            length -= 1U;
        }
        while (length > 0U && cursor[length - 1U] == ' ')
            length -= 1U;
        if (length > 0U && length < sizeof(part))
        {
            memcpy(part, cursor, length);
            part[length] = '\0';
            if (strstr(name, part) != NULL)
                return 1;
        }
        if (end == NULL)
            break;
        cursor = end + 1U;
    }
    return 0;
}

static char *build_test_wrapper_source(const char *original_source, size_t original_length, const char *test_name,
                                       size_t *out_length)
{
    char wrapper[512];
    size_t wrapper_length;
    size_t total_length;
    char *combined;

    *out_length = 0U;
    snprintf(wrapper, sizeof(wrapper),
             "\nfn main() -> i32 {\n"
             "    test.T t = test.T();\n"
             "    %s(t);\n"
             "    return 0;\n"
             "}\n",
             test_name);
    wrapper_length = strlen(wrapper);
    total_length = original_length + wrapper_length;
    combined = malloc(total_length + 1U);
    if (combined == NULL)
        return NULL;

    memcpy(combined, original_source, original_length);
    memcpy(combined + original_length, wrapper, wrapper_length);
    combined[total_length] = '\0';
    *out_length = total_length;
    return combined;
}

static char *build_test_wrapper_path(const char *test_file_path)
{
    static const char suffix[] = ".__vigil_test_wrapper__.vigil";
    size_t path_length;
    size_t suffix_length;
    char *wrapper_path;

    if (test_file_path == NULL)
        return NULL;

    path_length = strlen(test_file_path);
    suffix_length = sizeof(suffix) - 1U;
    wrapper_path = malloc(path_length + suffix_length + 1U);
    if (wrapper_path == NULL)
        return NULL;

    memcpy(wrapper_path, test_file_path, path_length);
    memcpy(wrapper_path + path_length, suffix, suffix_length);
    wrapper_path[path_length + suffix_length] = '\0';
    return wrapper_path;
}

static vigil_status_t compile_test_source(vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                          vigil_object_t **out_function, vigil_diagnostic_list_t *diagnostics,
                                          vigil_error_t *error)
{
    vigil_native_registry_t natives;
    vigil_status_t status;

    vigil_native_registry_init(&natives);
    vigil_stdlib_register_all(&natives, error);
    vigil_plugin_register_all(&natives, error);
    status = vigil_compile_source_with_natives(registry, source_id, &natives, out_function, diagnostics, error);
    vigil_native_registry_free(&natives);
    return status;
}

static void format_first_diagnostic_message(const vigil_source_registry_t *registry,
                                            const vigil_diagnostic_list_t *diagnostics, char *err_msg,
                                            size_t err_msg_size)
{
    const vigil_diagnostic_t *diagnostic;
    vigil_string_t line;
    vigil_runtime_t *runtime;
    vigil_error_t error;

    if (err_msg == NULL || err_msg_size == 0U || registry == NULL || diagnostics == NULL ||
        vigil_diagnostic_list_count(diagnostics) == 0U)
    {
        return;
    }

    diagnostic = vigil_diagnostic_list_get(diagnostics, 0U);
    if (diagnostic == NULL)
        return;

    runtime = registry->runtime;
    vigil_string_init(&line, runtime);
    memset(&error, 0, sizeof(error));
    if (vigil_diagnostic_format(registry, diagnostic, &line, &error) == VIGIL_STATUS_OK)
        snprintf(err_msg, err_msg_size, "%s", vigil_string_c_str(&line));
    vigil_string_free(&line);
}

static size_t test_parent_directory_length(const char *path)
{
    size_t length;

    if (path == NULL)
        return 0U;
    length = strlen(path);
    while (length > 0U && path[length - 1U] != '/' && path[length - 1U] != '\\')
        length -= 1U;
    while (length > 1U && (path[length - 1U] == '/' || path[length - 1U] == '\\'))
        length -= 1U;
    return length;
}

static const char *test_scope_root(const char *test_file_path, const char *project_root, char *buffer,
                                   size_t buffer_size)
{
    size_t length;

    if (project_root != NULL)
        return project_root;
    if (test_file_path == NULL || buffer == NULL || buffer_size == 0U)
        return NULL;

    length = test_parent_directory_length(test_file_path);
    if (length == 0U)
    {
        snprintf(buffer, buffer_size, ".");
        return buffer;
    }
    if (length + 1U > buffer_size)
        return NULL;
    memcpy(buffer, test_file_path, length);
    buffer[length] = '\0';
    return buffer;
}

static int prepare_test_coverage(const test_run_request_t *request, const test_coverage_state_t *state)
{
    vigil_status_t status;

    status = vigil_coverage_session_track_registry(request->coverage, state->registry, state->scope_root,
                                                   state->wrapper_path, request->options->include_deps, state->error);
    if (status != VIGIL_STATUS_OK)
    {
        snprintf(state->err_msg, state->err_msg_size, "%s", vigil_error_message(state->error));
        return 0;
    }

    status =
        vigil_coverage_session_register_function(request->coverage, state->registry, state->function, state->error);
    if (status != VIGIL_STATUS_OK)
    {
        snprintf(state->err_msg, state->err_msg_size, "%s", vigil_error_message(state->error));
        return 0;
    }
    return 1;
}

static int compile_wrapped_test_function(const test_compile_state_t *state, vigil_source_id_t *out_source_id,
                                         vigil_error_t *error)
{
    vigil_status_t status;

    if (!register_source_tree(state->registry, state->test_file_path, state->project_root, NULL, error))
    {
        snprintf(state->err_msg, state->err_msg_size, "%s", vigil_error_message(error));
        return 0;
    }
    if (vigil_source_registry_register(state->registry, state->wrapper_path, strlen(state->wrapper_path),
                                       state->combined, state->total_length, out_source_id, error) != VIGIL_STATUS_OK)
    {
        snprintf(state->err_msg, state->err_msg_size, "source registration failed");
        return 0;
    }

    status = compile_test_source(state->registry, *out_source_id, state->out_function, state->diagnostics, error);
    if (status == VIGIL_STATUS_OK)
        return 1;
    if (vigil_diagnostic_list_count(state->diagnostics) != 0U)
    {
        format_first_diagnostic_message(state->registry, state->diagnostics, state->err_msg, state->err_msg_size);
        if (state->err_msg[0] == '\0')
            snprintf(state->err_msg, state->err_msg_size, "compile error");
    }
    else
    {
        snprintf(state->err_msg, state->err_msg_size, "%s", vigil_error_message(error));
    }
    return 0;
}

static int open_test_runtime_vm(vigil_runtime_t **out_runtime, vigil_vm_t **out_vm, char *err_msg, size_t err_msg_size)
{
    vigil_error_t error;

    memset(&error, 0, sizeof(error));
    if (vigil_runtime_open(out_runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        snprintf(err_msg, err_msg_size, "runtime init failed");
        return 0;
    }
    if (vigil_vm_open(out_vm, *out_runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        vigil_runtime_close(out_runtime);
        snprintf(err_msg, err_msg_size, "vm init failed");
        return 0;
    }
    return 1;
}

static int run_one_test_with_coverage(const test_execution_state_t *state)
{
    test_coverage_state_t coverage_state;
    vigil_status_t status;

    coverage_state.registry = state->registry;
    coverage_state.function = state->function;
    coverage_state.scope_root = state->scope_root;
    coverage_state.wrapper_path = state->wrapper_path;
    coverage_state.err_msg = state->err_msg;
    coverage_state.err_msg_size = state->err_msg_size;
    coverage_state.error = state->error;
    if (!prepare_test_coverage(state->request, &coverage_state))
        return 1;

    vigil_coverage_session_attach_vm(state->request->coverage, state->vm, state->registry);
    status = vigil_vm_execute_function(state->vm, state->function, state->result, state->error);
    vigil_coverage_session_detach_vm(state->request->coverage, state->vm);
    if (status == VIGIL_STATUS_OK)
    {
        /* Re-scan after execution so imported callable globals resolved at
         * runtime still contribute their chunks to the final report. */
        (void)vigil_coverage_session_register_function(state->request->coverage, state->registry, state->function,
                                                       state->error);
        return 0;
    }

    snprintf(state->err_msg, state->err_msg_size, "%s", vigil_error_message(state->error));
    return 1;
}

static int prepare_test_runtime_inputs(const test_runtime_input_state_t *state)
{
    const char *root;

    *state->out_total_length = 0U;
    root = find_project_root(state->request->test_file_path, state->project_root, state->project_root_size)
               ? state->project_root
               : NULL;
    *state->out_scope_root =
        test_scope_root(state->request->test_file_path, root, state->scope_root_buffer, state->scope_root_size);
    *state->out_combined = build_test_wrapper_source(state->request->original_source, state->request->original_length,
                                                     state->request->test_name, state->out_total_length);
    *state->out_wrapper_path = build_test_wrapper_path(state->request->test_file_path);
    *state->out_root = root;
    return *state->out_combined != NULL && *state->out_wrapper_path != NULL;
}

static int run_one_test(const test_run_request_t *request, char *err_msg, size_t err_msg_size)
{
    vigil_runtime_t *runtime;
    vigil_vm_t *vm;
    vigil_error_t error;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_value_t result;
    vigil_source_id_t source_id;
    vigil_object_t *function;
    vigil_status_t status;
    int exit_code;
    size_t total_length;
    char project_root[4096];
    char scope_root_buffer[4096];
    const char *root;
    const char *scope_root;
    char *combined;
    char *wrapper_path;
    test_compile_state_t compile_state;
    test_runtime_input_state_t input_state;
    test_execution_state_t execution_state;

    runtime = NULL;
    vm = NULL;
    source_id = 0U;
    function = NULL;
    exit_code = 0;
    total_length = 0U;
    memset(&error, 0, sizeof(error));
    input_state.request = request;
    input_state.project_root = project_root;
    input_state.project_root_size = sizeof(project_root);
    input_state.scope_root_buffer = scope_root_buffer;
    input_state.scope_root_size = sizeof(scope_root_buffer);
    input_state.out_root = &root;
    input_state.out_scope_root = &scope_root;
    input_state.out_combined = &combined;
    input_state.out_wrapper_path = &wrapper_path;
    input_state.out_total_length = &total_length;
    if (!prepare_test_runtime_inputs(&input_state))
    {
        snprintf(err_msg, err_msg_size, "out of memory");
        free(wrapper_path);
        free(combined);
        return 1;
    }

    if (!open_test_runtime_vm(&runtime, &vm, err_msg, err_msg_size))
    {
        free(wrapper_path);
        free(combined);
        return 1;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_value_init_nil(&result);

    compile_state.registry = &registry;
    compile_state.diagnostics = &diagnostics;
    compile_state.test_file_path = request->test_file_path;
    compile_state.project_root = root;
    compile_state.wrapper_path = wrapper_path;
    compile_state.combined = combined;
    compile_state.total_length = total_length;
    compile_state.out_function = &function;
    compile_state.err_msg = err_msg;
    compile_state.err_msg_size = err_msg_size;
    if (!compile_wrapped_test_function(&compile_state, &source_id, &error))
    {
        vigil_object_release(&function);
        exit_code = 1;
        goto cleanup;
    }

    if (request->options->coverage)
    {
        execution_state.request = request;
        execution_state.vm = vm;
        execution_state.registry = &registry;
        execution_state.function = function;
        execution_state.result = &result;
        execution_state.scope_root = scope_root;
        execution_state.wrapper_path = wrapper_path;
        execution_state.err_msg = err_msg;
        execution_state.err_msg_size = err_msg_size;
        execution_state.error = &error;
        exit_code = run_one_test_with_coverage(&execution_state);
        vigil_object_release(&function);
        goto cleanup;
    }

    status = vigil_vm_execute_function(vm, function, &result, &error);
    vigil_object_release(&function);
    if (status != VIGIL_STATUS_OK)
    {
        snprintf(err_msg, err_msg_size, "%s", vigil_error_message(&error));
        exit_code = 1;
    }

cleanup:
    vigil_value_release(&result);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    free(wrapper_path);
    free(combined);
    return exit_code;
}

static int cmd_test_parse_coverage_toggle_arg(const char *arg, test_options_t *options)
{
    if (strcmp(arg, "--coverage") == 0)
    {
        options->coverage = 1;
        return 1;
    }
    if (strcmp(arg, "--include-deps") == 0)
    {
        options->include_deps = 1;
        return 1;
    }
    return 0;
}

static int cmd_test_parse_coverage_format_arg(int argc, char **argv, int *index, test_options_t *options)
{
    if (strcmp(argv[*index], "--format") == 0)
    {
        *index += 1;
        if (*index >= argc)
        {
            fprintf(stderr, "error: --format requires a value\n");
            return 2;
        }
        if (strcmp(argv[*index], "json") == 0)
        {
            options->coverage_format = VIGIL_COVERAGE_OUTPUT_JSON;
            return 1;
        }
        if (strcmp(argv[*index], "text") == 0)
        {
            options->coverage_format = VIGIL_COVERAGE_OUTPUT_TEXT;
            return 1;
        }
        fprintf(stderr, "error: unsupported coverage format '%s'\n", argv[*index]);
        return 2;
    }
    return 0;
}

static int cmd_test_parse_min_coverage_arg(int argc, char **argv, int *index, test_options_t *options)
{
    char *end;

    if (strcmp(argv[*index], "--min-coverage") != 0)
        return 0;
    *index += 1;
    if (*index >= argc)
    {
        fprintf(stderr, "error: --min-coverage requires a percentage\n");
        return 2;
    }
    options->min_coverage = strtod(argv[*index], &end);
    if (end == argv[*index] || *end != '\0' || options->min_coverage < 0.0 || options->min_coverage > 100.0)
    {
        fprintf(stderr, "error: --min-coverage must be a number between 0 and 100\n");
        return 2;
    }
    options->min_coverage_set = 1;
    return 1;
}

static int cmd_test_is_help_flag(const char *arg)
{
    return strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0;
}

static int cmd_test_parse_core_arg(int argc, char **argv, int *index, test_options_t *options)
{
    if (strcmp(argv[*index], "-v") == 0 || strcmp(argv[*index], "--verbose") == 0)
    {
        options->verbose = 1;
        return 1;
    }
    if (strcmp(argv[*index], "--transpile") == 0)
    {
        options->transpile = 1;
        return 1;
    }
    if (strcmp(argv[*index], "-run") != 0 && strcmp(argv[*index], "--run") != 0)
        return 0;
    *index += 1;
    if (*index >= argc)
    {
        fprintf(stderr, "error: -run requires a pattern\n");
        return 2;
    }
    options->filter = argv[*index];
    return 1;
}

static int cmd_test_print_help_and_exit(const char *arg)
{
    if (!cmd_test_is_help_flag(arg))
        return 0;
    printf("Usage: vigil test [--run pattern] [-v] [--coverage] [--transpile] [path...]\n\n"
           "Recursively finds and runs VIGIL test files (*_test.vigil).\n"
           "In a project root, defaults to the ./test directory.\n\n"
           "Flags:\n"
           "  -v, --verbose           Print passing tests and verbose coverage details\n"
           "  -run, --run             Filter test names by substring\n"
           "      --coverage          Collect line and branch coverage\n"
           "      --format            Coverage output format: text|json\n"
           "      --min-coverage      Fail if total line coverage is below N percent\n"
           "      --include-deps      Include imported user modules outside the project root\n"
           "      --transpile         Transpile each test file to C, build, and compare output\n");
    return 1;
}

static int cmd_test_parse_args(int argc, char **argv, test_options_t *options, str_list_t *targets)
{
    int index;

    options->verbose = 0;
    options->coverage = 0;
    options->include_deps = 0;
    options->min_coverage_set = 0;
    options->min_coverage = 0.0;
    options->coverage_format = VIGIL_COVERAGE_OUTPUT_TEXT;
    options->filter = NULL;
    options->transpile = 0;
    for (index = 2; index < argc; index += 1)
    {
        int parse_status;

        parse_status = cmd_test_parse_core_arg(argc, argv, &index, options);
        if (parse_status == 1)
            continue;
        if (parse_status == 2)
            return 2;

        parse_status = cmd_test_parse_coverage_toggle_arg(argv[index], options);
        if (parse_status == 1)
            continue;

        parse_status = cmd_test_parse_coverage_format_arg(argc, argv, &index, options);
        if (parse_status == 1)
            continue;
        if (parse_status == 2)
            return 2;

        parse_status = cmd_test_parse_min_coverage_arg(argc, argv, &index, options);
        if (parse_status == 1)
            continue;
        if (parse_status == 2)
            return 2;

        if (cmd_test_print_help_and_exit(argv[index]))
            return 0;
        sl_add(targets, argv[index]);
    }
    return -1;
}

static void cmd_test_add_default_target(str_list_t *targets)
{
    int has_manifest;
    int is_dir;

    if (targets->count != 0U)
        return;

    is_dir = 0;
    if (vigil_platform_is_directory("test", &is_dir) == VIGIL_STATUS_OK && is_dir)
    {
        has_manifest = 0;
        if (vigil_platform_file_exists("vigil.toml", &has_manifest) == VIGIL_STATUS_OK && has_manifest)
        {
            sl_add(targets, "test");
            return;
        }
    }
    sl_add(targets, ".");
}

static void cmd_test_collect_files(str_list_t *targets, str_list_t *test_files)
{
    size_t index;

    for (index = 0U; index < targets->count; index += 1U)
    {
        int is_dir;

        is_dir = 0;
        vigil_platform_is_directory(targets->items[index], &is_dir);
        if (is_dir)
        {
            collect_test_files(test_files, targets->items[index]);
            continue;
        }
        if (strlen(targets->items[index]) > 11U &&
            strcmp(targets->items[index] + strlen(targets->items[index]) - 11U, "_test.vigil") == 0)
        {
            sl_add(test_files, targets->items[index]);
        }
    }
}

static void cmd_test_print_summary(const test_options_t *options, int total_pass, int total_fail)
{
    FILE *stream;

    stream = test_output_stream(options);
    if (total_fail > 0)
        fprintf(stream, "\nFAIL: %d passed, %d failed\n", total_pass, total_fail);
    else if (total_pass > 0)
        fprintf(stream, "\nPASS: %d passed\n", total_pass);
}

static int scan_test_file_functions(const char *test_file_path, const char *source, size_t source_length,
                                    str_list_t *function_names)
{
    vigil_runtime_t *runtime;
    vigil_source_registry_t registry;
    vigil_source_id_t source_id;

    runtime = NULL;
    source_id = 0U;
    if (vigil_runtime_open(&runtime, NULL, NULL) != VIGIL_STATUS_OK)
        return 0;

    vigil_source_registry_init(&registry, runtime);
    if (vigil_source_registry_register(&registry, test_file_path, strlen(test_file_path), source, source_length,
                                       &source_id, NULL) == VIGIL_STATUS_OK)
    {
        scan_test_functions(runtime, &registry, source_id, function_names);
    }
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
    return 1;
}

static void report_test_result(const test_result_state_t *state, const char *name, double elapsed, int result,
                               const char *err_msg)
{
    if (result == 0)
    {
        *state->total_pass += 1;
        if (state->options->verbose)
            fprintf(state->stream, "=== RUN   %s\n--- PASS: %s (%.3fs)\n", name, name, elapsed);
        return;
    }

    *state->total_fail += 1;
    *state->file_failed = 1;
    fprintf(state->stream, "--- FAIL: %s (%s)\n    %s\n", name, state->test_file_path, err_msg);
}

static int run_transpile_validation(const char *test_file_path, const char *source, size_t source_length,
                                    const str_list_t *function_names, FILE *stream, int verbose)
{
    char vigil_bin[4096];
    char *tmpdir_base = NULL;
    char tmpdir[4096];
    char wrapper_file[4096];
    char cmd_buf[8192];
    char *interp_out = NULL;
    char *interp_err = NULL;
    char *transpile_out = NULL;
    char *transpile_err = NULL;
    char *build_out = NULL;
    char *build_err = NULL;
    char *native_out = NULL;
    char *native_err = NULL;
    int exit_code;
    int result = 0;
    vigil_error_t error;

    memset(&error, 0, sizeof(error));

    if (vigil_platform_self_exe(vigil_bin, sizeof(vigil_bin), &error) != VIGIL_STATUS_OK)
    {
        fprintf(stream, "--- FAIL: transpile (%s)\n    cannot determine vigil binary path\n", test_file_path);
        return 1;
    }

    if (vigil_platform_temp_dir(NULL, &tmpdir_base, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stream, "--- FAIL: transpile (%s)\n    cannot get temp directory\n", test_file_path);
        return 1;
    }
    snprintf(tmpdir, sizeof(tmpdir), "%s/vigil_transpile_test_%d", tmpdir_base, vigil_platform_getpid());
    free(tmpdir_base);

    if (vigil_platform_mkdir_p(tmpdir, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stream, "--- FAIL: transpile (%s)\n    cannot create temp directory\n", test_file_path);
        return 1;
    }

    /* Build a wrapper source that calls all test functions from main() */
    {
        size_t wrapper_cap = source_length + 1024 + function_names->count * 256;
        char *wrapper = malloc(wrapper_cap);
        if (!wrapper)
        {
            fprintf(stream, "--- FAIL: transpile (%s)\n    allocation failed\n", test_file_path);
            result = 1;
            goto cleanup;
        }
        size_t pos = 0;
        memcpy(wrapper + pos, source, source_length);
        pos += source_length;
        pos += (size_t)snprintf(wrapper + pos, wrapper_cap - pos,
                                "\nfn main() -> i32 {\n    test.T t = test.T()\n");
        for (size_t i = 0; i < function_names->count; i++)
            pos += (size_t)snprintf(wrapper + pos, wrapper_cap - pos, "    %s(t)\n", function_names->items[i]);
        pos += (size_t)snprintf(wrapper + pos, wrapper_cap - pos, "    return 0\n}\n");

        snprintf(wrapper_file, sizeof(wrapper_file), "%s/transpile_test.vigil", tmpdir);
        FILE *f = fopen(wrapper_file, "wb");
        if (!f)
        {
            free(wrapper);
            fprintf(stream, "--- FAIL: transpile (%s)\n    cannot write wrapper file\n", test_file_path);
            result = 1;
            goto cleanup;
        }
        fwrite(wrapper, 1, pos, f);
        fclose(f);
        free(wrapper);
    }

    /* Step 1: run wrapper via interpreter */
    {
        const char *argv[] = {vigil_bin, "run", wrapper_file, NULL};
        if (vigil_platform_exec(NULL, argv, &interp_out, &interp_err, &exit_code, &error) != VIGIL_STATUS_OK ||
            exit_code != 0)
        {
            fprintf(stream, "--- FAIL: transpile (%s)\n    interpreter failed (exit %d)\n", test_file_path,
                    exit_code);
            result = 1;
            goto cleanup;
        }
    }

    /* Step 2: transpile to C */
    {
        char c_dir[4096];
        snprintf(c_dir, sizeof(c_dir), "%s/c-out", tmpdir);
        const char *argv[] = {vigil_bin, "transpile", wrapper_file, "-o", c_dir, NULL};
        if (vigil_platform_exec(NULL, argv, &transpile_out, &transpile_err, &exit_code, &error) != VIGIL_STATUS_OK ||
            exit_code != 0)
        {
            fprintf(stream, "--- FAIL: transpile (%s)\n    transpile failed (exit %d): %s\n", test_file_path,
                    exit_code, transpile_err ? transpile_err : "");
            result = 1;
            goto cleanup;
        }

        /* Step 3: cmake configure */
        snprintf(cmd_buf, sizeof(cmd_buf), "%s/build", c_dir);
        {
            const char *argv2[] = {"cmake", "-B", cmd_buf, "-S", c_dir, "-DCMAKE_BUILD_TYPE=Release", NULL};
            free(build_out);
            free(build_err);
            build_out = NULL;
            build_err = NULL;
            if (vigil_platform_exec(NULL, argv2, &build_out, &build_err, &exit_code, &error) != VIGIL_STATUS_OK ||
                exit_code != 0)
            {
                fprintf(stream, "--- FAIL: transpile (%s)\n    cmake configure failed (exit %d)\n", test_file_path,
                        exit_code);
                result = 1;
                goto cleanup;
            }
        }

        /* Step 4: cmake build */
        {
            free(build_out);
            free(build_err);
            build_out = NULL;
            build_err = NULL;
            const char *argv3[] = {"cmake", "--build", cmd_buf, "--config", "Release", NULL};
            if (vigil_platform_exec(NULL, argv3, &build_out, &build_err, &exit_code, &error) != VIGIL_STATUS_OK ||
                exit_code != 0)
            {
                fprintf(stream, "--- FAIL: transpile (%s)\n    cmake build failed (exit %d)\n", test_file_path,
                        exit_code);
                result = 1;
                goto cleanup;
            }
        }

        /* Step 5: run the native binary */
        {
            char exe_path[4096];
#ifdef _WIN32
            {
                int exists = 0;
                snprintf(exe_path, sizeof(exe_path), "%s/Release/vigil_app.exe", cmd_buf);
                vigil_platform_file_exists(exe_path, &exists);
                if (!exists)
                    snprintf(exe_path, sizeof(exe_path), "%s/vigil_app.exe", cmd_buf);
            }
#else
            snprintf(exe_path, sizeof(exe_path), "%s/vigil_app", cmd_buf);
#endif
            const char *argv4[] = {exe_path, NULL};
            if (vigil_platform_exec(NULL, argv4, &native_out, &native_err, &exit_code, &error) != VIGIL_STATUS_OK)
            {
                fprintf(stream, "--- FAIL: transpile (%s)\n    native binary exec failed: %s\n", test_file_path,
                        vigil_error_message(&error));
                result = 1;
                goto cleanup;
            }
        }
    }

    /* Step 6: compare outputs */
    if (interp_out == NULL)
        interp_out = cli_strdup("");
    if (native_out == NULL)
        native_out = cli_strdup("");
    if (strcmp(interp_out, native_out) != 0)
    {
        fprintf(stream, "--- FAIL: transpile (%s)\n    output mismatch\n    interpreter: %s\n    native:      %s\n",
                test_file_path, interp_out, native_out);
        result = 1;
    }
    else if (verbose)
    {
        fprintf(stream, "--- PASS: transpile (%s)\n", test_file_path);
    }

cleanup:
    free(interp_out);
    free(interp_err);
    free(transpile_out);
    free(transpile_err);
    free(build_out);
    free(build_err);
    free(native_out);
    free(native_err);
    vigil_platform_remove_all(tmpdir, &error);
    return result;
}

static int run_test_file(const char *test_file_path, const test_options_t *options, vigil_coverage_session_t *coverage,
                         int *total_pass, int *total_fail)
{
    char *source;
    size_t source_length;
    vigil_error_t error;
    str_list_t function_names;
    int file_failed;
    int exit_code;
    clock_t file_start;
    FILE *stream;
    test_result_state_t result_state;

    source = NULL;
    source_length = 0U;
    memset(&error, 0, sizeof(error));
    memset(&function_names, 0, sizeof(function_names));
    file_failed = 0;
    exit_code = 0;
    file_start = clock();
    stream = test_output_stream(options);
    result_state.test_file_path = test_file_path;
    result_state.options = options;
    result_state.stream = stream;
    result_state.total_pass = total_pass;
    result_state.total_fail = total_fail;
    result_state.file_failed = &file_failed;

    if (vigil_platform_read_file(NULL, test_file_path, &source, &source_length, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stream, "error: %s: %s\n", test_file_path, vigil_error_message(&error));
        return 1;
    }

    (void)scan_test_file_functions(test_file_path, source, source_length, &function_names);

    for (size_t index = 0U; index < function_names.count; index += 1U)
    {
        test_run_request_t request;
        const char *name;
        clock_t test_start;
        double elapsed;
        char err_msg[512];
        int result;

        name = function_names.items[index];
        if (options->filter != NULL && !test_matches_filter(name, options->filter))
            continue;

        memset(err_msg, 0, sizeof(err_msg));
        test_start = clock();
        request.test_file_path = test_file_path;
        request.original_source = source;
        request.original_length = source_length;
        request.test_name = name;
        request.options = options;
        request.coverage = coverage;
        result = run_one_test(&request, err_msg, sizeof(err_msg));
        elapsed = (double)(clock() - test_start) / CLOCKS_PER_SEC;
        report_test_result(&result_state, name, elapsed, result, err_msg);
    }

    if (file_failed)
    {
        fprintf(stream, "FAIL\t%s\t%.3fs\n", test_file_path, (double)(clock() - file_start) / CLOCKS_PER_SEC);
        exit_code = 1;
    }
    else if (function_names.count > 0U)
    {
        if (options->transpile)
        {
            if (run_transpile_validation(test_file_path, source, source_length, &function_names, stream,
                                         options->verbose) != 0)
            {
                file_failed = 1;
                *total_fail += 1;
                exit_code = 1;
            }
        }
        if (file_failed)
            fprintf(stream, "FAIL\t%s\t%.3fs\n", test_file_path, (double)(clock() - file_start) / CLOCKS_PER_SEC);
        else
            fprintf(stream, "ok  \t%s\t%.3fs\n", test_file_path, (double)(clock() - file_start) / CLOCKS_PER_SEC);
    }

    sl_free(&function_names);
    free(source);
    return exit_code;
}

int vigil_cli_run_test_command(int argc, char **argv)
{
    test_options_t options;
    str_list_t targets;
    str_list_t test_files;
    vigil_coverage_session_t coverage;
    vigil_coverage_summary_t coverage_summary;
    vigil_error_t coverage_error;
    int total_pass;
    int total_fail;
    int exit_code;
    int parse_result;
    size_t index;

    memset(&targets, 0, sizeof(targets));
    memset(&test_files, 0, sizeof(test_files));
    vigil_coverage_session_init(&coverage);
    memset(&coverage_error, 0, sizeof(coverage_error));
    total_pass = 0;
    total_fail = 0;
    exit_code = 0;

    parse_result = cmd_test_parse_args(argc, argv, &options, &targets);
    if (parse_result >= 0)
    {
        sl_free(&targets);
        vigil_coverage_session_free(&coverage);
        return parse_result;
    }

    cmd_test_add_default_target(&targets);
    cmd_test_collect_files(&targets, &test_files);
    if (test_files.count == 0U)
    {
        printf("no test files found\n");
        sl_free(&targets);
        sl_free(&test_files);
        vigil_coverage_session_free(&coverage);
        return 0;
    }

    for (index = 0U; index < test_files.count; index += 1U)
    {
        if (run_test_file(test_files.items[index], &options, &coverage, &total_pass, &total_fail) != 0)
            exit_code = 1;
    }

    cmd_test_print_summary(&options, total_pass, total_fail);
    if (options.coverage)
    {
        coverage_summary = vigil_coverage_session_total_line_summary(&coverage);
        if (options.coverage_format == VIGIL_COVERAGE_OUTPUT_JSON)
        {
            if (vigil_coverage_session_print_json(&coverage, stdout, &coverage_error) != VIGIL_STATUS_OK)
            {
                fprintf(stderr, "error: failed to emit coverage json: %s\n", vigil_error_message(&coverage_error));
                exit_code = 1;
            }
        }
        else
        {
            vigil_coverage_session_print_text(&coverage, stdout, options.verbose, test_files.count);
        }
        if (options.min_coverage_set && coverage_summary.percent + 0.0001 < options.min_coverage)
        {
            fprintf(test_output_stream(&options), "coverage threshold not met: %.1f%% < %.1f%%\n",
                    coverage_summary.percent, options.min_coverage);
            exit_code = 1;
        }
    }
    sl_free(&targets);
    sl_free(&test_files);
    vigil_coverage_session_free(&coverage);
    return exit_code;
}
