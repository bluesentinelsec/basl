#include "vigil/runtime.h"
#include "vigil/semantic.h"
#include "vigil/source.h"
#include "vigil_test.h"

/* ── Type utilities ───────────────────────────────────────── */

TEST(SemanticTest, TypeInvalid)
{
    vigil_semantic_type_t type = vigil_semantic_type_invalid();
    ASSERT_TRUE(!vigil_semantic_type_is_valid(type));
}

TEST(SemanticTest, TypePrimitive)
{
    vigil_semantic_type_t i32_type = vigil_semantic_type_primitive(VIGIL_TYPE_I32);
    vigil_semantic_type_t str_type = vigil_semantic_type_primitive(VIGIL_TYPE_STRING);

    ASSERT_TRUE(vigil_semantic_type_is_valid(i32_type));
    ASSERT_TRUE(vigil_semantic_type_is_valid(str_type));
    ASSERT_EQ(i32_type.kind, VIGIL_TYPE_I32);
    ASSERT_EQ(str_type.kind, VIGIL_TYPE_STRING);
}

TEST(SemanticTest, TypeEqual)
{
    vigil_semantic_type_t a = vigil_semantic_type_primitive(VIGIL_TYPE_I32);
    vigil_semantic_type_t b = vigil_semantic_type_primitive(VIGIL_TYPE_I32);
    vigil_semantic_type_t c = vigil_semantic_type_primitive(VIGIL_TYPE_STRING);

    ASSERT_TRUE(vigil_semantic_type_equal(a, b));
    ASSERT_TRUE(!vigil_semantic_type_equal(a, c));
}

/* ── File creation/destruction ────────────────────────────── */

TEST(SemanticTest, FileCreateDestroy)
{
    vigil_runtime_t *runtime = NULL;
    vigil_semantic_file_t *file = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_semantic_file_create(&file, runtime, 0, &error), VIGIL_STATUS_OK);
    ASSERT_NE(file, NULL);
    ASSERT_EQ(file->source_id, 0);

    vigil_semantic_file_destroy(&file);
    ASSERT_EQ(file, NULL);

    vigil_runtime_close(&runtime);
}

TEST(SemanticTest, FileNodeAtEmpty)
{
    vigil_runtime_t *runtime = NULL;
    vigil_semantic_file_t *file = NULL;
    vigil_error_t error = {0};

    vigil_runtime_open(&runtime, NULL, &error);
    vigil_semantic_file_create(&file, runtime, 0, &error);

    /* Empty file should return NULL for any position */
    ASSERT_EQ(vigil_semantic_file_node_at(file, 0), NULL);
    ASSERT_EQ(vigil_semantic_file_node_at(file, 100), NULL);

    vigil_semantic_file_destroy(&file);
    vigil_runtime_close(&runtime);
}

/* ── Index creation/destruction ───────────────────────────── */

TEST(SemanticTest, IndexCreateDestroy)
{
    vigil_runtime_t *runtime = NULL;
    vigil_source_registry_t registry;
    vigil_semantic_index_t *index = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);

    vigil_source_registry_init(&registry, runtime);

    ASSERT_EQ(vigil_semantic_index_create(&index, runtime, &registry, &error), VIGIL_STATUS_OK);
    ASSERT_NE(index, NULL);

    vigil_semantic_index_destroy(&index);
    ASSERT_EQ(index, NULL);

    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(SemanticTest, IndexGetFileNotFound)
{
    vigil_runtime_t *runtime = NULL;
    vigil_source_registry_t registry;
    vigil_semantic_index_t *index = NULL;
    vigil_error_t error = {0};

    vigil_runtime_open(&runtime, NULL, &error);
    vigil_source_registry_init(&registry, runtime);
    vigil_semantic_index_create(&index, runtime, &registry, &error);

    /* Non-existent file should return NULL */
    ASSERT_EQ(vigil_semantic_index_get_file(index, 0), NULL);
    ASSERT_EQ(vigil_semantic_index_get_file(index, 999), NULL);

    vigil_semantic_index_destroy(&index);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

/* ── Type to string ───────────────────────────────────────── */

TEST(SemanticTest, TypeToString)
{
    vigil_semantic_type_t i32_type = vigil_semantic_type_primitive(VIGIL_TYPE_I32);
    char *str = vigil_semantic_type_to_string(NULL, i32_type);

    ASSERT_NE(str, NULL);
    EXPECT_STREQ(str, "i32");

    free(str);
}

TEST(SemanticTest, TypeToStringInvalid)
{
    vigil_semantic_type_t invalid = vigil_semantic_type_invalid();
    char *str = vigil_semantic_type_to_string(NULL, invalid);

    ASSERT_NE(str, NULL);
    EXPECT_STREQ(str, "<invalid>");

    free(str);
}

/* ── Index analysis ───────────────────────────────────────── */

TEST(SemanticTest, AnalyzeValidSource)
{
    vigil_runtime_t *runtime = NULL;
    vigil_source_registry_t registry;
    vigil_semantic_index_t *index = NULL;
    vigil_source_id_t source_id = 0;
    vigil_error_t error = {0};
    const vigil_semantic_file_t *file;

    vigil_runtime_open(&runtime, NULL, &error);
    vigil_source_registry_init(&registry, runtime);

    /* Register a simple valid source. */
    vigil_source_registry_register_cstr(&registry, "test.vigil", "fn main() -> i32 { return 0; }", &source_id, &error);

    vigil_semantic_index_create(&index, runtime, &registry, &error);

    /* Analyze should succeed. */
    ASSERT_EQ(vigil_semantic_index_analyze(index, source_id, &error), VIGIL_STATUS_OK);

    /* File should be in index. */
    file = vigil_semantic_index_get_file(index, source_id);
    ASSERT_NE(file, NULL);
    ASSERT_EQ(file->source_id, source_id);

    /* Should have no diagnostics for valid source. */
    ASSERT_EQ(vigil_diagnostic_list_count(&file->diagnostics), 0);

    /* Index should have symbols (at least 'main'). */
    ASSERT_TRUE(vigil_debug_symbol_table_count(&index->symbols) >= 1);

    vigil_semantic_index_destroy(&index);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(SemanticTest, AnalyzeInvalidSource)
{
    vigil_runtime_t *runtime = NULL;
    vigil_source_registry_t registry;
    vigil_semantic_index_t *index = NULL;
    vigil_source_id_t source_id = 0;
    vigil_error_t error = {0};
    const vigil_semantic_file_t *file;

    vigil_runtime_open(&runtime, NULL, &error);
    vigil_source_registry_init(&registry, runtime);

    /* Register source with syntax error. */
    vigil_source_registry_register_cstr(&registry, "test.vigil", "fn main() -> i32 { return }", /* missing expression */
                                        &source_id, &error);

    vigil_semantic_index_create(&index, runtime, &registry, &error);

    /* Analyze should still succeed (captures diagnostics). */
    ASSERT_EQ(vigil_semantic_index_analyze(index, source_id, &error), VIGIL_STATUS_OK);

    /* File should be in index. */
    file = vigil_semantic_index_get_file(index, source_id);
    ASSERT_NE(file, NULL);

    /* Should have diagnostics for invalid source. */
    ASSERT_TRUE(vigil_diagnostic_list_count(&file->diagnostics) > 0);

    vigil_semantic_index_destroy(&index);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(SemanticTest, TypeToStringWithRuntime)
{
    vigil_runtime_t *runtime = NULL;
    vigil_source_registry_t registry;
    vigil_semantic_index_t *index = NULL;
    vigil_source_id_t source_id = 0;
    vigil_error_t error = {0};

    vigil_runtime_open(&runtime, NULL, &error);
    vigil_source_registry_init(&registry, runtime);
    vigil_source_registry_register_cstr(&registry, "test.vigil", "fn main() -> i32 { return 0; }", &source_id, &error);
    vigil_semantic_index_create(&index, runtime, &registry, &error);
    vigil_semantic_index_analyze(index, source_id, &error);

    vigil_semantic_type_t i32_type = vigil_semantic_type_primitive(VIGIL_TYPE_I32);
    char *str = vigil_semantic_type_to_string(index, i32_type);
    ASSERT_NE(str, NULL);
    EXPECT_STREQ(str, "i32");

    const vigil_allocator_t *alloc = vigil_runtime_allocator(runtime);
    alloc->deallocate(alloc->user_data, str);

    vigil_semantic_index_destroy(&index);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(SemanticTest, ReferencesAtIdentifier)
{
    vigil_runtime_t *runtime = NULL;
    vigil_source_registry_t registry;
    vigil_semantic_index_t *index = NULL;
    vigil_source_id_t source_id = 0;
    vigil_error_t error = {0};
    vigil_semantic_reference_t *refs = NULL;
    size_t ref_count = 0;

    vigil_runtime_open(&runtime, NULL, &error);
    vigil_source_registry_init(&registry, runtime);
    vigil_source_registry_register_cstr(&registry, "test.vigil", "let x: i32 = 1;\nfn main() -> i32 { return x; }",
                                        &source_id, &error);
    vigil_semantic_index_create(&index, runtime, &registry, &error);
    vigil_semantic_index_analyze(index, source_id, &error);

    /* 'x' at offset 4 in "let x: i32 = 1;" */
    vigil_status_t status = vigil_semantic_index_references_at(index, source_id, 4, &refs, &ref_count, &error);
    ASSERT_EQ(status, VIGIL_STATUS_OK);

    if (refs != NULL)
    {
        void *tmp = refs;
        vigil_runtime_free(runtime, &tmp);
    }

    vigil_semantic_index_destroy(&index);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

/* ── Test registration ────────────────────────────────────── */

void register_semantic_tests(void)
{
    REGISTER_TEST(SemanticTest, TypeInvalid);
    REGISTER_TEST(SemanticTest, TypePrimitive);
    REGISTER_TEST(SemanticTest, TypeEqual);
    REGISTER_TEST(SemanticTest, FileCreateDestroy);
    REGISTER_TEST(SemanticTest, FileNodeAtEmpty);
    REGISTER_TEST(SemanticTest, IndexCreateDestroy);
    REGISTER_TEST(SemanticTest, IndexGetFileNotFound);
    REGISTER_TEST(SemanticTest, TypeToString);
    REGISTER_TEST(SemanticTest, TypeToStringInvalid);
    REGISTER_TEST(SemanticTest, AnalyzeValidSource);
    REGISTER_TEST(SemanticTest, AnalyzeInvalidSource);
    REGISTER_TEST(SemanticTest, TypeToStringWithRuntime);
    REGISTER_TEST(SemanticTest, ReferencesAtIdentifier);
}
