/* VIGIL standard library: fmt module.
 *
 * Platform-specific stdlib modules live in src/stdlib/.  They may use
 * any system headers needed for their platform.  The core interpreter
 * in src/ never includes these files.
 */
#include <stdio.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static void vigil_fmt_print_value(FILE *stream, vigil_value_t v)
{
    if (vigil_nanbox_is_int(v))
    {
        fprintf(stream, "%lld", (long long)vigil_nanbox_decode_int(v));
    }
    else if (vigil_nanbox_is_double(v))
    {
        fprintf(stream, "%g", vigil_nanbox_decode_double(v));
    }
    else if (vigil_nanbox_is_bool(v))
    {
        fprintf(stream, "%s", vigil_nanbox_decode_bool(v) ? "true" : "false");
    }
    else if (vigil_nanbox_is_nil(v))
    {
        fprintf(stream, "nil");
    }
    else if (vigil_nanbox_is_object(v))
    {
        const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
        if (obj != NULL && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            const char *text = vigil_string_object_c_str(obj);
            size_t len = vigil_string_object_length(obj);

            if (text != NULL)
            {
                fwrite(text, 1U, len, stream);
                return;
            }
        }
        fprintf(stream, "<object>");
    }
}

/* ── native callbacks ────────────────────────────────────────────── */

static vigil_status_t vigil_fmt_println(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base;
    size_t i;

    (void)error;
    base = vigil_vm_stack_depth(vm) - arg_count;
    for (i = 0U; i < arg_count; i++)
    {
        if (i > 0U)
        {
            fputc(' ', stdout);
        }
        vigil_fmt_print_value(stdout, vigil_vm_stack_get(vm, base + i));
    }
    fputc('\n', stdout);
    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_fmt_print(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base;
    size_t i;

    (void)error;
    base = vigil_vm_stack_depth(vm) - arg_count;
    for (i = 0U; i < arg_count; i++)
    {
        if (i > 0U)
        {
            fputc(' ', stdout);
        }
        vigil_fmt_print_value(stdout, vigil_vm_stack_get(vm, base + i));
    }
    fflush(stdout);
    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_fmt_eprintln(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base;
    size_t i;

    (void)error;
    base = vigil_vm_stack_depth(vm) - arg_count;
    for (i = 0U; i < arg_count; i++)
    {
        if (i > 0U)
        {
            fputc(' ', stderr);
        }
        vigil_fmt_print_value(stderr, vigil_vm_stack_get(vm, base + i));
    }
    fputc('\n', stderr);
    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int vigil_fmt_println_params[] = {VIGIL_TYPE_STRING};
static const int vigil_fmt_print_params[] = {VIGIL_TYPE_STRING};
static const int vigil_fmt_eprintln_params[] = {VIGIL_TYPE_STRING};
static const char *const vigil_fmt_value_param_names[] = {"value"};

static const vigil_native_symbol_doc_t vigil_fmt_module_doc = {
    "Formatted output functions.",
    "The fmt module provides functions for printing to stdout and stderr.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_fmt_print_doc = {
    "Print a string to stdout without a newline.",
    NULL,
    "fmt.print(\"hello \")\nfmt.print(\"world\")",
};

static const vigil_native_symbol_doc_t vigil_fmt_println_doc = {
    "Print a string to stdout with a newline.",
    NULL,
    "fmt.println(\"Hello, world!\")",
};

static const vigil_native_symbol_doc_t vigil_fmt_eprintln_doc = {
    "Print a string to stderr with a newline.",
    NULL,
    "fmt.eprintln(\"Error: something went wrong\")",
};

static const vigil_native_module_function_t vigil_fmt_functions[] = {
    {"println", 7U, vigil_fmt_println, 1U, vigil_fmt_println_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     vigil_fmt_value_param_names, NULL, NULL, &vigil_fmt_println_doc},
    {"print", 5U, vigil_fmt_print, 1U, vigil_fmt_print_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     vigil_fmt_value_param_names, NULL, NULL, &vigil_fmt_print_doc},
    {"eprintln", 8U, vigil_fmt_eprintln, 1U, vigil_fmt_eprintln_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     vigil_fmt_value_param_names, NULL, NULL, &vigil_fmt_eprintln_doc},
};

#define VIGIL_FMT_FUNCTION_COUNT (sizeof(vigil_fmt_functions) / sizeof(vigil_fmt_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_fmt = {"fmt", 3U, vigil_fmt_functions,  VIGIL_FMT_FUNCTION_COUNT,
                                                          NULL,  0U, &vigil_fmt_module_doc};
