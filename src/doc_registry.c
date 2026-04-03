#include "vigil/doc_registry.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/builtins.h"
#include "vigil/stdlib.h"
#include "vigil/type.h"

#include "plugin_registry.h"

/* ── regex Module Docs ────────────────────────────────────── */

/* ── random Module Docs ───────────────────────────────────── */

/* ── url Module Docs ──────────────────────────────────────── */

/* ── log module ───────────────────────────────────────────── */

/* ── csv Module Docs ──────────────────────────────────────── */

/* ── net Module Docs ──────────────────────────────────────── */

/* ── crypto Module Docs ───────────────────────────────────── */

/* ── Module List ──────────────────────────────────────────── */

typedef struct native_doc_cache
{
    const vigil_native_module_t *module;
    vigil_doc_entry_t *entries;
    size_t count;
} native_doc_cache_t;

typedef struct native_doc_buf
{
    char *data;
    size_t length;
    size_t capacity;
} native_doc_buf_t;

static native_doc_cache_t native_doc_caches[32];
static size_t native_doc_cache_count = 0U;
static const char *generated_module_names[32];
static size_t generated_module_name_count = 0U;
static int generated_module_names_ready = 0;

static int native_doc_module_has_docs(const vigil_native_module_t *module)
{
    size_t i;

    if (module == NULL)
        return 0;
    if (module->doc != NULL)
        return 1;

    for (i = 0U; i < module->function_count; i++)
    {
        if (module->functions[i].doc != NULL)
            return 1;
    }
    for (i = 0U; i < module->class_count; i++)
    {
        size_t j;
        const vigil_native_class_t *klass = &module->classes[i];
        if (klass->doc != NULL)
            return 1;
        for (j = 0U; j < klass->field_count; j++)
        {
            if (klass->fields[j].doc != NULL)
                return 1;
        }
        for (j = 0U; j < klass->method_count; j++)
        {
            if (klass->methods[j].doc != NULL)
                return 1;
        }
    }
    return 0;
}

static int native_doc_module_has_any_symbols(const vigil_native_module_t *module)
{
    return module != NULL &&
           (module->function_count > 0U || module->class_count > 0U || module->constant_count > 0U);
}

static void native_doc_buf_init(native_doc_buf_t *buf)
{
    buf->data = NULL;
    buf->length = 0U;
    buf->capacity = 0U;
}

static int native_doc_buf_reserve(native_doc_buf_t *buf, size_t extra)
{
    size_t needed = buf->length + extra + 1U;
    char *next;

    if (needed <= buf->capacity)
        return 1;
    buf->capacity = buf->capacity == 0U ? 64U : buf->capacity;
    while (buf->capacity < needed)
        buf->capacity *= 2U;
    next = (char *)realloc(buf->data, buf->capacity);
    if (next == NULL)
        return 0;
    buf->data = next;
    return 1;
}

static int native_doc_buf_append_len(native_doc_buf_t *buf, const char *text, size_t length)
{
    if (text == NULL || length == 0U)
        return 1;
    if (!native_doc_buf_reserve(buf, length))
        return 0;
    memcpy(buf->data + buf->length, text, length);
    buf->length += length;
    buf->data[buf->length] = '\0';
    return 1;
}

static int native_doc_buf_append(native_doc_buf_t *buf, const char *text)
{
    if (text == NULL)
        return 1;
    return native_doc_buf_append_len(buf, text, strlen(text));
}

static int native_doc_buf_append_char(native_doc_buf_t *buf, char ch)
{
    return native_doc_buf_append_len(buf, &ch, 1U);
}

static char *native_doc_buf_take(native_doc_buf_t *buf)
{
    char *out = buf->data;
    if (out == NULL)
    {
        out = (char *)malloc(1U);
        if (out != NULL)
            out[0] = '\0';
    }
    buf->data = NULL;
    buf->length = 0U;
    buf->capacity = 0U;
    return out;
}

static char *native_doc_printf(const char *fmt, ...)
{
    va_list args;
    va_list copy;
    int length;
    char *out;

    va_start(args, fmt);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0U, fmt, copy);
    va_end(copy);
    if (length < 0)
    {
        va_end(args);
        return NULL;
    }

    out = (char *)malloc((size_t)length + 1U);
    if (out == NULL)
    {
        va_end(args);
        return NULL;
    }
    vsnprintf(out, (size_t)length + 1U, fmt, args);
    va_end(args);
    return out;
}

static void native_doc_append_qualified_class_name(native_doc_buf_t *buf, const char *module_name,
                                                   const char *class_name)
{
    if (class_name == NULL)
    {
        native_doc_buf_append(buf, "object");
        return;
    }
    if (strchr(class_name, '.') != NULL || module_name == NULL)
    {
        native_doc_buf_append(buf, class_name);
        return;
    }
    native_doc_buf_append(buf, module_name);
    native_doc_buf_append_char(buf, '.');
    native_doc_buf_append(buf, class_name);
}

typedef struct native_doc_type_spec
{
    int kind;
    int object_kind;
    int element_type;
    const char *class_name;
    const vigil_native_type_t *ext;
    const char *override_name;
} native_doc_type_spec_t;

static native_doc_type_spec_t native_doc_type_spec(int kind, int object_kind, int element_type, const char *class_name,
                                                   const vigil_native_type_t *ext, const char *override_name)
{
    native_doc_type_spec_t spec;

    spec.kind = kind;
    spec.object_kind = object_kind;
    spec.element_type = element_type;
    spec.class_name = class_name;
    spec.ext = ext;
    spec.override_name = override_name;
    return spec;
}

static void native_doc_append_type_name(native_doc_buf_t *buf, const char *module_name, native_doc_type_spec_t spec)
{
    if (spec.override_name != NULL)
    {
        native_doc_buf_append(buf, spec.override_name);
        return;
    }

    if (spec.class_name != NULL && spec.class_name[0] != '\0')
    {
        native_doc_append_qualified_class_name(buf, module_name, spec.class_name);
        return;
    }

    if (spec.ext != NULL)
    {
        if (spec.ext->kind != VIGIL_TYPE_OBJECT || spec.ext->object_kind == 0)
        {
            native_doc_buf_append(buf, vigil_type_kind_name((vigil_type_kind_t)spec.ext->kind));
            return;
        }
        if (spec.ext->object_kind == 4)
        {
            native_doc_buf_append(buf, "array<");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->element_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append_char(buf, '>');
            return;
        }
        if (spec.ext->object_kind == 5)
        {
            native_doc_buf_append(buf, "map<");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->key_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append(buf, ", ");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->value_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append_char(buf, '>');
            return;
        }
    }

    if (spec.object_kind == VIGIL_NATIVE_FIELD_ARRAY || (spec.kind == VIGIL_TYPE_OBJECT && spec.element_type != 0))
    {
        native_doc_buf_append(buf, "array<");
        native_doc_append_type_name(buf, module_name, native_doc_type_spec(spec.element_type, 0, 0, NULL, NULL, NULL));
        native_doc_buf_append_char(buf, '>');
        return;
    }

    native_doc_buf_append(buf, vigil_type_kind_name((vigil_type_kind_t)spec.kind));
}

static char *native_doc_build_function_signature(const vigil_native_module_t *module,
                                                 const vigil_native_module_function_t *function)
{
    native_doc_buf_t buf;
    size_t i;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, function->name);
    native_doc_buf_append_char(&buf, '(');
    for (i = 0U; i < function->param_count; i++)
    {
        if (i != 0U)
            native_doc_buf_append(&buf, ", ");
        if (function->doc_param_names != NULL && function->doc_param_names[i] != NULL)
        {
            native_doc_buf_append(&buf, function->doc_param_names[i]);
            native_doc_buf_append(&buf, ": ");
        }
        native_doc_append_type_name(
            &buf, module->name,
            native_doc_type_spec(function->param_types != NULL ? function->param_types[i] : VIGIL_TYPE_INVALID, 0, 0,
                                 NULL, function->param_types_ext != NULL ? &function->param_types_ext[i] : NULL,
                                 function->doc_param_type_names != NULL ? function->doc_param_type_names[i] : NULL));
    }
    native_doc_buf_append(&buf, ") -> ");

    if (function->return_count > 1U && function->return_types != NULL)
    {
        native_doc_buf_append_char(&buf, '(');
        for (i = 0U; i < function->return_count; i++)
        {
            if (i != 0U)
                native_doc_buf_append(&buf, ", ");
            native_doc_append_type_name(&buf, module->name,
                                        native_doc_type_spec(function->return_types[i], 0, 0, NULL, NULL,
                                                             i == 0U ? function->doc_return_type_name : NULL));
        }
        native_doc_buf_append_char(&buf, ')');
    }
    else
    {
        native_doc_append_type_name(&buf, module->name,
                                    native_doc_type_spec(function->return_type, 0, function->return_element_type, NULL,
                                                         function->return_type_ext, function->doc_return_type_name));
    }

    return native_doc_buf_take(&buf);
}

static char *native_doc_build_method_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass,
                                               const vigil_native_class_method_t *method)
{
    native_doc_buf_t buf;
    size_t i;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, klass->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, method->name);
    native_doc_buf_append_char(&buf, '(');
    for (i = 0U; i < method->param_count; i++)
    {
        if (i != 0U)
            native_doc_buf_append(&buf, ", ");
        if (method->doc_param_names != NULL && method->doc_param_names[i] != NULL)
        {
            native_doc_buf_append(&buf, method->doc_param_names[i]);
            native_doc_buf_append(&buf, ": ");
        }
        native_doc_append_type_name(
            &buf, module->name,
            native_doc_type_spec(method->param_types != NULL ? method->param_types[i] : VIGIL_TYPE_INVALID, 0, 0, NULL,
                                 NULL, method->doc_param_type_names != NULL ? method->doc_param_type_names[i] : NULL));
    }
    native_doc_buf_append(&buf, ") -> ");

    if (method->return_count > 1U && method->return_types != NULL)
    {
        native_doc_buf_append_char(&buf, '(');
        for (i = 0U; i < method->return_count; i++)
        {
            if (i != 0U)
                native_doc_buf_append(&buf, ", ");
            native_doc_append_type_name(&buf, module->name,
                                        native_doc_type_spec(method->return_types[i], 0, 0, NULL, NULL,
                                                             i == 0U ? method->doc_return_type_name : NULL));
        }
        native_doc_buf_append_char(&buf, ')');
    }
    else
    {
        native_doc_append_type_name(&buf, module->name,
                                    native_doc_type_spec(method->return_type, 0, method->return_element_type,
                                                         method->return_class_name, NULL,
                                                         method->doc_return_type_name));
    }

    return native_doc_buf_take(&buf);
}

static char *native_doc_build_field_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass,
                                              const vigil_native_class_field_t *field)
{
    native_doc_buf_t buf;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, klass->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, field->name);
    native_doc_buf_append(&buf, ": ");
    native_doc_append_type_name(&buf, module->name,
                                native_doc_type_spec(field->type, field->object_kind, field->element_type,
                                                     field->class_name, NULL, field->doc_type_name));
    return native_doc_buf_take(&buf);
}

static char *native_doc_build_class_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass)
{
    return native_doc_printf("class %s.%s", module->name, klass->name);
}

static const vigil_native_module_t *native_doc_find_stdlib_module(const char *name)
{
    VIGIL_STDLIB_MODULE_TABLE(mods);
    size_t i;

    if (name == NULL)
        return NULL;

    for (i = 0U; i < sizeof(mods) / sizeof(mods[0]); i++)
    {
        if (mods[i].module != NULL && strcmp(mods[i].name, name) == 0)
            return mods[i].module;
    }
    return NULL;
}

static const vigil_native_module_t *native_doc_find_plugin_module(const char *name)
{
    vigil_plugin_entry_t mods[1U + VIGIL_PLUGIN_COUNT];
    size_t i;

    if (name == NULL)
        return NULL;

    vigil_plugin_fill_table_(mods);
    for (i = 0U; i < VIGIL_PLUGIN_COUNT; i++)
    {
        if (mods[i].module != NULL && strcmp(mods[i].name, name) == 0)
            return mods[i].module;
    }
    return NULL;
}

static size_t native_doc_count_class_entries(const vigil_native_class_t *klass)
{
    size_t count = 0U;
    size_t i;

    if (klass->doc != NULL || klass->field_count > 0U || klass->method_count > 0U)
        count += 1U;
    for (i = 0U; i < klass->field_count; i++)
    {
        count += 1U;
    }
    for (i = 0U; i < klass->method_count; i++)
    {
        count += 1U;
    }
    return count;
}

static size_t native_doc_count_module_entries(const vigil_native_module_t *module)
{
    size_t count = 1U;
    size_t i;

    for (i = 0U; i < module->function_count; i++)
        count += 1U;
    for (i = 0U; i < module->constant_count; i++)
        count += 1U;
    for (i = 0U; i < module->class_count; i++)
        count += native_doc_count_class_entries(&module->classes[i]);
    return count;
}

static void native_doc_fill_class_entries(native_doc_cache_t *cache, size_t *index, const vigil_native_module_t *module,
                                          const vigil_native_class_t *klass)
{
    size_t i;

    if (klass->doc != NULL || klass->field_count > 0U || klass->method_count > 0U)
    {
        cache->entries[*index].name = native_doc_printf("%s.%s", module->name, klass->name);
        cache->entries[*index].signature = native_doc_build_class_signature(module, klass);
        cache->entries[*index].summary = klass->doc != NULL ? klass->doc->summary : NULL;
        cache->entries[*index].description = klass->doc != NULL ? klass->doc->description : NULL;
        cache->entries[*index].example = klass->doc != NULL ? klass->doc->example : NULL;
        *index += 1U;
    }

    for (i = 0U; i < klass->field_count; i++)
    {
        const vigil_native_class_field_t *field = &klass->fields[i];
        cache->entries[*index].name = native_doc_printf("%s.%s.%s", module->name, klass->name, field->name);
        cache->entries[*index].signature = native_doc_build_field_signature(module, klass, field);
        cache->entries[*index].summary = field->doc != NULL ? field->doc->summary : NULL;
        cache->entries[*index].description = field->doc != NULL ? field->doc->description : NULL;
        cache->entries[*index].example = field->doc != NULL ? field->doc->example : NULL;
        *index += 1U;
    }

    for (i = 0U; i < klass->method_count; i++)
    {
        const vigil_native_class_method_t *method = &klass->methods[i];
        cache->entries[*index].name = native_doc_printf("%s.%s.%s", module->name, klass->name, method->name);
        cache->entries[*index].signature = native_doc_build_method_signature(module, klass, method);
        cache->entries[*index].summary = method->doc != NULL ? method->doc->summary : NULL;
        cache->entries[*index].description = method->doc != NULL ? method->doc->description : NULL;
        cache->entries[*index].example = method->doc != NULL ? method->doc->example : NULL;
        *index += 1U;
    }
}

static native_doc_cache_t *native_doc_build_module_cache(const vigil_native_module_t *module)
{
    native_doc_cache_t *cache;
    size_t count;
    size_t i;
    size_t index = 0U;

    if (module == NULL || (!native_doc_module_has_docs(module) && !native_doc_module_has_any_symbols(module)) ||
        native_doc_cache_count >= 32U)
        return NULL;

    count = native_doc_count_module_entries(module);

    cache = &native_doc_caches[native_doc_cache_count++];
    memset(cache, 0, sizeof(*cache));
    cache->module = module;
    cache->entries = (vigil_doc_entry_t *)calloc(count, sizeof(vigil_doc_entry_t));
    if (cache->entries == NULL)
    {
        native_doc_cache_count -= 1U;
        return NULL;
    }
    cache->count = count;

    cache->entries[index].name = module->name;
    cache->entries[index].signature = NULL;
    cache->entries[index].summary = module->doc != NULL ? module->doc->summary : NULL;
    cache->entries[index].description = module->doc != NULL ? module->doc->description : NULL;
    cache->entries[index].example = module->doc != NULL ? module->doc->example : NULL;
    index += 1U;

    for (i = 0U; i < module->function_count; i++)
    {
        const vigil_native_module_function_t *function = &module->functions[i];
        cache->entries[index].name = native_doc_printf("%s.%s", module->name, function->name);
        cache->entries[index].signature = native_doc_build_function_signature(module, function);
        cache->entries[index].summary = function->doc != NULL ? function->doc->summary : NULL;
        cache->entries[index].description = function->doc != NULL ? function->doc->description : NULL;
        cache->entries[index].example = function->doc != NULL ? function->doc->example : NULL;
        index += 1U;
    }

    for (i = 0U; i < module->constant_count; i++)
    {
        const vigil_native_module_constant_t *nc = &module->constants[i];
        cache->entries[index].name = native_doc_printf("%s.%s", module->name, nc->name);
        cache->entries[index].signature =
            native_doc_printf("%s.%s = %lld", module->name, nc->name, (long long)nc->int_value);
        cache->entries[index].summary = nc->doc != NULL ? nc->doc->summary : NULL;
        cache->entries[index].description = nc->doc != NULL ? nc->doc->description : NULL;
        cache->entries[index].example = nc->doc != NULL ? nc->doc->example : NULL;
        index += 1U;
    }

    for (i = 0U; i < module->class_count; i++)
        native_doc_fill_class_entries(cache, &index, module, &module->classes[i]);

    cache->count = index;
    return cache;
}

static native_doc_cache_t *native_doc_get_module_cache(const char *module_name)
{
    const vigil_native_module_t *module;
    size_t i;

    if (module_name == NULL)
        return NULL;

    for (i = 0U; i < native_doc_cache_count; i++)
    {
        if (strcmp(native_doc_caches[i].module->name, module_name) == 0)
            return &native_doc_caches[i];
    }

    module = native_doc_find_stdlib_module(module_name);
    if (module == NULL)
        module = native_doc_find_plugin_module(module_name);
    if (module == NULL)
        return NULL;
    return native_doc_build_module_cache(module);
}

static const vigil_doc_entry_t *native_doc_lookup_entry(const char *name)
{
    const char *dot;
    native_doc_cache_t *cache;
    size_t i;
    char module_name[128];
    size_t module_length;

    if (name == NULL)
        return NULL;

    dot = strchr(name, '.');
    if (dot == NULL)
        cache = native_doc_get_module_cache(name);
    else
    {
        module_length = (size_t)(dot - name);
        if (module_length >= sizeof(module_name))
            return NULL;
        memcpy(module_name, name, module_length);
        module_name[module_length] = '\0';
        cache = native_doc_get_module_cache(module_name);
    }

    if (cache == NULL)
        return NULL;

    for (i = 0U; i < cache->count; i++)
    {
        if (strcmp(cache->entries[i].name, name) == 0)
            return &cache->entries[i];
    }
    return NULL;
}

static void native_doc_init_module_names(void)
{
    VIGIL_STDLIB_MODULE_TABLE(mods);
    vigil_plugin_entry_t plugin_mods[1U + VIGIL_PLUGIN_COUNT];
    size_t i;

    if (generated_module_names_ready)
        return;

    generated_module_name_count = 0U;
    generated_module_names[generated_module_name_count++] = "builtins";
    for (i = 0U; i < sizeof(mods) / sizeof(mods[0]); i++)
    {
        if (mods[i].module != NULL)
            generated_module_names[generated_module_name_count++] = mods[i].name;
    }
    vigil_plugin_fill_table_(plugin_mods);
    for (i = 0U; i < VIGIL_PLUGIN_COUNT; i++)
    {
        if (plugin_mods[i].module != NULL)
            generated_module_names[generated_module_name_count++] = plugin_mods[i].name;
    }
    generated_module_names_ready = 1;
}

/* ── Lookup Implementation ────────────────────────────────── */

const vigil_doc_entry_t *vigil_doc_lookup(const char *name)
{
    const vigil_doc_entry_t *generated;

    if (name == NULL)
        return NULL;

    generated = native_doc_lookup_entry(name);
    if (generated != NULL)
        return generated;
    generated = vigil_builtin_doc_lookup(name);
    if (generated != NULL)
        return generated;
    return vigil_string_method_doc_lookup(name);
}

const char **vigil_doc_list_modules(size_t *count)
{
    native_doc_init_module_names();
    if (count != NULL)
        *count = generated_module_name_count;
    return generated_module_names;
}

const vigil_doc_entry_t *vigil_doc_list_module(const char *module_name, size_t *count)
{
    native_doc_cache_t *generated;
    if (module_name == NULL)
        return NULL;

    generated = native_doc_get_module_cache(module_name);
    if (generated != NULL)
    {
        if (count != NULL)
            *count = generated->count;
        return generated->entries;
    }
    if (strcmp(module_name, "builtins") == 0)
    {
        return vigil_builtin_doc_entries(count);
    }
    if (strcmp(module_name, "strings") == 0)
    {
        return vigil_string_method_doc_entries(count);
    }
    return NULL;
}

vigil_status_t vigil_doc_entry_render(const vigil_allocator_t *allocator, const vigil_doc_entry_t *entry,
                                      char **out_text, size_t *out_length, vigil_error_t *error)
{
    char *buf;
    size_t len = 0;
    size_t cap = 1024;
    vigil_allocator_t a;

    if (entry == NULL || out_text == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "doc: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (allocator != NULL && vigil_allocator_is_valid(allocator))
        a = *allocator;
    else
        a = vigil_default_allocator();

    buf = (char *)a.allocate(a.user_data, cap);
    if (buf == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    /* Name/signature */
    if (entry->signature != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n\n", entry->signature);
    }
    else
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n\n", entry->name);
    }

    /* Summary */
    if (entry->summary != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n", entry->summary);
    }

    /* Description */
    if (entry->description != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "\n%s\n", entry->description);
    }

    /* Example */
    if (entry->example != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "\nExample:\n  %s\n", entry->example);
    }

    *out_text = buf;
    if (out_length)
        *out_length = len;
    return VIGIL_STATUS_OK;
}
