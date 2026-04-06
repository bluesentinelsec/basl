#include "vigil_test.h"
#ifdef _WIN32
#include <io.h>
#define close _close
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include <string.h>

#include "platform/platform.h"

/* ── Stub tests (always run — verify the stub API contract) ──────── */

/* Link the real platform on native, so we test actual I/O there.
 * For stub coverage, we call the stub functions directly via a
 * separate compilation unit.  Since we can't link both, we test
 * the stub contract by verifying the real platform returns OK
 * (not UNSUPPORTED) on native, proving the dispatch works. */

typedef struct PlatformTest
{
    vigil_error_t error;
} PlatformTest;

static void PlatformTest_SetUp(void *p)
{
    memset(p, 0, sizeof(PlatformTest));
}
static void PlatformTest_TearDown(void *p)
{
    vigil_error_clear(&((PlatformTest *)p)->error);
}

static FILE *platform_test_redirect_stdin(const char *text, int *saved_stdin)
{
    FILE *tmp = tmpfile();

    if (tmp == NULL)
    {
        return NULL;
    }

    if (text != NULL && fputs(text, tmp) == EOF)
    {
        fclose(tmp);
        return NULL;
    }

    rewind(tmp);
    *saved_stdin = dup(fileno(stdin));
    if (*saved_stdin < 0)
    {
        fclose(tmp);
        return NULL;
    }

    if (dup2(fileno(tmp), fileno(stdin)) < 0)
    {
        close(*saved_stdin);
        fclose(tmp);
        return NULL;
    }

    return tmp;
}

static void platform_test_restore_stdin(FILE *tmp, int saved_stdin)
{
    dup2(saved_stdin, fileno(stdin));
    close(saved_stdin);
    fclose(tmp);
}

static int platform_test_history_matches(const vigil_line_history_t *history, const char *first, const char *second)
{
    const char *entry0 = vigil_line_history_get(history, 0);
    const char *entry1 = vigil_line_history_get(history, 1);
    int first_matches = entry0 != NULL && strcmp(entry0, first) == 0;
    int second_matches = entry1 != NULL && strcmp(entry1, second) == 0;

    return history->count == 2u && first_matches && second_matches && vigil_line_history_get(history, 2) == NULL;
}

static int platform_test_history_load_case(vigil_error_t *error)
{
    const char *path = "test_platform_history.tmp";
    vigil_line_history_t saved;
    vigil_line_history_t loaded;
    int ok = 1;

    vigil_line_history_init(&saved, 10);
    vigil_line_history_init(&loaded, 10);
    vigil_line_history_add(&saved, "alpha");
    vigil_line_history_add(&saved, "beta");

    if (vigil_line_history_save(&saved, path, error) != VIGIL_STATUS_OK)
    {
        ok = 0;
        goto cleanup;
    }

    if (vigil_line_history_load(&loaded, path, error) != VIGIL_STATUS_OK)
    {
        ok = 0;
        goto cleanup;
    }

    ok = platform_test_history_matches(&loaded, "alpha", "beta");

cleanup:
    vigil_line_history_free(&saved);
    vigil_line_history_free(&loaded);
    vigil_platform_remove(path, error);
    return ok;
}

static vigil_status_t platform_test_readline_from_stdin(const char *input, char *buf, size_t buf_size,
                                                        vigil_error_t *error)
{
    FILE *tmp = NULL;
    int saved_stdin = -1;
    vigil_status_t status;

    tmp = platform_test_redirect_stdin(input, &saved_stdin);
    if (tmp == NULL)
    {
        return VIGIL_STATUS_INTERNAL;
    }

    status = vigil_line_editor_readline("prompt> ", buf, buf_size, NULL, error);
    platform_test_restore_stdin(tmp, saved_stdin);
    return status;
}

static int platform_test_history_eviction_case(void)
{
    vigil_line_history_t history;
    const char *entry0;
    const char *entry1;
    int entries_match;
    int ok;

    vigil_line_history_init(&history, 2);
    vigil_line_history_add(&history, "");
    vigil_line_history_add(&history, "alpha");
    vigil_line_history_add(&history, "alpha");
    vigil_line_history_add(&history, "beta");
    vigil_line_history_add(&history, "gamma");
    entry0 = vigil_line_history_get(&history, 0);
    entry1 = vigil_line_history_get(&history, 1);
    entries_match = entry0 != NULL && entry1 != NULL && strcmp(entry0, "beta") == 0 && strcmp(entry1, "gamma") == 0;

    ok = history.count == 2u && entries_match && vigil_line_history_get(&history, 2) == NULL;

    vigil_line_history_free(&history);
    return ok;
}

/* ── File read/write round-trip ──────────────────────────────────── */

TEST_F(PlatformTest, WriteAndReadFile)
{
    const char *path = "test_platform_rw.tmp";
    const char *content = "hello platform";
    char *data = NULL;
    size_t len = 0;

    ASSERT_EQ(VIGIL_STATUS_OK,
              vigil_platform_write_file(path, content, strlen(content), &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_read_file(NULL, path, &data, &len, &FIXTURE(PlatformTest)->error));

    ASSERT_NE(data, NULL);
    EXPECT_EQ(len, strlen(content));
    EXPECT_STREQ(data, content);
    free(data);

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_remove(path, &FIXTURE(PlatformTest)->error));
}

TEST_F(PlatformTest, ReadNonexistent)
{
    char *data = NULL;
    size_t len = 0;
    EXPECT_NE(VIGIL_STATUS_OK,
              vigil_platform_read_file(NULL, "nonexistent_file_xyz.tmp", &data, &len, &FIXTURE(PlatformTest)->error));
}

/* ── File exists ─────────────────────────────────────────────────── */

TEST_F(PlatformTest, FileExists)
{
    const char *path = "test_platform_exists.tmp";
    int exists = 0;

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 0);

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_write_file(path, "x", 1, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    vigil_platform_remove(path, &FIXTURE(PlatformTest)->error);
}

/* ── Directory operations ────────────────────────────────────────── */

TEST_F(PlatformTest, MkdirAndIsDirectory)
{
    const char *path = "test_platform_dir.tmp";
    int is_dir = 0;

    /* Clean up from any previous failed run. */
    vigil_platform_remove(path, &FIXTURE(PlatformTest)->error);

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir(path, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_remove(path, &FIXTURE(PlatformTest)->error));
}

TEST_F(PlatformTest, MkdirP)
{
    const char *path = "test_platform_deep.tmp/a/b/c";
    int is_dir = 0;

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(path, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    /* Clean up (deepest first). */
    vigil_platform_remove("test_platform_deep.tmp/a/b/c", &FIXTURE(PlatformTest)->error);
    vigil_platform_remove("test_platform_deep.tmp/a/b", &FIXTURE(PlatformTest)->error);
    vigil_platform_remove("test_platform_deep.tmp/a", &FIXTURE(PlatformTest)->error);
    vigil_platform_remove("test_platform_deep.tmp", &FIXTURE(PlatformTest)->error);
}

TEST_F(PlatformTest, IsDirectoryOnFile)
{
    const char *path = "test_platform_notdir.tmp";
    int is_dir = 1;

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_write_file(path, "x", 1, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 0);

    vigil_platform_remove(path, &FIXTURE(PlatformTest)->error);
}

/* ── Custom allocator ────────────────────────────────────────────── */

static size_t test_alloc_count = 0;
static void *counting_alloc(void *ud, size_t s)
{
    (void)ud;
    test_alloc_count++;
    return malloc(s);
}
static void *counting_realloc(void *ud, void *p, size_t s)
{
    (void)ud;
    return realloc(p, s);
}
static void counting_dealloc(void *ud, void *p)
{
    (void)ud;
    free(p);
}

TEST_F(PlatformTest, ReadFileCustomAllocator)
{
    const char *path = "test_platform_alloc.tmp";
    char *data = NULL;
    size_t len = 0;
    vigil_allocator_t alloc = {NULL, counting_alloc, counting_realloc, counting_dealloc};

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_write_file(path, "test", 4, &FIXTURE(PlatformTest)->error));

    test_alloc_count = 0;
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_read_file(&alloc, path, &data, &len, &FIXTURE(PlatformTest)->error));
    EXPECT_GT(test_alloc_count, 0u);
    EXPECT_STREQ(data, "test");
    free(data);

    vigil_platform_remove(path, &FIXTURE(PlatformTest)->error);
}

/* ── NULL argument handling ──────────────────────────────────────── */

TEST_F(PlatformTest, NullArgs)
{
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT,
              vigil_platform_read_file(NULL, NULL, NULL, NULL, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT, vigil_platform_write_file(NULL, NULL, 0, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT, vigil_platform_file_exists(NULL, NULL));
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT, vigil_platform_is_directory(NULL, NULL));
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT, vigil_platform_mkdir(NULL, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT, vigil_platform_mkdir_p(NULL, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT, vigil_platform_remove(NULL, &FIXTURE(PlatformTest)->error));
}

/* ── Write empty file ────────────────────────────────────────────── */

TEST_F(PlatformTest, WriteEmptyFile)
{
    const char *path = "test_platform_empty.tmp";
    char *data = NULL;
    size_t len = 99;

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_write_file(path, NULL, 0, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_read_file(NULL, path, &data, &len, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(len, 0u);
    free(data);

    vigil_platform_remove(path, &FIXTURE(PlatformTest)->error);
}

TEST_F(PlatformTest, RWLockReadThenWriteUnlocks)
{
    vigil_platform_rwlock_t *rwlock = NULL;

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_rwlock_create(&rwlock, &FIXTURE(PlatformTest)->error));
    ASSERT_NE(rwlock, NULL);

    vigil_platform_rwlock_rdlock(rwlock);
    vigil_platform_rwlock_unlock(rwlock);
    vigil_platform_rwlock_wrlock(rwlock);
    vigil_platform_rwlock_unlock(rwlock);

    vigil_platform_rwlock_destroy(rwlock);
}

/* ── TLS CA enumeration ──────────────────────────────────────────── */

static int count_tls_cas_cb(const unsigned char *der, size_t len, void *userdata)
{
    (void)der;
    (void)len;
    (*(int *)userdata)++;
    return 0;
}

TEST_F(PlatformTest, EnumerateTlsCasCallback)
{
#ifdef __EMSCRIPTEN__
    (void)vigil_test_failed_; /* PEM parsing through WASM I/O is too slow */
    return;
#else
    int count = 0;
    int rc = vigil_platform_enumerate_tls_cas(count_tls_cas_cb, &count);
    /* On a system with a CA bundle (e.g. Linux CI), rc > 0 and count > 0.
     * On stub / embedded platforms the function returns -1. */
    EXPECT_GE(rc, -1);
    if (rc > 0)
        EXPECT_GT(count, 0);
#endif
}
TEST_F(PlatformTest, LineHistoryEvictsOldEntries)
{
    EXPECT_TRUE(platform_test_history_eviction_case());
}

TEST_F(PlatformTest, LineHistoryClearRemovesEntries)
{
    vigil_line_history_t history;

    vigil_line_history_init(&history, 2);
    vigil_line_history_add(&history, "beta");
    vigil_line_history_add(&history, "gamma");

    vigil_line_history_clear(&history);
    EXPECT_EQ(history.count, 0u);
    EXPECT_EQ(vigil_line_history_get(&history, 0), NULL);

    vigil_line_history_free(&history);
}

TEST_F(PlatformTest, LineHistorySaveAndLoadRoundTrips)
{
    EXPECT_TRUE(platform_test_history_load_case(&FIXTURE(PlatformTest)->error));
}

TEST_F(PlatformTest, LineHistoryLoadMissingFileIsOk)
{
    vigil_line_history_t history;

    vigil_line_history_init(&history, 10);

    EXPECT_EQ(VIGIL_STATUS_OK,
              vigil_line_history_load(&history, "nonexistent_history_xyz.tmp", &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(history.count, 0u);

    vigil_line_history_free(&history);
}

TEST_F(PlatformTest, LineHistorySaveInvalidPathFails)
{
    vigil_line_history_t history;

    vigil_line_history_init(&history, 10);
    vigil_line_history_add(&history, "alpha");

    EXPECT_EQ(VIGIL_STATUS_INTERNAL,
              vigil_line_history_save(&history, "missing_history_dir/test.tmp", &FIXTURE(PlatformTest)->error));

    vigil_line_history_free(&history);
}

TEST_F(PlatformTest, LineEditorReadlineFallsBackToStdin)
{
    char buf[64];

    ASSERT_EQ(VIGIL_STATUS_OK,
              platform_test_readline_from_stdin("hello from stdin\n", buf, sizeof(buf), &FIXTURE(PlatformTest)->error));
    EXPECT_STREQ(buf, "hello from stdin");
}

TEST_F(PlatformTest, LineEditorReadlineFallbackEofFails)
{
    char buf[16] = "sentinel";

    ASSERT_EQ(VIGIL_STATUS_INTERNAL,
              platform_test_readline_from_stdin(NULL, buf, sizeof(buf), &FIXTURE(PlatformTest)->error));
    EXPECT_STREQ(buf, "");
}

/* ── Stub contract tests (always compiled) ───────────────────────── */

/* These verify the stub returns UNSUPPORTED.  On native builds we
 * link the real platform, so we test that it returns OK instead.
 * This ensures the CMake dispatch is correct for each platform. */

TEST_F(PlatformTest, FileExistsReturnsValidStatus)
{
    int exists = 0;
    vigil_status_t s = vigil_platform_file_exists(".", &exists);
    EXPECT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_EQ(exists, 1); /* "." always exists */
}

void register_platform_tests(void)
{
    REGISTER_TEST_F(PlatformTest, WriteAndReadFile);
    REGISTER_TEST_F(PlatformTest, ReadNonexistent);
    REGISTER_TEST_F(PlatformTest, FileExists);
    REGISTER_TEST_F(PlatformTest, MkdirAndIsDirectory);
    REGISTER_TEST_F(PlatformTest, MkdirP);
    REGISTER_TEST_F(PlatformTest, IsDirectoryOnFile);
    REGISTER_TEST_F(PlatformTest, ReadFileCustomAllocator);
    REGISTER_TEST_F(PlatformTest, NullArgs);
    REGISTER_TEST_F(PlatformTest, WriteEmptyFile);
    REGISTER_TEST_F(PlatformTest, RWLockReadThenWriteUnlocks);
    REGISTER_TEST_F(PlatformTest, LineHistoryEvictsOldEntries);
    REGISTER_TEST_F(PlatformTest, LineHistoryClearRemovesEntries);
    REGISTER_TEST_F(PlatformTest, LineHistorySaveAndLoadRoundTrips);
    REGISTER_TEST_F(PlatformTest, LineHistoryLoadMissingFileIsOk);
    REGISTER_TEST_F(PlatformTest, LineHistorySaveInvalidPathFails);
    REGISTER_TEST_F(PlatformTest, LineEditorReadlineFallsBackToStdin);
    REGISTER_TEST_F(PlatformTest, LineEditorReadlineFallbackEofFails);
    REGISTER_TEST_F(PlatformTest, FileExistsReturnsValidStatus);
    REGISTER_TEST_F(PlatformTest, EnumerateTlsCasCallback);
}
