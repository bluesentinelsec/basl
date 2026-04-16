/* VIGIL standard library: args module.
 *
 * Provides access to CLI arguments and an argparse builder.
 *
 * Module functions:
 *   args.count() -> i32           returns argument count
 *   args.at(i32 index) -> string  returns argument at index
 *
 * Parser class (builder pattern):
 *   args.Parser.new(string prog, string desc) -> Parser
 *   p.flag(string name, string short, string desc) -> Parser
 *   p.option(string name, string short, string desc, string default) -> Parser
 *   p.option_int(string name, string short, string desc, i32 default) -> Parser
 *   p.option_f64(string name, string short, string desc, f64 default) -> Parser
 *   p.option_multi(string name, string short, string desc) -> Parser
 *   p.option_choice(string name, string short, string desc, string choices, string default) -> Parser
 *   p.required() -> Parser
 *   p.positional(string name, string desc) -> Parser
 *   p.required_positional(string name, string desc) -> Parser
 *   p.positional_star(string name, string desc) -> Parser
 *   p.positional_plus(string name, string desc) -> Parser
 *   p.positional_optional(string name, string desc) -> Parser
 *   p.version(string ver) -> Parser
 *   p.subcommand(string name, string desc) -> Parser
 *   p.env(string env_var) -> Parser
 *   p.config(string key) -> Parser
 *   p.parse() -> err
 *   p.load_config(string path) -> err
 *   p.get(string name) -> string
 *   p.get_bool(string name) -> bool
 *   p.get_int(string name) -> i32
 *   p.get_f64(string name) -> f64
 *   p.get_multi(string name) -> array<string>
 *   p.get_positionals() -> array<string>
 *   p.get_subcommand() -> string
 *   p.help() -> string
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

/* ── Allocator helpers ───────────────────────────────────────────── */

static const vigil_allocator_t *args_get_alloc(vigil_vm_t *vm)
{
    return vigil_runtime_allocator(vigil_vm_runtime(vm));
}

static void args_dealloc(vigil_vm_t *vm, void *ptr)
{
    const vigil_allocator_t *a = args_get_alloc(vm);
    if (a)
        a->deallocate(a->user_data, ptr);
    else
        free(ptr);
}

/* ── Parser field indices ────────────────────────────────────────── */

enum
{
    F_PROG = 0,       /* string */
    F_DESC,           /* string */
    F_NAMES,          /* array<string> */
    F_SHORTS,         /* array<string> */
    F_TYPES,          /* array<string> */
    F_DEFAULTS,       /* array<string> */
    F_DESCS,          /* array<string> */
    F_REQUIRED,       /* array<string> */
    F_POS_NAMES,      /* array<string> */
    F_POS_DESCS,      /* array<string> */
    F_VALUES,         /* array<string>  key=value pairs after parse */
    F_POSITIONALS,    /* array<string>  positional args after parse */
    F_VERSION_STR,    /* string */
    F_POS_REQUIRED,   /* array<string> */
    F_POS_NARGS,      /* array<string> */
    F_SUBCOMMANDS,    /* array<string>  "name:desc" pairs */
    F_SUB_NAME,       /* string */
    F_ENV_VARS,       /* array<string>  "name=ENV_VAR" pairs */
    F_CONFIG_KEYS,    /* array<string>  "name=config_key" pairs */
    F_CONFIG_DATA,    /* array<string>  "key=value" pairs from loaded config file */
    FIELD_COUNT
};

/* ── Helpers ─────────────────────────────────────────────────────── */

static vigil_object_t *get_self(vigil_vm_t *vm, size_t base)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base);
    return (vigil_object_t *)vigil_nanbox_decode_ptr(v);
}

static vigil_object_t *get_field_obj(vigil_object_t *self, size_t idx)
{
    vigil_value_t v;
    vigil_instance_object_get_field(self, idx, &v);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    return obj;
}

static const char *get_field_str(vigil_object_t *self, size_t idx)
{
    vigil_object_t *obj = get_field_obj(self, idx);
    if (obj == NULL)
        return "";
    return vigil_string_object_c_str(obj);
}

static const char *get_string_val(vigil_vm_t *vm, size_t slot)
{
    vigil_value_t v = vigil_vm_stack_get(vm, slot);
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (obj == NULL)
        return "";
    return vigil_string_object_c_str(obj);
}

static vigil_status_t make_string(vigil_runtime_t *rt, const char *s, vigil_value_t *out, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_string_object_new_cstr(rt, s, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_init_object(out, &obj);
    return VIGIL_STATUS_OK;
}

static vigil_status_t make_empty_array(vigil_runtime_t *rt, vigil_value_t *out, vigil_error_t *error)
{
    vigil_object_t *arr = NULL;
    vigil_status_t st = vigil_array_object_new(rt, NULL, 0, &arr, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_init_object(out, &arr);
    return VIGIL_STATUS_OK;
}

static vigil_status_t array_push_str(vigil_object_t *arr, vigil_runtime_t *rt, const char *s, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_status_t st = make_string(rt, s, &v, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    st = vigil_array_object_append(arr, &v, error);
    vigil_value_release(&v);
    return st;
}

static const char *array_get_str(vigil_object_t *arr, size_t idx)
{
    vigil_value_t v;
    if (!vigil_array_object_get(arr, idx, &v))
        return "";
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    const char *s = (obj != NULL) ? vigil_string_object_c_str(obj) : "";
    vigil_value_release(&v);
    return s;
}

/* Push self back as return value (for builder pattern). */
static vigil_status_t return_self(vigil_vm_t *vm, vigil_object_t *self, size_t arg_count, vigil_error_t *error)
{
    vigil_object_retain(self);
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t v = vigil_nanbox_encode_object(self);
    vigil_status_t st = vigil_vm_stack_push(vm, &v, error);
    vigil_object_release(&self);
    return st;
}

/* ── Module-level functions (count, at) ──────────────────────────── */

static vigil_status_t vigil_args_count(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    const char *const *argv = NULL;
    size_t argc = 0;
    vigil_value_t val;
    (void)arg_count;
    (void)error;
    vigil_vm_get_args(vm, &argv, &argc);
    vigil_value_init_int(&val, (int64_t)argc);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t vigil_args_at(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    const char *const *argv = NULL;
    size_t argc = 0;
    size_t base;
    int64_t index;
    vigil_value_t val;
    vigil_object_t *str = NULL;
    vigil_status_t status;

    (void)arg_count;
    base = vigil_vm_stack_depth(vm) - 1;
    index = vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base)});
    vigil_vm_stack_pop_n(vm, 1);
    vigil_vm_get_args(vm, &argv, &argc);

    if (index < 0 || (size_t)index >= argc)
    {
        status = vigil_string_object_new(vigil_vm_runtime(vm), "", 0, &str, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_value_init_object(&val, &str);
        status = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return status;
    }

    status = vigil_string_object_new_cstr(vigil_vm_runtime(vm), argv[index], &str, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &str);
    status = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return status;
}

/* ── Parser.new (static factory) ─────────────────────────────────── */

static vigil_status_t parser_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [class_index, prog_str, desc_str] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t ci = (size_t)vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base));
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_value_t fields[FIELD_COUNT];
    vigil_object_t *inst = NULL;
    vigil_value_t result;
    vigil_status_t s;
    size_t i;

    /* Copy prog and desc strings from stack */
    fields[F_PROG] = vigil_value_copy(&(vigil_value_t){vigil_vm_stack_get(vm, base + 1)});
    fields[F_DESC] = vigil_value_copy(&(vigil_value_t){vigil_vm_stack_get(vm, base + 2)});

    /* Initialize string fields to empty strings */
    s = make_string(rt, "", &fields[F_VERSION_STR], error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    s = make_string(rt, "", &fields[F_SUB_NAME], error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;

    /* Create empty arrays for all array fields */
    for (i = F_NAMES; i <= F_POSITIONALS; i++)
    {
        s = make_empty_array(rt, &fields[i], error);
        if (s != VIGIL_STATUS_OK)
            goto cleanup;
    }
    for (i = F_POS_REQUIRED; i <= F_POS_NARGS; i++)
    {
        s = make_empty_array(rt, &fields[i], error);
        if (s != VIGIL_STATUS_OK)
            goto cleanup;
    }
    s = make_empty_array(rt, &fields[F_SUBCOMMANDS], error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    s = make_empty_array(rt, &fields[F_ENV_VARS], error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    s = make_empty_array(rt, &fields[F_CONFIG_KEYS], error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    s = make_empty_array(rt, &fields[F_CONFIG_DATA], error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;

    s = vigil_instance_object_new(rt, ci, fields, FIELD_COUNT, &inst, error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_init_object(&result, &inst);
    s = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    for (i = 0; i < FIELD_COUNT; i++)
        vigil_value_release(&fields[i]);
    return s;

cleanup:
    for (i = 0; i < FIELD_COUNT; i++)
        vigil_value_release(&fields[i]);
    return s;
}

/* ── Builder methods ─────────────────────────────────────────────── */

/* Append one option definition to the parallel arrays. */
static vigil_status_t append_opt(vigil_object_t *self, vigil_runtime_t *rt, const char *name, const char *sht,
                                 const char *typ, const char *def, const char *desc, const char *req,
                                 vigil_error_t *error)
{
    vigil_status_t s;
    s = array_push_str(get_field_obj(self, F_NAMES), rt, name, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = array_push_str(get_field_obj(self, F_SHORTS), rt, sht, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = array_push_str(get_field_obj(self, F_TYPES), rt, typ, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = array_push_str(get_field_obj(self, F_DEFAULTS), rt, def, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = array_push_str(get_field_obj(self, F_DESCS), rt, desc, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return array_push_str(get_field_obj(self, F_REQUIRED), rt, req, error);
}

static vigil_status_t parser_flag(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    vigil_status_t s = append_opt(self, vigil_vm_runtime(vm), name, sht, "bool", "false", desc, "false", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_option(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    const char *def = get_string_val(vm, base + 4);
    vigil_status_t s = append_opt(self, vigil_vm_runtime(vm), name, sht, "string", def, desc, "false", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_option_int(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, name, short, desc, i32_default] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    int32_t def_int = vigil_nanbox_decode_i32(vigil_vm_stack_get(vm, base + 4));
    char def[32];
    snprintf(def, sizeof(def), "%d", (int)def_int);
    vigil_status_t s = append_opt(self, vigil_vm_runtime(vm), name, sht, "int", def, desc, "false", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_option_f64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, name, short, desc, f64_default] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    double def_f64 = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, base + 4));
    char def[64];
    snprintf(def, sizeof(def), "%g", def_f64);
    vigil_status_t s = append_opt(self, vigil_vm_runtime(vm), name, sht, "f64", def, desc, "false", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_option_multi(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    vigil_status_t s = append_opt(self, vigil_vm_runtime(vm), name, sht, "multi", "", desc, "false", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_option_choice(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, name, short, desc, choices, default] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    const char *choices = get_string_val(vm, base + 4);
    const char *def = get_string_val(vm, base + 5);
    char typ[280];
    snprintf(typ, sizeof(typ), "choice:%s", choices);
    vigil_status_t s = append_opt(self, vigil_vm_runtime(vm), name, sht, typ, def, desc, "false", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_mark_required(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    vigil_object_t *req_arr = get_field_obj(self, F_REQUIRED);
    size_t len = vigil_array_object_length(req_arr);
    if (len > 0)
    {
        vigil_value_t v;
        vigil_status_t s = make_string(vigil_vm_runtime(vm), "true", &v, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = vigil_array_object_set(req_arr, len - 1, &v, error);
        vigil_value_release(&v);
        if (s != VIGIL_STATUS_OK)
            return s;
    }
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_version(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, ver_string] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    vigil_value_t ver = vigil_value_copy(&(vigil_value_t){vigil_vm_stack_get(vm, base + 1)});
    vigil_status_t s = vigil_instance_object_set_field(self, F_VERSION_STR, &ver, error);
    vigil_value_release(&ver);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

/* ── Positional builder methods ──────────────────────────────────── */

/* Internal helper to add a positional with required and nargs flags. */
static vigil_status_t add_positional(vigil_object_t *self, vigil_runtime_t *rt, const char *name, const char *desc,
                                     const char *req, const char *nargs, vigil_error_t *error)
{
    vigil_status_t s;
    s = array_push_str(get_field_obj(self, F_POS_NAMES), rt, name, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = array_push_str(get_field_obj(self, F_POS_DESCS), rt, desc, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = array_push_str(get_field_obj(self, F_POS_REQUIRED), rt, req, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return array_push_str(get_field_obj(self, F_POS_NARGS), rt, nargs, error);
}

static vigil_status_t parser_positional(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *desc = get_string_val(vm, base + 2);
    vigil_status_t s = add_positional(self, vigil_vm_runtime(vm), name, desc, "false", "1", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_required_positional(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *desc = get_string_val(vm, base + 2);
    vigil_status_t s = add_positional(self, vigil_vm_runtime(vm), name, desc, "true", "1", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_positional_star(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *desc = get_string_val(vm, base + 2);
    vigil_status_t s = add_positional(self, vigil_vm_runtime(vm), name, desc, "false", "*", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_positional_plus(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *desc = get_string_val(vm, base + 2);
    vigil_status_t s = add_positional(self, vigil_vm_runtime(vm), name, desc, "false", "+", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_positional_optional(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *desc = get_string_val(vm, base + 2);
    vigil_status_t s = add_positional(self, vigil_vm_runtime(vm), name, desc, "false", "?", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

/* ── Subcommand builder ──────────────────────────────────────────── */

static vigil_status_t parser_subcommand(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, name, desc] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *desc = get_string_val(vm, base + 2);
    char buf[512];
    snprintf(buf, sizeof(buf), "%s:%s", name, desc);
    vigil_status_t s = array_push_str(get_field_obj(self, F_SUBCOMMANDS), vigil_vm_runtime(vm), buf, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

/* ── Env var fallback builder ────────────────────────────────────── */

static vigil_status_t parser_env(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, env_var] — applies to last declared option */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *env_var = get_string_val(vm, base + 1);
    vigil_object_t *names_arr = get_field_obj(self, F_NAMES);
    size_t len = vigil_array_object_length(names_arr);
    if (len > 0)
    {
        const char *name = array_get_str(names_arr, len - 1);
        char buf[512];
        snprintf(buf, sizeof(buf), "%s=%s", name, env_var);
        vigil_status_t s = array_push_str(get_field_obj(self, F_ENV_VARS), vigil_vm_runtime(vm), buf, error);
        if (s != VIGIL_STATUS_OK)
            return s;
    }
    return return_self(vm, self, arg_count, error);
}

/* ── Config key builder ──────────────────────────────────────────── */

static vigil_status_t parser_config(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, key] — applies to last declared option */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *key = get_string_val(vm, base + 1);
    vigil_object_t *names_arr = get_field_obj(self, F_NAMES);
    size_t len = vigil_array_object_length(names_arr);
    if (len > 0)
    {
        const char *name = array_get_str(names_arr, len - 1);
        char buf[512];
        snprintf(buf, sizeof(buf), "%s=%s", name, key);
        vigil_status_t s = array_push_str(get_field_obj(self, F_CONFIG_KEYS), vigil_vm_runtime(vm), buf, error);
        if (s != VIGIL_STATUS_OK)
            return s;
    }
    return return_self(vm, self, arg_count, error);
}

/* ── parse() — the core argument parser ──────────────────────────── */

/* Forward declaration: parser_help is used by parse() for --help. */
static vigil_status_t parser_help(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error);

/* Find option index by long or short name. Returns -1 if not found. */
static int find_opt_idx(vigil_object_t *names_arr, vigil_object_t *shorts_arr, const char *key)
{
    size_t len = vigil_array_object_length(names_arr);
    for (size_t i = 0; i < len; i++)
    {
        const char *n = array_get_str(names_arr, i);
        const char *s = array_get_str(shorts_arr, i);
        if (strcmp(n, key) == 0 || (s[0] != '\0' && strcmp(s, key) == 0))
            return (int)i;
    }
    return -1;
}

static vigil_status_t parser_update_vals_entry(vigil_object_t *vals_arr, vigil_runtime_t *rt, const char *name,
                                               const char *val, vigil_error_t *error)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s=%s", name, val);
    size_t nlen = strlen(name);
    size_t vlen = vigil_array_object_length(vals_arr);
    for (size_t vi = 0; vi < vlen; vi++)
    {
        const char *entry = array_get_str(vals_arr, vi);
        if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=')
        {
            vigil_value_t sv;
            vigil_status_t s = make_string(rt, buf, &sv, error);
            if (s != VIGIL_STATUS_OK)
                return s;
            s = vigil_array_object_set(vals_arr, vi, &sv, error);
            vigil_value_release(&sv);
            return s;
        }
    }
    return VIGIL_STATUS_OK;
}

static int parser_check_required(vigil_object_t *vals_arr, vigil_object_t *names_arr, vigil_object_t *types_arr,
                                 vigil_object_t *req_arr, size_t opt_count, char *err_buf, size_t err_size)
{
    for (size_t i = 0; i < opt_count; i++)
    {
        const char *req = array_get_str(req_arr, i);
        if (strcmp(req, "true") != 0)
            continue;
        const char *name = array_get_str(names_arr, i);
        const char *typ = array_get_str(types_arr, i);
        if (strcmp(typ, "bool") == 0)
            continue;

        char prefix[280];
        if (strcmp(typ, "multi") == 0)
            snprintf(prefix, sizeof(prefix), "__multi__%s=", name);
        else
            snprintf(prefix, sizeof(prefix), "%s=", name);
        size_t plen = strlen(prefix);
        int found = 0;
        size_t vlen = vigil_array_object_length(vals_arr);
        for (size_t vi = 0; vi < vlen; vi++)
        {
            const char *entry = array_get_str(vals_arr, vi);
            if (strncmp(entry, prefix, plen) == 0)
            {
                if (strcmp(typ, "multi") == 0 || entry[plen] != '\0')
                    found = 1;
                break;
            }
        }
        if (!found)
        {
            snprintf(err_buf, err_size, "required option --%s not provided", name);
            return 0;
        }
    }
    return 1;
}

static const char *parser_extract_key(const char *arg, char *key_buf, size_t key_size, const char **inline_val)
{
    *inline_val = NULL;
    if (arg[0] == '-' && arg[1] == '-')
    {
        const char *eq = strchr(arg + 2, '=');
        if (eq != NULL)
        {
            size_t klen = (size_t)(eq - (arg + 2));
            if (klen >= key_size)
                klen = key_size - 1;
            memcpy(key_buf, arg + 2, klen);
            key_buf[klen] = '\0';
            *inline_val = eq + 1;
            return key_buf;
        }
        return arg + 2;
    }
    return arg + 1;
}

/* Check if a value is in a pipe-separated choices string. */
static int validate_choice(const char *choices, const char *val)
{
    size_t vlen = strlen(val);
    const char *p = choices;
    while (*p)
    {
        const char *sep = strchr(p, '|');
        size_t clen = sep ? (size_t)(sep - p) : strlen(p);
        if (clen == vlen && strncmp(p, val, clen) == 0)
            return 1;
        if (!sep)
            break;
        p = sep + 1;
    }
    return 0;
}

/* Look up env var for an option name. Returns the env value or NULL. */
static const char *lookup_env_var(vigil_object_t *env_arr, const char *name)
{
    size_t nlen = strlen(name);
    size_t elen = vigil_array_object_length(env_arr);
    for (size_t i = 0; i < elen; i++)
    {
        const char *entry = array_get_str(env_arr, i);
        if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=')
        {
            const char *env_name = entry + nlen + 1;
            const char *val = getenv(env_name);
            return val;
        }
    }
    return NULL;
}

static vigil_status_t parser_parse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self] — no argc parameter, always parse all args from index 0 */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_status_t s;

    const char *const *argv = NULL;
    size_t argc = 0;
    vigil_vm_get_args(vm, &argv, &argc);

    vigil_object_t *names_arr = get_field_obj(self, F_NAMES);
    vigil_object_t *shorts_arr = get_field_obj(self, F_SHORTS);
    vigil_object_t *types_arr = get_field_obj(self, F_TYPES);
    vigil_object_t *defaults_arr = get_field_obj(self, F_DEFAULTS);
    vigil_object_t *req_arr = get_field_obj(self, F_REQUIRED);
    vigil_object_t *subcmds_arr = get_field_obj(self, F_SUBCOMMANDS);
    vigil_object_t *env_arr = get_field_obj(self, F_ENV_VARS);
    size_t opt_count = vigil_array_object_length(names_arr);
    size_t subcmd_count = vigil_array_object_length(subcmds_arr);

    /* Clear and rebuild values + positionals arrays */
    vigil_object_t *vals_arr = NULL;
    vigil_object_t *pos_arr = NULL;
    s = vigil_array_object_new(rt, NULL, 0, &vals_arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = vigil_array_object_new(rt, NULL, 0, &pos_arr, error);
    if (s != VIGIL_STATUS_OK)
    {
        vigil_object_release(&vals_arr);
        return s;
    }

    char err_buf[256];

    /* Built-in --help/-h check */
    for (size_t hi = 0; hi < argc; hi++)
    {
        if (strcmp(argv[hi], "--help") == 0 || strcmp(argv[hi], "-h") == 0)
        {
            /* Print help to stdout using the help generation logic */
            vigil_object_release(&vals_arr);
            vigil_object_release(&pos_arr);

            /* Call parser_help to get the text, then print it */
            /* We do it inline to avoid stack manipulation issues */
            vigil_vm_stack_pop_n(vm, arg_count);
            {
                /* Push self for help call */
                vigil_object_retain(self);
                vigil_value_t sv = vigil_nanbox_encode_object(self);
                s = vigil_vm_stack_push(vm, &sv, error);
                vigil_object_release(&self);
                if (s != VIGIL_STATUS_OK)
                    return s;
                s = parser_help(vm, 1, error);
                if (s != VIGIL_STATUS_OK)
                    return s;
                /* Pop the help string, print it */
                vigil_value_t hv = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1);
                vigil_object_t *hobj = (vigil_object_t *)vigil_nanbox_decode_ptr(hv);
                if (hobj)
                    printf("%s\n", vigil_string_object_c_str(hobj));
                vigil_vm_stack_pop_n(vm, 1);
            }
            {
                vigil_object_t *err_obj = NULL;
                s = vigil_error_object_new_cstr(rt, "help requested", 1, &err_obj, error);
                if (s != VIGIL_STATUS_OK)
                    return s;
                vigil_value_t ev;
                vigil_value_init_object(&ev, &err_obj);
                s = vigil_vm_stack_push(vm, &ev, error);
                vigil_value_release(&ev);
            }
            return s;
        }
    }

    /* Built-in --version/-V check */
    {
        const char *ver = get_field_str(self, F_VERSION_STR);
        if (ver[0] != '\0')
        {
            for (size_t vi = 0; vi < argc; vi++)
            {
                if (strcmp(argv[vi], "--version") == 0 || strcmp(argv[vi], "-V") == 0)
                {
                    vigil_object_release(&vals_arr);
                    vigil_object_release(&pos_arr);
                    printf("%s\n", ver);
                    vigil_vm_stack_pop_n(vm, arg_count);
                    {
                        vigil_object_t *err_obj = NULL;
                        s = vigil_error_object_new_cstr(rt, "version requested", 1, &err_obj, error);
                        if (s != VIGIL_STATUS_OK)
                            return s;
                        vigil_value_t ev;
                        vigil_value_init_object(&ev, &err_obj);
                        s = vigil_vm_stack_push(vm, &ev, error);
                        vigil_value_release(&ev);
                    }
                    return s;
                }
            }
        }
    }

    /* Set defaults */
    for (size_t i = 0; i < opt_count; i++)
    {
        const char *name = array_get_str(names_arr, i);
        const char *typ = array_get_str(types_arr, i);
        const char *def = array_get_str(defaults_arr, i);
        char buf[512];
        if (strcmp(typ, "bool") == 0)
            snprintf(buf, sizeof(buf), "%s=false", name);
        else if (strcmp(typ, "multi") == 0)
            continue;
        else
            snprintf(buf, sizeof(buf), "%s=%s", name, def);
        s = array_push_str(vals_arr, rt, buf, error);
        if (s != VIGIL_STATUS_OK)
            goto fail;
    }

    /* Track which options were explicitly set on CLI */
    /* We use a simple bitmask approach — allocate a small array */
    int *cli_set = NULL;
    if (opt_count > 0)
    {
        cli_set = (int *)args_get_alloc(vm)->allocate(args_get_alloc(vm)->user_data, opt_count * sizeof(int));
        if (cli_set)
            memset(cli_set, 0, opt_count * sizeof(int));
        if (!cli_set)
        {
            snprintf(err_buf, sizeof(err_buf), "out of memory");
            goto err_out;
        }
    }

    /* Parse loop */
    size_t pos = 0;
    int past_dd = 0;
    int subcmd_found = 0;

    while (pos < argc)
    {
        const char *arg = argv[pos];

        if (!past_dd && strcmp(arg, "--") == 0)
        {
            past_dd = 1;
            pos++;
            continue;
        }

        if (past_dd || arg[0] != '-')
        {
            /* Check for subcommand: first non-flag arg when subcommands are declared */
            if (!subcmd_found && subcmd_count > 0)
            {
                int matched = 0;
                for (size_t si = 0; si < subcmd_count; si++)
                {
                    const char *entry = array_get_str(subcmds_arr, si);
                    const char *colon = strchr(entry, ':');
                    size_t nlen = colon ? (size_t)(colon - entry) : strlen(entry);
                    if (strlen(arg) == nlen && strncmp(arg, entry, nlen) == 0)
                    {
                        matched = 1;
                        break;
                    }
                }
                if (!matched)
                {
                    args_dealloc(vm, cli_set);
                    snprintf(err_buf, sizeof(err_buf), "unknown subcommand: %s", arg);
                    goto err_out;
                }
                /* Store subcommand name */
                vigil_value_t sv;
                s = make_string(rt, arg, &sv, error);
                if (s != VIGIL_STATUS_OK)
                {
                    args_dealloc(vm, cli_set);
                    goto fail;
                }
                s = vigil_instance_object_set_field(self, F_SUB_NAME, &sv, error);
                vigil_value_release(&sv);
                if (s != VIGIL_STATUS_OK)
                {
                    args_dealloc(vm, cli_set);
                    goto fail;
                }
                subcmd_found = 1;
                /* Store subcommand and all remaining args as positionals */
                for (size_t ri = pos; ri < argc; ri++)
                {
                    s = array_push_str(pos_arr, rt, argv[ri], error);
                    if (s != VIGIL_STATUS_OK)
                    {
                        args_dealloc(vm, cli_set);
                        goto fail;
                    }
                }
                pos = argc; /* done */
                continue;
            }

            s = array_push_str(pos_arr, rt, arg, error);
            if (s != VIGIL_STATUS_OK)
            {
                args_dealloc(vm, cli_set);
                goto fail;
            }
            pos++;
            continue;
        }

        /* Extract key and optional inline value from --key=value */
        const char *inline_val = NULL;
        char key_buf[256];
        const char *key = parser_extract_key(arg, key_buf, sizeof(key_buf), &inline_val);

        int idx = find_opt_idx(names_arr, shorts_arr, key);
        if (idx < 0)
        {
            args_dealloc(vm, cli_set);
            snprintf(err_buf, sizeof(err_buf), "unknown option: %s", arg);
            goto err_out;
        }

        const char *name = array_get_str(names_arr, (size_t)idx);
        const char *typ = array_get_str(types_arr, (size_t)idx);
        cli_set[idx] = 1;

        if (strcmp(typ, "bool") == 0)
        {
            s = parser_update_vals_entry(vals_arr, rt, name, "true", error);
            if (s != VIGIL_STATUS_OK)
            {
                args_dealloc(vm, cli_set);
                goto fail;
            }
            pos++;
        }
        else
        {
            const char *val = inline_val;
            if (val == NULL)
            {
                pos++;
                if (pos >= argc)
                {
                    args_dealloc(vm, cli_set);
                    snprintf(err_buf, sizeof(err_buf), "option --%s requires a value", name);
                    goto err_out;
                }
                val = argv[pos];
            }
            /* Validate int */
            if (strcmp(typ, "int") == 0)
            {
                char *end = NULL;
                errno = 0;
                (void)strtol(val, &end, 10);
                if (errno != 0 || end == val || *end != '\0')
                {
                    args_dealloc(vm, cli_set);
                    snprintf(err_buf, sizeof(err_buf), "option --%s requires an integer, got: %s", name, val);
                    goto err_out;
                }
            }
            /* Validate f64 */
            if (strcmp(typ, "f64") == 0)
            {
                char *end = NULL;
                errno = 0;
                (void)strtod(val, &end);
                if (errno != 0 || end == val || *end != '\0')
                {
                    args_dealloc(vm, cli_set);
                    snprintf(err_buf, sizeof(err_buf), "option --%s requires a number, got: %s", name, val);
                    goto err_out;
                }
            }
            /* Validate choice */
            if (strncmp(typ, "choice:", 7) == 0)
            {
                const char *choices = typ + 7;
                if (!validate_choice(choices, val))
                {
                    args_dealloc(vm, cli_set);
                    snprintf(err_buf, sizeof(err_buf), "option --%s must be one of: %s, got: %s", name, choices, val);
                    goto err_out;
                }
            }
            if (strcmp(typ, "multi") == 0)
            {
                char buf[512];
                snprintf(buf, sizeof(buf), "__multi__%s=%s", name, val);
                s = array_push_str(vals_arr, rt, buf, error);
                if (s != VIGIL_STATUS_OK)
                {
                    args_dealloc(vm, cli_set);
                    goto fail;
                }
            }
            else
            {
                s = parser_update_vals_entry(vals_arr, rt, name, val, error);
                if (s != VIGIL_STATUS_OK)
                {
                    args_dealloc(vm, cli_set);
                    goto fail;
                }
            }
            pos++;
        }
    }

    /* Environment variable fallback: for options not set on CLI */
    for (size_t i = 0; i < opt_count; i++)
    {
        if (cli_set && cli_set[i])
            continue;
        const char *typ = array_get_str(types_arr, i);
        if (strcmp(typ, "bool") == 0 || strcmp(typ, "multi") == 0)
            continue;
        const char *name = array_get_str(names_arr, i);
        const char *env_val = lookup_env_var(env_arr, name);
        if (env_val)
        {
            s = parser_update_vals_entry(vals_arr, rt, name, env_val, error);
            if (s != VIGIL_STATUS_OK)
            {
                args_dealloc(vm, cli_set);
                goto fail;
            }
            cli_set[i] = 1; /* mark as set so config won't override */
        }
    }

    /* Config file fallback: for options not set on CLI or via env var */
    {
        vigil_object_t *config_data_arr = get_field_obj(self, F_CONFIG_DATA);
        size_t cd_count = vigil_array_object_length(config_data_arr);
        for (size_t cdi = 0; cdi < cd_count; cdi++)
        {
            const char *entry = array_get_str(config_data_arr, cdi);
            const char *ceq = strchr(entry, '=');
            if (!ceq)
                continue;
            size_t nlen = (size_t)(ceq - entry);
            const char *cval = ceq + 1;
            /* Find the option index */
            for (size_t oi = 0; oi < opt_count; oi++)
            {
                const char *n = array_get_str(names_arr, oi);
                if (strlen(n) == nlen && strncmp(n, entry, nlen) == 0)
                {
                    if (!cli_set[oi])
                    {
                        s = parser_update_vals_entry(vals_arr, rt, n, cval, error);
                        if (s != VIGIL_STATUS_OK)
                        {
                            args_dealloc(vm, cli_set);
                            goto fail;
                        }
                        cli_set[oi] = 1;
                    }
                    break;
                }
            }
        }
    }

    args_dealloc(vm, cli_set);
    cli_set = NULL;

    /* Check required options */
    if (!parser_check_required(vals_arr, names_arr, types_arr, req_arr, opt_count, err_buf, sizeof(err_buf)))
        goto err_out;

    /* Check required positionals and nargs */
    {
        vigil_object_t *pn_arr = get_field_obj(self, F_POS_NAMES);
        vigil_object_t *pr_arr = get_field_obj(self, F_POS_REQUIRED);
        vigil_object_t *pnargs_arr = get_field_obj(self, F_POS_NARGS);
        size_t pos_decl_count = vigil_array_object_length(pn_arr);
        size_t pos_actual_count = vigil_array_object_length(pos_arr);
        size_t pos_consumed = 0;

        for (size_t pi = 0; pi < pos_decl_count; pi++)
        {
            const char *pname = array_get_str(pn_arr, pi);
            const char *preq = array_get_str(pr_arr, pi);
            const char *pnarg = array_get_str(pnargs_arr, pi);

            if (strcmp(pnarg, "1") == 0)
            {
                if (strcmp(preq, "true") == 0 && pos_consumed >= pos_actual_count)
                {
                    snprintf(err_buf, sizeof(err_buf), "required positional argument <%s> not provided", pname);
                    goto err_out;
                }
                if (pos_consumed < pos_actual_count)
                    pos_consumed++;
            }
            else if (strcmp(pnarg, "+") == 0)
            {
                /* One or more: need at least one remaining */
                size_t remaining = (pos_consumed < pos_actual_count) ? pos_actual_count - pos_consumed : 0;
                if (remaining < 1)
                {
                    snprintf(err_buf, sizeof(err_buf), "positional argument <%s> requires one or more values", pname);
                    goto err_out;
                }
                pos_consumed = pos_actual_count; /* consumes all remaining */
            }
            else if (strcmp(pnarg, "*") == 0)
            {
                pos_consumed = pos_actual_count; /* consumes all remaining */
            }
            else if (strcmp(pnarg, "?") == 0)
            {
                if (pos_consumed < pos_actual_count)
                    pos_consumed++;
            }
        }
    }

    /* Store results in instance fields */
    {
        vigil_value_t vv;
        vigil_value_init_object(&vv, &vals_arr);
        s = vigil_instance_object_set_field(self, F_VALUES, &vv, error);
        vigil_value_release(&vv);
        if (s != VIGIL_STATUS_OK)
            goto fail;
        vigil_value_init_object(&vv, &pos_arr);
        s = vigil_instance_object_set_field(self, F_POSITIONALS, &vv, error);
        vigil_value_release(&vv);
        if (s != VIGIL_STATUS_OK)
            goto fail;
    }

    /* Return ok */
    vigil_vm_stack_pop_n(vm, arg_count);
    {
        vigil_object_t *ok_obj = NULL;
        s = vigil_error_object_new_cstr(rt, "", 0, &ok_obj, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t ev;
        vigil_value_init_object(&ev, &ok_obj);
        s = vigil_vm_stack_push(vm, &ev, error);
        vigil_value_release(&ev);
    }
    return s;

err_out:
    vigil_object_release(&vals_arr);
    vigil_object_release(&pos_arr);
    vigil_vm_stack_pop_n(vm, arg_count);
    {
        vigil_object_t *err_obj = NULL;
        s = vigil_error_object_new_cstr(rt, err_buf, 1, &err_obj, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t ev;
        vigil_value_init_object(&ev, &err_obj);
        s = vigil_vm_stack_push(vm, &ev, error);
        vigil_value_release(&ev);
    }
    return s;

fail:
    vigil_object_release(&vals_arr);
    vigil_object_release(&pos_arr);
    return s;
}

/* ── load_config() — load key=value config file ──────────────────── */

static vigil_status_t parser_load_config(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, path] -> err */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *path = get_string_val(vm, base + 1);
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_status_t s;

    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "cannot open config file: %s", path);
        vigil_object_t *err_obj = NULL;
        s = vigil_error_object_new_cstr(rt, err_buf, 1, &err_obj, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t ev;
        vigil_value_init_object(&ev, &err_obj);
        s = vigil_vm_stack_push(vm, &ev, error);
        vigil_value_release(&ev);
        return s;
    }

    vigil_object_t *config_keys_arr = get_field_obj(self, F_CONFIG_KEYS);
    vigil_object_t *config_data_arr = get_field_obj(self, F_CONFIG_DATA);
    size_t ck_count = vigil_array_object_length(config_keys_arr);

    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        /* Skip comments and blank lines */
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\0')
            continue;

        /* Parse key=value */
        const char *eq = strchr(p, '=');
        if (!eq)
            continue;
        size_t klen = (size_t)(eq - p);
        while (klen > 0 && (p[klen - 1] == ' ' || p[klen - 1] == '\t'))
            klen--;
        const char *val = eq + 1;
        while (*val == ' ' || *val == '\t')
            val++;

        /* Find which option this config key maps to and store as opt_name=value */
        for (size_t ci = 0; ci < ck_count; ci++)
        {
            const char *entry = array_get_str(config_keys_arr, ci);
            const char *ceq = strchr(entry, '=');
            if (!ceq)
                continue;
            const char *config_key = ceq + 1;
            size_t cklen = strlen(config_key);
            if (cklen != klen || strncmp(config_key, p, klen) != 0)
                continue;

            /* Extract option name and store as "opt_name=value" in config_data */
            size_t opt_nlen = (size_t)(ceq - entry);
            char buf[512];
            snprintf(buf, sizeof(buf), "%.*s=%s", (int)opt_nlen, entry, val);
            s = array_push_str(config_data_arr, rt, buf, error);
            if (s != VIGIL_STATUS_OK)
            {
                fclose(fp);
                goto push_ok;
            }
            break;
        }
    }
    fclose(fp);

push_ok:
    /* Return ok */
    vigil_vm_stack_pop_n(vm, arg_count);
    {
        vigil_object_t *ok_obj = NULL;
        s = vigil_error_object_new_cstr(rt, "", 0, &ok_obj, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t ev;
        vigil_value_init_object(&ev, &ok_obj);
        s = vigil_vm_stack_push(vm, &ev, error);
        vigil_value_release(&ev);
    }
    return s;
}

/* ── Getter methods ──────────────────────────────────────────────── */

static vigil_status_t parser_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    vigil_object_t *vals = get_field_obj(self, F_VALUES);
    size_t nlen = strlen(name);
    size_t vlen = vigil_array_object_length(vals);
    const char *result = "";

    for (size_t i = 0; i < vlen; i++)
    {
        const char *entry = array_get_str(vals, i);
        if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=')
        {
            result = entry + nlen + 1;
            break;
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_object_t *str = NULL;
    vigil_status_t s = vigil_string_object_new_cstr(vigil_vm_runtime(vm), result, &str, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &str);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t parser_get_bool(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    vigil_object_t *vals = get_field_obj(self, F_VALUES);
    size_t nlen = strlen(name);
    size_t vlen = vigil_array_object_length(vals);
    int result = 0;

    for (size_t i = 0; i < vlen; i++)
    {
        const char *entry = array_get_str(vals, i);
        if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=')
        {
            result = strcmp(entry + nlen + 1, "true") == 0;
            break;
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t v;
    vigil_value_init_bool(&v, result);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t parser_get_int(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    vigil_object_t *vals = get_field_obj(self, F_VALUES);
    size_t nlen = strlen(name);
    size_t vlen = vigil_array_object_length(vals);
    int32_t result = 0;

    for (size_t i = 0; i < vlen; i++)
    {
        const char *entry = array_get_str(vals, i);
        if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=')
        {
            result = (int32_t)strtol(entry + nlen + 1, NULL, 10);
            break;
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t v;
    vigil_value_init_int(&v, (int64_t)result);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t parser_get_f64(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    vigil_object_t *vals = get_field_obj(self, F_VALUES);
    size_t nlen = strlen(name);
    size_t vlen = vigil_array_object_length(vals);
    double result = 0.0;

    for (size_t i = 0; i < vlen; i++)
    {
        const char *entry = array_get_str(vals, i);
        if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=')
        {
            result = strtod(entry + nlen + 1, NULL);
            break;
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t v;
    vigil_value_init_float(&v, result);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t parser_get_multi(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *vals = get_field_obj(self, F_VALUES);
    size_t vlen = vigil_array_object_length(vals);
    vigil_status_t s;

    char prefix[280];
    snprintf(prefix, sizeof(prefix), "__multi__%s=", name);
    size_t plen = strlen(prefix);

    vigil_object_t *result = NULL;
    s = vigil_array_object_new(rt, NULL, 0, &result, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    for (size_t i = 0; i < vlen; i++)
    {
        const char *entry = array_get_str(vals, i);
        if (strncmp(entry, prefix, plen) == 0)
        {
            s = array_push_str(result, rt, entry + plen, error);
            if (s != VIGIL_STATUS_OK)
            {
                vigil_object_release(&result);
                return s;
            }
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t v;
    vigil_value_init_object(&v, &result);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

static vigil_status_t parser_get_positionals(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    vigil_value_t field_val;
    vigil_instance_object_get_field(self, F_POSITIONALS, &field_val);
    vigil_value_t copy = vigil_value_copy(&field_val);
    vigil_value_release(&field_val);
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t s = vigil_vm_stack_push(vm, &copy, error);
    vigil_value_release(&copy);
    return s;
}

static vigil_status_t parser_get_subcommand(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *sub = get_field_str(self, F_SUB_NAME);
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_object_t *str = NULL;
    vigil_status_t s = vigil_string_object_new_cstr(vigil_vm_runtime(vm), sub, &str, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &str);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── help() — generate help text ─────────────────────────────────── */

static vigil_status_t parser_help(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *prog = get_field_str(self, F_PROG);
    const char *desc = get_field_str(self, F_DESC);
    vigil_object_t *names_arr = get_field_obj(self, F_NAMES);
    vigil_object_t *shorts_arr = get_field_obj(self, F_SHORTS);
    vigil_object_t *types_arr = get_field_obj(self, F_TYPES);
    vigil_object_t *defaults_arr = get_field_obj(self, F_DEFAULTS);
    vigil_object_t *descs_arr = get_field_obj(self, F_DESCS);
    vigil_object_t *req_arr = get_field_obj(self, F_REQUIRED);
    vigil_object_t *pn_arr = get_field_obj(self, F_POS_NAMES);
    vigil_object_t *pd_arr = get_field_obj(self, F_POS_DESCS);
    vigil_object_t *pr_arr = get_field_obj(self, F_POS_REQUIRED);
    vigil_object_t *pnargs_arr = get_field_obj(self, F_POS_NARGS);
    vigil_object_t *subcmds_arr = get_field_obj(self, F_SUBCOMMANDS);
    vigil_object_t *env_arr = get_field_obj(self, F_ENV_VARS);
    size_t opt_count = vigil_array_object_length(names_arr);
    size_t pos_count = vigil_array_object_length(pn_arr);
    size_t subcmd_count = vigil_array_object_length(subcmds_arr);

    char buf[8192];
    size_t off = 0;

    /* Usage line */
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "Usage: %s", prog);
    if (subcmd_count > 0)
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, " <command>");
    if (opt_count > 0)
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, " [options]");
    for (size_t i = 0; i < pos_count && off < sizeof(buf) - 1; i++)
    {
        const char *pname = array_get_str(pn_arr, i);
        const char *preq = array_get_str(pr_arr, i);
        const char *pnarg = array_get_str(pnargs_arr, i);
        if (strcmp(preq, "true") == 0 || strcmp(pnarg, "+") == 0)
        {
            if (strcmp(pnarg, "+") == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " <%s>...", pname);
            else
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " <%s>", pname);
        }
        else if (strcmp(pnarg, "*") == 0)
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, " [%s]...", pname);
        else if (strcmp(pnarg, "?") == 0)
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, " [%s]", pname);
        else
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, " [%s]", pname);
    }
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\n\n%s\n", desc);

    /* Subcommands section */
    if (subcmd_count > 0)
    {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\nCommands:\n");
        for (size_t i = 0; i < subcmd_count && off < sizeof(buf) - 1; i++)
        {
            const char *entry = array_get_str(subcmds_arr, i);
            const char *colon = strchr(entry, ':');
            char sname[64];
            const char *sdesc = "";
            if (colon)
            {
                size_t nlen = (size_t)(colon - entry);
                if (nlen >= sizeof(sname))
                    nlen = sizeof(sname) - 1;
                memcpy(sname, entry, nlen);
                sname[nlen] = '\0';
                sdesc = colon + 1;
            }
            else
            {
                snprintf(sname, sizeof(sname), "%s", entry);
            }
            char padded[128];
            snprintf(padded, sizeof(padded), "  %s", sname);
            size_t pl = strlen(padded);
            while (pl < 30 && pl < sizeof(padded) - 1)
                padded[pl++] = ' ';
            padded[pl] = '\0';
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s\n", padded, sdesc);
        }
    }

    /* Options section */
    if (opt_count > 0)
    {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\nOptions:\n");
        for (size_t i = 0; i < opt_count && off < sizeof(buf) - 1; i++)
        {
            const char *name = array_get_str(names_arr, i);
            const char *sht = array_get_str(shorts_arr, i);
            const char *typ = array_get_str(types_arr, i);
            const char *def = array_get_str(defaults_arr, i);
            const char *d = array_get_str(descs_arr, i);
            const char *req = array_get_str(req_arr, i);

            char flag[128];
            if (sht[0] != '\0')
                snprintf(flag, sizeof(flag), "  --%s, -%s", name, sht);
            else
                snprintf(flag, sizeof(flag), "  --%s", name);

            if (strcmp(typ, "string") == 0 || strcmp(typ, "int") == 0 || strcmp(typ, "f64") == 0 ||
                strcmp(typ, "multi") == 0 || strncmp(typ, "choice:", 7) == 0)
            {
                size_t fl = strlen(flag);
                snprintf(flag + fl, sizeof(flag) - fl, " VALUE");
            }

            size_t fl = strlen(flag);
            while (fl < 30 && fl < sizeof(flag) - 1)
                flag[fl++] = ' ';
            flag[fl] = '\0';

            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", flag, d);

            /* Show choices */
            if (strncmp(typ, "choice:", 7) == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " [choices: %s]", typ + 7);

            if (strcmp(req, "true") == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " (required)");
            else if (def[0] != '\0' && strcmp(typ, "bool") != 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " [default: %s]", def);

            /* Show env var if set */
            {
                size_t nlen = strlen(name);
                size_t elen = vigil_array_object_length(env_arr);
                for (size_t ei = 0; ei < elen; ei++)
                {
                    const char *eentry = array_get_str(env_arr, ei);
                    if (strncmp(eentry, name, nlen) == 0 && eentry[nlen] == '=')
                    {
                        off += (size_t)snprintf(buf + off, sizeof(buf) - off, " [env: %s]", eentry + nlen + 1);
                        break;
                    }
                }
            }

            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\n");
        }
    }

    /* Arguments section */
    if (pos_count > 0)
    {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\nArguments:\n");
        for (size_t i = 0; i < pos_count && off < sizeof(buf) - 1; i++)
        {
            const char *pname = array_get_str(pn_arr, i);
            const char *pdesc = array_get_str(pd_arr, i);
            const char *preq = array_get_str(pr_arr, i);
            const char *pnarg = array_get_str(pnargs_arr, i);

            char padded[128];
            snprintf(padded, sizeof(padded), "  %s", pname);
            size_t pl = strlen(padded);
            while (pl < 30 && pl < sizeof(padded) - 1)
                padded[pl++] = ' ';
            padded[pl] = '\0';

            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", padded, pdesc);
            if (strcmp(preq, "true") == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " (required)");
            if (strcmp(pnarg, "*") == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " (zero or more)");
            else if (strcmp(pnarg, "+") == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " (one or more)");
            else if (strcmp(pnarg, "?") == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " (optional)");
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\n");
        }
    }

    /* Remove trailing newline */
    if (off > 0 && buf[off - 1] == '\n')
        off--;
    buf[off] = '\0';

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_object_t *str = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), buf, off, &str, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &str);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int vigil_args_at_params[] = {VIGIL_TYPE_I32};
static const char *const vigil_args_at_param_names[] = {"index"};

static const vigil_native_symbol_doc_t vigil_args_module_doc = {
    "Command-line argument access.",
    "The args module provides access to command-line arguments.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_args_count_doc = {
    "Return the number of command-line arguments.",
    "Counts the script arguments passed after the program name.",
    "i32 argc = args.count()",
};

static const vigil_native_symbol_doc_t vigil_args_at_doc = {
    "Return a command-line argument by index.",
    "Returns an empty string if the index is out of range.",
    "string first = args.at(0)",
};

static const vigil_native_module_function_t vigil_args_functions[] = {
    {"count", 5, vigil_args_count, 0, NULL, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_args_count_doc},
    {"at", 2, vigil_args_at, 1, vigil_args_at_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     vigil_args_at_param_names, NULL, NULL, &vigil_args_at_doc}};

/* ── Parser class descriptor ─────────────────────────────────────── */

static const vigil_native_symbol_doc_t parser_class_doc = {
    "Command-line parser builder.",
    "Builds declarative CLI parsers with flags, options, positional arguments, subcommands, env var fallback, config "
    "file integration, and generated help text.",
    NULL,
};

static const vigil_native_symbol_doc_t parser_prog_doc = {
    "Program name.", "Stores the program name shown in generated usage text.", "string name = parser.prog"};
static const vigil_native_symbol_doc_t parser_desc_doc = {
    "Program description.", "Stores the description shown in generated help text.", "string desc = parser.desc"};
static const vigil_native_symbol_doc_t parser_names_doc = {
    "Declared option names.", "Holds the long names for declared options.", "array<string> names = parser.names"};
static const vigil_native_symbol_doc_t parser_shorts_doc = {
    "Declared short option names.", "Holds the short aliases for declared options.",
    "array<string> shorts = parser.shorts"};
static const vigil_native_symbol_doc_t parser_types_doc = {
    "Declared option types.", "Stores the internal option type tags for each declared option.",
    "array<string> types = parser.types"};
static const vigil_native_symbol_doc_t parser_defaults_doc = {
    "Declared default values.", "Stores default string values for declared options.",
    "array<string> defaults = parser.defaults"};
static const vigil_native_symbol_doc_t parser_descs_doc = {
    "Declared option descriptions.", "Stores help text for declared options.", "array<string> descs = parser.descs"};
static const vigil_native_symbol_doc_t parser_required_doc = {
    "Required-option markers.", "Stores whether each declared option is required.",
    "array<string> required = parser.required"};
static const vigil_native_symbol_doc_t parser_pos_names_doc = {
    "Declared positional argument names.", "Stores names for declared positional arguments.",
    "array<string> names = parser.pos_names"};
static const vigil_native_symbol_doc_t parser_pos_descs_doc = {
    "Declared positional argument descriptions.", "Stores help text for declared positional arguments.",
    "array<string> descs = parser.pos_descs"};
static const vigil_native_symbol_doc_t parser_values_doc = {
    "Parsed option values.", "Stores parsed option values after a successful parse.",
    "array<string> values = parser.values"};
static const vigil_native_symbol_doc_t parser_positionals_doc = {
    "Parsed positional values.", "Stores parsed positional arguments after a successful parse.",
    "array<string> values = parser.positionals"};
static const vigil_native_symbol_doc_t parser_version_str_doc = {
    "Version string.", "Stores the version string for --version output.", "string ver = parser.version_str"};
static const vigil_native_symbol_doc_t parser_pos_required_doc = {
    "Positional required markers.", "Tracks whether each positional argument is required.",
    "array<string> req = parser.pos_required"};
static const vigil_native_symbol_doc_t parser_pos_nargs_doc = {
    "Positional nargs specifiers.", "Tracks the nargs mode for each positional (1, *, +, ?).",
    "array<string> nargs = parser.pos_nargs"};
static const vigil_native_symbol_doc_t parser_subcommands_doc = {
    "Declared subcommands.", "Stores subcommand name:desc pairs.",
    "array<string> cmds = parser.subcommands"};
static const vigil_native_symbol_doc_t parser_sub_name_doc = {
    "Matched subcommand.", "Stores the matched subcommand name after parse.", "string cmd = parser.sub_name"};
static const vigil_native_symbol_doc_t parser_env_vars_doc = {
    "Environment variable fallbacks.", "Stores name=ENV_VAR pairs for env var fallback.",
    "array<string> envs = parser.env_vars"};
static const vigil_native_symbol_doc_t parser_config_keys_doc = {
    "Config file key mappings.", "Stores name=config_key pairs for config file integration.",
    "array<string> keys = parser.config_keys"};

static const vigil_native_class_field_t parser_fields[] = {
    {"prog", 4U, VIGIL_TYPE_STRING, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &parser_prog_doc},
    {"desc", 4U, VIGIL_TYPE_STRING, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &parser_desc_doc},
    {"names", 5U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL, &parser_names_doc},
    {"shorts", 6U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL, &parser_shorts_doc},
    {"types", 5U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL, &parser_types_doc},
    {"defaults", 8U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_defaults_doc},
    {"descs", 5U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL, &parser_descs_doc},
    {"required", 8U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_required_doc},
    {"pos_names", 9U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_pos_names_doc},
    {"pos_descs", 9U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_pos_descs_doc},
    {"values", 6U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL, &parser_values_doc},
    {"positionals", 11U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_positionals_doc},
    {"version_str", 11U, VIGIL_TYPE_STRING, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &parser_version_str_doc},
    {"pos_required", 12U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_pos_required_doc},
    {"pos_nargs", 9U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_pos_nargs_doc},
    {"subcommands", 11U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_subcommands_doc},
    {"sub_name", 8U, VIGIL_TYPE_STRING, VIGIL_NATIVE_FIELD_PRIMITIVE, NULL, 0U, 0, NULL, &parser_sub_name_doc},
    {"env_vars", 8U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_env_vars_doc},
    {"config_keys", 11U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_config_keys_doc},
    {"config_data", 11U, VIGIL_TYPE_OBJECT, VIGIL_NATIVE_FIELD_ARRAY, NULL, 0U, VIGIL_TYPE_STRING, NULL,
     &parser_config_keys_doc},
};

/* ── Parameter type arrays ───────────────────────────────────────── */

static const int str3_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int str4_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int str5_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING,
                                  VIGIL_TYPE_STRING};
static const int str2_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int str1_params[] = {VIGIL_TYPE_STRING};
static const int str3_i32_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int str3_f64_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_F64};

/* ── Parameter name arrays ───────────────────────────────────────── */

static const char *const parser_new_param_names[] = {"prog", "desc"};
static const char *const parser_flag_param_names[] = {"name", "short", "desc"};
static const char *const parser_option_param_names[] = {"name", "short", "desc", "default"};
static const char *const parser_option_int_param_names[] = {"name", "short", "desc", "default"};
static const char *const parser_option_f64_param_names[] = {"name", "short", "desc", "default"};
static const char *const parser_option_choice_param_names[] = {"name", "short", "desc", "choices", "default"};
static const char *const parser_positional_param_names[] = {"name", "desc"};
static const char *const parser_name_param_names[] = {"name"};
static const char *const parser_version_param_names[] = {"ver"};
static const char *const parser_subcommand_param_names[] = {"name", "desc"};
static const char *const parser_env_param_names[] = {"env_var"};
static const char *const parser_config_param_names[] = {"key"};
static const char *const parser_load_config_param_names[] = {"path"};

/* ── Method doc strings ──────────────────────────────────────────── */

static const vigil_native_symbol_doc_t parser_new_doc = {
    "Create a parser.",
    "Constructs a new parser configured with a program name and description.",
    "args.Parser parser = args.Parser.new(\"vigil\", \"Run scripts\")",
};
static const vigil_native_symbol_doc_t parser_flag_doc = {
    "Declare a boolean flag.",
    "Adds a boolean flag option and returns the parser for chaining.",
    "parser.flag(\"verbose\", \"v\", \"Enable verbose output\")",
};
static const vigil_native_symbol_doc_t parser_option_doc = {
    "Declare a string option.",
    "Adds a string-valued option and returns the parser for chaining.",
    "parser.option(\"output\", \"o\", \"Output file\", \"out.txt\")",
};
static const vigil_native_symbol_doc_t parser_option_int_doc = {
    "Declare an integer option.",
    "Adds an integer-valued option with an i32 default and returns the parser for chaining.",
    "parser.option_int(\"retries\", \"r\", \"Retry count\", 3)",
};
static const vigil_native_symbol_doc_t parser_option_f64_doc = {
    "Declare a floating-point option.",
    "Adds an f64-valued option and returns the parser for chaining.",
    "parser.option_f64(\"threshold\", \"t\", \"Score threshold\", 0.5)",
};
static const vigil_native_symbol_doc_t parser_option_multi_doc = {
    "Declare a repeated string option.",
    "Adds an option that can be provided multiple times and returns the parser for chaining.",
    "parser.option_multi(\"include\", \"I\", \"Include path\")",
};
static const vigil_native_symbol_doc_t parser_option_choice_doc = {
    "Declare a choice option.",
    "Adds an option constrained to a pipe-separated set of valid values.",
    "parser.option_choice(\"format\", \"f\", \"Output format\", \"json|csv|xml\", \"json\")",
};
static const vigil_native_symbol_doc_t parser_mark_required_doc = {
    "Mark the last declared option as required.",
    "Sets the most recently declared option to required and returns the parser for chaining.",
    "parser.option(\"config\", \"c\", \"Config file\", \"\").mark_required()",
};
static const vigil_native_symbol_doc_t parser_version_doc = {
    "Set the version string.",
    "Enables --version/-V flag that prints the version and exits.",
    "parser.version(\"1.0.0\")",
};
static const vigil_native_symbol_doc_t parser_positional_doc = {
    "Declare a positional argument.",
    "Adds an optional positional argument and returns the parser for chaining.",
    "parser.positional(\"input\", \"Input file\")",
};
static const vigil_native_symbol_doc_t parser_required_positional_doc = {
    "Declare a required positional argument.",
    "Adds a required positional argument and returns the parser for chaining.",
    "parser.required_positional(\"input\", \"Input file\")",
};
static const vigil_native_symbol_doc_t parser_positional_star_doc = {
    "Declare a zero-or-more positional.",
    "Adds a positional that collects zero or more remaining arguments.",
    "parser.positional_star(\"files\", \"Input files\")",
};
static const vigil_native_symbol_doc_t parser_positional_plus_doc = {
    "Declare a one-or-more positional.",
    "Adds a positional that collects one or more remaining arguments.",
    "parser.positional_plus(\"files\", \"Input files\")",
};
static const vigil_native_symbol_doc_t parser_positional_optional_doc = {
    "Declare an optional positional.",
    "Adds a positional that accepts zero or one value.",
    "parser.positional_optional(\"output\", \"Output file\")",
};
static const vigil_native_symbol_doc_t parser_subcommand_doc = {
    "Declare a subcommand.",
    "Adds a named subcommand with a description.",
    "parser.subcommand(\"build\", \"Build the project\")",
};
static const vigil_native_symbol_doc_t parser_env_doc = {
    "Set env var fallback for the last option.",
    "If the option is not provided on the command line, its value is read from the named environment variable.",
    "parser.option(\"token\", \"t\", \"API token\", \"\").env(\"API_TOKEN\")",
};
static const vigil_native_symbol_doc_t parser_config_doc = {
    "Set config file key for the last option.",
    "Maps the last declared option to a key in a config file loaded with load_config.",
    "parser.option(\"host\", \"H\", \"Host\", \"localhost\").config(\"server.host\")",
};
static const vigil_native_symbol_doc_t parser_parse_doc = {
    "Parse command-line arguments.",
    "Parses all script arguments. Handles --help, --version, subcommands, env vars. Returns err.",
    "err e = parser.parse()",
};
static const vigil_native_symbol_doc_t parser_load_config_doc = {
    "Load a config file.",
    "Reads a key=value config file. For options not set on CLI or via env var, uses the config value.",
    "err e = parser.load_config(\"config.ini\")",
};
static const vigil_native_symbol_doc_t parser_get_doc = {
    "Get a parsed string option.",
    "Returns the parsed string value for the named option.",
    "string out = parser.get(\"output\")",
};
static const vigil_native_symbol_doc_t parser_get_bool_doc = {
    "Get a parsed boolean flag.",
    "Returns true when the named flag was provided.",
    "bool verbose = parser.get_bool(\"verbose\")",
};
static const vigil_native_symbol_doc_t parser_get_int_doc = {
    "Get a parsed integer option.",
    "Returns the parsed i32 value for the named option. Returns 0 if not found.",
    "i32 retries = parser.get_int(\"retries\")",
};
static const vigil_native_symbol_doc_t parser_get_f64_doc = {
    "Get a parsed floating-point option.",
    "Returns the parsed f64 value for the named option. Returns 0.0 if not found.",
    "f64 threshold = parser.get_f64(\"threshold\")",
};
static const vigil_native_symbol_doc_t parser_get_multi_doc = {
    "Get repeated option values.",
    "Returns all parsed values for a repeated option.",
    "array<string> includes = parser.get_multi(\"include\")",
};
static const vigil_native_symbol_doc_t parser_get_positionals_doc = {
    "Get parsed positional arguments.",
    "Returns the parsed positional arguments in declaration order.",
    "array<string> values = parser.get_positionals()",
};
static const vigil_native_symbol_doc_t parser_get_subcommand_doc = {
    "Get the matched subcommand.",
    "Returns the name of the subcommand matched during parse, or empty string if none.",
    "string cmd = parser.get_subcommand()",
};
static const vigil_native_symbol_doc_t parser_help_doc = {
    "Render help text.",
    "Builds formatted usage, subcommands, options, and arguments help text.",
    "string help = parser.help()",
};

/* ── Method registration table ───────────────────────────────────── */

static const vigil_native_class_method_t parser_methods[] = {
    /* new(prog, desc) -> Parser (static) */
    {"new", 3U, parser_new, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Parser", 6U, 0, parser_new_param_names,
     NULL, NULL, &parser_new_doc},
    /* flag(name, short, desc) -> Parser */
    {"flag", 4U, parser_flag, 3U, str3_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_flag_param_names, NULL, NULL, &parser_flag_doc},
    /* option(name, short, desc, default) -> Parser */
    {"option", 6U, parser_option, 4U, str4_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_option_param_names, NULL, NULL, &parser_option_doc},
    /* option_int(name, short, desc, i32 default) -> Parser */
    {"option_int", 10U, parser_option_int, 4U, str3_i32_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_option_int_param_names, NULL, NULL, &parser_option_int_doc},
    /* option_f64(name, short, desc, f64 default) -> Parser */
    {"option_f64", 10U, parser_option_f64, 4U, str3_f64_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_option_f64_param_names, NULL, NULL, &parser_option_f64_doc},
    /* option_multi(name, short, desc) -> Parser */
    {"option_multi", 12U, parser_option_multi, 3U, str3_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_flag_param_names, NULL, NULL, &parser_option_multi_doc},
    /* option_choice(name, short, desc, choices, default) -> Parser */
    {"option_choice", 13U, parser_option_choice, 5U, str5_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_option_choice_param_names, NULL, NULL, &parser_option_choice_doc},
    /* mark_required() -> Parser */
    {"mark_required", 13U, parser_mark_required, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0, NULL,
     NULL, NULL, &parser_mark_required_doc},
    /* version(ver) -> Parser */
    {"version", 7U, parser_version, 1U, str1_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_version_param_names, NULL, NULL, &parser_version_doc},
    /* positional(name, desc) -> Parser */
    {"positional", 10U, parser_positional, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_positional_param_names, NULL, NULL, &parser_positional_doc},
    /* required_positional(name, desc) -> Parser */
    {"required_positional", 19U, parser_required_positional, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser",
     6U, 0, parser_positional_param_names, NULL, NULL, &parser_required_positional_doc},
    /* positional_star(name, desc) -> Parser */
    {"positional_star", 15U, parser_positional_star, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_positional_param_names, NULL, NULL, &parser_positional_star_doc},
    /* positional_plus(name, desc) -> Parser */
    {"positional_plus", 15U, parser_positional_plus, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_positional_param_names, NULL, NULL, &parser_positional_plus_doc},
    /* positional_optional(name, desc) -> Parser */
    {"positional_optional", 19U, parser_positional_optional, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser",
     6U, 0, parser_positional_param_names, NULL, NULL, &parser_positional_optional_doc},
    /* subcommand(name, desc) -> Parser */
    {"subcommand", 10U, parser_subcommand, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_subcommand_param_names, NULL, NULL, &parser_subcommand_doc},
    /* env(env_var) -> Parser */
    {"env", 3U, parser_env, 1U, str1_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0, parser_env_param_names,
     NULL, NULL, &parser_env_doc},
    /* config(key) -> Parser */
    {"config", 6U, parser_config, 1U, str1_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_config_param_names, NULL, NULL, &parser_config_doc},
    /* parse() -> err */
    {"parse", 5U, parser_parse, 0U, NULL, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &parser_parse_doc},
    /* load_config(path) -> err */
    {"load_config", 11U, parser_load_config, 1U, str1_params, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0,
     parser_load_config_param_names, NULL, NULL, &parser_load_config_doc},
    /* get(name) -> string */
    {"get", 3U, parser_get, 1U, str1_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, parser_name_param_names,
     NULL, NULL, &parser_get_doc},
    /* get_bool(name) -> bool */
    {"get_bool", 8U, parser_get_bool, 1U, str1_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0,
     parser_name_param_names, NULL, NULL, &parser_get_bool_doc},
    /* get_int(name) -> i32 */
    {"get_int", 7U, parser_get_int, 1U, str1_params, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, 0U, 0,
     parser_name_param_names, NULL, NULL, &parser_get_int_doc},
    /* get_f64(name) -> f64 */
    {"get_f64", 7U, parser_get_f64, 1U, str1_params, VIGIL_TYPE_F64, 1U, NULL, 0, NULL, 0U, 0,
     parser_name_param_names, NULL, NULL, &parser_get_f64_doc},
    /* get_multi(name) -> array<string> */
    {"get_multi", 9U, parser_get_multi, 1U, str1_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, 0U, VIGIL_TYPE_STRING,
     parser_name_param_names, NULL, NULL, &parser_get_multi_doc},
    /* get_positionals() -> array<string> */
    {"get_positionals", 15U, parser_get_positionals, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, 0U,
     VIGIL_TYPE_STRING, NULL, NULL, NULL, &parser_get_positionals_doc},
    /* get_subcommand() -> string */
    {"get_subcommand", 14U, parser_get_subcommand, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL,
     NULL, &parser_get_subcommand_doc},
    /* help() -> string */
    {"help", 4U, parser_help, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, NULL, NULL, NULL,
     &parser_help_doc},
};

static const vigil_native_class_t vigil_args_classes[] = {
    {"Parser", 6U, parser_fields, FIELD_COUNT, parser_methods, sizeof(parser_methods) / sizeof(parser_methods[0]), NULL,
     &parser_class_doc},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_args = {"args",
                                                           4,
                                                           vigil_args_functions,
                                                           sizeof(vigil_args_functions) /
                                                               sizeof(vigil_args_functions[0]),
                                                           vigil_args_classes,
                                                           1U,
                                                           &vigil_args_module_doc,
                                                           NULL,
                                                           0U};
