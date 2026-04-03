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
 *   p.option_int(string name, string short, string desc, string default) -> Parser
 *   p.option_multi(string name, string short, string desc) -> Parser
 *   p.required() -> Parser
 *   p.positional(string name, string desc) -> Parser
 *   p.parse(i32 start) -> err
 *   p.get(string name) -> string
 *   p.get_bool(string name) -> bool
 *   p.get_multi(string name) -> array<string>
 *   p.get_positionals() -> array<string>
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

/* ── Parser field indices ────────────────────────────────────────── */

enum
{
    F_PROG = 0,    /* string */
    F_DESC,        /* string */
    F_NAMES,       /* array<string> */
    F_SHORTS,      /* array<string> */
    F_TYPES,       /* array<string> */
    F_DEFAULTS,    /* array<string> */
    F_DESCS,       /* array<string> */
    F_REQUIRED,    /* array<string> */
    F_POS_NAMES,   /* array<string> */
    F_POS_DESCS,   /* array<string> */
    F_VALUES,      /* array<string>  key=value pairs after parse */
    F_POSITIONALS, /* array<string>  positional args after parse */
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

    /* Create empty arrays for all array fields */
    for (i = F_NAMES; i < FIELD_COUNT; i++)
    {
        s = make_empty_array(rt, &fields[i], error);
        if (s != VIGIL_STATUS_OK)
            goto cleanup;
    }

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

/* ── Builder methods (flag, option, option_int, option_multi, required, positional) ── */

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
    /* stack: [self, name, short, desc] */
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
    /* stack: [self, name, short, desc, default] */
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
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    const char *def = get_string_val(vm, base + 4);
    vigil_status_t s = append_opt(self, vigil_vm_runtime(vm), name, sht, "int", def, desc, "false", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

static vigil_status_t parser_option_multi(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, name, short, desc] */
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

static vigil_status_t parser_mark_required(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* Mark the last-added option as required. stack: [self] */
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

static vigil_status_t parser_positional(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, name, desc] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *desc = get_string_val(vm, base + 2);
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_status_t s;
    s = array_push_str(get_field_obj(self, F_POS_NAMES), rt, name, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = array_push_str(get_field_obj(self, F_POS_DESCS), rt, desc, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return return_self(vm, self, arg_count, error);
}

/* ── parse() — the core argument parser ──────────────────────────── */

/* Find option index by long or short name. Returns -1 if not found. */
static int find_opt_idx(vigil_object_t *names_arr, vigil_object_t *shorts_arr, const char *key)
{
    size_t len = vigil_array_object_length(names_arr);
    size_t i;
    for (i = 0; i < len; i++)
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

static vigil_status_t parser_parse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, start_i32] */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    int64_t start = vigil_value_as_int(&(vigil_value_t){vigil_vm_stack_get(vm, base + 1)});
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
    size_t opt_count = vigil_array_object_length(names_arr);

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

    /* Set defaults */
    size_t i;
    for (i = 0; i < opt_count; i++)
    {
        const char *name = array_get_str(names_arr, i);
        const char *typ = array_get_str(types_arr, i);
        const char *def = array_get_str(defaults_arr, i);
        char buf[512];
        if (strcmp(typ, "bool") == 0)
            snprintf(buf, sizeof(buf), "%s=false", name);
        else if (strcmp(typ, "multi") == 0)
            continue; /* multi values tracked separately */
        else
            snprintf(buf, sizeof(buf), "%s=%s", name, def);
        s = array_push_str(vals_arr, rt, buf, error);
        if (s != VIGIL_STATUS_OK)
            goto fail;
    }

    /* Parse loop */
    int64_t pos = start;
    int past_dd = 0;
    char err_buf[256];

    while (pos >= 0 && (size_t)pos < argc)
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
            s = array_push_str(pos_arr, rt, arg, error);
            if (s != VIGIL_STATUS_OK)
                goto fail;
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
            snprintf(err_buf, sizeof(err_buf), "unknown option: %s", arg);
            goto err_out;
        }

        const char *name = array_get_str(names_arr, (size_t)idx);
        const char *typ = array_get_str(types_arr, (size_t)idx);

        if (strcmp(typ, "bool") == 0)
        {
            s = parser_update_vals_entry(vals_arr, rt, name, "true", error);
            if (s != VIGIL_STATUS_OK)
                goto fail;
            pos++;
        }
        else
        {
            /* string, int, or multi — needs a value */
            const char *val = inline_val;
            if (val == NULL)
            {
                pos++;
                if ((size_t)pos >= argc)
                {
                    snprintf(err_buf, sizeof(err_buf), "option --%s requires a value", name);
                    goto err_out;
                }
                val = argv[pos];
            }
            if (strcmp(typ, "int") == 0)
            {
                char *end = NULL;
                errno = 0;
                (void)strtol(val, &end, 10);
                if (errno != 0 || end == val || *end != '\0')
                {
                    snprintf(err_buf, sizeof(err_buf), "option --%s requires an integer, got: %s", name, val);
                    goto err_out;
                }
            }
            if (strcmp(typ, "multi") == 0)
            {
                char buf[512];
                snprintf(buf, sizeof(buf), "__multi__%s=%s", name, val);
                s = array_push_str(vals_arr, rt, buf, error);
                if (s != VIGIL_STATUS_OK)
                    goto fail;
            }
            else
            {
                s = parser_update_vals_entry(vals_arr, rt, name, val, error);
                if (s != VIGIL_STATUS_OK)
                    goto fail;
            }
            pos++;
        }
    }

    /* Check required options */
    if (!parser_check_required(vals_arr, names_arr, types_arr, req_arr, opt_count, err_buf, sizeof(err_buf)))
        goto err_out;

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

/* ── Getter methods ──────────────────────────────────────────────── */

static vigil_status_t parser_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, name] -> string */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    vigil_object_t *vals = get_field_obj(self, F_VALUES);
    size_t nlen = strlen(name);
    size_t vlen = vigil_array_object_length(vals);
    const char *result = "";
    size_t i;

    for (i = 0; i < vlen; i++)
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
    /* stack: [self, name] -> bool */
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    vigil_object_t *vals = get_field_obj(self, F_VALUES);
    size_t nlen = strlen(name);
    size_t vlen = vigil_array_object_length(vals);
    int result = 0;
    size_t i;

    for (i = 0; i < vlen; i++)
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

static vigil_status_t parser_get_multi(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self, name] -> array<string> */
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

    size_t i;
    for (i = 0; i < vlen; i++)
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
    /* stack: [self] -> array<string> */
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

/* ── help() — generate help text ─────────────────────────────────── */

static vigil_status_t parser_help(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    /* stack: [self] -> string */
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
    size_t opt_count = vigil_array_object_length(names_arr);
    size_t pos_count = vigil_array_object_length(pn_arr);

    /* Build help string into a buffer */
    char buf[4096];
    size_t off = 0;
    size_t i;

    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "Usage: %s [options]", prog);
    for (i = 0; i < pos_count; i++)
    {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, " <%s>", array_get_str(pn_arr, i));
    }
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\n\n%s\n", desc);

    if (opt_count > 0)
    {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\nOptions:\n");
        for (i = 0; i < opt_count; i++)
        {
            const char *name = array_get_str(names_arr, i);
            const char *sht = array_get_str(shorts_arr, i);
            const char *typ = array_get_str(types_arr, i);
            const char *def = array_get_str(defaults_arr, i);
            const char *d = array_get_str(descs_arr, i);
            const char *req = array_get_str(req_arr, i);

            char flag[64];
            if (sht[0] != '\0')
                snprintf(flag, sizeof(flag), "  --%s, -%s", name, sht);
            else
                snprintf(flag, sizeof(flag), "  --%s", name);

            if (strcmp(typ, "string") == 0 || strcmp(typ, "int") == 0 || strcmp(typ, "multi") == 0)
            {
                size_t fl = strlen(flag);
                snprintf(flag + fl, sizeof(flag) - fl, " VALUE");
            }

            /* Pad to 30 chars */
            size_t fl = strlen(flag);
            while (fl < 30 && fl < sizeof(flag) - 1)
            {
                flag[fl++] = ' ';
            }
            flag[fl] = '\0';

            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", flag, d);
            if (strcmp(req, "true") == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " (required)");
            else if (def[0] != '\0' && strcmp(typ, "bool") != 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " [default: %s]", def);
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\n");
            if (off >= sizeof(buf) - 1)
                break;
        }
    }

    if (pos_count > 0)
    {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\nArguments:\n");
        for (i = 0; i < pos_count && off < sizeof(buf) - 1; i++)
        {
            char pname[64];
            snprintf(pname, sizeof(pname), "  %s", array_get_str(pn_arr, i));
            size_t pl = strlen(pname);
            while (pl < 30 && pl < sizeof(pname) - 1)
            {
                pname[pl++] = ' ';
            }
            pname[pl] = '\0';
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s\n", pname, array_get_str(pd_arr, i));
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
    "Builds declarative CLI parsers with flags, options, positional arguments, and generated help text.",
    NULL,
};

static const vigil_native_symbol_doc_t parser_prog_doc = {
    "Program name.",
    "Stores the program name shown in generated usage text.",
    "string name = parser.prog",
};

static const vigil_native_symbol_doc_t parser_desc_doc = {
    "Program description.",
    "Stores the description shown in generated help text.",
    "string desc = parser.desc",
};

static const vigil_native_symbol_doc_t parser_names_doc = {
    "Declared option names.",
    "Holds the long names for declared options.",
    "array<string> names = parser.names",
};

static const vigil_native_symbol_doc_t parser_shorts_doc = {
    "Declared short option names.",
    "Holds the short aliases for declared options.",
    "array<string> shorts = parser.shorts",
};

static const vigil_native_symbol_doc_t parser_types_doc = {
    "Declared option types.",
    "Stores the internal option type tags for each declared option.",
    "array<string> types = parser.types",
};

static const vigil_native_symbol_doc_t parser_defaults_doc = {
    "Declared default values.",
    "Stores default string values for declared options.",
    "array<string> defaults = parser.defaults",
};

static const vigil_native_symbol_doc_t parser_descs_doc = {
    "Declared option descriptions.",
    "Stores help text for declared options.",
    "array<string> descs = parser.descs",
};

static const vigil_native_symbol_doc_t parser_required_doc = {
    "Required-option markers.",
    "Stores whether each declared option is required.",
    "array<string> required = parser.required",
};

static const vigil_native_symbol_doc_t parser_pos_names_doc = {
    "Declared positional argument names.",
    "Stores names for declared positional arguments.",
    "array<string> names = parser.pos_names",
};

static const vigil_native_symbol_doc_t parser_pos_descs_doc = {
    "Declared positional argument descriptions.",
    "Stores help text for declared positional arguments.",
    "array<string> descs = parser.pos_descs",
};

static const vigil_native_symbol_doc_t parser_values_doc = {
    "Parsed option values.",
    "Stores parsed option values after a successful parse.",
    "array<string> values = parser.values",
};

static const vigil_native_symbol_doc_t parser_positionals_doc = {
    "Parsed positional values.",
    "Stores parsed positional arguments after a successful parse.",
    "array<string> values = parser.positionals",
};

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
};

static const int str3_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int str4_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int str2_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int str1_params[] = {VIGIL_TYPE_STRING};
static const int i32_params[] = {VIGIL_TYPE_I32};
static const char *const parser_new_param_names[] = {"prog", "desc"};
static const char *const parser_flag_param_names[] = {"name", "short", "desc"};
static const char *const parser_option_param_names[] = {"name", "short", "default", "desc"};
static const char *const parser_positional_param_names[] = {"name", "desc"};
static const char *const parser_parse_param_names[] = {"argc"};
static const char *const parser_name_param_names[] = {"name"};

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
    "parser.option(\"output\", \"o\", \"out.txt\", \"Output file\")",
};

static const vigil_native_symbol_doc_t parser_option_int_doc = {
    "Declare an integer option.",
    "Adds an integer-valued option and returns the parser for chaining.",
    "parser.option_int(\"retries\", \"r\", \"3\", \"Retry count\")",
};

static const vigil_native_symbol_doc_t parser_option_multi_doc = {
    "Declare a repeated string option.",
    "Adds an option that can be provided multiple times and returns the parser for chaining.",
    "parser.option_multi(\"include\", \"I\", \"Include path\")",
};

static const vigil_native_symbol_doc_t parser_mark_required_doc = {
    "Mark the last declared option as required.",
    "Sets the most recently declared option to required and returns the parser for chaining.",
    "parser.option(\"config\", \"c\", \"\", \"Config file\").mark_required()",
};

static const vigil_native_symbol_doc_t parser_positional_doc = {
    "Declare a positional argument.",
    "Adds a positional argument and returns the parser for chaining.",
    "parser.positional(\"input\", \"Input file\")",
};

static const vigil_native_symbol_doc_t parser_parse_doc = {
    "Parse command-line arguments.",
    "Parses the process arguments currently available to the runtime. Returns an error value indicating success or "
    "failure.",
    "error err = parser.parse(args.count())",
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

static const vigil_native_symbol_doc_t parser_help_doc = {
    "Render help text.",
    "Builds formatted usage and option help text for the parser.",
    "string help = parser.help()",
};

static const vigil_native_class_method_t parser_methods[] = {
    {"new", 3U, parser_new, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 1, "Parser", 6U, 0, parser_new_param_names,
     NULL, NULL, &parser_new_doc},
    {"flag", 4U, parser_flag, 3U, str3_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0, parser_flag_param_names,
     NULL, NULL, &parser_flag_doc},
    {"option", 6U, parser_option, 4U, str4_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_option_param_names, NULL, NULL, &parser_option_doc},
    {"option_int", 10U, parser_option_int, 4U, str4_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_option_param_names, NULL, NULL, &parser_option_int_doc},
    {"option_multi", 12U, parser_option_multi, 3U, str3_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_flag_param_names, NULL, NULL, &parser_option_multi_doc},
    {"mark_required", 13U, parser_mark_required, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0, NULL, NULL,
     NULL, &parser_mark_required_doc},
    {"positional", 10U, parser_positional, 2U, str2_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, "Parser", 6U, 0,
     parser_positional_param_names, NULL, NULL, &parser_positional_doc},
    {"parse", 5U, parser_parse, 1U, i32_params, VIGIL_TYPE_ERR, 1U, NULL, 0, NULL, 0U, 0, parser_parse_param_names,
     NULL, NULL, &parser_parse_doc},
    {"get", 3U, parser_get, 1U, str1_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, 0U, 0, parser_name_param_names, NULL,
     NULL, &parser_get_doc},
    {"get_bool", 8U, parser_get_bool, 1U, str1_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, 0U, 0,
     parser_name_param_names, NULL, NULL, &parser_get_bool_doc},
    {"get_multi", 9U, parser_get_multi, 1U, str1_params, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, 0U, VIGIL_TYPE_STRING,
     parser_name_param_names, NULL, NULL, &parser_get_multi_doc},
    {"get_positionals", 15U, parser_get_positionals, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, 0U,
     VIGIL_TYPE_STRING, NULL, NULL, NULL, &parser_get_positionals_doc},
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
    NULL, 0U};
