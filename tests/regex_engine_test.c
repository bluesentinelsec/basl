#include "../src/stdlib/regex.h"
#include "vigil_test.h"

/* ── vigil_regex_find ─────────────────────────────────────────── */

TEST(RegexEngine, FindNoMatch)
{
    char err[64];
    vigil_regex_t *re = vigil_regex_compile(NULL, "xyz", 3, err, sizeof(err));
    ASSERT_NE(re, NULL);

    vigil_regex_result_t r;
    bool found = vigil_regex_find(re, "abc", 3, &r);
    EXPECT_FALSE(found);

    vigil_regex_free(re);
}

TEST(RegexEngine, FindResultNull)
{
    char err[64];
    vigil_regex_t *re = vigil_regex_compile(NULL, "[0-9]+", 6, err, sizeof(err));
    ASSERT_NE(re, NULL);

    /* result=NULL must not crash */
    bool found = vigil_regex_find(re, "abc123", 6, NULL);
    EXPECT_TRUE(found);

    vigil_regex_free(re);
}

TEST(RegexEngine, FindMatch)
{
    char err[64];
    vigil_regex_t *re = vigil_regex_compile(NULL, "[a-z]+", 6, err, sizeof(err));
    ASSERT_NE(re, NULL);

    vigil_regex_result_t r;
    bool found = vigil_regex_find(re, "123abc456", 9, &r);
    EXPECT_TRUE(found);
    EXPECT_EQ(r.groups[0].start, (size_t)3);
    EXPECT_EQ(r.groups[0].end, (size_t)6);

    vigil_regex_free(re);
}

TEST(RegexEngine, FindAllMultiple)
{
    char err[64];
    vigil_regex_t *re = vigil_regex_compile(NULL, "[0-9]+", 6, err, sizeof(err));
    ASSERT_NE(re, NULL);

    vigil_regex_result_t results[8];
    size_t count = vigil_regex_find_all(re, "a1b22c333", 9, results, 8);
    EXPECT_EQ(count, (size_t)3);

    vigil_regex_free(re);
}

TEST(RegexEngine, FindAllNoMatch)
{
    char err[64];
    vigil_regex_t *re = vigil_regex_compile(NULL, "[0-9]+", 6, err, sizeof(err));
    ASSERT_NE(re, NULL);

    vigil_regex_result_t results[8];
    size_t count = vigil_regex_find_all(re, "abcdef", 6, results, 8);
    EXPECT_EQ(count, (size_t)0);

    vigil_regex_free(re);
}

TEST(RegexEngine, CompileInvalid)
{
    char err[64];
    vigil_regex_t *re = vigil_regex_compile(NULL, "*bad", 4, err, sizeof(err));
    EXPECT_EQ(re, NULL);
}

void register_regex_engine_tests(void)
{
    REGISTER_TEST(RegexEngine, FindNoMatch);
    REGISTER_TEST(RegexEngine, FindResultNull);
    REGISTER_TEST(RegexEngine, FindMatch);
    REGISTER_TEST(RegexEngine, FindAllMultiple);
    REGISTER_TEST(RegexEngine, FindAllNoMatch);
    REGISTER_TEST(RegexEngine, CompileInvalid);
}
