#include "vigil_test.h"

#include "vigil/doc.h"
#include "vigil/lexer.h"
#include "vigil/runtime.h"
#include "vigil/source.h"

#include <string.h>

/* ── Fixture ─────────────────────────────────────────────────────── */

typedef struct VigilDocTest
{
    vigil_runtime_t *runtime;
    vigil_source_registry_t registry;
    vigil_token_list_t tokens;
    vigil_diagnostic_list_t diagnostics;
    vigil_error_t error;
} VigilDocTest;

static void VigilDocTest_SetUp(void *p)
{
    VigilDocTest *self = (VigilDocTest *)p;
    memset(self, 0, sizeof(*self));
    vigil_runtime_open(&self->runtime, NULL, &self->error);
    vigil_source_registry_init(&self->registry, self->runtime);
    vigil_token_list_init(&self->tokens, self->runtime);
    vigil_diagnostic_list_init(&self->diagnostics, self->runtime);
}

static void VigilDocTest_TearDown(void *p)
{
    VigilDocTest *self = (VigilDocTest *)p;
    vigil_diagnostic_list_free(&self->diagnostics);
    vigil_token_list_free(&self->tokens);
    vigil_source_registry_free(&self->registry);
    vigil_runtime_close(&self->runtime);
}

#define F FIXTURE(VigilDocTest)

static char *doc_render_helper(VigilDocTest *f, const char *src_text, const char *symbol)
{
    vigil_source_id_t source_id = 0;
    char *text = NULL;
    size_t length = 0;
    vigil_doc_module_t module;

    if (vigil_source_registry_register_cstr(&f->registry, "test.vigil", src_text, &source_id, &f->error) !=
        VIGIL_STATUS_OK)
        return NULL;
    if (vigil_lex_source(&f->registry, source_id, &f->tokens, &f->diagnostics, &f->error) != VIGIL_STATUS_OK)
        return NULL;
    if (vigil_doc_extract(NULL, "test.vigil", 9, src_text, strlen(src_text), &f->tokens, &module, &f->error) !=
        VIGIL_STATUS_OK)
        return NULL;

    vigil_doc_render(&module, symbol, &text, &length, &f->error);
    vigil_doc_module_free(&module);
    return text; /* caller must free */
}

static vigil_status_t doc_render_status_helper(VigilDocTest *f, const char *src_text, const char *symbol, char **text,
                                               size_t *length)
{
    vigil_source_id_t source_id = 0;
    vigil_doc_module_t module;
    vigil_status_t status;

    *text = NULL;
    if (length != NULL)
        *length = 0;

    if (vigil_source_registry_register_cstr(&f->registry, "test.vigil", src_text, &source_id, &f->error) !=
        VIGIL_STATUS_OK)
        return VIGIL_STATUS_INTERNAL;
    if (vigil_lex_source(&f->registry, source_id, &f->tokens, &f->diagnostics, &f->error) != VIGIL_STATUS_OK)
        return VIGIL_STATUS_INTERNAL;
    if (vigil_doc_extract(NULL, "test.vigil", 9, src_text, strlen(src_text), &f->tokens, &module, &f->error) !=
        VIGIL_STATUS_OK)
        return VIGIL_STATUS_INTERNAL;

    status = vigil_doc_render(&module, symbol, text, length, &f->error);
    vigil_doc_module_free(&module);
    return status;
}

TEST_F(VigilDocTest, ModuleViewShowsAllPublicSymbols)
{
    char *out = doc_render_helper(F,
                                  "// Geometry helpers.\n"
                                  "\n"
                                  "// Mathematical answer.\n"
                                  "pub const i32 answer = 42;\n"
                                  "\n"
                                  "// Adds two numbers.\n"
                                  "pub fn add(i32 a, i32 b) -> i32 {\n"
                                  "\treturn a + b;\n"
                                  "}\n"
                                  "\n"
                                  "fn hidden_fn() -> i32 {\n"
                                  "\treturn 0;\n"
                                  "}\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "MODULE\n  test") != NULL);
    EXPECT_TRUE(strstr(out, "SUMMARY\n  Geometry helpers.") != NULL);
    EXPECT_TRUE(strstr(out, "CONSTANTS\n  answer i32\n    Mathematical answer.") != NULL);
    EXPECT_TRUE(strstr(out, "FUNCTIONS\n  add(i32 a, i32 b) -> i32\n    Adds two numbers.") != NULL);
    EXPECT_TRUE(strstr(out, "hidden_fn") == NULL);
    free(out);
}

TEST_F(VigilDocTest, ModuleSummarySkipsLeadingBlankLines)
{
    char *out = doc_render_helper(F,
                                  "\n"
                                  "\t\n"
                                  "// Geometry helpers.\n"
                                  "    // With normalization.\n"
                                  "pub fn add(i32 a, i32 b) -> i32 {\n"
                                  "\treturn a + b;\n"
                                  "}\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "SUMMARY\n  Geometry helpers.\n  With normalization.") != NULL);
    free(out);
}

TEST_F(VigilDocTest, DeclarationCommentCollectsMultipleIndentedLines)
{
    char *out = doc_render_helper(F,
                                  "pub class Point {\n"
                                  "\t// X coordinate.\n"
                                  "\t   // Used by layout.\n"
                                  "\tpub i32 x;\n"
                                  "}\n",
                                  "Point.x");

    EXPECT_STREQ(out, "Point.x i32\n\nX coordinate.\nUsed by layout.\n");
    free(out);
}

TEST_F(VigilDocTest, ClassWithFieldsAndMethods)
{
    char *out = doc_render_helper(F,
                                  "// A point in world space.\n"
                                  "pub class Point implements Formatter {\n"
                                  "\t// X coordinate.\n"
                                  "\tpub i32 x;\n"
                                  "\n"
                                  "\ti32 hidden;\n"
                                  "\n"
                                  "\t// Formats the point name.\n"
                                  "\tpub fn format(string label) -> string {\n"
                                  "\t\treturn label;\n"
                                  "\t}\n"
                                  "\n"
                                  "\tfn secret() -> i32 {\n"
                                  "\t\treturn self.hidden;\n"
                                  "\t}\n"
                                  "}\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "Point implements Formatter") != NULL);
    EXPECT_TRUE(strstr(out, "A point in world space.") != NULL);
    EXPECT_TRUE(strstr(out, "Fields\n      x i32\n        X coordinate.") != NULL);
    EXPECT_TRUE(strstr(out, "Methods\n      format(string label) -> string\n        Formats the point name.") != NULL);
    EXPECT_TRUE(strstr(out, "hidden") == NULL);
    EXPECT_TRUE(strstr(out, "secret") == NULL);
    free(out);
}

TEST_F(VigilDocTest, InterfaceWithMethods)
{
    char *out = doc_render_helper(F,
                                  "// A named formatter.\n"
                                  "pub interface Formatter {\n"
                                  "\t// Formats a label.\n"
                                  "\tfn format(string label) -> string;\n"
                                  "}\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "INTERFACES\n  Formatter") != NULL);
    EXPECT_TRUE(strstr(out, "A named formatter.") != NULL);
    EXPECT_TRUE(strstr(out, "format(string label) -> string") != NULL);
    EXPECT_TRUE(strstr(out, "Formats a label.") != NULL);
    free(out);
}

TEST_F(VigilDocTest, EnumWithVariants)
{
    char *out = doc_render_helper(F,
                                  "// Primary colors.\n"
                                  "pub enum Color {\n"
                                  "\tRed,\n"
                                  "\tGreen,\n"
                                  "\tBlue\n"
                                  "}\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "ENUMS\n  Color") != NULL);
    EXPECT_TRUE(strstr(out, "Primary colors.") != NULL);
    EXPECT_TRUE(strstr(out, "Variants\n      Red\n      Green\n      Blue") != NULL);
    free(out);
}

TEST_F(VigilDocTest, VariableDeclaration)
{
    char *out = doc_render_helper(F,
                                  "// A mutable counter.\n"
                                  "pub i32 count = 0;\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "VARIABLES\n  count i32\n    A mutable counter.") != NULL);
    free(out);
}

TEST_F(VigilDocTest, SymbolLookupFunction)
{
    char *out = doc_render_helper(F,
                                  "// Adds two numbers.\n"
                                  "pub fn add(i32 a, i32 b) -> i32 {\n"
                                  "\treturn a + b;\n"
                                  "}\n",
                                  "add");

    EXPECT_TRUE(strstr(out, "add(i32 a, i32 b) -> i32") != NULL);
    EXPECT_TRUE(strstr(out, "Adds two numbers.") != NULL);
    free(out);
}

TEST_F(VigilDocTest, SymbolLookupVariable)
{
    char *out = doc_render_helper(F,
                                  "// A mutable counter.\n"
                                  "pub i32 count = 0;\n",
                                  "count");

    EXPECT_STREQ(out, "count i32\n\nA mutable counter.\n");
    free(out);
}

TEST_F(VigilDocTest, SymbolLookupEnum)
{
    char *out = doc_render_helper(F,
                                  "// Primary colors.\n"
                                  "pub enum Color {\n"
                                  "\tRed,\n"
                                  "\tGreen,\n"
                                  "\tBlue\n"
                                  "}\n",
                                  "Color");

    EXPECT_STREQ(out, "Color\n\n  Primary colors.\n\n  Variants\n    Red\n    Green\n    Blue\n");
    free(out);
}

TEST_F(VigilDocTest, SymbolLookupInterface)
{
    char *out = doc_render_helper(F,
                                  "// A named formatter.\n"
                                  "pub interface Formatter {\n"
                                  "\t// Formats a label.\n"
                                  "\tfn format(string label) -> string;\n"
                                  "}\n",
                                  "Formatter");

    EXPECT_STREQ(out, "Formatter\n\n  A named formatter.\n\n  Methods\n    format(string label) -> string\n"
                      "      Formats a label.\n");
    free(out);
}

TEST_F(VigilDocTest, SymbolLookupClass)
{
    char *out = doc_render_helper(F,
                                  "// A point in world space.\n"
                                  "pub class Point implements Formatter {\n"
                                  "\t// X coordinate.\n"
                                  "\tpub i32 x;\n"
                                  "\t// Formats the point name.\n"
                                  "\tpub fn format(string label) -> string {\n"
                                  "\t\treturn label;\n"
                                  "\t}\n"
                                  "}\n",
                                  "Point");

    EXPECT_STREQ(out, "Point implements Formatter\n\n  A point in world space.\n\n  Fields\n    x i32\n"
                      "      X coordinate.\n\n  Methods\n    format(string label) -> string\n"
                      "      Formats the point name.\n");
    free(out);
}

TEST_F(VigilDocTest, SymbolLookupClassMember)
{
    char *out = doc_render_helper(F,
                                  "pub class Point {\n"
                                  "\t// X coordinate.\n"
                                  "\tpub i32 x;\n"
                                  "\t// Measures size.\n"
                                  "\tpub fn size() -> i32 {\n"
                                  "\t\treturn self.x;\n"
                                  "\t}\n"
                                  "}\n",
                                  "Point.x");

    EXPECT_STREQ(out, "Point.x i32\n\nX coordinate.\n");
    free(out);
}

TEST_F(VigilDocTest, SymbolLookupClassMethod)
{
    char *out = doc_render_helper(F,
                                  "pub class Point {\n"
                                  "\t// Measures size.\n"
                                  "\tpub fn size() -> i32 {\n"
                                  "\t\treturn self.x;\n"
                                  "\t}\n"
                                  "}\n",
                                  "Point.size");

    EXPECT_STREQ(out, "Point.size() -> i32\n\nMeasures size.\n");
    free(out);
}

TEST_F(VigilDocTest, SymbolLookupInterfaceMethod)
{
    char *out = doc_render_helper(F,
                                  "pub interface Formatter {\n"
                                  "\t// Formats a label.\n"
                                  "\tfn format(string label) -> string;\n"
                                  "}\n",
                                  "Formatter.format");

    EXPECT_STREQ(out, "Formatter.format(string label) -> string\n\nFormats a label.\n");
    free(out);
}

TEST_F(VigilDocTest, MissingQualifiedSymbolReturnsError)
{
    const char *src = "pub class Point {\n\tpub i32 x;\n}\n";
    char *text = NULL;
    size_t length = 0;
    EXPECT_EQ(doc_render_status_helper(F, src, "Point.missing", &text, &length), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(text, NULL);
}

TEST_F(VigilDocTest, MissingSymbolReturnsError)
{
    const char *src = "pub fn add(i32 a, i32 b) -> i32 {\n\treturn a + b;\n}\n";
    char *text = NULL;
    size_t length = 0;
    EXPECT_EQ(doc_render_status_helper(F, src, "missing", &text, &length), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(text, NULL);
}

TEST_F(VigilDocTest, ComplexReturnTypes)
{
    char *out = doc_render_helper(F,
                                  "pub fn get_pair() -> (i32, err) {\n"
                                  "\treturn 0, ok;\n"
                                  "}\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "get_pair() -> (i32, err)") != NULL);
    free(out);
}

TEST_F(VigilDocTest, GenericTypes)
{
    char *out = doc_render_helper(F,
                                  "pub fn process(array<string> items, map<string, i32> counts) -> void {\n"
                                  "}\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "process(array<string> items, map<string, i32> counts) -> void") != NULL);
    free(out);
}

TEST_F(VigilDocTest, PrivateDeclarationsHidden)
{
    char *out = doc_render_helper(F,
                                  "fn private_fn() -> i32 {\n"
                                  "\treturn 0;\n"
                                  "}\n"
                                  "class PrivateClass {\n"
                                  "\tpub i32 x;\n"
                                  "}\n"
                                  "interface PrivateIface {\n"
                                  "\tfn method() -> void;\n"
                                  "}\n"
                                  "enum PrivateEnum {\n"
                                  "\tA,\n"
                                  "\tB\n"
                                  "}\n"
                                  "const i32 PRIVATE_CONST = 42;\n"
                                  "i32 private_var = 0;\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "private") == NULL);
    EXPECT_TRUE(strstr(out, "Private") == NULL);
    EXPECT_TRUE(strstr(out, "PRIVATE") == NULL);
    free(out);
}

TEST_F(VigilDocTest, ScriptModeShowsPrivateDeclarations)
{
    char *out = doc_render_helper(F,
                                  "import std.io;\n"
                                  "fn helper() -> i32 {\n"
                                  "\treturn 1;\n"
                                  "}\n"
                                  "class LocalPoint {\n"
                                  "\ti32 x;\n"
                                  "}\n"
                                  "i32 local_value = 7;\n"
                                  "fn main() -> i32 {\n"
                                  "\treturn helper();\n"
                                  "}\n",
                                  NULL);

    EXPECT_TRUE(strstr(out, "FUNCTIONS\n  helper() -> i32") != NULL);
    EXPECT_TRUE(strstr(out, "CLASSES\n  LocalPoint") != NULL);
    EXPECT_TRUE(strstr(out, "VARIABLES\n  local_value i32") != NULL);
    EXPECT_TRUE(strstr(out, "std.io") == NULL);
    free(out);
}

TEST_F(VigilDocTest, EmptyFileShowsModuleOnly)
{
    char *out = doc_render_helper(F, "", NULL);
    EXPECT_TRUE(strstr(out, "MODULE\n  test") != NULL);
    EXPECT_TRUE(strstr(out, "SUMMARY") == NULL);
    EXPECT_TRUE(strstr(out, "FUNCTIONS") == NULL);
    free(out);
}

TEST_F(VigilDocTest, ModuleNameFromPath)
{
    vigil_source_id_t source_id = 0;
    const char *src = "pub fn hello() -> void {\n}\n";
    ASSERT_EQ(vigil_source_registry_register_cstr(&F->registry, "lib/utils.vigil", src, &source_id, &F->error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_lex_source(&F->registry, source_id, &F->tokens, &F->diagnostics, &F->error), VIGIL_STATUS_OK);

    vigil_doc_module_t module;
    ASSERT_EQ(vigil_doc_extract(NULL, "lib/utils.vigil", 14, src, strlen(src), &F->tokens, &module, &F->error),
              VIGIL_STATUS_OK);

    char *text = NULL;
    size_t length = 0;
    ASSERT_EQ(vigil_doc_render(&module, NULL, &text, &length, &F->error), VIGIL_STATUS_OK);
    char *out = text;
    EXPECT_TRUE(strstr(out, "MODULE\n  utils") != NULL);
    free(out);
    vigil_doc_module_free(&module);
}

#undef F

void register_doc_tests(void)
{
    REGISTER_TEST_F(VigilDocTest, ModuleViewShowsAllPublicSymbols);
    REGISTER_TEST_F(VigilDocTest, ModuleSummarySkipsLeadingBlankLines);
    REGISTER_TEST_F(VigilDocTest, DeclarationCommentCollectsMultipleIndentedLines);
    REGISTER_TEST_F(VigilDocTest, ClassWithFieldsAndMethods);
    REGISTER_TEST_F(VigilDocTest, InterfaceWithMethods);
    REGISTER_TEST_F(VigilDocTest, EnumWithVariants);
    REGISTER_TEST_F(VigilDocTest, VariableDeclaration);
    REGISTER_TEST_F(VigilDocTest, SymbolLookupFunction);
    REGISTER_TEST_F(VigilDocTest, SymbolLookupVariable);
    REGISTER_TEST_F(VigilDocTest, SymbolLookupEnum);
    REGISTER_TEST_F(VigilDocTest, SymbolLookupInterface);
    REGISTER_TEST_F(VigilDocTest, SymbolLookupClass);
    REGISTER_TEST_F(VigilDocTest, SymbolLookupClassMember);
    REGISTER_TEST_F(VigilDocTest, SymbolLookupClassMethod);
    REGISTER_TEST_F(VigilDocTest, SymbolLookupInterfaceMethod);
    REGISTER_TEST_F(VigilDocTest, MissingQualifiedSymbolReturnsError);
    REGISTER_TEST_F(VigilDocTest, MissingSymbolReturnsError);
    REGISTER_TEST_F(VigilDocTest, ComplexReturnTypes);
    REGISTER_TEST_F(VigilDocTest, GenericTypes);
    REGISTER_TEST_F(VigilDocTest, PrivateDeclarationsHidden);
    REGISTER_TEST_F(VigilDocTest, ScriptModeShowsPrivateDeclarations);
    REGISTER_TEST_F(VigilDocTest, EmptyFileShowsModuleOnly);
    REGISTER_TEST_F(VigilDocTest, ModuleNameFromPath);
}
