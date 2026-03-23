#include "vigil/semantic.h"

#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/native_module.h"
#include "vigil/stdlib.h"
#include "vigil/value.h"

/* ── Type Utilities ───────────────────────────────────────── */

vigil_semantic_type_t vigil_semantic_type_invalid(void)
{
    vigil_semantic_type_t type;
    type.kind = VIGIL_TYPE_INVALID;
    type.definition_index = SIZE_MAX;
    return type;
}

vigil_semantic_type_t vigil_semantic_type_primitive(vigil_type_kind_t kind)
{
    vigil_semantic_type_t type;
    type.kind = kind;
    type.definition_index = SIZE_MAX;
    return type;
}

int vigil_semantic_type_is_valid(vigil_semantic_type_t type)
{
    return type.kind != VIGIL_TYPE_INVALID;
}

int vigil_semantic_type_equal(vigil_semantic_type_t left, vigil_semantic_type_t right)
{
    return left.kind == right.kind && left.definition_index == right.definition_index;
}

/* ── Allocation Helpers ───────────────────────────────────── */

static void *sem_alloc(vigil_runtime_t *runtime, size_t size, vigil_error_t *error)
{
    void *ptr = NULL;
    if (vigil_runtime_alloc(runtime, size, &ptr, error) != VIGIL_STATUS_OK)
    {
        return NULL;
    }
    return ptr;
}

static void sem_free(vigil_runtime_t *runtime, void *ptr)
{
    void *p = ptr;
    vigil_runtime_free(runtime, &p);
}

/* ── Node Allocation ──────────────────────────────────────── */

static vigil_semantic_node_t *semantic_node_alloc(vigil_runtime_t *runtime, vigil_error_t *error)
{
    vigil_semantic_node_t *node = sem_alloc(runtime, sizeof(vigil_semantic_node_t), error);
    if (node != NULL)
    {
        memset(node, 0, sizeof(vigil_semantic_node_t));
        node->kind = VIGIL_NODE_INVALID;
        node->type = vigil_semantic_type_invalid();
    }
    return node;
}

/* Suppress unused warning until analyzer is implemented */
VIGIL_API vigil_semantic_node_t *vigil_semantic_node_create(vigil_runtime_t *runtime, vigil_semantic_node_kind_t kind,
                                                            vigil_source_span_t span, vigil_error_t *error)
{
    vigil_semantic_node_t *node = semantic_node_alloc(runtime, error);
    if (node != NULL)
    {
        node->kind = kind;
        node->span = span;
    }
    return node;
}

static void semantic_node_free(vigil_runtime_t *runtime, vigil_semantic_node_t *node)
{
    size_t i;
    if (node == NULL)
        return;

    switch (node->kind)
    {
    case VIGIL_NODE_FUNCTION_DECL:
        semantic_node_free(runtime, node->data.function_decl.body);
        break;

    case VIGIL_NODE_CLASS_DECL:
        for (i = 0; i < node->data.class_decl.member_count; i++)
        {
            semantic_node_free(runtime, node->data.class_decl.members[i]);
        }
        sem_free(runtime, node->data.class_decl.members);
        break;

    case VIGIL_NODE_BLOCK_STMT:
        for (i = 0; i < node->data.block.statement_count; i++)
        {
            semantic_node_free(runtime, node->data.block.statements[i]);
        }
        sem_free(runtime, node->data.block.statements);
        break;

    case VIGIL_NODE_IF_STMT:
        semantic_node_free(runtime, node->data.if_stmt.condition);
        semantic_node_free(runtime, node->data.if_stmt.then_branch);
        semantic_node_free(runtime, node->data.if_stmt.else_branch);
        break;

    case VIGIL_NODE_WHILE_STMT:
        semantic_node_free(runtime, node->data.while_stmt.condition);
        semantic_node_free(runtime, node->data.while_stmt.body);
        break;

    case VIGIL_NODE_RETURN_STMT:
        semantic_node_free(runtime, node->data.return_stmt.value);
        break;

    case VIGIL_NODE_EXPR_STMT:
        semantic_node_free(runtime, node->data.expr_stmt.expression);
        break;

    case VIGIL_NODE_ASSIGN_STMT:
        semantic_node_free(runtime, node->data.assign.target);
        semantic_node_free(runtime, node->data.assign.value);
        break;

    case VIGIL_NODE_BINARY_EXPR:
        semantic_node_free(runtime, node->data.binary.left);
        semantic_node_free(runtime, node->data.binary.right);
        break;

    case VIGIL_NODE_UNARY_EXPR:
        semantic_node_free(runtime, node->data.unary.operand);
        break;

    case VIGIL_NODE_CALL_EXPR:
        semantic_node_free(runtime, node->data.call.callee);
        for (i = 0; i < node->data.call.argument_count; i++)
        {
            semantic_node_free(runtime, node->data.call.arguments[i]);
        }
        sem_free(runtime, node->data.call.arguments);
        break;

    case VIGIL_NODE_METHOD_CALL_EXPR:
        semantic_node_free(runtime, node->data.method_call.receiver);
        for (i = 0; i < node->data.method_call.argument_count; i++)
        {
            semantic_node_free(runtime, node->data.method_call.arguments[i]);
        }
        sem_free(runtime, node->data.method_call.arguments);
        break;

    case VIGIL_NODE_INDEX_EXPR:
        semantic_node_free(runtime, node->data.index_expr.object);
        semantic_node_free(runtime, node->data.index_expr.index);
        break;

    case VIGIL_NODE_FIELD_ACCESS_EXPR:
        semantic_node_free(runtime, node->data.field_access.object);
        break;

    case VIGIL_NODE_VAR_DECL:
        semantic_node_free(runtime, node->data.var_decl.initializer);
        break;

    default:
        break;
    }

    sem_free(runtime, node);
}

/* ── File API ─────────────────────────────────────────────── */

vigil_status_t vigil_semantic_file_create(vigil_semantic_file_t **out, vigil_runtime_t *runtime,
                                          vigil_source_id_t source_id, vigil_error_t *error)
{
    vigil_semantic_file_t *file;

    if (out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    file = sem_alloc(runtime, sizeof(vigil_semantic_file_t), error);
    if (file == NULL)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memset(file, 0, sizeof(vigil_semantic_file_t));
    file->runtime = runtime;
    file->source_id = source_id;
    vigil_diagnostic_list_init(&file->diagnostics, runtime);

    *out = file;
    return VIGIL_STATUS_OK;
}

void vigil_semantic_file_destroy(vigil_semantic_file_t **file)
{
    vigil_semantic_file_t *f;
    size_t i;

    if (file == NULL || *file == NULL)
        return;
    f = *file;

    for (i = 0; i < f->declaration_count; i++)
    {
        semantic_node_free(f->runtime, f->declarations[i]);
    }
    sem_free(f->runtime, f->declarations);
    sem_free(f->runtime, f->nodes_by_position);
    sem_free(f->runtime, f->references);
    vigil_diagnostic_list_free(&f->diagnostics);
    sem_free(f->runtime, f);
    *file = NULL;
}

/*
 * Binary search for node containing offset.
 */
const vigil_semantic_node_t *vigil_semantic_file_node_at(const vigil_semantic_file_t *file, size_t offset)
{
    size_t lo, hi, mid;
    const vigil_semantic_node_t *best = NULL;
    const vigil_semantic_node_t *node;

    if (file == NULL || file->node_count == 0)
        return NULL;

    lo = 0;
    hi = file->node_count;

    while (lo < hi)
    {
        mid = lo + (hi - lo) / 2;
        node = file->nodes_by_position[mid];

        if (offset < node->span.start_offset)
        {
            hi = mid;
        }
        else if (offset >= node->span.end_offset)
        {
            lo = mid + 1;
        }
        else
        {
            /* offset is within this node's span */
            best = node;
            /* Look for a more specific (smaller) node */
            lo = mid + 1;
        }
    }

    return best;
}

vigil_semantic_type_t vigil_semantic_file_type_at(const vigil_semantic_file_t *file, size_t offset)
{
    const vigil_semantic_node_t *node = vigil_semantic_file_node_at(file, offset);
    if (node == NULL)
        return vigil_semantic_type_invalid();
    return node->type;
}

/* ── Index API ────────────────────────────────────────────── */

vigil_status_t vigil_semantic_index_create(vigil_semantic_index_t **out, vigil_runtime_t *runtime,
                                           const vigil_source_registry_t *sources, vigil_error_t *error)
{
    vigil_semantic_index_t *index;

    if (out == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    index = sem_alloc(runtime, sizeof(vigil_semantic_index_t), error);
    if (index == NULL)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memset(index, 0, sizeof(vigil_semantic_index_t));
    index->runtime = runtime;
    index->sources = sources;
    vigil_debug_symbol_table_init(&index->symbols, runtime);

    *out = index;
    return VIGIL_STATUS_OK;
}

void vigil_semantic_index_destroy(vigil_semantic_index_t **index)
{
    vigil_semantic_index_t *idx;
    size_t i;

    if (index == NULL || *index == NULL)
        return;
    idx = *index;

    for (i = 0; i < idx->file_count; i++)
    {
        vigil_semantic_file_destroy(&idx->files[i]);
    }
    sem_free(idx->runtime, idx->files);
    vigil_debug_symbol_table_free(&idx->symbols);
    sem_free(idx->runtime, idx);
    *index = NULL;
}

const vigil_semantic_file_t *vigil_semantic_index_get_file(const vigil_semantic_index_t *index,
                                                           vigil_source_id_t source_id)
{
    size_t i;
    if (index == NULL)
        return NULL;

    for (i = 0; i < index->file_count; i++)
    {
        if (index->files[i]->source_id == source_id)
        {
            return index->files[i];
        }
    }
    return NULL;
}

void vigil_semantic_index_remove(vigil_semantic_index_t *index, vigil_source_id_t source_id)
{
    size_t i;
    if (index == NULL)
        return;

    for (i = 0; i < index->file_count; i++)
    {
        if (index->files[i]->source_id == source_id)
        {
            vigil_semantic_file_destroy(&index->files[i]);
            /* Shift remaining files down */
            for (; i + 1 < index->file_count; i++)
            {
                index->files[i] = index->files[i + 1];
            }
            index->file_count--;
            return;
        }
    }
}

/* ── Index Analysis ───────────────────────────────────────── */

static vigil_status_t semantic_index_grow_files(vigil_semantic_index_t *index, vigil_error_t *error)
{
    size_t new_capacity;
    vigil_semantic_file_t **new_files;

    if (index->file_count < index->file_capacity)
    {
        return VIGIL_STATUS_OK;
    }

    new_capacity = index->file_capacity == 0 ? 4 : index->file_capacity * 2;
    new_files = sem_alloc(index->runtime, new_capacity * sizeof(vigil_semantic_file_t *), error);
    if (new_files == NULL)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (index->files != NULL)
    {
        memcpy(new_files, index->files, index->file_count * sizeof(vigil_semantic_file_t *));
        sem_free(index->runtime, index->files);
    }

    index->files = new_files;
    index->file_capacity = new_capacity;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_semantic_index_analyze(vigil_semantic_index_t *index, vigil_source_id_t source_id,
                                            vigil_error_t *error)
{
    vigil_status_t status;
    vigil_semantic_file_t *file = NULL;
    vigil_native_registry_t natives;
    vigil_object_t *function = NULL;
    vigil_debug_symbol_table_t file_symbols;
    int symbols_initialized = 0;
    size_t i, count;

    if (index == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "index must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Remove existing analysis for this file. */
    vigil_semantic_index_remove(index, source_id);

    /* Create new semantic file. */
    status = vigil_semantic_file_create(&file, index->runtime, source_id, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    /* Initialize natives and file symbols. */
    vigil_native_registry_init(&natives);
    vigil_stdlib_register_all(&natives, error);
    vigil_debug_symbol_table_init(&file_symbols, index->runtime);
    symbols_initialized = 1;

    /* Compile to get diagnostics and symbols. */
    status = vigil_compile_source_with_debug_info(index->sources, source_id, &natives, &function, &file->diagnostics,
                                                  &file_symbols, error);

    /* Copy symbols to index (even if compile failed, we want partial symbols). */
    count = vigil_debug_symbol_table_count(&file_symbols);
    for (i = 0; i < count; i++)
    {
        const vigil_debug_symbol_t *sym = vigil_debug_symbol_table_get(&file_symbols, i);
        vigil_debug_symbol_table_add(&index->symbols, sym->kind, sym->name, sym->name_length, sym->span, sym->is_public,
                                     sym->parent_index, error);
    }

    /* Clean up compilation artifacts. */
    if (function != NULL)
    {
        vigil_object_release(&function);
    }
    vigil_native_registry_free(&natives);
    if (symbols_initialized)
    {
        vigil_debug_symbol_table_free(&file_symbols);
    }

    /* Add file to index. */
    status = semantic_index_grow_files(index, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_semantic_file_destroy(&file);
        return status;
    }

    index->files[index->file_count++] = file;
    return VIGIL_STATUS_OK;
}

/* ── Type Formatting ──────────────────────────────────────── */

char *vigil_semantic_type_to_string(const vigil_semantic_index_t *index, vigil_semantic_type_t type)
{
    const char *name;
    char *result;
    size_t len;

    (void)index; /* Will be used for class/interface names */

    if (!vigil_semantic_type_is_valid(type))
    {
        name = "<invalid>";
    }
    else
    {
        name = vigil_type_kind_name(type.kind);
    }

    len = strlen(name);
    result = malloc(len + 1);
    if (result != NULL)
    {
        memcpy(result, name, len + 1);
    }
    return result;
}

/* ── Go to Definition ─────────────────────────────────────── */

vigil_status_t vigil_semantic_index_definition_at(const vigil_semantic_index_t *index, vigil_source_id_t source_id,
                                                  size_t offset, vigil_source_span_t *out_definition,
                                                  vigil_error_t *error)
{
    const vigil_semantic_file_t *file = NULL;
    const vigil_semantic_node_t *node;
    size_t i;

    if (index == NULL || out_definition == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "semantic: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Find file */
    for (i = 0; i < index->file_count; i++)
    {
        if (index->files[i]->source_id == source_id)
        {
            file = index->files[i];
            break;
        }
    }

    if (file == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "semantic: source not found");
        return VIGIL_STATUS_INTERNAL;
    }

    node = vigil_semantic_file_node_at(file, offset);
    if (node == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "semantic: no node at position");
        return VIGIL_STATUS_INTERNAL;
    }

    /* If node is an identifier expression, look up the definition */
    if (node->kind == VIGIL_NODE_IDENTIFIER_EXPR)
    {
        const char *name = node->data.identifier.name;
        size_t name_len = node->data.identifier.name_length;

        /* Search symbols for matching name */
        size_t sym_count = vigil_debug_symbol_table_count(&index->symbols);
        for (i = 0; i < sym_count; i++)
        {
            const vigil_debug_symbol_t *sym = vigil_debug_symbol_table_get(&index->symbols, i);
            if (sym->name_length == name_len && strncmp(sym->name, name, name_len) == 0)
            {
                *out_definition = sym->span;
                return VIGIL_STATUS_OK;
            }
        }
    }

    /* Node is already a definition or no definition found */
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "semantic: definition not found");
    return VIGIL_STATUS_INTERNAL;
}

/* ── Find References ──────────────────────────────────────── */

vigil_status_t vigil_semantic_index_references_at(const vigil_semantic_index_t *index, vigil_source_id_t source_id,
                                                  size_t offset, vigil_semantic_reference_t **out_references,
                                                  size_t *out_count, vigil_error_t *error)
{
    const vigil_semantic_file_t *file = NULL;
    const vigil_semantic_node_t *node;
    const char *target_name = NULL;
    size_t target_name_len = 0;
    vigil_semantic_reference_t *refs = NULL;
    size_t ref_count = 0;
    size_t ref_capacity = 0;
    size_t i, j;

    if (index == NULL || out_references == NULL || out_count == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "semantic: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_references = NULL;
    *out_count = 0;

    /* Find file and node at position */
    for (i = 0; i < index->file_count; i++)
    {
        if (index->files[i]->source_id == source_id)
        {
            file = index->files[i];
            break;
        }
    }

    if (file == NULL)
    {
        return VIGIL_STATUS_OK; /* No file, no references */
    }

    node = vigil_semantic_file_node_at(file, offset);
    if (node == NULL)
    {
        return VIGIL_STATUS_OK;
    }

    /* Get target name from identifier or symbol definition */
    if (node->kind == VIGIL_NODE_IDENTIFIER_EXPR)
    {
        target_name = node->data.identifier.name;
        target_name_len = node->data.identifier.name_length;
    }
    else if (node->kind == VIGIL_NODE_FUNCTION_DECL || node->kind == VIGIL_NODE_CLASS_DECL)
    {
        /* Look up symbol at this span */
        size_t sym_count = vigil_debug_symbol_table_count(&index->symbols);
        for (i = 0; i < sym_count; i++)
        {
            const vigil_debug_symbol_t *sym = vigil_debug_symbol_table_get(&index->symbols, i);
            if (sym->span.start_offset == node->span.start_offset)
            {
                target_name = sym->name;
                target_name_len = sym->name_length;
                break;
            }
        }
    }

    if (target_name == NULL)
    {
        return VIGIL_STATUS_OK;
    }

    /* Collect all references to this name across all files */
    for (i = 0; i < index->file_count; i++)
    {
        const vigil_semantic_file_t *f = index->files[i];
        for (j = 0; j < f->node_count; j++)
        {
            const vigil_semantic_node_t *n = f->nodes_by_position[j];
            if (n->kind == VIGIL_NODE_IDENTIFIER_EXPR && n->data.identifier.name_length == target_name_len &&
                strncmp(n->data.identifier.name, target_name, target_name_len) == 0)
            {

                /* Grow array if needed */
                if (ref_count >= ref_capacity)
                {
                    size_t new_cap = ref_capacity == 0 ? 8 : ref_capacity * 2;
                    vigil_semantic_reference_t *new_refs = realloc(refs, new_cap * sizeof(*refs));
                    if (new_refs == NULL)
                    {
                        free(refs);
                        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
                        return VIGIL_STATUS_OUT_OF_MEMORY;
                    }
                    refs = new_refs;
                    ref_capacity = new_cap;
                }

                refs[ref_count].span = n->span;
                refs[ref_count].symbol_kind = VIGIL_DEBUG_SYMBOL_FUNCTION; /* Placeholder */
                refs[ref_count].symbol_index = 0;
                refs[ref_count].is_write = 0;
                ref_count++;
            }
        }
    }

    *out_references = refs;
    *out_count = ref_count;
    return VIGIL_STATUS_OK;
}
