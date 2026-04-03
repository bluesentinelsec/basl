#include "vigil_test.h"

#include <stdio.h>
#include <string.h>

#include "vigil/vigil.h"

static vigil_source_id_t RegisterSource(int *vigil_test_failed_, vigil_source_registry_t *registry, const char *path,
                                        const char *text, vigil_error_t *error)
{
    vigil_source_id_t source_id = 0U;

    EXPECT_EQ(vigil_source_registry_register_cstr(registry, path, text, &source_id, error), VIGIL_STATUS_OK);
    return source_id;
}

static const vigil_token_t *TokenAt(int *vigil_test_failed_, const vigil_token_list_t *tokens, size_t index)
{
    const vigil_token_t *token;

    token = vigil_token_list_get(tokens, index);
    EXPECT_NE(token, NULL);
    return token;
}

TEST(VigilLexerTest, TokenizesSimpleFunction)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_token_list_t tokens;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_token_list_init(&tokens, runtime);
    source_id = RegisterSource(vigil_test_failed_, &registry, "simple.vigil", "fn main() -> i32 { return 0; }", &error);

    ASSERT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 0U);

    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 0)->kind, VIGIL_TOKEN_FN);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 1)->kind, VIGIL_TOKEN_IDENTIFIER);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 2)->kind, VIGIL_TOKEN_LPAREN);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 3)->kind, VIGIL_TOKEN_RPAREN);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 4)->kind, VIGIL_TOKEN_ARROW);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 5)->kind, VIGIL_TOKEN_IDENTIFIER);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 6)->kind, VIGIL_TOKEN_LBRACE);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 7)->kind, VIGIL_TOKEN_RETURN);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 8)->kind, VIGIL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 9)->kind, VIGIL_TOKEN_SEMICOLON);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 10)->kind, VIGIL_TOKEN_RBRACE);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 11)->kind, VIGIL_TOKEN_SEMICOLON); /* ASI after } at EOF */
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 12)->kind, VIGIL_TOKEN_EOF);

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilLexerTest, SkipsCommentsAndWhitespace)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_token_list_t tokens;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_token_list_init(&tokens, runtime);
    source_id = RegisterSource(vigil_test_failed_, &registry, "comments.vigil",
                               "// leading\nimport /* inner */ \"fmt\";\n", &error);

    ASSERT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 0)->kind, VIGIL_TOKEN_IMPORT);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 1)->kind, VIGIL_TOKEN_STRING_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 2)->kind, VIGIL_TOKEN_SEMICOLON);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 3)->kind, VIGIL_TOKEN_EOF);

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilLexerTest, TokenizesNumericAndStringLiteralForms)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_token_list_t tokens;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_token_list_init(&tokens, runtime);
    source_id = RegisterSource(vigil_test_failed_, &registry, "literals.vigil",
                               "0 0xFF 0b10 0o7 3.14 1e6 \"x\" `y` 'z' f\"hi {name}\"", &error);

    ASSERT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 0)->kind, VIGIL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 1)->kind, VIGIL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 2)->kind, VIGIL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 3)->kind, VIGIL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 4)->kind, VIGIL_TOKEN_FLOAT_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 5)->kind, VIGIL_TOKEN_FLOAT_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 6)->kind, VIGIL_TOKEN_STRING_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 7)->kind, VIGIL_TOKEN_RAW_STRING_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 8)->kind, VIGIL_TOKEN_CHAR_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 9)->kind, VIGIL_TOKEN_FSTRING_LITERAL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 10)->kind, VIGIL_TOKEN_SEMICOLON); /* ASI at EOF */
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 11)->kind, VIGIL_TOKEN_EOF);

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilLexerTest, ReportsUnexpectedCharacter)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_token_list_t tokens;
    vigil_source_id_t source_id;
    const vigil_diagnostic_t *diagnostic;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_token_list_init(&tokens, runtime);
    source_id = RegisterSource(vigil_test_failed_, &registry, "bad.vigil", "@", &error);

    EXPECT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    EXPECT_EQ(error.type, VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "unexpected character"), 0);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    diagnostic = vigil_diagnostic_list_get(&diagnostics, 0U);
    ASSERT_NE(diagnostic, NULL);
    EXPECT_EQ(diagnostic->severity, VIGIL_DIAGNOSTIC_ERROR);
    EXPECT_EQ(diagnostic->span.source_id, source_id);
    EXPECT_EQ(diagnostic->span.start_offset, 0U);
    EXPECT_EQ(diagnostic->span.end_offset, 1U);
    EXPECT_STREQ(vigil_string_c_str(&diagnostic->message), "unexpected character");

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilLexerTest, ReportsUnterminatedStringAndBlockComment)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_token_list_t tokens;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_token_list_init(&tokens, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "string.vigil", "\"unterminated", &error);
    EXPECT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "unterminated string literal");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "comment.vigil", "/* never closes", &error);
    EXPECT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "unterminated block comment");

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilLexerTest, ReportsInvalidPrefixedNumericLiterals)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_token_list_t tokens;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_token_list_init(&tokens, runtime);

    source_id = RegisterSource(vigil_test_failed_, &registry, "badnum1.vigil", "0x", &error);
    EXPECT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "expected digits after numeric base prefix");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "badnum2.vigil", "0b129", &error);
    EXPECT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "invalid digits for numeric base prefix");

    vigil_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(vigil_test_failed_, &registry, "badnum3.vigil", "0o78", &error);
    EXPECT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_SYNTAX_ERROR);
    ASSERT_EQ(vigil_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(vigil_string_c_str(&vigil_diagnostic_list_get(&diagnostics, 0U)->message),
                 "invalid digits for numeric base prefix");

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilLexerTest, TokenizesNilKeyword)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_token_list_t tokens;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_token_list_init(&tokens, runtime);
    source_id = RegisterSource(vigil_test_failed_, &registry, "nil.vigil", "nil", &error);

    ASSERT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 0)->kind, VIGIL_TOKEN_NIL);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 1)->kind, VIGIL_TOKEN_SEMICOLON); /* ASI at EOF */
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 2)->kind, VIGIL_TOKEN_EOF);

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilLexerTest, TokenizesWalrusOperator)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_token_list_t tokens;
    vigil_source_id_t source_id;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_token_list_init(&tokens, runtime);
    source_id = RegisterSource(vigil_test_failed_, &registry, "walrus.vigil", "x := 1", &error);

    ASSERT_EQ(vigil_lex_source(&registry, source_id, &tokens, &diagnostics, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 0)->kind, VIGIL_TOKEN_IDENTIFIER);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 1)->kind, VIGIL_TOKEN_WALRUS);
    EXPECT_EQ(TokenAt(vigil_test_failed_, &tokens, 2)->kind, VIGIL_TOKEN_INT_LITERAL);

    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

void register_lexer_tests(void)
{
    REGISTER_TEST(VigilLexerTest, TokenizesSimpleFunction);
    REGISTER_TEST(VigilLexerTest, SkipsCommentsAndWhitespace);
    REGISTER_TEST(VigilLexerTest, TokenizesNumericAndStringLiteralForms);
    REGISTER_TEST(VigilLexerTest, ReportsUnexpectedCharacter);
    REGISTER_TEST(VigilLexerTest, ReportsUnterminatedStringAndBlockComment);
    REGISTER_TEST(VigilLexerTest, ReportsInvalidPrefixedNumericLiterals);
    REGISTER_TEST(VigilLexerTest, TokenizesNilKeyword);
    REGISTER_TEST(VigilLexerTest, TokenizesWalrusOperator);
}
