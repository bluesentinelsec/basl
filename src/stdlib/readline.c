/* VIGIL standard library: readline module.
 *
 * Provides line editing with history support.
 *
 * readline.input(string prompt) -> string
 * readline.history_add(string line)
 * readline.history_get(i32 index) -> string
 * readline.history_length() -> i32
 * readline.history_clear()
 * readline.history_load(string path)
 * readline.history_save(string path)
 */
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"
#include "platform/platform.h"

/* ── Global history (shared across all calls) ────────────────────── */

static vigil_line_history_t g_history;
static int g_history_initialized = 0;

static void ensure_history(void)
{
    if (!g_history_initialized)
    {
        vigil_line_history_init(&g_history, 1000);
        g_history_initialized = 1;
    }
}

/* ── Native callbacks ────────────────────────────────────────────── */

static vigil_status_t vigil_readline_input(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base;
    vigil_value_t prompt_val;
    const char *prompt = "";
    char buf[4096];
    vigil_status_t status;
    vigil_object_t *result = NULL;
    vigil_value_t val;

    (void)arg_count;
    ensure_history();

    base = vigil_vm_stack_depth(vm) - 1;
    prompt_val = (vigil_value_t){vigil_vm_stack_get(vm, base)};
    vigil_vm_stack_pop_n(vm, 1);

    if (vigil_nanbox_is_object(prompt_val))
    {
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(prompt_val);
        if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            prompt = vigil_string_object_c_str(obj);
        }
    }

    status = vigil_line_editor_readline(prompt, buf, sizeof(buf), &g_history, error);
    if (status != VIGIL_STATUS_OK)
    {
        /* EOF — return empty string. */
        status = vigil_string_object_new(vigil_vm_runtime(vm), "", 0, &result, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_value_init_object(&val, &result);
        return vigil_vm_stack_push(vm, &val, error);
    }

    /* Add to history if non-empty. */
    if (buf[0])
        vigil_line_history_add(&g_history, buf);

    status = vigil_string_object_new_cstr(vigil_vm_runtime(vm), buf, &result, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &result);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t vigil_readline_history_add(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base;
    vigil_value_t line_val;
    const char *line = "";

    (void)arg_count;
    (void)error;
    ensure_history();

    base = vigil_vm_stack_depth(vm) - 1;
    line_val = (vigil_value_t){vigil_vm_stack_get(vm, base)};
    vigil_vm_stack_pop_n(vm, 1);

    if (vigil_nanbox_is_object(line_val))
    {
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(line_val);
        if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            line = vigil_string_object_c_str(obj);
        }
    }

    vigil_line_history_add(&g_history, line);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_readline_history_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base;
    int64_t index;
    const char *entry;
    vigil_object_t *result = NULL;
    vigil_value_t val;
    vigil_status_t status;

    (void)arg_count;
    ensure_history();

    base = vigil_vm_stack_depth(vm) - 1;
    index = vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    vigil_vm_stack_pop_n(vm, 1);

    entry = vigil_line_history_get(&g_history, (size_t)index);
    if (!entry)
        entry = "";

    status = vigil_string_object_new_cstr(vigil_vm_runtime(vm), entry, &result, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &result);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t vigil_readline_history_length(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_value_t val;
    (void)arg_count;
    (void)error;
    ensure_history();
    vigil_value_init_int(&val, (int64_t)g_history.count);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t vigil_readline_history_clear(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)vm;
    (void)arg_count;
    (void)error;
    ensure_history();
    vigil_line_history_clear(&g_history);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_readline_history_load(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base;
    vigil_value_t path_val;
    const char *path = "";

    (void)arg_count;
    ensure_history();

    base = vigil_vm_stack_depth(vm) - 1;
    path_val = (vigil_value_t){vigil_vm_stack_get(vm, base)};
    vigil_vm_stack_pop_n(vm, 1);

    if (vigil_nanbox_is_object(path_val))
    {
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(path_val);
        if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            path = vigil_string_object_c_str(obj);
        }
    }

    return vigil_line_history_load(&g_history, path, error);
}

static vigil_status_t vigil_readline_history_save(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base;
    vigil_value_t path_val;
    const char *path = "";

    (void)arg_count;
    ensure_history();

    base = vigil_vm_stack_depth(vm) - 1;
    path_val = (vigil_value_t){vigil_vm_stack_get(vm, base)};
    vigil_vm_stack_pop_n(vm, 1);

    if (vigil_nanbox_is_object(path_val))
    {
        vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(path_val);
        if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            path = vigil_string_object_c_str(obj);
        }
    }

    return vigil_line_history_save(&g_history, path, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int vigil_readline_string_param[] = {VIGIL_TYPE_STRING};
static const int vigil_readline_i32_param[] = {VIGIL_TYPE_I32};
static const char *const readline_prompt_param_names[] = {"prompt"};
static const char *const readline_line_param_names[] = {"line"};
static const char *const readline_index_param_names[] = {"index"};
static const char *const readline_path_param_names[] = {"path"};

static const vigil_native_symbol_doc_t vigil_readline_module_doc = {
    "Interactive line input.",
    "Read user input with prompt and history support.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_readline_input_doc = {
    "Read a line of input.",
    "Displays the prompt and reads a line from the terminal.",
    "string line = readline.input(\"> \")",
};

static const vigil_native_symbol_doc_t vigil_readline_history_add_doc = {
    "Add a line to history.",
    "Stores the line for recall with history_get.",
    "readline.history_add(line)",
};

static const vigil_native_symbol_doc_t vigil_readline_history_get_doc = {
    "Get a history entry.",
    "Returns the history entry at the given index.",
    "string h = readline.history_get(0)",
};

static const vigil_native_symbol_doc_t vigil_readline_history_length_doc = {
    "Get history length.",
    "Returns the number of entries in the history.",
    "i32 n = readline.history_length()",
};

static const vigil_native_symbol_doc_t vigil_readline_history_clear_doc = {
    "Clear line history.",
    "Removes all entries from the in-memory history list.",
    "readline.history_clear()",
};

static const vigil_native_symbol_doc_t vigil_readline_history_load_doc = {
    "Load history from a file.",
    "Loads previously saved history entries from the given file path.",
    "readline.history_load(\".vigil_history\")",
};

static const vigil_native_symbol_doc_t vigil_readline_history_save_doc = {
    "Save history to a file.",
    "Writes the current in-memory history entries to the given file path.",
    "readline.history_save(\".vigil_history\")",
};

static const vigil_native_module_function_t vigil_readline_functions[] = {
    {"input", 5, vigil_readline_input, 1, vigil_readline_string_param, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     readline_prompt_param_names, NULL, NULL, &vigil_readline_input_doc},
    {"history_add", 11, vigil_readline_history_add, 1, vigil_readline_string_param, VIGIL_TYPE_VOID, 0, NULL, 0, NULL,
     NULL, 0U, readline_line_param_names, NULL, NULL, &vigil_readline_history_add_doc},
    {"history_get", 11, vigil_readline_history_get, 1, vigil_readline_i32_param, VIGIL_TYPE_STRING, 1, NULL, 0, NULL,
     NULL, 0U, readline_index_param_names, NULL, NULL, &vigil_readline_history_get_doc},
    {"history_length", 14, vigil_readline_history_length, 0, NULL, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, &vigil_readline_history_length_doc},
    {"history_clear", 13, vigil_readline_history_clear, 0, NULL, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL, 0U, NULL,
     NULL, NULL, &vigil_readline_history_clear_doc},
    {"history_load", 12, vigil_readline_history_load, 1, vigil_readline_string_param, VIGIL_TYPE_VOID, 0, NULL, 0, NULL,
     NULL, 0U, readline_path_param_names, NULL, NULL, &vigil_readline_history_load_doc},
    {"history_save", 12, vigil_readline_history_save, 1, vigil_readline_string_param, VIGIL_TYPE_VOID, 0, NULL, 0, NULL,
     NULL, 0U, readline_path_param_names, NULL, NULL, &vigil_readline_history_save_doc}};

VIGIL_API const vigil_native_module_t vigil_stdlib_readline = {
    "readline", 8, vigil_readline_functions,  sizeof(vigil_readline_functions) / sizeof(vigil_readline_functions[0]),
    NULL,       0, &vigil_readline_module_doc,
    NULL, 0U};
