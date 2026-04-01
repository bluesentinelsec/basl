#include "vigil/builtins.h"
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

TEST(DocRegistryTest, LookupBuiltinConversionsAndErrorConstructor)
{
    const vigil_doc_entry_t *to_i32 = vigil_doc_lookup("i32");
    const vigil_doc_entry_t *err_ctor = vigil_doc_lookup("err");

    ASSERT_NE(to_i32, NULL);
    ASSERT_NE(err_ctor, NULL);
    EXPECT_STREQ(to_i32->signature, "i32(value: integer | f64) -> i32");
    EXPECT_STREQ(err_ctor->signature, "err(message: string, kind: i32) -> err");
}

TEST(DocRegistryTest, CoversAllDeclaredBuiltinsAndStringMethods)
{
    const vigil_builtin_descriptor_t *builtins = NULL;
    const vigil_string_method_descriptor_t *string_methods = NULL;
    size_t builtin_count = 0U;
    size_t string_method_count = 0U;
    size_t i;

    builtins = vigil_builtin_descriptors(&builtin_count);
    ASSERT_NE(builtins, NULL);
    EXPECT_GT(builtin_count, 0U);

    for (i = 0U; i < builtin_count; i += 1U)
    {
        const vigil_doc_entry_t *entry = vigil_doc_lookup(builtins[i].name);
        ASSERT_NE(entry, NULL);
        EXPECT_STREQ(entry->name, builtins[i].name);
        ASSERT_NE(entry->signature, NULL);
        ASSERT_NE(entry->summary, NULL);
    }

    string_methods = vigil_string_method_descriptors(&string_method_count);
    ASSERT_NE(string_methods, NULL);
    EXPECT_GT(string_method_count, 0U);

    for (i = 0U; i < string_method_count; i += 1U)
    {
        const vigil_doc_entry_t *entry = vigil_doc_lookup(string_methods[i].doc_entry->name);
        ASSERT_NE(entry, NULL);
        EXPECT_STREQ(entry->name, string_methods[i].doc_entry->name);
        ASSERT_NE(entry->signature, NULL);
        ASSERT_NE(entry->summary, NULL);
    }
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

TEST(DocRegistryTest, NativeDescriptorDocsAreCompleteForCompiledModules)
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
        &vigil_stdlib_json,     &vigil_stdlib_log,    &vigil_stdlib_math,
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
    size_t module_index;

    for (module_index = 0U; module_index < sizeof(modules) / sizeof(modules[0]); module_index += 1U)
    {
        const vigil_native_module_t *module = modules[module_index];
        size_t function_index;
        size_t class_index;

        ASSERT_NE(module, NULL);
        ASSERT_NE(module->doc, NULL);
        ASSERT_NE(module->doc->summary, NULL);

        for (function_index = 0U; function_index < module->function_count; function_index += 1U)
        {
            const vigil_native_module_function_t *function = &module->functions[function_index];

            ASSERT_NE(function->doc, NULL);
            ASSERT_NE(function->doc->summary, NULL);
        }

        for (class_index = 0U; class_index < module->class_count; class_index += 1U)
        {
            const vigil_native_class_t *klass = &module->classes[class_index];
            size_t field_index;
            size_t method_index;

            ASSERT_NE(klass->doc, NULL);
            ASSERT_NE(klass->doc->summary, NULL);

            for (field_index = 0U; field_index < klass->field_count; field_index += 1U)
            {
                const vigil_native_class_field_t *field = &klass->fields[field_index];

                ASSERT_NE(field->doc, NULL);
                ASSERT_NE(field->doc->summary, NULL);
            }

            for (method_index = 0U; method_index < klass->method_count; method_index += 1U)
            {
                const vigil_native_class_method_t *method = &klass->methods[method_index];

                ASSERT_NE(method->doc, NULL);
                ASSERT_NE(method->doc->summary, NULL);
            }
        }
    }
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForFmtArgsAndTest)
{
    const vigil_doc_entry_t *fmt_print = vigil_doc_lookup("fmt.print");
    const vigil_doc_entry_t *parser_new = vigil_doc_lookup("args.Parser.new");
    const vigil_doc_entry_t *test_assert = vigil_doc_lookup("test.T.assert");

    ASSERT_NE(fmt_print, NULL);
    ASSERT_NE(parser_new, NULL);
    ASSERT_NE(test_assert, NULL);
    EXPECT_STREQ(fmt_print->signature, "fmt.print(value: string) -> void");
    EXPECT_STREQ(fmt_print->summary, "Print a string to stdout without a newline.");
    EXPECT_STREQ(parser_new->signature, "args.Parser.new(prog: string, desc: string) -> args.Parser");
    EXPECT_STREQ(parser_new->summary, "Create a parser.");
    EXPECT_STREQ(test_assert->signature, "test.T.assert(condition: bool, message: string) -> void");
    EXPECT_STREQ(test_assert->summary, "Assert that a condition is true.");
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForRandomAndParse)
{
    const vigil_doc_entry_t *random_range = vigil_doc_lookup("random.range");
    const vigil_doc_entry_t *parse_i32 = vigil_doc_lookup("parse.i32");

    ASSERT_NE(random_range, NULL);
    ASSERT_NE(parse_i32, NULL);
    EXPECT_STREQ(random_range->signature, "random.range(min: i32, max: i32) -> i32");
    EXPECT_STREQ(parse_i32->signature, "parse.i32(s: string) -> (i32, err)");
}

TEST(DocRegistryTest, ReadlineDocsRenderWhenModuleIsCompiledIn)
{
#ifdef VIGIL_HAS_STDLIB_READLINE
    const vigil_doc_entry_t *readline_input = vigil_doc_lookup("readline.input");

    ASSERT_NE(readline_input, NULL);
    EXPECT_STREQ(readline_input->signature, "readline.input(prompt: string) -> string");
#else
    (void)vigil_test_failed_;
#endif
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForUrlYamlCsvAndLog)
{
    const vigil_doc_entry_t *url_scheme = vigil_doc_lookup("url.scheme");
    const vigil_doc_entry_t *yaml_get = vigil_doc_lookup("yaml.get");
    const vigil_doc_entry_t *csv_parse = vigil_doc_lookup("csv.parse");
    const vigil_doc_entry_t *log_set_level = vigil_doc_lookup("log.set_level");

    ASSERT_NE(url_scheme, NULL);
    ASSERT_NE(yaml_get, NULL);
    ASSERT_NE(csv_parse, NULL);
    ASSERT_NE(log_set_level, NULL);
    EXPECT_STREQ(url_scheme->signature, "url.scheme(url: string) -> string");
    EXPECT_STREQ(yaml_get->signature, "yaml.get(yaml: string, path: string) -> string");
    EXPECT_STREQ(csv_parse->signature, "csv.parse(data: string) -> array<array<string>>");
    EXPECT_STREQ(log_set_level->signature, "log.set_level(level: string) -> void");
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForRegexAndAtomic)
{
    const vigil_doc_entry_t *regex_find_all = vigil_doc_lookup("regex.find_all");
    const vigil_doc_entry_t *atomic_store = vigil_doc_lookup("atomic.store");

    ASSERT_NE(regex_find_all, NULL);
    ASSERT_NE(atomic_store, NULL);
    EXPECT_STREQ(regex_find_all->signature, "regex.find_all(pattern: string, input: string) -> array<string>");
    EXPECT_STREQ(atomic_store->signature, "atomic.store(a: i64, val: i64) -> bool");
}

TEST(DocRegistryTest, StringMethodDocsIncludeToC)
{
    const vigil_doc_entry_t *to_c = vigil_doc_lookup("strings.to_c");

    ASSERT_NE(to_c, NULL);
    EXPECT_STREQ(to_c->signature, "s.to_c() -> string");
    EXPECT_STREQ(to_c->summary, "Return s escaped as a C string literal body.");
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForNetAndTime)
{
#ifdef VIGIL_HAS_STDLIB_NET
    const vigil_doc_entry_t *net_send = vigil_doc_lookup("net.udp_send");
    ASSERT_NE(net_send, NULL);
    EXPECT_STREQ(net_send->signature, "net.udp_send(sock: i64, host: string, port: i32, data: string) -> i32");
#endif

#ifdef VIGIL_HAS_STDLIB_TIME
    const vigil_doc_entry_t *time_date = vigil_doc_lookup("time.date");

    ASSERT_NE(time_date, NULL);
    EXPECT_STREQ(time_date->signature, "time.date(y: i32, m: i32, d: i32, h: i32, min: i32, s: i32) -> i64");
#endif

#if !defined(VIGIL_HAS_STDLIB_NET) && !defined(VIGIL_HAS_STDLIB_TIME)
    (void)vigil_test_failed_;
#endif
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForCrypto)
{
    const vigil_doc_entry_t *pbkdf2 = vigil_doc_lookup("crypto.pbkdf2");
    const vigil_doc_entry_t *encrypt = vigil_doc_lookup("crypto.encrypt");

    ASSERT_NE(pbkdf2, NULL);
    ASSERT_NE(encrypt, NULL);
    EXPECT_STREQ(pbkdf2->signature, "crypto.pbkdf2(password: string, salt: string, iterations: i32, key_len: i32) -> "
                                    "string");
    EXPECT_STREQ(pbkdf2->summary, "PBKDF2 key derivation.");
    EXPECT_STREQ(encrypt->signature, "crypto.encrypt(key: string, nonce: string, plaintext: string) -> string");
    EXPECT_STREQ(encrypt->summary, "AES-256-GCM encryption.");
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForThreadAndCompress)
{
    const vigil_doc_entry_t *compress_zip = vigil_doc_lookup("compress.zip_create_level");

    ASSERT_NE(compress_zip, NULL);
    EXPECT_STREQ(compress_zip->signature,
                 "compress.zip_create_level(names: array<string>, contents: array<string>, level: i32) -> string");
    EXPECT_STREQ(compress_zip->summary, "Create ZIP archive at level.");

#ifdef VIGIL_HAS_STDLIB_THREAD
    {
        const vigil_doc_entry_t *thread_wait = vigil_doc_lookup("thread.wait_timeout");

        ASSERT_NE(thread_wait, NULL);
        EXPECT_STREQ(thread_wait->signature, "thread.wait_timeout(c: i64, m: i64, ms: i64) -> bool");
        EXPECT_STREQ(thread_wait->summary, "Wait on a condition variable with timeout.");
    }
#endif
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForFfiAndUnsafe)
{
    const vigil_doc_entry_t *unsafe_copy = vigil_doc_lookup("unsafe.copy");

    ASSERT_NE(unsafe_copy, NULL);
    EXPECT_STREQ(unsafe_copy->signature,
                 "unsafe.copy(dst: i64, dst_off: i32, src: i64, src_off: i32, len: i32) -> void");
    EXPECT_STREQ(unsafe_copy->summary, "Copy bytes between buffers.");

#ifdef VIGIL_HAS_STDLIB_FFI
    {
        const vigil_doc_entry_t *ffi_bind = vigil_doc_lookup("ffi.bind");

        ASSERT_NE(ffi_bind, NULL);
        EXPECT_STREQ(ffi_bind->signature, "ffi.bind(lib: i64, name: string, signature: string) -> i64");
        EXPECT_STREQ(ffi_bind->summary, "Bind a C function by signature.");
    }
#endif
}

TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForHttpJsonFsAndMath)
{
    const vigil_doc_entry_t *json_parse = vigil_doc_lookup("json.Value.parse");
    const vigil_doc_entry_t *math_rotate = vigil_doc_lookup("math.Vec3.rotateByQuaternion");

    ASSERT_NE(json_parse, NULL);
    ASSERT_NE(math_rotate, NULL);
    EXPECT_STREQ(json_parse->signature, "json.Value.parse(text: string) -> (json.Value, err)");
    EXPECT_STREQ(json_parse->summary, "Parse JSON text.");
    EXPECT_STREQ(math_rotate->signature, "math.Vec3.rotateByQuaternion(rotation: math.Quaternion) -> math.Vec3");
    EXPECT_STREQ(math_rotate->summary, "Rotate the vector by a quaternion.");

#ifdef VIGIL_HAS_STDLIB_HTTP
    {
        const vigil_doc_entry_t *http_get = vigil_doc_lookup("http.get");

        ASSERT_NE(http_get, NULL);
        EXPECT_STREQ(http_get->signature, "http.get(url: string) -> (i32, string, string)");
        EXPECT_STREQ(http_get->summary, "Issue an HTTP GET request.");
    }
#endif

#ifdef VIGIL_HAS_STDLIB_FS
    {
        const vigil_doc_entry_t *fs_reader_open = vigil_doc_lookup("fs.Reader.open");

        ASSERT_NE(fs_reader_open, NULL);
        EXPECT_STREQ(fs_reader_open->signature, "fs.Reader.open(path: string) -> (fs.Reader, err)");
        EXPECT_STREQ(fs_reader_open->summary, "Open a file for reading.");
    }
#endif
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
    REGISTER_TEST(DocRegistryTest, LookupBuiltinConversionsAndErrorConstructor);
    REGISTER_TEST(DocRegistryTest, CoversAllDeclaredBuiltinsAndStringMethods);
    REGISTER_TEST(DocRegistryTest, LookupModule);
    REGISTER_TEST(DocRegistryTest, LookupQualified);
    REGISTER_TEST(DocRegistryTest, LookupNotFound);
    REGISTER_TEST(DocRegistryTest, ListModules);
    REGISTER_TEST(DocRegistryTest, ListModuleContents);
    REGISTER_TEST(DocRegistryTest, RenderEntry);
    REGISTER_TEST(DocRegistryTest, CoversAllStdlibModulesAndFunctions);
    REGISTER_TEST(DocRegistryTest, NativeDescriptorDocsAreCompleteForCompiledModules);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForFmtArgsAndTest);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForRandomAndParse);
    REGISTER_TEST(DocRegistryTest, ReadlineDocsRenderWhenModuleIsCompiledIn);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForUrlYamlCsvAndLog);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForRegexAndAtomic);
    REGISTER_TEST(DocRegistryTest, StringMethodDocsIncludeToC);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForNetAndTime);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForCrypto);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForThreadAndCompress);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForFfiAndUnsafe);
    REGISTER_TEST(DocRegistryTest, DescriptorBackedDocsRenderDerivedSignaturesForHttpJsonFsAndMath);
    REGISTER_TEST(DocRegistryTest, ModuleListUsesCanonicalStdlibSet);
}
