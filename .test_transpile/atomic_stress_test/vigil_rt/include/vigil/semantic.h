#ifndef VIGIL_SEMANTIC_H
#define VIGIL_SEMANTIC_H

#include <stddef.h>

#include "vigil/debug_info.h"
#include "vigil/diagnostic.h"
#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/type.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Semantic analysis model for LSP support.
     *
     * Provides a queryable representation of a VIGIL program with:
     * - Type information at any source position
     * - Symbol definitions and references
     * - Scope-aware symbol enumeration
     * - Incremental updates on file changes
     */

    /* ── Typed Node Kinds ─────────────────────────────────────── */

    typedef enum vigil_semantic_node_kind
    {
        VIGIL_NODE_INVALID = 0,

        /* Declarations */
        VIGIL_NODE_FUNCTION_DECL,
        VIGIL_NODE_CLASS_DECL,
        VIGIL_NODE_INTERFACE_DECL,
        VIGIL_NODE_ENUM_DECL,
        VIGIL_NODE_FIELD_DECL,
        VIGIL_NODE_METHOD_DECL,
        VIGIL_NODE_PARAM_DECL,
        VIGIL_NODE_VAR_DECL,
        VIGIL_NODE_CONST_DECL,

        /* Statements */
        VIGIL_NODE_BLOCK_STMT,
        VIGIL_NODE_IF_STMT,
        VIGIL_NODE_WHILE_STMT,
        VIGIL_NODE_FOR_STMT,
        VIGIL_NODE_RETURN_STMT,
        VIGIL_NODE_BREAK_STMT,
        VIGIL_NODE_CONTINUE_STMT,
        VIGIL_NODE_EXPR_STMT,
        VIGIL_NODE_ASSIGN_STMT,

        /* Expressions */
        VIGIL_NODE_LITERAL_EXPR,
        VIGIL_NODE_IDENTIFIER_EXPR,
        VIGIL_NODE_BINARY_EXPR,
        VIGIL_NODE_UNARY_EXPR,
        VIGIL_NODE_CALL_EXPR,
        VIGIL_NODE_METHOD_CALL_EXPR,
        VIGIL_NODE_INDEX_EXPR,
        VIGIL_NODE_FIELD_ACCESS_EXPR,
        VIGIL_NODE_ARRAY_LITERAL_EXPR,
        VIGIL_NODE_MAP_LITERAL_EXPR,
        VIGIL_NODE_NEW_EXPR,
        VIGIL_NODE_CAST_EXPR,
        VIGIL_NODE_TERNARY_EXPR
    } vigil_semantic_node_kind_t;

    /* ── Semantic Type ────────────────────────────────────────── */

    /*
     * Resolved type information. Mirrors vigil_binding_type_t but is
     * part of the public semantic API.
     */
    typedef struct vigil_semantic_type
    {
        vigil_type_kind_t kind;
        size_t definition_index; /* Index into classes/interfaces/enums/etc */
    } vigil_semantic_type_t;

    /* ── Semantic Node ────────────────────────────────────────── */

    /*
     * A node in the typed AST. Every node has:
     * - A kind identifying what it represents
     * - A source span for position queries
     * - A resolved type (for expressions)
     * - Links to child nodes
     */
    typedef struct vigil_semantic_node vigil_semantic_node_t;

    struct vigil_semantic_node
    {
        vigil_semantic_node_kind_t kind;
        vigil_source_span_t span;
        vigil_semantic_type_t type;

        /* Node-specific data stored inline to avoid extra allocations */
        union {
            struct
            {
                size_t function_index;
                vigil_semantic_node_t *body;
            } function_decl;

            struct
            {
                size_t class_index;
                vigil_semantic_node_t **members;
                size_t member_count;
            } class_decl;

            struct
            {
                vigil_semantic_node_t **statements;
                size_t statement_count;
            } block;

            struct
            {
                vigil_semantic_node_t *condition;
                vigil_semantic_node_t *then_branch;
                vigil_semantic_node_t *else_branch;
            } if_stmt;

            struct
            {
                vigil_semantic_node_t *condition;
                vigil_semantic_node_t *body;
            } while_stmt;

            struct
            {
                vigil_semantic_node_t *value;
            } return_stmt;

            struct
            {
                vigil_semantic_node_t *expression;
            } expr_stmt;

            struct
            {
                vigil_semantic_node_t *target;
                vigil_semantic_node_t *value;
            } assign;

            struct
            {
                int64_t int_value;
                double float_value;
                const char *string_value;
                size_t string_length;
                int bool_value;
            } literal;

            struct
            {
                const char *name;
                size_t name_length;
                size_t resolved_index; /* Local slot or global index */
                int is_local;
            } identifier;

            struct
            {
                vigil_binary_operator_kind_t op;
                vigil_semantic_node_t *left;
                vigil_semantic_node_t *right;
            } binary;

            struct
            {
                vigil_unary_operator_kind_t op;
                vigil_semantic_node_t *operand;
            } unary;

            struct
            {
                vigil_semantic_node_t *callee;
                vigil_semantic_node_t **arguments;
                size_t argument_count;
                size_t function_index;
            } call;

            struct
            {
                vigil_semantic_node_t *receiver;
                const char *method_name;
                size_t method_name_length;
                vigil_semantic_node_t **arguments;
                size_t argument_count;
                size_t method_index;
            } method_call;

            struct
            {
                vigil_semantic_node_t *object;
                vigil_semantic_node_t *index;
            } index_expr;

            struct
            {
                vigil_semantic_node_t *object;
                const char *field_name;
                size_t field_name_length;
                size_t field_index;
            } field_access;

            struct
            {
                const char *name;
                size_t name_length;
                vigil_semantic_type_t var_type;
                vigil_semantic_node_t *initializer;
                int is_const;
            } var_decl;
        } data;
    };

    /* ── Symbol Reference ─────────────────────────────────────── */

    /*
     * Tracks where a symbol is used (not just defined).
     */
    typedef struct vigil_semantic_reference
    {
        vigil_source_span_t span;
        vigil_debug_symbol_kind_t symbol_kind;
        size_t symbol_index;
        int is_write; /* true if this is an assignment target */
    } vigil_semantic_reference_t;

    /* ── Semantic File ────────────────────────────────────────── */

    /*
     * Semantic analysis result for a single source file.
     */
    typedef struct vigil_semantic_file
    {
        vigil_runtime_t *runtime;
        vigil_source_id_t source_id;

        /* Top-level declarations as typed nodes */
        vigil_semantic_node_t **declarations;
        size_t declaration_count;
        size_t declaration_capacity;

        /* All nodes in span order for position queries */
        vigil_semantic_node_t **nodes_by_position;
        size_t node_count;
        size_t node_capacity;

        /* References to symbols (for find-references) */
        vigil_semantic_reference_t *references;
        size_t reference_count;
        size_t reference_capacity;

        /* Diagnostics from semantic analysis */
        vigil_diagnostic_list_t diagnostics;
    } vigil_semantic_file_t;

    /* ── Semantic Index ───────────────────────────────────────── */

    /*
     * Cross-file semantic index for a workspace.
     */
    typedef struct vigil_semantic_index
    {
        vigil_runtime_t *runtime;
        const vigil_source_registry_t *sources;

        /* Per-file semantic data */
        vigil_semantic_file_t **files;
        size_t file_count;
        size_t file_capacity;

        /* Global symbol table (merged from all files) */
        vigil_debug_symbol_table_t symbols;
    } vigil_semantic_index_t;

    /* ── File API ─────────────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_semantic_file_create(vigil_semantic_file_t **out, vigil_runtime_t *runtime,
                                                        vigil_source_id_t source_id, vigil_error_t *error);

    VIGIL_API void vigil_semantic_file_destroy(vigil_semantic_file_t **file);

    /*
     * Find the innermost node containing the given position.
     * Returns NULL if no node contains the position.
     */
    VIGIL_API const vigil_semantic_node_t *vigil_semantic_file_node_at(const vigil_semantic_file_t *file,
                                                                       size_t offset);

    /*
     * Get the type at a source position.
     * Returns invalid type if position is not within a typed expression.
     */
    VIGIL_API vigil_semantic_type_t vigil_semantic_file_type_at(const vigil_semantic_file_t *file, size_t offset);

    /*
     * Get all symbols visible at a position (for completion).
     */
    VIGIL_API vigil_status_t vigil_semantic_file_visible_symbols(const vigil_semantic_file_t *file, size_t offset,
                                                                 vigil_debug_symbol_table_t *out_symbols,
                                                                 vigil_error_t *error);

    /* ── Index API ────────────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_semantic_index_create(vigil_semantic_index_t **out, vigil_runtime_t *runtime,
                                                         const vigil_source_registry_t *sources, vigil_error_t *error);

    VIGIL_API void vigil_semantic_index_destroy(vigil_semantic_index_t **index);

    /*
     * Analyze a source file and add/update it in the index.
     */
    VIGIL_API vigil_status_t vigil_semantic_index_analyze(vigil_semantic_index_t *index, vigil_source_id_t source_id,
                                                          vigil_error_t *error);

    /*
     * Remove a file from the index.
     */
    VIGIL_API void vigil_semantic_index_remove(vigil_semantic_index_t *index, vigil_source_id_t source_id);

    /*
     * Get semantic data for a file (NULL if not analyzed).
     */
    VIGIL_API const vigil_semantic_file_t *vigil_semantic_index_get_file(const vigil_semantic_index_t *index,
                                                                         vigil_source_id_t source_id);

    /*
     * Find definition of symbol at position.
     */
    VIGIL_API vigil_status_t vigil_semantic_index_definition_at(const vigil_semantic_index_t *index,
                                                                vigil_source_id_t source_id, size_t offset,
                                                                vigil_source_span_t *out_definition,
                                                                vigil_error_t *error);

    /*
     * Find all references to symbol at position.
     */
    VIGIL_API vigil_status_t vigil_semantic_index_references_at(const vigil_semantic_index_t *index,
                                                                vigil_source_id_t source_id, size_t offset,
                                                                vigil_semantic_reference_t **out_references,
                                                                size_t *out_count, vigil_error_t *error);

    /* ── Type Utilities ───────────────────────────────────────── */

    VIGIL_API vigil_semantic_type_t vigil_semantic_type_invalid(void);
    VIGIL_API vigil_semantic_type_t vigil_semantic_type_primitive(vigil_type_kind_t kind);
    VIGIL_API int vigil_semantic_type_is_valid(vigil_semantic_type_t type);
    VIGIL_API int vigil_semantic_type_equal(vigil_semantic_type_t left, vigil_semantic_type_t right);

    /*
     * Format type as human-readable string.
     * Caller must free the returned string.
     */
    VIGIL_API char *vigil_semantic_type_to_string(const vigil_semantic_index_t *index, vigil_semantic_type_t type);

#ifdef __cplusplus
}
#endif

#endif
