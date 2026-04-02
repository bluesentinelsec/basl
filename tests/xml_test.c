#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "internal/vigil_xml.h"

/* ── Basic parsing ───────────────────────────────────────────────── */

TEST(VigilXmlTest, ParsesSimpleElement)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    EXPECT_EQ(vigil_xml_parse("<root/>", 7, &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    ASSERT_NE(doc->root, (void *)NULL);
    EXPECT_STREQ(doc->root->tag, "root");
    EXPECT_EQ(doc->root->child_count, 0U);
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, ParsesElementWithText)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    EXPECT_EQ(vigil_xml_parse("<msg>hello</msg>", 16, &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(doc->root->text, "hello");
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, ParsesAttributes)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<item id=\"42\" name='test'/>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_EQ(doc->root->attribute_count, 2U);
    EXPECT_STREQ(vigil_xml_element_attr(doc->root, "id"), "42");
    EXPECT_STREQ(vigil_xml_element_attr(doc->root, "name"), "test");
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, ParsesNestedElements)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<root><a>1</a><b>2</b></root>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_EQ(doc->root->child_count, 2U);
    EXPECT_STREQ(doc->root->children[0]->tag, "a");
    EXPECT_STREQ(doc->root->children[0]->text, "1");
    EXPECT_STREQ(doc->root->children[1]->tag, "b");
    EXPECT_STREQ(doc->root->children[1]->text, "2");
    vigil_xml_document_free(doc);
}

/* ── Entity and character references ─────────────────────────────── */

TEST(VigilXmlTest, DecodesEntityReferences)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<t>&amp;&lt;&gt;&apos;&quot;</t>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(doc->root->text, "&<>'\"");
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, DecodesCharacterReferences)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<t>&#65;&#x20AC;</t>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    /* &#65; = 'A', &#x20AC; = '€' (E2 82 AC) */
    EXPECT_EQ(doc->root->text[0], 'A');
    EXPECT_EQ((unsigned char)doc->root->text[1], 0xE2U);
    EXPECT_EQ((unsigned char)doc->root->text[2], 0x82U);
    EXPECT_EQ((unsigned char)doc->root->text[3], 0xACU);
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, DecodesEntitiesInAttributes)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<t val=\"a&amp;b\"/>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(vigil_xml_element_attr(doc->root, "val"), "a&b");
    vigil_xml_document_free(doc);
}

/* ── CDATA, comments, PIs ────────────────────────────────────────── */

TEST(VigilXmlTest, ParsesCdataSection)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<t><![CDATA[<not>&xml</not>]]></t>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(doc->root->text, "<not>&xml</not>");
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, SkipsComments)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<!-- comment --><root><!-- inner --></root>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(doc->root->tag, "root");
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, SkipsProcessingInstructions)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<?target data?><root/>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(doc->root->tag, "root");
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, SkipsDoctype)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<!DOCTYPE html><root/>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(doc->root->tag, "root");
    vigil_xml_document_free(doc);
}

/* ── XML declaration ─────────────────────────────────────────────── */

TEST(VigilXmlTest, ParsesXmlDeclaration)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(doc->version, "1.0");
    EXPECT_STREQ(doc->encoding, "UTF-8");
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, SkipsUtf8Bom)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "\xEF\xBB\xBF<root/>";
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    ASSERT_NE(doc, (void *)NULL);
    EXPECT_STREQ(doc->root->tag, "root");
    vigil_xml_document_free(doc);
}

/* ── Error cases ─────────────────────────────────────────────────── */

TEST(VigilXmlTest, RejectsMismatchedTags)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    EXPECT_EQ(vigil_xml_parse("<a></b>", 7, &doc, &err), VIGIL_STATUS_SYNTAX_ERROR);
    EXPECT_EQ(doc, (void *)NULL);
    EXPECT_NE(strstr(err.message, "mismatched"), (void *)NULL);
}

TEST(VigilXmlTest, RejectsUnclosedElement)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    EXPECT_EQ(vigil_xml_parse("<a>", 3, &doc, &err), VIGIL_STATUS_SYNTAX_ERROR);
    EXPECT_EQ(doc, (void *)NULL);
    EXPECT_NE(strstr(err.message, "unclosed"), (void *)NULL);
}

TEST(VigilXmlTest, RejectsEmptyInput)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    EXPECT_EQ(vigil_xml_parse("", 0, &doc, &err), VIGIL_STATUS_SYNTAX_ERROR);
    EXPECT_EQ(doc, (void *)NULL);
}

TEST(VigilXmlTest, RejectsNullInput)
{
    vigil_xml_document_t *doc = NULL;
    EXPECT_EQ(vigil_xml_parse(NULL, 0, &doc, NULL), VIGIL_STATUS_INVALID_ARGUMENT);
}

TEST(VigilXmlTest, RejectsDeeplyNestedInput)
{
    /* Build a string with 300 levels of nesting — exceeds XML_MAX_DEPTH (256) */
    char xml[4096];
    size_t pos = 0U;
    int i;
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;

    for (i = 0; i < 300 && pos + 4U < sizeof(xml); i++)
    {
        xml[pos++] = '<';
        xml[pos++] = 'a';
        xml[pos++] = '>';
    }
    for (i = 0; i < 300 && pos + 5U < sizeof(xml); i++)
    {
        xml[pos++] = '<';
        xml[pos++] = '/';
        xml[pos++] = 'a';
        xml[pos++] = '>';
    }
    xml[pos] = '\0';
    EXPECT_EQ(vigil_xml_parse(xml, pos, &doc, &err), VIGIL_STATUS_SYNTAX_ERROR);
    EXPECT_EQ(doc, (void *)NULL);
    EXPECT_NE(strstr(err.message, "depth"), (void *)NULL);
}

/* ── Query helpers ───────────────────────────────────────────────── */

TEST(VigilXmlTest, ElementChildFindsFirstMatch)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<r><a>1</a><b>2</b><a>3</a></r>";
    const vigil_xml_element_t *found;
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    found = vigil_xml_element_child(doc->root, "a");
    ASSERT_NE(found, (void *)NULL);
    EXPECT_STREQ(found->text, "1");
    EXPECT_EQ(vigil_xml_element_child(doc->root, "z"), (void *)NULL);
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, ChildrenByTagCollectsMatches)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<r><a>1</a><b>2</b><a>3</a></r>";
    const vigil_xml_element_t *results[4];
    size_t count;
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    count = vigil_xml_element_children_by_tag(doc->root, "a", results, 4);
    EXPECT_EQ(count, 2U);
    EXPECT_STREQ(results[0]->text, "1");
    EXPECT_STREQ(results[1]->text, "3");
    vigil_xml_document_free(doc);
}

TEST(VigilXmlTest, AllTextConcatenatesRecursively)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_error_t err;
    const char *xml = "<r>hello <b>world</b></r>";
    char *all;
    size_t len;
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    all = vigil_xml_element_all_text(doc->root, &len);
    ASSERT_NE(all, (void *)NULL);
    EXPECT_STREQ(all, "hello world");
    free(all);
    vigil_xml_document_free(doc);
}

/* ── Serialization round-trip ────────────────────────────────────── */

TEST(VigilXmlTest, SerializationRoundTrip)
{
    vigil_xml_document_t *doc = NULL;
    vigil_xml_document_t *doc2 = NULL;
    vigil_xml_error_t err;
    const char *xml = "<?xml version=\"1.0\"?><root a=\"1\"><child>text</child></root>";
    char *output;
    size_t output_len;
    EXPECT_EQ(vigil_xml_parse(xml, strlen(xml), &doc, &err), VIGIL_STATUS_OK);
    output = vigil_xml_document_to_string(doc, &output_len);
    ASSERT_NE(output, (void *)NULL);
    EXPECT_EQ(vigil_xml_parse(output, output_len, &doc2, &err), VIGIL_STATUS_OK);
    EXPECT_STREQ(doc2->root->tag, "root");
    EXPECT_STREQ(vigil_xml_element_attr(doc2->root, "a"), "1");
    EXPECT_EQ(doc2->root->child_count, 1U);
    EXPECT_STREQ(doc2->root->children[0]->text, "text");
    free(output);
    vigil_xml_document_free(doc);
    vigil_xml_document_free(doc2);
}

/* ── Registration ────────────────────────────────────────────────── */

void register_xml_tests(void)
{
    REGISTER_TEST(VigilXmlTest, ParsesSimpleElement);
    REGISTER_TEST(VigilXmlTest, ParsesElementWithText);
    REGISTER_TEST(VigilXmlTest, ParsesAttributes);
    REGISTER_TEST(VigilXmlTest, ParsesNestedElements);
    REGISTER_TEST(VigilXmlTest, DecodesEntityReferences);
    REGISTER_TEST(VigilXmlTest, DecodesCharacterReferences);
    REGISTER_TEST(VigilXmlTest, DecodesEntitiesInAttributes);
    REGISTER_TEST(VigilXmlTest, ParsesCdataSection);
    REGISTER_TEST(VigilXmlTest, SkipsComments);
    REGISTER_TEST(VigilXmlTest, SkipsProcessingInstructions);
    REGISTER_TEST(VigilXmlTest, SkipsDoctype);
    REGISTER_TEST(VigilXmlTest, ParsesXmlDeclaration);
    REGISTER_TEST(VigilXmlTest, SkipsUtf8Bom);
    REGISTER_TEST(VigilXmlTest, RejectsMismatchedTags);
    REGISTER_TEST(VigilXmlTest, RejectsUnclosedElement);
    REGISTER_TEST(VigilXmlTest, RejectsEmptyInput);
    REGISTER_TEST(VigilXmlTest, RejectsNullInput);
    REGISTER_TEST(VigilXmlTest, RejectsDeeplyNestedInput);
    REGISTER_TEST(VigilXmlTest, ElementChildFindsFirstMatch);
    REGISTER_TEST(VigilXmlTest, ChildrenByTagCollectsMatches);
    REGISTER_TEST(VigilXmlTest, AllTextConcatenatesRecursively);
    REGISTER_TEST(VigilXmlTest, SerializationRoundTrip);
}
