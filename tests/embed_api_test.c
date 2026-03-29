/* Tests for the vigil/easy.h embedding API. */
#include "vigil_test.h"

#include "vigil/easy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── one-liner tests ─────────────────────────────────────────────── */

TEST(EmbedAPI, RunStringReturnsExitCode)
{
    EXPECT_EQ(vigil_run_string("fn main() -> i32 { return 42; }"), 42);
}

TEST(EmbedAPI, RunStringReturnsZero)
{
    EXPECT_EQ(vigil_run_string("fn main() -> i32 { return 0; }"), 0);
}

TEST(EmbedAPI, RunBytesWorksWithoutNullTerminator)
{
    const char src[] = "fn main() -> i32 { return 7; }";
    EXPECT_EQ(vigil_run_bytes(src, sizeof(src) - 1), 7);
}

TEST(EmbedAPI, RunFileExecutesScript)
{
    const char *src = "fn main() -> i32 { return 55; }\n";
    const char *path = "vigil_embed_test_tmp.vigil";
    FILE *f = fopen(path, "wb");
    ASSERT_NE(f, NULL);
    fwrite(src, 1, strlen(src), f);
    fclose(f);
    EXPECT_EQ(vigil_run_file(path), 55);
    remove(path);
}

TEST(EmbedAPI, RunStringReturnsNegativeOneOnError)
{
    EXPECT_EQ(vigil_run_string("this is not valid vigil"), -1);
}

/* ── stateful API tests ──────────────────────────────────────────── */

TEST(EmbedAPI, DoStringExposesResult)
{
    vigil_state_t *V = vigil_open();
    ASSERT_NE(V, NULL);
    EXPECT_EQ(vigil_dostring(V, "fn main() -> i32 { return 99; }"), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_get_result_int(V), 99);
    EXPECT_EQ(vigil_has_error(V), 0);
    vigil_close(V);
}

TEST(EmbedAPI, DoBytesExposesResult)
{
    const char src[] = "fn main() -> i32 { return 13; }";
    vigil_state_t *V = vigil_open();
    ASSERT_NE(V, NULL);
    EXPECT_EQ(vigil_dobytes(V, src, sizeof(src) - 1), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_get_result_int(V), 13);
    vigil_close(V);
}

TEST(EmbedAPI, ErrorExposesMessage)
{
    vigil_state_t *V = vigil_open();
    ASSERT_NE(V, NULL);
    EXPECT_NE(vigil_dostring(V, "fn main() -> i32 { return xyz; }"), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_has_error(V), 1);
    EXPECT_NE(vigil_get_error(V), NULL);
    vigil_close(V);
}

TEST(EmbedAPI, FloatResult)
{
    vigil_state_t *V = vigil_open();
    ASSERT_NE(V, NULL);
    EXPECT_EQ(vigil_dostring(V, "fn main() -> i32 { return 0; }"), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_get_result_int(V), 0);
    vigil_close(V);
}

/* ── null safety ─────────────────────────────────────────────────── */

TEST(EmbedAPI, NullRunInputsReturnNegativeOne)
{
    EXPECT_EQ(vigil_run_string(NULL), -1);
    EXPECT_EQ(vigil_run_bytes(NULL, 0), -1);
    EXPECT_EQ(vigil_run_file(NULL), -1);
}

TEST(EmbedAPI, NullStateAccessIsSafe)
{
    EXPECT_EQ(vigil_has_error(NULL), 0);
    EXPECT_EQ(vigil_get_error(NULL), NULL);
    EXPECT_EQ(vigil_get_result_int(NULL), 0);
    vigil_close(NULL); /* must not crash */
}

/* ── registration ────────────────────────────────────────────────── */

void register_embed_api_tests(void)
{
    REGISTER_TEST(EmbedAPI, RunStringReturnsExitCode);
    REGISTER_TEST(EmbedAPI, RunStringReturnsZero);
    REGISTER_TEST(EmbedAPI, RunBytesWorksWithoutNullTerminator);
    REGISTER_TEST(EmbedAPI, RunFileExecutesScript);
    REGISTER_TEST(EmbedAPI, RunStringReturnsNegativeOneOnError);
    REGISTER_TEST(EmbedAPI, DoStringExposesResult);
    REGISTER_TEST(EmbedAPI, DoBytesExposesResult);
    REGISTER_TEST(EmbedAPI, ErrorExposesMessage);
    REGISTER_TEST(EmbedAPI, FloatResult);
    REGISTER_TEST(EmbedAPI, NullRunInputsReturnNegativeOne);
    REGISTER_TEST(EmbedAPI, NullStateAccessIsSafe);
}
