#ifndef VIGIL_DOC_H
#define VIGIL_DOC_H

#include <stddef.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/status.h"
#include "vigil/token.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Doc comment ─────────────────────────────────────────────────── */

    typedef struct vigil_doc_comment
    {
        char *text;
        size_t length;
    } vigil_doc_comment_t;

    /* ── Doc parameter ───────────────────────────────────────────────── */

    typedef struct vigil_doc_param
    {
        char *type_text;
        size_t type_length;
        char *name;
        size_t name_length;
    } vigil_doc_param_t;

    /* ── Doc symbol kind ─────────────────────────────────────────────── */

    typedef enum vigil_doc_kind
    {
        VIGIL_DOC_FUNCTION = 0,
        VIGIL_DOC_CLASS = 1,
        VIGIL_DOC_INTERFACE = 2,
        VIGIL_DOC_ENUM = 3,
        VIGIL_DOC_CONSTANT = 4,
        VIGIL_DOC_VARIABLE = 5
    } vigil_doc_kind_t;

    /* ── Doc symbol ──────────────────────────────────────────────────── */

    typedef struct vigil_doc_symbol
    {
        vigil_doc_kind_t kind;
        char *name;
        size_t name_length;
        vigil_doc_comment_t comment;

        /* Functions, class methods, interface methods. */
        vigil_doc_param_t *params;
        size_t param_count;
        char *return_text;
        size_t return_length;

        /* Classes. */
        struct vigil_doc_symbol *fields;
        size_t field_count;
        struct vigil_doc_symbol *methods;
        size_t method_count;
        char *implements_text;
        size_t implements_length;

        /* Interfaces. */
        struct vigil_doc_symbol *iface_methods;
        size_t iface_method_count;

        /* Enums. */
        char **variant_names;
        size_t *variant_name_lengths;
        size_t variant_count;

        /* Constants and variables. */
        char *type_text;
        size_t type_length;
    } vigil_doc_symbol_t;

    /* ── Doc module ──────────────────────────────────────────────────── */

    typedef struct vigil_doc_module
    {
        char *name;
        size_t name_length;
        vigil_doc_comment_t summary;
        vigil_doc_symbol_t *symbols;
        size_t symbol_count;
        size_t symbol_capacity;
        const vigil_allocator_t *allocator;
    } vigil_doc_module_t;

    /* ── Public API ──────────────────────────────────────────────────── */

    /*
     * Extract documentation from source text and its token stream.
     * The token list must have been produced by vigil_lex_source for the
     * same source text.  Only public declarations are included.
     */
    VIGIL_API vigil_status_t vigil_doc_extract(const vigil_allocator_t *allocator, const char *filename,
                                               size_t filename_length, const char *source_text, size_t source_length,
                                               const vigil_token_list_t *tokens, vigil_doc_module_t *out_module,
                                               vigil_error_t *error);

    /*
     * Render the full module documentation to a string.
     * If symbol is non-NULL, render only that symbol (supports "Class.method").
     * Caller must free *out_text with the allocator (or free() if NULL).
     */
    VIGIL_API vigil_status_t vigil_doc_render(const vigil_doc_module_t *module, const char *symbol, char **out_text,
                                              size_t *out_length, vigil_error_t *error);

    /*
     * Free all memory owned by a doc module.
     */
    VIGIL_API void vigil_doc_module_free(vigil_doc_module_t *module);

#ifdef __cplusplus
}
#endif

#endif
