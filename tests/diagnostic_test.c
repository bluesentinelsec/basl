#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "vigil/vigil.h"

struct AllocatorStats
{
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

static void *CountedAllocate(void *user_data, size_t size)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->allocate_calls += 1;
    return calloc(1U, size);
}

static void *CountedReallocate(void *user_data, void *memory, size_t size)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->reallocate_calls += 1;
    return realloc(memory, size);
}

static void CountedDeallocate(void *user_data, void *memory)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->deallocate_calls += 1;
    free(memory);
}

TEST(VigilDiagnosticTest, InitStartsEmpty)
{
    vigil_diagnostic_list_t list;

    vigil_diagnostic_list_init(&list, NULL);

    EXPECT_EQ(list.runtime, NULL);
    EXPECT_EQ(list.items, NULL);
    EXPECT_EQ(list.count, 0U);
    EXPECT_EQ(list.capacity, 0U);
}

TEST(VigilDiagnosticTest, AppendCopiesMessageAndSpan)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_diagnostic_list_t list;
    vigil_source_span_t span = {7U, 2U, 5U};
    const vigil_diagnostic_t *diagnostic;
    char message[] = "unexpected token";

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_diagnostic_list_init(&list, runtime);

    ASSERT_EQ(vigil_diagnostic_list_append(&list, VIGIL_DIAGNOSTIC_ERROR, span, message, strlen(message), &error),
              VIGIL_STATUS_OK);

    message[0] = 'X';
    diagnostic = vigil_diagnostic_list_get(&list, 0U);
    ASSERT_NE(diagnostic, NULL);
    EXPECT_EQ(diagnostic->severity, VIGIL_DIAGNOSTIC_ERROR);
    EXPECT_EQ(diagnostic->span.source_id, 7U);
    EXPECT_EQ(diagnostic->span.start_offset, 2U);
    EXPECT_EQ(diagnostic->span.end_offset, 5U);
    EXPECT_STREQ(vigil_string_c_str(&diagnostic->message), "unexpected token");

    vigil_diagnostic_list_free(&list);
    vigil_runtime_close(&runtime);
}

TEST(VigilDiagnosticTest, ClearDropsItemsButKeepsListUsable)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_diagnostic_list_t list;
    size_t capacity;
    vigil_source_span_t warning_span = {1U, 0U, 1U};
    vigil_source_span_t note_span = {1U, 1U, 2U};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_diagnostic_list_init(&list, runtime);

    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_WARNING, warning_span, "warning", &error),
              VIGIL_STATUS_OK);
    capacity = list.capacity;

    vigil_diagnostic_list_clear(&list);

    EXPECT_EQ(vigil_diagnostic_list_count(&list), 0U);
    EXPECT_EQ(list.capacity, capacity);
    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_NOTE, note_span, "note", &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_diagnostic_list_count(&list), 1U);

    vigil_diagnostic_list_free(&list);
    vigil_runtime_close(&runtime);
}

TEST(VigilDiagnosticTest, RejectsMissingRuntime)
{
    vigil_diagnostic_list_t list;
    vigil_error_t error = {0};
    vigil_source_span_t span = {0U, 0U, 0U};

    vigil_diagnostic_list_init(&list, NULL);

    EXPECT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_ERROR, span, "bad", &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "diagnostic list runtime must not be null"), 0);
}

TEST(VigilDiagnosticTest, UsesRuntimeAllocatorHooks)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_diagnostic_list_t list;
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};
    vigil_source_span_t error_span = {1U, 0U, 1U};
    vigil_source_span_t note_span = {1U, 2U, 3U};
    vigil_source_span_t warning_span = {1U, 4U, 5U};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    vigil_diagnostic_list_init(&list, runtime);

    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_ERROR, error_span, "error", &error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_NOTE, note_span, "note", &error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_WARNING, warning_span, "warning", &error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_NOTE, note_span, "note 2", &error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_ERROR, error_span, "error 2", &error),
              VIGIL_STATUS_OK);

    EXPECT_GE(stats.allocate_calls, 3);

    vigil_diagnostic_list_free(&list);
    EXPECT_GE(stats.deallocate_calls, 3);
    vigil_runtime_close(&runtime);
}

TEST(VigilDiagnosticTest, FreeResetsWholeList)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_diagnostic_list_t list;
    vigil_source_span_t span = {1U, 0U, 1U};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_diagnostic_list_init(&list, runtime);
    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_ERROR, span, "error", &error), VIGIL_STATUS_OK);

    vigil_diagnostic_list_free(&list);

    EXPECT_EQ(list.runtime, NULL);
    EXPECT_EQ(list.items, NULL);
    EXPECT_EQ(list.count, 0U);
    EXPECT_EQ(list.capacity, 0U);
    vigil_runtime_close(&runtime);
}

TEST(VigilDiagnosticTest, SeverityNamesAreStable)
{
    EXPECT_STREQ(vigil_diagnostic_severity_name(VIGIL_DIAGNOSTIC_ERROR), "error");
    EXPECT_STREQ(vigil_diagnostic_severity_name(VIGIL_DIAGNOSTIC_WARNING), "warning");
    EXPECT_STREQ(vigil_diagnostic_severity_name(VIGIL_DIAGNOSTIC_NOTE), "note");
}

TEST(VigilDiagnosticTest, FormatIncludesPathLineColumnSeverityAndMessage)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_source_id_t source_id = 0U;
    vigil_diagnostic_list_t list;
    vigil_string_t output;
    vigil_source_span_t span;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&list, runtime);
    vigil_string_init(&output, runtime);

    ASSERT_EQ(vigil_source_registry_register_cstr(&registry, "main.vigil", "fn main() -> i32 {\n    return 1;\n}\n",
                                                  &source_id, &error),
              VIGIL_STATUS_OK);

    span.source_id = source_id;
    span.start_offset = 23U;
    span.end_offset = 29U;
    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_ERROR, span, "unexpected token", &error),
              VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_diagnostic_format(&registry, vigil_diagnostic_list_get(&list, 0U), &output, &error),
              VIGIL_STATUS_OK);
    EXPECT_STREQ(vigil_string_c_str(&output), "main.vigil:2:5: error: unexpected token");

    vigil_string_free(&output);
    vigil_diagnostic_list_free(&list);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilDiagnosticTest, FormatDoesNotTruncateLongPathPrefixes)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_source_id_t source_id = 0U;
    vigil_diagnostic_list_t list;
    vigil_string_t output;
    vigil_source_span_t span;
    const char *path = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.vigil";

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&list, runtime);
    vigil_string_init(&output, runtime);

    ASSERT_EQ(vigil_source_registry_register_cstr(&registry, path, "fn main() -> i32 {\n    return 1;\n}\n", &source_id,
                                                  &error),
              VIGIL_STATUS_OK);

    span.source_id = source_id;
    span.start_offset = 23U;
    span.end_offset = 29U;
    ASSERT_EQ(vigil_diagnostic_list_append_cstr(&list, VIGIL_DIAGNOSTIC_ERROR, span, "unexpected token", &error),
              VIGIL_STATUS_OK);

    ASSERT_EQ(vigil_diagnostic_format(&registry, vigil_diagnostic_list_get(&list, 0U), &output, &error),
              VIGIL_STATUS_OK);
    EXPECT_NE(strstr(vigil_string_c_str(&output), "error: unexpected token"), NULL);

    vigil_string_free(&output);
    vigil_diagnostic_list_free(&list);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

void register_diagnostic_tests(void)
{
    REGISTER_TEST(VigilDiagnosticTest, InitStartsEmpty);
    REGISTER_TEST(VigilDiagnosticTest, AppendCopiesMessageAndSpan);
    REGISTER_TEST(VigilDiagnosticTest, ClearDropsItemsButKeepsListUsable);
    REGISTER_TEST(VigilDiagnosticTest, RejectsMissingRuntime);
    REGISTER_TEST(VigilDiagnosticTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(VigilDiagnosticTest, FreeResetsWholeList);
    REGISTER_TEST(VigilDiagnosticTest, SeverityNamesAreStable);
    REGISTER_TEST(VigilDiagnosticTest, FormatIncludesPathLineColumnSeverityAndMessage);
    REGISTER_TEST(VigilDiagnosticTest, FormatDoesNotTruncateLongPathPrefixes);
}
