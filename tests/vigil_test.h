/*
 * vigil_test.h — Minimal C11 test harness with a GoogleTest-compatible API.
 *
 * Single-header, no dependencies beyond C11 stdlib.  No compiler
 * extensions — registration is explicit rather than automatic.
 *
 * Usage:
 *   #define VIGIL_TEST_IMPLEMENTATION   // in exactly ONE .c file
 *   #include "vigil_test.h"
 *
 * Each test file defines tests with TEST() / TEST_F() and provides a
 * register function that calls REGISTER_TEST / REGISTER_TEST_F for
 * each test.  test_main.c calls all register functions before running.
 */
#ifndef VIGIL_TEST_H
#define VIGIL_TEST_H

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Test registration ───────────────────────────────────────────── */

    typedef void (*vigil_test_fn_t)(int *vigil_test_failed_);
    typedef void (*vigil_test_fixture_fn_t)(void *fixture);

    typedef struct vigil_test_entry
    {
        const char *suite;
        const char *name;
        vigil_test_fn_t fn;
        vigil_test_fixture_fn_t setup;
        vigil_test_fixture_fn_t teardown;
        size_t fixture_size;
    } vigil_test_entry_t;

#ifndef VIGIL_TEST_MAX
#define VIGIL_TEST_MAX 2048
#endif

    extern vigil_test_entry_t vigil_test_entries_[];
    extern int vigil_test_count_;
    extern void *vigil_test_fixture_ptr_;

    static inline void vigil_test_register_(const char *suite, const char *name, vigil_test_fn_t fn,
                                            vigil_test_fixture_fn_t setup, vigil_test_fixture_fn_t teardown,
                                            size_t fixture_size)
    {
        if (vigil_test_count_ < VIGIL_TEST_MAX)
        {
            vigil_test_entry_t *e = &vigil_test_entries_[vigil_test_count_++];
            e->suite = suite;
            e->name = name;
            e->fn = fn;
            e->setup = setup;
            e->teardown = teardown;
            e->fixture_size = fixture_size;
        }
    }

    /* ── Explicit registration macros ────────────────────────────────── */

#define REGISTER_TEST(suite, name)                                                                                     \
    vigil_test_register_(#suite, #name, vigil_test_##suite##_##name##_body_, NULL, NULL, 0)

#define REGISTER_TEST_F(fixture, name)                                                                                 \
    vigil_test_register_(#fixture, #name, vigil_test_##fixture##_##name##_body_, fixture##_SetUp, fixture##_TearDown,  \
                         sizeof(fixture))

    /* ── TEST() / TEST_F() — define the test body ────────────────────── */

#define TEST(suite, name) static void vigil_test_##suite##_##name##_body_(int *vigil_test_failed_)

#define TEST_F(fixture, name) static void vigil_test_##fixture##_##name##_body_(int *vigil_test_failed_)

#define FIXTURE(type) ((type *)vigil_test_fixture_ptr_)

    /* ── Assertion helpers ───────────────────────────────────────────── */

#define VIGIL_TEST_FAIL_(msg)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        fprintf(stderr, "  %s:%d: Failure\n%s\n", __FILE__, __LINE__, msg);                                            \
        *vigil_test_failed_ = 1;                                                                                       \
    } while (0)

#define VIGIL_TEST_FATAL_(msg)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        VIGIL_TEST_FAIL_(msg);                                                                                         \
        return;                                                                                                        \
    } while (0)

    /* ── EXPECT / ASSERT macros ──────────────────────────────────────── */

#define EXPECT_EQ(a, b)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        long long a_ = (long long)(a), b_ = (long long)(b);                                                            \
        if (a_ != b_)                                                                                                  \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected: %lld\n    Actual:   %lld", b_, a_);                            \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define ASSERT_EQ(a, b)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        long long a_ = (long long)(a), b_ = (long long)(b);                                                            \
        if (a_ != b_)                                                                                                  \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected: %lld\n    Actual:   %lld", b_, a_);                            \
            VIGIL_TEST_FATAL_(msg_);                                                                                   \
        }                                                                                                              \
    } while (0)

#define EXPECT_NE(a, b)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        long long a_ = (long long)(a), b_ = (long long)(b);                                                            \
        if (a_ == b_)                                                                                                  \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected not equal, both: %lld", a_);                                    \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define ASSERT_NE(a, b)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        long long a_ = (long long)(a), b_ = (long long)(b);                                                            \
        if (a_ == b_)                                                                                                  \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected not equal, both: %lld", a_);                                    \
            VIGIL_TEST_FATAL_(msg_);                                                                                   \
        }                                                                                                              \
    } while (0)

#define EXPECT_TRUE(expr)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
            VIGIL_TEST_FAIL_("    Expected true: " #expr);                                                             \
    } while (0)

#define ASSERT_TRUE(expr)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
            VIGIL_TEST_FATAL_("    Expected true: " #expr);                                                            \
    } while (0)

#define EXPECT_FALSE(expr)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((expr))                                                                                                    \
            VIGIL_TEST_FAIL_("    Expected false: " #expr);                                                            \
    } while (0)

#define EXPECT_STREQ(a, b)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        const char *a_ = (a), *b_ = (b);                                                                               \
        if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0)                                                           \
        {                                                                                                              \
            char msg_[512];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected: \"%s\"\n    Actual:   \"%s\"", b_ ? b_ : "(null)",             \
                     a_ ? a_ : "(null)");                                                                              \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_STRNE(a, b)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        const char *a_ = (a), *b_ = (b);                                                                               \
        if (a_ && b_ && strcmp(a_, b_) == 0)                                                                           \
            VIGIL_TEST_FAIL_("    Expected not equal strings");                                                        \
    } while (0)

#define EXPECT_GE(a, b)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        long long a_ = (long long)(a), b_ = (long long)(b);                                                            \
        if (a_ < b_)                                                                                                   \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected %lld >= %lld", a_, b_);                                         \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_GT(a, b)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        long long a_ = (long long)(a), b_ = (long long)(b);                                                            \
        if (a_ <= b_)                                                                                                  \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected %lld > %lld", a_, b_);                                          \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_LT(a, b)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        long long a_ = (long long)(a), b_ = (long long)(b);                                                            \
        if (a_ >= b_)                                                                                                  \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected %lld < %lld", a_, b_);                                          \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_LE(a, b)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        long long a_ = (long long)(a), b_ = (long long)(b);                                                            \
        if (a_ > b_)                                                                                                   \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected %lld <= %lld", a_, b_);                                         \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_DOUBLE_EQ(a, b)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        double a_ = (double)(a), b_ = (double)(b);                                                                     \
        if (fabs(a_ - b_) > 1e-10)                                                                                     \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected: %g\n    Actual:   %g", b_, a_);                                \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_NEAR(a, b, tol)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        double a_ = (double)(a), b_ = (double)(b), t_ = (double)(tol);                                                 \
        if (fabs(a_ - b_) > t_)                                                                                        \
        {                                                                                                              \
            char msg_[256];                                                                                            \
            snprintf(msg_, sizeof(msg_), "    Expected: %g (+/-%g)\n    Actual:   %g", b_, t_, a_);                    \
            VIGIL_TEST_FAIL_(msg_);                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_DEATH(stmt, pattern)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)(pattern);                                                                                               \
        fprintf(stderr, "  [SKIPPED] EXPECT_DEATH not supported in C harness\n");                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

/* ── Implementation ──────────────────────────────────────────────── */

#ifdef VIGIL_TEST_IMPLEMENTATION

vigil_test_entry_t vigil_test_entries_[VIGIL_TEST_MAX];
int vigil_test_count_ = 0;
void *vigil_test_fixture_ptr_ = NULL;

static int vigil_test_run_all_(void)
{
    int passed = 0, failed = 0, skipped = 0;
    int i;
    const char *filter = getenv("VIGIL_TEST_FILTER");

    printf("[==========] Running %d test(s).\n", vigil_test_count_);

    for (i = 0; i < vigil_test_count_; i++)
    {
        vigil_test_entry_t *e = &vigil_test_entries_[i];
        int test_failed = 0;
        char fullname[512];

        snprintf(fullname, sizeof(fullname), "%s.%s", e->suite, e->name);
        if (filter && !strstr(fullname, filter))
        {
            skipped++;
            continue;
        }

        printf("[ RUN      ] %s\n", fullname);

        if (e->fixture_size > 0)
        {
            vigil_test_fixture_ptr_ = calloc(1, e->fixture_size);
            if (e->setup)
                e->setup(vigil_test_fixture_ptr_);
        }

        e->fn(&test_failed);

        if (e->fixture_size > 0)
        {
            if (e->teardown)
                e->teardown(vigil_test_fixture_ptr_);
            free(vigil_test_fixture_ptr_);
            vigil_test_fixture_ptr_ = NULL;
        }

        if (test_failed)
        {
            printf("[   FAILED ] %s\n", fullname);
            failed++;
        }
        else
        {
            printf("[       OK ] %s\n", fullname);
            passed++;
        }
    }

    printf("[==========] %d test(s) ran.\n", passed + failed);
    if (skipped > 0)
        printf("[  SKIPPED ] %d test(s).\n", skipped);
    printf("[  PASSED  ] %d test(s).\n", passed);
    if (failed > 0)
        printf("[  FAILED  ] %d test(s).\n", failed);

    return failed > 0 ? 1 : 0;
}

#endif /* VIGIL_TEST_IMPLEMENTATION */

/* ── Portable temp directory ──────────────────────────────────────── */

static inline const char *vigil_test_getenv(const char *name, char *buf, size_t cap)
{
#ifdef _MSC_VER
    size_t len;

    if (name == NULL || buf == NULL || cap == 0U)
        return NULL;

    if (getenv_s(&len, buf, cap, name) != 0 || len == 0U || buf[0] == '\0')
        return NULL;
    return buf;
#else
    (void)buf;
    (void)cap;
    return getenv(name);
#endif
}

static inline const char *vigil_test_tmpdir(void)
{
    static char tmpdir[4096];
    const char *d = vigil_test_getenv("VIGIL_TEST_TMPDIR", tmpdir, sizeof(tmpdir));
    if (d && d[0])
        return d;
    d = vigil_test_getenv("TMPDIR", tmpdir, sizeof(tmpdir));
    if (d && d[0])
        return d;
#ifdef _WIN32
    d = vigil_test_getenv("TEMP", tmpdir, sizeof(tmpdir));
    if (d && d[0])
        return d;
    d = vigil_test_getenv("TMP", tmpdir, sizeof(tmpdir));
    if (d && d[0])
        return d;
    return ".";
#else
    return "/tmp";
#endif
}

#endif /* VIGIL_TEST_H */
