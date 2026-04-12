/*
 * vigil_xml.h — Internal XML DOM types and parser API.
 *
 * Pure C11, no platform dependencies.  The parser builds an in-memory
 * DOM tree from a UTF-8 XML string.
 */

#ifndef VIGIL_XML_H
#define VIGIL_XML_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"
#include "vigil/status.h"

/* ── DOM types ───────────────────────────────────────────────────── */

typedef struct vigil_xml_attribute
{
    char *name;
    char *namespace_uri; /* resolved, NULL if none */
    char *value;
} vigil_xml_attribute_t;

typedef struct vigil_xml_element vigil_xml_element_t;

struct vigil_xml_element
{
    char *tag;           /* local name */
    char *namespace_uri; /* resolved, NULL if none */
    vigil_xml_attribute_t *attributes;
    size_t attribute_count;
    size_t attribute_capacity;
    vigil_xml_element_t *parent;
    vigil_xml_element_t **children;
    size_t child_count;
    size_t child_capacity;
    char *text;      /* concatenated direct text content */
    size_t text_len; /* byte length of text */
    size_t text_cap;
};

typedef struct vigil_xml_document
{
    vigil_xml_element_t *root;
    char *version;  /* from <?xml?> declaration, NULL if absent */
    char *encoding; /* from <?xml?> declaration, NULL if absent */
} vigil_xml_document_t;

/* ── Parser error info ───────────────────────────────────────────── */

typedef struct vigil_xml_error
{
    char message[256];
    size_t line;
    size_t column;
} vigil_xml_error_t;

/* ── API ─────────────────────────────────────────────────────────── */

VIGIL_API vigil_status_t vigil_xml_parse(const char *text, size_t length, vigil_xml_document_t **out_document,
                                         vigil_xml_error_t *out_error);

VIGIL_API void vigil_xml_document_free(vigil_xml_document_t *document);

/* Serialize a document back to an XML string.  Caller must free the
 * returned string with free(). */
VIGIL_API char *vigil_xml_document_to_string(const vigil_xml_document_t *document, size_t *out_length);

/* Element query helpers. */
VIGIL_API const char *vigil_xml_element_attr(const vigil_xml_element_t *element, const char *name);
VIGIL_API const vigil_xml_element_t *vigil_xml_element_child(const vigil_xml_element_t *element, const char *tag);
VIGIL_API size_t vigil_xml_element_children_by_tag(const vigil_xml_element_t *element, const char *tag,
                                                   const vigil_xml_element_t **out_children, size_t max_count);

/* Recursive text content (concatenates all descendant text). */
VIGIL_API char *vigil_xml_element_all_text(const vigil_xml_element_t *element, size_t *out_length);

#endif /* VIGIL_XML_H */
