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
} test_options_t;

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

static const char *test_scope_root(const char *test_file_path, const char *project_root, char *buffer,
                                   size_t buffer_size)
{
    size_t length;

    if (project_root != NULL)
        return project_root;
    if (test_file_path == NULL || buffer == NULL || buffer_size == 0U)
        return NULL;

    length = strlen(test_file_path);
    if (length + 1U > buffer_size)
        return NULL;
    memcpy(buffer, test_file_path, length + 1U);
    while (length > 0U && buffer[length - 1U] != '/' && buffer[length - 1U] != '\\')
        length -= 1U;
    if (length == 0U)
    {
        snprintf(buffer, buffer_size, ".");
        return buffer;
    }
    while (length > 1U && (buffer[length - 1U] == '/' || buffer[length - 1U] == '\\'))
        length -= 1U;
    buffer[length] = '\0';
    return buffer;
}

static int run_one_test(const char *test_file_path, const char *original_source, size_t original_length,
                        const char *test_name, const test_options_t *options, vigil_coverage_session_t *coverage,
                        char *err_msg, size_t err_msg_size)
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

    runtime = NULL;
    vm = NULL;
    source_id = 0U;
    function = NULL;
    exit_code = 0;
    total_length = 0U;
    memset(&error, 0, sizeof(error));
    root = find_project_root(test_file_path, project_root, sizeof(project_root)) ? project_root : NULL;
    scope_root = test_scope_root(test_file_path, root, scope_root_buffer, sizeof(scope_root_buffer));
    combined = build_test_wrapper_source(original_source, original_length, test_name, &total_length);
    wrapper_path = build_test_wrapper_path(test_file_path);
    if (combined == NULL || wrapper_path == NULL)
    {
        snprintf(err_msg, err_msg_size, "out of memory");
        free(wrapper_path);
        free(combined);
        return 1;
    }

    if (vigil_runtime_open(&runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        snprintf(err_msg, err_msg_size, "runtime init failed");
        free(wrapper_path);
        free(combined);
        return 1;
    }
    if (vigil_vm_open(&vm, runtime, NULL, &error) != VIGIL_STATUS_OK)
    {
        free(wrapper_path);
        free(combined);
        vigil_runtime_close(&runtime);
        snprintf(err_msg, err_msg_size, "vm init failed");
        return 1;
    }

    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_value_init_nil(&result);

    if (!register_source_tree(&registry, test_file_path, root, NULL, &error))
    {
        snprintf(err_msg, err_msg_size, "%s", vigil_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }
    if (vigil_source_registry_register(&registry, wrapper_path, strlen(wrapper_path), combined, total_length,
                                       &source_id, &error) != VIGIL_STATUS_OK)
    {
        snprintf(err_msg, err_msg_size, "source registration failed");
        exit_code = 1;
        goto cleanup;
    }

    status = compile_test_source(&registry, source_id, &function, &diagnostics, &error);
    if (status != VIGIL_STATUS_OK)
    {
        if (vigil_diagnostic_list_count(&diagnostics) != 0U)
        {
            format_first_diagnostic_message(&registry, &diagnostics, err_msg, err_msg_size);
            if (err_msg[0] == '\0')
                snprintf(err_msg, err_msg_size, "compile error");
        }
        else
        {
            snprintf(err_msg, err_msg_size, "%s", vigil_error_message(&error));
        }
        vigil_object_release(&function);
        exit_code = 1;
        goto cleanup;
    }

    if (options->coverage)
    {
        status = vigil_coverage_session_track_registry(coverage, &registry, scope_root, wrapper_path,
                                                       options->include_deps, &error);
        if (status != VIGIL_STATUS_OK)
        {
            snprintf(err_msg, err_msg_size, "%s", vigil_error_message(&error));
            vigil_object_release(&function);
            exit_code = 1;
            goto cleanup;
        }
        status = vigil_coverage_session_register_function(coverage, &registry, function, &error);
        if (status != VIGIL_STATUS_OK)
        {
            snprintf(err_msg, err_msg_size, "%s", vigil_error_message(&error));
            vigil_object_release(&function);
            exit_code = 1;
            goto cleanup;
        }
        vigil_coverage_session_attach_vm(coverage, vm, &registry);
    }

    status = vigil_vm_execute_function(vm, function, &result, &error);
    if (options->coverage)
    {
        vigil_coverage_session_detach_vm(coverage, vm);
        if (status == VIGIL_STATUS_OK)
            (void)vigil_coverage_session_register_function(coverage, &registry, function, &error);
    }
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
    for (index = 2; index < argc; index += 1)
    {
        if (strcmp(argv[index], "-v") == 0 || strcmp(argv[index], "--verbose") == 0)
        {
            options->verbose = 1;
            continue;
        }
        if (strcmp(argv[index], "-run") == 0 || strcmp(argv[index], "--run") == 0)
        {
            index += 1;
            if (index >= argc)
            {
                fprintf(stderr, "error: -run requires a pattern\n");
                return 2;
            }
            options->filter = argv[index];
            continue;
        }
        if (strcmp(argv[index], "--coverage") == 0)
        {
            options->coverage = 1;
            continue;
        }
        if (strcmp(argv[index], "--include-deps") == 0)
        {
            options->include_deps = 1;
            continue;
        }
        if (strcmp(argv[index], "--format") == 0)
        {
            index += 1;
            if (index >= argc)
            {
                fprintf(stderr, "error: --format requires a value\n");
                return 2;
            }
            if (strcmp(argv[index], "json") == 0)
            {
                options->coverage_format = VIGIL_COVERAGE_OUTPUT_JSON;
                continue;
            }
            if (strcmp(argv[index], "text") == 0)
            {
                options->coverage_format = VIGIL_COVERAGE_OUTPUT_TEXT;
                continue;
            }
            fprintf(stderr, "error: unsupported coverage format '%s'\n", argv[index]);
            return 2;
        }
        if (strcmp(argv[index], "--min-coverage") == 0)
        {
            char *end;

            index += 1;
            if (index >= argc)
            {
                fprintf(stderr, "error: --min-coverage requires a percentage\n");
                return 2;
            }
            options->min_coverage = strtod(argv[index], &end);
            if (end == argv[index] || *end != '\0' || options->min_coverage < 0.0 || options->min_coverage > 100.0)
            {
                fprintf(stderr, "error: --min-coverage must be a number between 0 and 100\n");
                return 2;
            }
            options->min_coverage_set = 1;
            continue;
        }
        if (strcmp(argv[index], "-h") == 0 || strcmp(argv[index], "--help") == 0)
        {
            printf("Usage: vigil test [--run pattern] [-v] [--coverage] [path...]\n\n"
                   "Recursively finds and runs VIGIL test files (*_test.vigil).\n"
                   "In a project root, defaults to the ./test directory.\n\n"
                   "Flags:\n"
                   "  -v, --verbose           Print passing tests and verbose coverage details\n"
                   "  -run, --run             Filter test names by substring\n"
                   "      --coverage          Collect line and branch coverage\n"
                   "      --format            Coverage output format: text|json\n"
                   "      --min-coverage      Fail if total line coverage is below N percent\n"
                   "      --include-deps      Include imported user modules outside the project root\n");
            return 0;
        }
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

    source = NULL;
    source_length = 0U;
    memset(&error, 0, sizeof(error));
    memset(&function_names, 0, sizeof(function_names));
    file_failed = 0;
    exit_code = 0;
    file_start = clock();
    stream = test_output_stream(options);

    if (vigil_platform_read_file(NULL, test_file_path, &source, &source_length, &error) != VIGIL_STATUS_OK)
    {
        fprintf(stream, "error: %s: %s\n", test_file_path, vigil_error_message(&error));
        return 1;
    }

    {
        vigil_runtime_t *runtime;
        vigil_source_registry_t registry;
        vigil_source_id_t source_id;

        runtime = NULL;
        source_id = 0U;
        if (vigil_runtime_open(&runtime, NULL, NULL) == VIGIL_STATUS_OK)
        {
            vigil_source_registry_init(&registry, runtime);
            if (vigil_source_registry_register(&registry, test_file_path, strlen(test_file_path), source, source_length,
                                               &source_id, NULL) == VIGIL_STATUS_OK)
            {
                scan_test_functions(runtime, &registry, source_id, &function_names);
            }
            vigil_source_registry_free(&registry);
            vigil_runtime_close(&runtime);
        }
    }

    for (size_t index = 0U; index < function_names.count; index += 1U)
    {
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
        result = run_one_test(test_file_path, source, source_length, name, options, coverage, err_msg, sizeof(err_msg));
        elapsed = (double)(clock() - test_start) / CLOCKS_PER_SEC;
        if (result == 0)
        {
            *total_pass += 1;
            if (options->verbose)
                fprintf(stream, "=== RUN   %s\n--- PASS: %s (%.3fs)\n", name, name, elapsed);
        }
        else
        {
            *total_fail += 1;
            file_failed = 1;
            fprintf(stream, "--- FAIL: %s (%s)\n    %s\n", name, test_file_path, err_msg);
        }
    }

    if (file_failed)
    {
        fprintf(stream, "FAIL\t%s\t%.3fs\n", test_file_path, (double)(clock() - file_start) / CLOCKS_PER_SEC);
        exit_code = 1;
    }
    else if (function_names.count > 0U)
    {
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
