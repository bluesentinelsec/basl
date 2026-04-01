#include "vigil/doc_registry.h"
#include "vigil/stdlib.h"
#include "vigil_test.h"

#include <stdio.h>
#include <string.h>

TEST(DocRegistryTest, LookupBuiltin)
{
    const vigil_doc_entry_t *entry = vigil_doc_lookup("len");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "len");
    ASSERT_NE(entry->signature, NULL);
    ASSERT_NE(entry->summary, NULL);
}

TEST(DocRegistryTest, LookupModule)
{
    const vigil_doc_entry_t *entry = vigil_doc_lookup("math");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "math");
    EXPECT_EQ(entry->signature, NULL); /* Modules have no signature */
    ASSERT_NE(entry->summary, NULL);
}

TEST(DocRegistryTest, LookupQualified)
{
    const vigil_doc_entry_t *entry = vigil_doc_lookup("math.sqrt");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "math.sqrt");
    ASSERT_NE(entry->signature, NULL);
}

TEST(DocRegistryTest, LookupNotFound)
{
    const vigil_doc_entry_t *entry = vigil_doc_lookup("nonexistent");
    EXPECT_EQ(entry, NULL);
}

TEST(DocRegistryTest, ListModules)
{
    size_t count = 0;
    const char **modules = vigil_doc_list_modules(&count);
    ASSERT_NE(modules, NULL);
    EXPECT_GT(count, 0u);
}

TEST(DocRegistryTest, ListModuleContents)
{
    size_t count = 0;
    const vigil_doc_entry_t *entries = vigil_doc_list_module("math", &count);
    ASSERT_NE(entries, NULL);
    EXPECT_GT(count, 1u); /* Module entry + functions */
}

TEST(DocRegistryTest, RenderEntry)
{
    const vigil_doc_entry_t *entry = vigil_doc_lookup("len");
    char *text = NULL;
    size_t len = 0;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_doc_entry_render(NULL, entry, &text, &len, &error), VIGIL_STATUS_OK);
    ASSERT_NE(text, NULL);
    EXPECT_GT(len, 0u);
    free(text);
}

static int module_name_in_list(const char *name, const char **modules, size_t module_count)
{
    size_t i;

    for (i = 0U; i < module_count; i += 1U)
    {
        if (strcmp(name, modules[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

TEST(DocRegistryTest, CoversAllStdlibModulesAndFunctions)
{
    const vigil_native_module_t *modules[] = {
        &vigil_stdlib_args,     &vigil_stdlib_atomic, &vigil_stdlib_compress, &vigil_stdlib_crypto, &vigil_stdlib_csv,
#ifdef VIGIL_HAS_STDLIB_FFI
        &vigil_stdlib_ffi,
#endif
        &vigil_stdlib_fmt,
#ifdef VIGIL_HAS_STDLIB_FS
        &vigil_stdlib_fs,
#endif
#ifdef VIGIL_HAS_STDLIB_HTTP
        &vigil_stdlib_http,
#endif
        &vigil_stdlib_log,      &vigil_stdlib_math,
#ifdef VIGIL_HAS_STDLIB_NET
        &vigil_stdlib_net,
#endif
        &vigil_stdlib_parse,    &vigil_stdlib_random,
#ifdef VIGIL_HAS_STDLIB_READLINE
        &vigil_stdlib_readline,
#endif
        &vigil_stdlib_regex,    &vigil_stdlib_test,
#ifdef VIGIL_HAS_STDLIB_THREAD
        &vigil_stdlib_thread,
#endif
#ifdef VIGIL_HAS_STDLIB_TIME
        &vigil_stdlib_time,
#endif
        &vigil_stdlib_unsafe,   &vigil_stdlib_url,    &vigil_stdlib_yaml,
    };
    size_t module_count = 0U;
    const char **listed_modules = vigil_doc_list_modules(&module_count);
    size_t module_index;

    ASSERT_NE(listed_modules, NULL);

    for (module_index = 0U; module_index < sizeof(modules) / sizeof(modules[0]); module_index += 1U)
    {
        const vigil_native_module_t *module = modules[module_index];
        const vigil_doc_entry_t *module_entry = NULL;
        const vigil_doc_entry_t *module_entries = NULL;
        size_t entry_count = 0U;
        size_t expected_min_entries = 1U + module->function_count;
        size_t function_index;
        size_t class_index;

        ASSERT_NE(module, NULL);
        EXPECT_TRUE(module_name_in_list(module->name, listed_modules, module_count));

        module_entry = vigil_doc_lookup(module->name);
        ASSERT_NE(module_entry, NULL);
        EXPECT_EQ(module_entry->signature, NULL);

        module_entries = vigil_doc_list_module(module->name, &entry_count);
        ASSERT_NE(module_entries, NULL);

        for (function_index = 0U; function_index < module->function_count; function_index += 1U)
        {
            const vigil_native_module_function_t *function = &module->functions[function_index];
            const vigil_doc_entry_t *entry = NULL;
            char qualified_name[128];
            int written;

            written = snprintf(qualified_name, sizeof(qualified_name), "%s.%s", module->name, function->name);
            ASSERT_TRUE(written > 0);
            ASSERT_TRUE((size_t)written < sizeof(qualified_name));

            entry = vigil_doc_lookup(qualified_name);
            ASSERT_NE(entry, NULL);
            EXPECT_STREQ(entry->name, qualified_name);
            ASSERT_NE(entry->signature, NULL);
            ASSERT_NE(entry->summary, NULL);
        }

        for (class_index = 0U; class_index < module->class_count; class_index += 1U)
        {
            const vigil_native_class_t *klass = &module->classes[class_index];
            const vigil_doc_entry_t *class_entry = NULL;
            char class_name[160];
            int written;
            size_t field_index;
            size_t method_index;

            expected_min_entries += 1U + klass->field_count + klass->method_count;

            written = snprintf(class_name, sizeof(class_name), "%s.%s", module->name, klass->name);
            ASSERT_TRUE(written > 0);
            ASSERT_TRUE((size_t)written < sizeof(class_name));

            class_entry = vigil_doc_lookup(class_name);
            ASSERT_NE(class_entry, NULL);
            EXPECT_STREQ(class_entry->name, class_name);
            ASSERT_NE(class_entry->signature, NULL);
            ASSERT_NE(class_entry->summary, NULL);

            for (field_index = 0U; field_index < klass->field_count; field_index += 1U)
            {
                const vigil_native_class_field_t *field = &klass->fields[field_index];
                const vigil_doc_entry_t *field_entry = NULL;
                char field_name[192];

                written = snprintf(field_name, sizeof(field_name), "%s.%s", class_name, field->name);
                ASSERT_TRUE(written > 0);
                ASSERT_TRUE((size_t)written < sizeof(field_name));

                field_entry = vigil_doc_lookup(field_name);
                ASSERT_NE(field_entry, NULL);
                EXPECT_STREQ(field_entry->name, field_name);
                ASSERT_NE(field_entry->signature, NULL);
                ASSERT_NE(field_entry->summary, NULL);
            }

            for (method_index = 0U; method_index < klass->method_count; method_index += 1U)
            {
                const vigil_native_class_method_t *method = &klass->methods[method_index];
                const vigil_doc_entry_t *method_entry = NULL;
                char method_name[192];

                written = snprintf(method_name, sizeof(method_name), "%s.%s", class_name, method->name);
                ASSERT_TRUE(written > 0);
                ASSERT_TRUE((size_t)written < sizeof(method_name));

                method_entry = vigil_doc_lookup(method_name);
                ASSERT_NE(method_entry, NULL);
                EXPECT_STREQ(method_entry->name, method_name);
                ASSERT_NE(method_entry->signature, NULL);
                ASSERT_NE(method_entry->summary, NULL);
            }
        }

        EXPECT_GE(entry_count, expected_min_entries);
    }
}

TEST(DocRegistryTest, DescriptorBackedFmtAndArgsDocsRenderDerivedSignatures)
{
    const vigil_doc_entry_t *fmt_print = vigil_doc_lookup("fmt.print");
    const vigil_doc_entry_t *parser_new = vigil_doc_lookup("args.Parser.new");

    ASSERT_NE(fmt_print, NULL);
    ASSERT_NE(parser_new, NULL);
    EXPECT_STREQ(fmt_print->signature, "fmt.print(value: string) -> void");
    EXPECT_STREQ(fmt_print->summary, "Print a string to stdout without a newline.");
    EXPECT_STREQ(parser_new->signature, "args.Parser.new(prog: string, desc: string) -> args.Parser");
    EXPECT_STREQ(parser_new->summary, "Create a parser.");
}

TEST(DocRegistryTest, ModuleListUsesCanonicalStdlibSet)
{
    size_t count = 0U;
    const char **modules = vigil_doc_list_modules(&count);

    ASSERT_NE(modules, NULL);
    EXPECT_TRUE(module_name_in_list("builtins", modules, count));
    EXPECT_TRUE(module_name_in_list("fmt", modules, count));
    EXPECT_TRUE(module_name_in_list("args", modules, count));
    EXPECT_FALSE(module_name_in_list("strings", modules, count));
    EXPECT_FALSE(module_name_in_list("sdl", modules, count));
}

void register_doc_registry_tests(void)
{
    REGISTER_TEST(DocRegistryTest, LookupBuiltin);
    REGISTER_TEST(DocRegistryTest, LookupModule);
    REGISTER_TEST(DocRegistryTest, LookupQualified);
    REGISTER_TEST(DocRegistryTest, LookupNotFound);
    REGISTER_TEST(DocRegistryTest, ListModules);
    REGISTER_TEST(DocRegistryTest, ListModuleContents);
    REGISTER_TEST(DocRegistryTest, RenderEntry);
    REGISTER_TEST(DocRegistryTest, CoversAllStdlibModulesAndFunctions);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedFmtAndArgsDocsRenderDerivedSignatures);
    REGISTER_TEST(DocRegistryTest, ModuleListUsesCanonicalStdlibSet);
}
