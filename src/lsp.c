#include "vigil/lsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/doc_registry.h"
#include "vigil/fmt.h"
#include "vigil/json.h"
#include "vigil/lexer.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/stdlib.h"

/* ── Server State ─────────────────────────────────────────── */

struct vigil_lsp_server
{
    vigil_jsonrpc_transport_t transport;
    vigil_allocator_t allocator;
    vigil_runtime_t *runtime;
    vigil_source_registry_t sources;
    vigil_semantic_index_t *index;
    int initialized;
    int shutdown_requested;
};

/* ── JSON Helpers ─────────────────────────────────────────── */

static vigil_status_t jset_str(vigil_json_value_t *obj, const char *key, const char *val,
                               const vigil_allocator_t *alloc, vigil_error_t *error)
{
    vigil_json_value_t *v = NULL;
    vigil_status_t s = vigil_json_string_new(alloc, val, strlen(val), &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_json_object_set(obj, key, strlen(key), v, error);
}

static vigil_status_t jset_int(vigil_json_value_t *obj, const char *key, int64_t val, const vigil_allocator_t *alloc,
                               vigil_error_t *error)
{
    vigil_json_value_t *v = NULL;
    vigil_status_t s = vigil_json_number_new(alloc, (double)val, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_json_object_set(obj, key, strlen(key), v, error);
}

static vigil_status_t jset_bool(vigil_json_value_t *obj, const char *key, int val, const vigil_allocator_t *alloc,
                                vigil_error_t *error)
{
    vigil_json_value_t *v = NULL;
    vigil_status_t s = vigil_json_bool_new(alloc, val, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_json_object_set(obj, key, strlen(key), v, error);
}

static vigil_status_t jset_null(vigil_json_value_t *obj, const char *key, const vigil_allocator_t *alloc,
                                vigil_error_t *error)
{
    vigil_json_value_t *v = NULL;
    vigil_status_t s = vigil_json_null_new(alloc, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_json_object_set(obj, key, strlen(key), v, error);
}

static vigil_status_t jset_obj(vigil_json_value_t *obj, const char *key, vigil_json_value_t *val, vigil_error_t *error)
{
    return vigil_json_object_set(obj, key, strlen(key), val, error);
}

/* ── Response Builders ────────────────────────────────────── */

static vigil_status_t lsp_make_response(const vigil_allocator_t *alloc, const vigil_json_value_t *id,
                                        vigil_json_value_t *result, vigil_json_value_t **out, vigil_error_t *error)
{
    vigil_json_value_t *response = NULL;
    vigil_status_t s;

    s = vigil_json_object_new(alloc, &response, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    jset_str(response, "jsonrpc", "2.0", alloc, error);

    if (id != NULL)
    {
        if (vigil_json_type(id) == VIGIL_JSON_NUMBER)
        {
            jset_int(response, "id", (int64_t)vigil_json_number_value(id), alloc, error);
        }
        else
        {
            const char *str = vigil_json_string_value(id);
            if (str != NULL)
            {
                size_t len = vigil_json_string_length(id);
                vigil_json_value_t *id_copy = NULL;
                vigil_json_string_new(alloc, str, len, &id_copy, error);
                jset_obj(response, "id", id_copy, error);
            }
        }
    }

    if (result != NULL)
    {
        jset_obj(response, "result", result, error);
    }
    else
    {
        jset_null(response, "result", alloc, error);
    }

    *out = response;
    return VIGIL_STATUS_OK;
}

/* ── Semantic Token Support ───────────────────────────────── */

/*
 * Token type indices — must match the order advertised in handle_initialize.
 * Clients map these indices to colours via their active colour theme.
 */
#define VIGIL_STOKEN_TYPE_NAMESPACE 0u
#define VIGIL_STOKEN_TYPE_TYPE 1u
#define VIGIL_STOKEN_TYPE_CLASS 2u
#define VIGIL_STOKEN_TYPE_ENUM 3u
#define VIGIL_STOKEN_TYPE_INTERFACE 4u
#define VIGIL_STOKEN_TYPE_FUNCTION 5u
#define VIGIL_STOKEN_TYPE_VARIABLE 6u
#define VIGIL_STOKEN_TYPE_PROPERTY 7u
#define VIGIL_STOKEN_TYPE_ENUM_MEMBER 8u
#define VIGIL_STOKEN_TYPE_METHOD 9u
#define VIGIL_STOKEN_TYPE_PARAMETER 10u

/* Token modifier bitmasks — must match the order advertised in handle_initialize. */
#define VIGIL_STOKEN_MOD_DECLARATION 0x01u
#define VIGIL_STOKEN_MOD_DEFINITION 0x02u
#define VIGIL_STOKEN_MOD_READONLY 0x04u
#define VIGIL_STOKEN_MOD_STATIC 0x08u

typedef struct
{
    size_t start_offset;
    size_t length;
    uint32_t token_type;
    uint32_t token_modifiers;
} lsp_sem_token_t;

typedef struct
{
    lsp_sem_token_t *data;
    size_t count;
    size_t capacity;
    vigil_runtime_t *runtime;
} sem_token_list_t;

static int sem_token_cmp(const void *a, const void *b)
{
    const lsp_sem_token_t *ta = (const lsp_sem_token_t *)a;
    const lsp_sem_token_t *tb = (const lsp_sem_token_t *)b;
    if (ta->start_offset < tb->start_offset)
        return -1;
    if (ta->start_offset > tb->start_offset)
        return 1;
    return 0;
}

static void symbol_kind_to_sem_token(vigil_debug_symbol_kind_t kind, uint32_t *out_type, uint32_t *out_mods)
{
    switch (kind)
    {
    case VIGIL_DEBUG_SYMBOL_FUNCTION:
        *out_type = VIGIL_STOKEN_TYPE_FUNCTION;
        *out_mods = VIGIL_STOKEN_MOD_DECLARATION | VIGIL_STOKEN_MOD_DEFINITION;
        break;
    case VIGIL_DEBUG_SYMBOL_CLASS:
        *out_type = VIGIL_STOKEN_TYPE_CLASS;
        *out_mods = VIGIL_STOKEN_MOD_DECLARATION | VIGIL_STOKEN_MOD_DEFINITION;
        break;
    case VIGIL_DEBUG_SYMBOL_INTERFACE:
        *out_type = VIGIL_STOKEN_TYPE_INTERFACE;
        *out_mods = VIGIL_STOKEN_MOD_DECLARATION | VIGIL_STOKEN_MOD_DEFINITION;
        break;
    case VIGIL_DEBUG_SYMBOL_ENUM:
        *out_type = VIGIL_STOKEN_TYPE_ENUM;
        *out_mods = VIGIL_STOKEN_MOD_DECLARATION | VIGIL_STOKEN_MOD_DEFINITION;
        break;
    case VIGIL_DEBUG_SYMBOL_ENUM_MEMBER:
        *out_type = VIGIL_STOKEN_TYPE_ENUM_MEMBER;
        *out_mods = VIGIL_STOKEN_MOD_DECLARATION | VIGIL_STOKEN_MOD_DEFINITION | VIGIL_STOKEN_MOD_READONLY;
        break;
    case VIGIL_DEBUG_SYMBOL_FIELD:
        *out_type = VIGIL_STOKEN_TYPE_PROPERTY;
        *out_mods = VIGIL_STOKEN_MOD_DECLARATION;
        break;
    case VIGIL_DEBUG_SYMBOL_METHOD:
        *out_type = VIGIL_STOKEN_TYPE_METHOD;
        *out_mods = VIGIL_STOKEN_MOD_DECLARATION | VIGIL_STOKEN_MOD_DEFINITION;
        break;
    case VIGIL_DEBUG_SYMBOL_GLOBAL_CONST:
        *out_type = VIGIL_STOKEN_TYPE_VARIABLE;
        *out_mods = VIGIL_STOKEN_MOD_READONLY | VIGIL_STOKEN_MOD_STATIC;
        break;
    case VIGIL_DEBUG_SYMBOL_GLOBAL_VAR:
        *out_type = VIGIL_STOKEN_TYPE_VARIABLE;
        *out_mods = VIGIL_STOKEN_MOD_STATIC;
        break;
    default:
        *out_type = VIGIL_STOKEN_TYPE_VARIABLE;
        *out_mods = 0;
        break;
    }
}

/* Returns 1 on success, 0 on allocation failure. */
static int sem_token_list_push(sem_token_list_t *list, lsp_sem_token_t tok)
{
    if (list->count == list->capacity)
    {
        size_t new_cap = (list->capacity == 0) ? 64u : list->capacity * 2u;
        size_t new_size = new_cap * sizeof(*list->data);
        void *mem = list->data;
        vigil_status_t s;
        if (mem == NULL)
            s = vigil_runtime_alloc(list->runtime, new_size, &mem, NULL);
        else
            s = vigil_runtime_realloc(list->runtime, &mem, new_size, NULL);
        if (s != VIGIL_STATUS_OK)
            return 0;
        list->data = mem;
        list->capacity = new_cap;
    }
    list->data[list->count++] = tok;
    return 1;
}

static int collect_declaration_tokens(sem_token_list_t *list, const vigil_semantic_index_t *index,
                                      vigil_source_id_t source_id)
{
    size_t i, sym_count;

    sym_count = vigil_debug_symbol_table_count(&index->symbols);
    for (i = 0; i < sym_count; i++)
    {
        const vigil_debug_symbol_t *sym = vigil_debug_symbol_table_get(&index->symbols, i);
        lsp_sem_token_t tok;

        if (sym->span.source_id != source_id || sym->name_length == 0)
            continue;

        symbol_kind_to_sem_token(sym->kind, &tok.token_type, &tok.token_modifiers);
        tok.start_offset = sym->span.start_offset;
        tok.length = sym->name_length;
        if (!sem_token_list_push(list, tok))
            return 0;
    }
    return 1;
}

static int collect_reference_tokens(sem_token_list_t *list, const vigil_semantic_file_t *sem_file)
{
    size_t i;

    for (i = 0; i < sem_file->reference_count; i++)
    {
        const vigil_semantic_reference_t *ref = &sem_file->references[i];
        lsp_sem_token_t tok;

        if (ref->span.end_offset <= ref->span.start_offset)
            continue;

        symbol_kind_to_sem_token(ref->symbol_kind, &tok.token_type, &tok.token_modifiers);
        tok.token_modifiers = 0; /* references carry no declaration/definition flags */
        tok.start_offset = ref->span.start_offset;
        tok.length = ref->span.end_offset - ref->span.start_offset;
        if (!sem_token_list_push(list, tok))
            return 0;
    }
    return 1;
}

/* ── Request Handlers ─────────────────────────────────────── */

static vigil_status_t handle_initialize(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                        const vigil_json_value_t *params, vigil_json_value_t **out,
                                        vigil_error_t *error)
{
    vigil_json_value_t *result = NULL;
    vigil_json_value_t *capabilities = NULL;
    vigil_json_value_t *server_info = NULL;
    const vigil_allocator_t *a = &server->allocator;

    (void)params;
    server->initialized = 1;

    vigil_json_object_new(a, &result, error);
    vigil_json_object_new(a, &capabilities, error);
    vigil_json_object_new(a, &server_info, error);

    /* Text document sync: full document on open/change. */
    jset_int(capabilities, "textDocumentSync", 1, a, error);

    /* Document symbols (outline). */
    jset_bool(capabilities, "documentSymbolProvider", 1, a, error);

    /* Hover (type info). */
    jset_bool(capabilities, "hoverProvider", 1, a, error);

    /* Go to definition. */
    jset_bool(capabilities, "definitionProvider", 1, a, error);

    /* Completion. */
    jset_bool(capabilities, "completionProvider", 1, a, error);

    /* Find references. */
    jset_bool(capabilities, "referencesProvider", 1, a, error);

    /* Rename. */
    jset_bool(capabilities, "renameProvider", 1, a, error);

    /* Formatting. */
    jset_bool(capabilities, "documentFormattingProvider", 1, a, error);

    /* Signature help. */
    {
        vigil_json_value_t *sig_opts = NULL;
        vigil_json_value_t *trigger_chars = NULL;
        vigil_json_value_t *open_paren = NULL;
        vigil_json_value_t *comma = NULL;

        vigil_json_object_new(a, &sig_opts, error);
        vigil_json_array_new(a, &trigger_chars, error);
        vigil_json_string_new(a, "(", 1, &open_paren, error);
        vigil_json_string_new(a, ",", 1, &comma, error);

        vigil_json_array_push(trigger_chars, open_paren, error);
        vigil_json_array_push(trigger_chars, comma, error);
        jset_obj(sig_opts, "triggerCharacters", trigger_chars, error);
        jset_obj(capabilities, "signatureHelpProvider", sig_opts, error);
    }

    /* Semantic tokens. */
    {
        static const char *type_names[] = {"namespace", "type",     "class",      "enum",   "interface", "function",
                                           "variable",  "property", "enumMember", "method", "parameter"};
        static const char *mod_names[] = {"declaration", "definition", "readonly", "static"};
        vigil_json_value_t *st_opts = NULL;
        vigil_json_value_t *legend = NULL;
        vigil_json_value_t *token_types = NULL;
        vigil_json_value_t *token_modifiers = NULL;
        size_t j;

        vigil_json_object_new(a, &st_opts, error);
        vigil_json_object_new(a, &legend, error);
        vigil_json_array_new(a, &token_types, error);
        vigil_json_array_new(a, &token_modifiers, error);

        for (j = 0; j < sizeof(type_names) / sizeof(type_names[0]); j++)
        {
            vigil_json_value_t *s = NULL;
            vigil_json_string_new(a, type_names[j], strlen(type_names[j]), &s, error);
            vigil_json_array_push(token_types, s, error);
        }
        for (j = 0; j < sizeof(mod_names) / sizeof(mod_names[0]); j++)
        {
            vigil_json_value_t *s = NULL;
            vigil_json_string_new(a, mod_names[j], strlen(mod_names[j]), &s, error);
            vigil_json_array_push(token_modifiers, s, error);
        }

        jset_obj(legend, "tokenTypes", token_types, error);
        jset_obj(legend, "tokenModifiers", token_modifiers, error);
        jset_obj(st_opts, "legend", legend, error);
        jset_bool(st_opts, "full", 1, a, error);
        jset_obj(capabilities, "semanticTokensProvider", st_opts, error);
    }

    /* Server info. */
    jset_str(server_info, "name", "vigil-lsp", a, error);
    jset_str(server_info, "version", "0.1.0", a, error);

    jset_obj(result, "capabilities", capabilities, error);
    jset_obj(result, "serverInfo", server_info, error);

    return lsp_make_response(a, id, result, out, error);
}

static vigil_status_t handle_shutdown(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                      const vigil_json_value_t *params, vigil_json_value_t **out, vigil_error_t *error)
{
    (void)params;
    server->shutdown_requested = 1;
    return lsp_make_response(&server->allocator, id, NULL, out, error);
}

/* ── URI Lookup Helper ────────────────────────────────────── */

static vigil_source_id_t find_source_by_uri(vigil_lsp_server_t *server, const char *uri, size_t uri_len)
{
    size_t i;
    for (i = 0; i < server->index->file_count; i++)
    {
        const vigil_source_file_t *src =
            vigil_source_registry_get(&server->sources, server->index->files[i]->source_id);
        if (src != NULL && vigil_string_length(&src->path) == uri_len &&
            strncmp(vigil_string_c_str(&src->path), uri, uri_len) == 0)
        {
            return server->index->files[i]->source_id;
        }
    }
    return 0;
}

static const vigil_semantic_file_t *find_semantic_file(vigil_lsp_server_t *server, vigil_source_id_t source_id)
{
    size_t i;
    for (i = 0; i < server->index->file_count; i++)
    {
        if (server->index->files[i]->source_id == source_id)
        {
            return server->index->files[i];
        }
    }
    return NULL;
}

/* ── Notifications ────────────────────────────────────────── */

static void offset_to_line_col(const char *text, size_t text_len, size_t offset, size_t *line, size_t *col)
{
    size_t l = 0, c = 0, i;
    for (i = 0; i < text_len && i < offset; i++)
    {
        if (text[i] == '\n')
        {
            l++;
            c = 0;
        }
        else
        {
            c++;
        }
    }
    *line = l;
    *col = c;
}

static size_t line_col_to_offset(const char *text, size_t text_len, size_t line, size_t col)
{
    size_t l = 0, c = 0, i;
    for (i = 0; i < text_len; i++)
    {
        if (l == line && c == col)
            return i;
        if (text[i] == '\n')
        {
            l++;
            c = 0;
        }
        else
        {
            c++;
        }
    }
    return i;
}

static vigil_status_t encode_tokens_to_json(const sem_token_list_t *list, const vigil_source_file_t *src,
                                            const vigil_allocator_t *a, vigil_json_value_t *data_array,
                                            vigil_error_t *error)
{
    const char *text = vigil_string_c_str(&src->text);
    size_t text_len = vigil_string_length(&src->text);
    size_t prev_line = 0, prev_col = 0;
    size_t i;

    for (i = 0; i < list->count; i++)
    {
        size_t line, col, delta_line, delta_col;
        vigil_json_value_t *n = NULL;

        offset_to_line_col(text, text_len, list->data[i].start_offset, &line, &col);
        delta_line = line - prev_line;
        delta_col = (delta_line == 0) ? (col - prev_col) : col;

        /* Each token is 5 uint32 values: deltaLine, deltaStartChar, length, tokenType, tokenModifiers */
#define PUSH_UINT(v)                                                                                                   \
    vigil_json_number_new(a, (double)(v), &n, error);                                                                  \
    vigil_json_array_push(data_array, n, error);                                                                       \
    n = NULL

        PUSH_UINT(delta_line);
        PUSH_UINT(delta_col);
        PUSH_UINT(list->data[i].length);
        PUSH_UINT(list->data[i].token_type);
        PUSH_UINT(list->data[i].token_modifiers);

#undef PUSH_UINT

        prev_line = line;
        prev_col = col;
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t lsp_send_notification(vigil_lsp_server_t *server, const char *method, vigil_json_value_t *params,
                                            vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    vigil_json_value_t *msg = NULL;
    vigil_status_t st;

    st = vigil_json_object_new(a, &msg, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_json_free(&params);
        return st;
    }

    jset_str(msg, "jsonrpc", "2.0", a, error);
    jset_str(msg, "method", method, a, error);
    if (params != NULL)
    {
        jset_obj(msg, "params", params, error);
    }

    st = vigil_jsonrpc_write(&server->transport, msg, error);
    vigil_json_free(&msg);
    return st;
}

static vigil_status_t publish_diagnostics(vigil_lsp_server_t *server, const char *uri, size_t uri_len,
                                          vigil_source_id_t source_id, vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    vigil_json_value_t *params = NULL;
    vigil_json_value_t *diag_array = NULL;
    vigil_json_value_t *uri_val = NULL;
    const vigil_source_file_t *src;
    const vigil_semantic_file_t *sem_file = NULL;
    size_t i, count;

    /* Find semantic file for this source */
    for (i = 0; i < server->index->file_count; i++)
    {
        if (server->index->files[i]->source_id == source_id)
        {
            sem_file = server->index->files[i];
            break;
        }
    }

    vigil_json_object_new(a, &params, error);
    vigil_json_array_new(a, &diag_array, error);
    vigil_json_string_new(a, uri, uri_len, &uri_val, error);
    jset_obj(params, "uri", uri_val, error);

    if (sem_file != NULL)
    {
        src = vigil_source_registry_get(&server->sources, source_id);
        count = vigil_diagnostic_list_count(&sem_file->diagnostics);

        for (i = 0; i < count; i++)
        {
            const vigil_diagnostic_t *d = vigil_diagnostic_list_get(&sem_file->diagnostics, i);
            vigil_json_value_t *diag = NULL;
            vigil_json_value_t *range = NULL;
            vigil_json_value_t *start_pos = NULL;
            vigil_json_value_t *end_pos = NULL;
            size_t start_line, start_col, end_line, end_col;
            int severity;

            /* Map severity: LSP uses 1=Error, 2=Warning, 3=Info, 4=Hint */
            switch (d->severity)
            {
            case VIGIL_DIAGNOSTIC_ERROR:
                severity = 1;
                break;
            case VIGIL_DIAGNOSTIC_WARNING:
                severity = 2;
                break;
            default:
                severity = 3;
                break;
            }

            /* Convert offsets to line/column */
            offset_to_line_col(vigil_string_c_str(&src->text), vigil_string_length(&src->text), d->span.start_offset,
                               &start_line, &start_col);
            offset_to_line_col(vigil_string_c_str(&src->text), vigil_string_length(&src->text), d->span.end_offset,
                               &end_line, &end_col);

            vigil_json_object_new(a, &diag, error);
            vigil_json_object_new(a, &range, error);
            vigil_json_object_new(a, &start_pos, error);
            vigil_json_object_new(a, &end_pos, error);

            jset_int(start_pos, "line", (int64_t)start_line, a, error);
            jset_int(start_pos, "character", (int64_t)start_col, a, error);
            jset_int(end_pos, "line", (int64_t)end_line, a, error);
            jset_int(end_pos, "character", (int64_t)end_col, a, error);

            jset_obj(range, "start", start_pos, error);
            jset_obj(range, "end", end_pos, error);
            jset_obj(diag, "range", range, error);
            jset_int(diag, "severity", severity, a, error);
            jset_str(diag, "source", "vigil", a, error);
            {
                vigil_json_value_t *msg_val = NULL;
                vigil_json_string_new(a, vigil_string_c_str(&d->message), vigil_string_length(&d->message), &msg_val,
                                      error);
                jset_obj(diag, "message", msg_val, error);
            }

            vigil_json_array_push(diag_array, diag, error);
        }
    }

    jset_obj(params, "diagnostics", diag_array, error);
    return lsp_send_notification(server, "textDocument/publishDiagnostics", params, error);
}

static vigil_status_t handle_did_open(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                      const vigil_json_value_t *params, vigil_json_value_t **out, vigil_error_t *err)
{
    const vigil_json_value_t *text_doc;
    const vigil_json_value_t *uri_val;
    const vigil_json_value_t *text_val;
    const char *uri;
    size_t uri_len;
    const char *text;
    size_t text_len;
    vigil_source_id_t source_id;
    vigil_error_t local_err = {0};

    (void)id;
    (void)out;
    (void)err;

    text_doc = vigil_json_object_get(params, "textDocument");
    if (text_doc == NULL)
        return VIGIL_STATUS_OK;

    uri_val = vigil_json_object_get(text_doc, "uri");
    text_val = vigil_json_object_get(text_doc, "text");
    if (uri_val == NULL || text_val == NULL)
        return VIGIL_STATUS_OK;

    uri = vigil_json_string_value(uri_val);
    text = vigil_json_string_value(text_val);
    if (uri == NULL || text == NULL)
        return VIGIL_STATUS_OK;

    uri_len = vigil_json_string_length(uri_val);
    text_len = vigil_json_string_length(text_val);

    /* Register source and analyze. */
    if (vigil_source_registry_register(&server->sources, uri, uri_len, text, text_len, &source_id, &local_err) ==
        VIGIL_STATUS_OK)
    {
        vigil_semantic_index_analyze(server->index, source_id, &local_err);
        publish_diagnostics(server, uri, uri_len, source_id, &local_err);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t handle_did_change(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                        const vigil_json_value_t *params, vigil_json_value_t **out, vigil_error_t *err)
{
    const vigil_json_value_t *text_doc;
    const vigil_json_value_t *uri_val;
    const vigil_json_value_t *changes;
    const vigil_json_value_t *change;
    const vigil_json_value_t *text_val;
    const char *uri;
    size_t uri_len;
    const char *text;
    size_t text_len;
    vigil_source_id_t source_id;
    vigil_error_t local_err = {0};

    (void)id;
    (void)out;
    (void)err;

    text_doc = vigil_json_object_get(params, "textDocument");
    changes = vigil_json_object_get(params, "contentChanges");
    if (text_doc == NULL || changes == NULL)
        return VIGIL_STATUS_OK;

    uri_val = vigil_json_object_get(text_doc, "uri");
    if (uri_val == NULL)
        return VIGIL_STATUS_OK;

    /* Full sync: take the last change's text */
    if (vigil_json_array_count(changes) == 0)
        return VIGIL_STATUS_OK;
    change = vigil_json_array_get(changes, vigil_json_array_count(changes) - 1);
    if (change == NULL)
        return VIGIL_STATUS_OK;

    text_val = vigil_json_object_get(change, "text");
    if (text_val == NULL)
        return VIGIL_STATUS_OK;

    uri = vigil_json_string_value(uri_val);
    text = vigil_json_string_value(text_val);
    if (uri == NULL || text == NULL)
        return VIGIL_STATUS_OK;

    uri_len = vigil_json_string_length(uri_val);
    text_len = vigil_json_string_length(text_val);

    /* Re-register and re-analyze */
    if (vigil_source_registry_register(&server->sources, uri, uri_len, text, text_len, &source_id, &local_err) ==
        VIGIL_STATUS_OK)
    {
        vigil_semantic_index_analyze(server->index, source_id, &local_err);
        publish_diagnostics(server, uri, uri_len, source_id, &local_err);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t handle_did_close(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                       const vigil_json_value_t *params, vigil_json_value_t **out, vigil_error_t *error)
{
    (void)server;
    (void)id;
    (void)params;
    (void)out;
    (void)error;
    /* No cleanup needed - source registry doesn't support removal yet */
    return VIGIL_STATUS_OK;
}

static vigil_status_t handle_references(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                        const vigil_json_value_t *params, vigil_json_value_t **out,
                                        vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    const vigil_json_value_t *text_doc;
    const vigil_json_value_t *position;
    const vigil_json_value_t *uri_val;
    const vigil_json_value_t *line_val;
    const vigil_json_value_t *char_val;
    const vigil_source_file_t *src;
    vigil_semantic_reference_t *refs = NULL;
    size_t ref_count = 0;
    vigil_json_value_t *result = NULL;
    vigil_source_id_t source_id;
    size_t line, col, offset, i;

    text_doc = vigil_json_object_get(params, "textDocument");
    position = vigil_json_object_get(params, "position");
    if (text_doc == NULL || position == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = vigil_json_object_get(text_doc, "uri");
    line_val = vigil_json_object_get(position, "line");
    char_val = vigil_json_object_get(position, "character");
    if (uri_val == NULL || line_val == NULL || char_val == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, vigil_json_string_value(uri_val), vigil_json_string_length(uri_val));
    if (source_id == 0)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = vigil_source_registry_get(&server->sources, source_id);
    line = (size_t)vigil_json_number_value(line_val);
    col = (size_t)vigil_json_number_value(char_val);
    offset = line_col_to_offset(vigil_string_c_str(&src->text), vigil_string_length(&src->text), line, col);

    vigil_json_array_new(a, &result, error);

    if (vigil_semantic_index_references_at(server->index, source_id, offset, &refs, &ref_count, error) ==
            VIGIL_STATUS_OK &&
        refs != NULL)
    {
        for (i = 0; i < ref_count; i++)
        {
            vigil_json_value_t *loc = NULL;
            vigil_json_value_t *range = NULL;
            vigil_json_value_t *start_pos = NULL;
            vigil_json_value_t *end_pos = NULL;
            const vigil_source_file_t *ref_src;
            size_t start_line, start_col, end_line, end_col;

            ref_src = vigil_source_registry_get(&server->sources, refs[i].span.source_id);
            if (ref_src == NULL)
                continue;

            offset_to_line_col(vigil_string_c_str(&ref_src->text), vigil_string_length(&ref_src->text),
                               refs[i].span.start_offset, &start_line, &start_col);
            offset_to_line_col(vigil_string_c_str(&ref_src->text), vigil_string_length(&ref_src->text),
                               refs[i].span.end_offset, &end_line, &end_col);

            vigil_json_object_new(a, &loc, error);
            vigil_json_object_new(a, &range, error);
            vigil_json_object_new(a, &start_pos, error);
            vigil_json_object_new(a, &end_pos, error);

            {
                vigil_json_value_t *uri_out = NULL;
                vigil_json_string_new(a, vigil_string_c_str(&ref_src->path), vigil_string_length(&ref_src->path),
                                      &uri_out, error);
                jset_obj(loc, "uri", uri_out, error);
            }

            jset_int(start_pos, "line", (int64_t)start_line, a, error);
            jset_int(start_pos, "character", (int64_t)start_col, a, error);
            jset_int(end_pos, "line", (int64_t)end_line, a, error);
            jset_int(end_pos, "character", (int64_t)end_col, a, error);

            jset_obj(range, "start", start_pos, error);
            jset_obj(range, "end", end_pos, error);
            jset_obj(loc, "range", range, error);

            vigil_json_array_push(result, loc, error);
        }
        {
            void *tmp = refs;
            vigil_runtime_free(server->runtime, &tmp);
        }
    }

    return lsp_make_response(a, id, result, out, error);
}

static vigil_status_t handle_formatting(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                        const vigil_json_value_t *params, vigil_json_value_t **out,
                                        vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    const vigil_json_value_t *text_doc;
    const vigil_json_value_t *uri_val;
    const vigil_source_file_t *src;
    vigil_json_value_t *result = NULL;
    vigil_token_list_t tokens = {0};
    char *formatted = NULL;
    size_t formatted_len = 0;
    vigil_source_id_t source_id;

    text_doc = vigil_json_object_get(params, "textDocument");
    if (text_doc == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = vigil_json_object_get(text_doc, "uri");
    if (uri_val == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, vigil_json_string_value(uri_val), vigil_json_string_length(uri_val));
    if (source_id == 0)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = vigil_source_registry_get(&server->sources, source_id);

    /* Lex and format */
    vigil_token_list_init(&tokens, server->runtime);
    if (vigil_lex_source(&server->sources, source_id, &tokens, NULL, error) != VIGIL_STATUS_OK)
    {
        vigil_token_list_free(&tokens);
        return lsp_make_response(a, id, NULL, out, error);
    }

    if (vigil_fmt(vigil_string_c_str(&src->text), vigil_string_length(&src->text), &tokens, &formatted, &formatted_len,
                  error) != VIGIL_STATUS_OK)
    {
        vigil_token_list_free(&tokens);
        return lsp_make_response(a, id, NULL, out, error);
    }

    vigil_token_list_free(&tokens);

    /* Build single edit replacing entire document */
    {
        vigil_json_value_t *edit = NULL;
        vigil_json_value_t *range = NULL;
        vigil_json_value_t *start_pos = NULL;
        vigil_json_value_t *end_pos = NULL;
        vigil_json_value_t *new_text = NULL;
        size_t end_line, end_col;

        offset_to_line_col(vigil_string_c_str(&src->text), vigil_string_length(&src->text),
                           vigil_string_length(&src->text), &end_line, &end_col);

        vigil_json_array_new(a, &result, error);
        vigil_json_object_new(a, &edit, error);
        vigil_json_object_new(a, &range, error);
        vigil_json_object_new(a, &start_pos, error);
        vigil_json_object_new(a, &end_pos, error);
        vigil_json_string_new(a, formatted, formatted_len, &new_text, error);

        jset_int(start_pos, "line", 0, a, error);
        jset_int(start_pos, "character", 0, a, error);
        jset_int(end_pos, "line", (int64_t)end_line, a, error);
        jset_int(end_pos, "character", (int64_t)end_col, a, error);

        jset_obj(range, "start", start_pos, error);
        jset_obj(range, "end", end_pos, error);
        jset_obj(edit, "range", range, error);
        jset_obj(edit, "newText", new_text, error);

        vigil_json_array_push(result, edit, error);
    }

    free(formatted);
    return lsp_make_response(a, id, result, out, error);
}

static vigil_status_t handle_signature_help(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                            const vigil_json_value_t *params, vigil_json_value_t **out,
                                            vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    const vigil_json_value_t *text_doc;
    const vigil_json_value_t *position;
    const vigil_json_value_t *uri_val;
    const vigil_json_value_t *line_val;
    const vigil_json_value_t *char_val;
    const vigil_source_file_t *src;
    const vigil_semantic_file_t *sem_file;
    const vigil_semantic_node_t *node;
    vigil_source_id_t source_id;
    size_t line, col, offset;

    text_doc = vigil_json_object_get(params, "textDocument");
    position = vigil_json_object_get(params, "position");
    if (text_doc == NULL || position == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = vigil_json_object_get(text_doc, "uri");
    line_val = vigil_json_object_get(position, "line");
    char_val = vigil_json_object_get(position, "character");
    if (uri_val == NULL || line_val == NULL || char_val == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, vigil_json_string_value(uri_val), vigil_json_string_length(uri_val));
    if (source_id == 0)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    sem_file = find_semantic_file(server, source_id);
    if (sem_file == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = vigil_source_registry_get(&server->sources, source_id);
    line = (size_t)vigil_json_number_value(line_val);
    col = (size_t)vigil_json_number_value(char_val);
    offset = line_col_to_offset(vigil_string_c_str(&src->text), vigil_string_length(&src->text), line, col);

    /* Find call expression containing this position */
    node = vigil_semantic_file_node_at(sem_file, offset);
    if (node != NULL && node->kind != VIGIL_NODE_CALL_EXPR && node->kind != VIGIL_NODE_METHOD_CALL_EXPR)
    {
        /* Walk up - for now just check nodes at position */
        node = NULL;
    }

    if (node != NULL && (node->kind == VIGIL_NODE_CALL_EXPR || node->kind == VIGIL_NODE_METHOD_CALL_EXPR))
    {
        vigil_json_value_t *result = NULL;
        vigil_json_value_t *signatures = NULL;
        vigil_json_value_t *sig = NULL;
        vigil_json_value_t *label = NULL;
        const char *func_name = NULL;
        size_t func_name_len = 0;

        if (node->kind == VIGIL_NODE_METHOD_CALL_EXPR)
        {
            func_name = node->data.method_call.method_name;
            func_name_len = node->data.method_call.method_name_length;
        }
        else if (node->data.call.callee != NULL && node->data.call.callee->kind == VIGIL_NODE_IDENTIFIER_EXPR)
        {
            func_name = node->data.call.callee->data.identifier.name;
            func_name_len = node->data.call.callee->data.identifier.name_length;
        }

        if (func_name != NULL)
        {
            char sig_label[256];
            int len = snprintf(sig_label, sizeof(sig_label), "%.*s(...)", (int)func_name_len, func_name);

            vigil_json_object_new(a, &result, error);
            vigil_json_array_new(a, &signatures, error);
            vigil_json_object_new(a, &sig, error);
            vigil_json_string_new(a, sig_label, (size_t)len, &label, error);

            jset_obj(sig, "label", label, error);
            vigil_json_array_push(signatures, sig, error);

            jset_obj(result, "signatures", signatures, error);
            jset_int(result, "activeSignature", 0, a, error);
            jset_int(result, "activeParameter", 0, a, error);

            return lsp_make_response(a, id, result, out, error);
        }
    }

    return lsp_make_response(a, id, NULL, out, error);
}

static vigil_status_t handle_rename(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                    const vigil_json_value_t *params, vigil_json_value_t **out, vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    const vigil_json_value_t *text_doc;
    const vigil_json_value_t *position;
    const vigil_json_value_t *new_name_val;
    const vigil_json_value_t *uri_val;
    const vigil_json_value_t *line_val;
    const vigil_json_value_t *char_val;
    const vigil_source_file_t *src;
    vigil_semantic_reference_t *refs = NULL;
    size_t ref_count = 0;
    vigil_json_value_t *result = NULL;
    vigil_json_value_t *changes = NULL;
    const char *new_name;
    size_t new_name_len;
    vigil_source_id_t source_id;
    size_t line, col, offset, i;

    text_doc = vigil_json_object_get(params, "textDocument");
    position = vigil_json_object_get(params, "position");
    new_name_val = vigil_json_object_get(params, "newName");
    if (text_doc == NULL || position == NULL || new_name_val == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = vigil_json_object_get(text_doc, "uri");
    line_val = vigil_json_object_get(position, "line");
    char_val = vigil_json_object_get(position, "character");
    new_name = vigil_json_string_value(new_name_val);
    new_name_len = vigil_json_string_length(new_name_val);

    if (uri_val == NULL || line_val == NULL || char_val == NULL || new_name == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, vigil_json_string_value(uri_val), vigil_json_string_length(uri_val));
    if (source_id == 0)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = vigil_source_registry_get(&server->sources, source_id);
    line = (size_t)vigil_json_number_value(line_val);
    col = (size_t)vigil_json_number_value(char_val);
    offset = line_col_to_offset(vigil_string_c_str(&src->text), vigil_string_length(&src->text), line, col);

    vigil_json_object_new(a, &result, error);
    vigil_json_object_new(a, &changes, error);

    if (vigil_semantic_index_references_at(server->index, source_id, offset, &refs, &ref_count, error) ==
            VIGIL_STATUS_OK &&
        refs != NULL)
    {
        for (i = 0; i < ref_count; i++)
        {
            const vigil_source_file_t *ref_src;
            vigil_json_value_t *edits_array = NULL;
            vigil_json_value_t *edit = NULL;
            vigil_json_value_t *range = NULL;
            vigil_json_value_t *start_pos = NULL;
            vigil_json_value_t *end_pos = NULL;
            vigil_json_value_t *new_text = NULL;
            size_t start_line, start_col, end_line, end_col;
            const char *ref_uri;
            size_t ref_uri_len;

            ref_src = vigil_source_registry_get(&server->sources, refs[i].span.source_id);
            if (ref_src == NULL)
                continue;

            ref_uri = vigil_string_c_str(&ref_src->path);
            ref_uri_len = vigil_string_length(&ref_src->path);

            /* Get or create edits array for this URI */
            {
                const vigil_json_value_t *existing = vigil_json_object_get(changes, ref_uri);
                if (existing == NULL)
                {
                    vigil_json_array_new(a, &edits_array, error);
                    vigil_json_object_set(changes, ref_uri, ref_uri_len, edits_array, error);
                }
                else
                {
                    edits_array = (vigil_json_value_t *)existing; /* Safe: we own changes */
                }
            }

            offset_to_line_col(vigil_string_c_str(&ref_src->text), vigil_string_length(&ref_src->text),
                               refs[i].span.start_offset, &start_line, &start_col);
            offset_to_line_col(vigil_string_c_str(&ref_src->text), vigil_string_length(&ref_src->text),
                               refs[i].span.end_offset, &end_line, &end_col);

            vigil_json_object_new(a, &edit, error);
            vigil_json_object_new(a, &range, error);
            vigil_json_object_new(a, &start_pos, error);
            vigil_json_object_new(a, &end_pos, error);
            vigil_json_string_new(a, new_name, new_name_len, &new_text, error);

            jset_int(start_pos, "line", (int64_t)start_line, a, error);
            jset_int(start_pos, "character", (int64_t)start_col, a, error);
            jset_int(end_pos, "line", (int64_t)end_line, a, error);
            jset_int(end_pos, "character", (int64_t)end_col, a, error);

            jset_obj(range, "start", start_pos, error);
            jset_obj(range, "end", end_pos, error);
            jset_obj(edit, "range", range, error);
            jset_obj(edit, "newText", new_text, error);

            vigil_json_array_push(edits_array, edit, error);
        }
        {
            void *tmp = refs;
            vigil_runtime_free(server->runtime, &tmp);
        }
    }

    jset_obj(result, "changes", changes, error);
    return lsp_make_response(a, id, result, out, error);
}

/* String method completions for LSP */
static const struct
{
    const char *name;
    const char *detail;
    const char *doc;
} string_method_completions[] = {
    {"len", "() -> i32", "Return the length of the string"},
    {"contains", "(sub: string) -> bool", "Check if string contains substring"},
    {"starts_with", "(prefix: string) -> bool", "Check if string starts with prefix"},
    {"ends_with", "(suffix: string) -> bool", "Check if string ends with suffix"},
    {"trim", "() -> string", "Remove leading/trailing whitespace"},
    {"trim_left", "() -> string", "Remove leading whitespace"},
    {"trim_right", "() -> string", "Remove trailing whitespace"},
    {"trim_prefix", "(prefix: string) -> string", "Remove prefix if present"},
    {"trim_suffix", "(suffix: string) -> string", "Remove suffix if present"},
    {"to_upper", "() -> string", "Convert to uppercase"},
    {"to_lower", "() -> string", "Convert to lowercase"},
    {"replace", "(old: string, new: string) -> string", "Replace all occurrences"},
    {"split", "(sep: string) -> array<string>", "Split by separator"},
    {"index_of", "(sub: string) -> (i32, bool)", "Find first occurrence"},
    {"last_index_of", "(sub: string) -> (i32, bool)", "Find last occurrence"},
    {"substr", "(start: i32, len: i32) -> (string, err)", "Extract substring"},
    {"char_at", "(i: i32) -> (string, err)", "Get character at index"},
    {"bytes", "() -> array<u8>", "Get raw bytes"},
    {"reverse", "() -> string", "Reverse the string"},
    {"is_empty", "() -> bool", "Check if empty"},
    {"char_count", "() -> i32", "Count Unicode code points"},
    {"repeat", "(n: i32) -> string", "Repeat n times"},
    {"count", "(sub: string) -> i32", "Count occurrences"},
    {"fields", "() -> array<string>", "Split on whitespace"},
    {"join", "(arr: array<string>) -> string", "Join array with separator"},
    {"cut", "(sep: string) -> (string, string, bool)", "Cut around first separator"},
    {"equal_fold", "(t: string) -> bool", "Case-insensitive comparison"},
};

#define STRING_METHOD_COUNT (sizeof(string_method_completions) / sizeof(string_method_completions[0]))

static vigil_status_t handle_completion(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                        const vigil_json_value_t *params, vigil_json_value_t **out,
                                        vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    vigil_json_value_t *result = NULL;
    size_t i, count;

    (void)params; /* Position not used yet - return all symbols */

    vigil_json_array_new(a, &result, error);

    count = vigil_debug_symbol_table_count(&server->index->symbols);
    for (i = 0; i < count; i++)
    {
        const vigil_debug_symbol_t *sym = vigil_debug_symbol_table_get(&server->index->symbols, i);
        vigil_json_value_t *item = NULL;
        vigil_json_value_t *label = NULL;
        int kind;

        /* Map to LSP CompletionItemKind */
        switch (sym->kind)
        {
        case VIGIL_DEBUG_SYMBOL_FUNCTION:
            kind = 3;
            break; /* Function */
        case VIGIL_DEBUG_SYMBOL_CLASS:
            kind = 7;
            break; /* Class */
        case VIGIL_DEBUG_SYMBOL_INTERFACE:
            kind = 8;
            break; /* Interface */
        case VIGIL_DEBUG_SYMBOL_ENUM:
            kind = 13;
            break; /* Enum */
        case VIGIL_DEBUG_SYMBOL_FIELD:
            kind = 5;
            break; /* Field */
        case VIGIL_DEBUG_SYMBOL_METHOD:
            kind = 2;
            break; /* Method */
        case VIGIL_DEBUG_SYMBOL_GLOBAL_CONST:
            kind = 21;
            break; /* Constant */
        case VIGIL_DEBUG_SYMBOL_GLOBAL_VAR:
            kind = 6;
            break; /* Variable */
        default:
            kind = 1;
            break; /* Text */
        }

        vigil_json_object_new(a, &item, error);
        vigil_json_string_new(a, sym->name, sym->name_length, &label, error);
        jset_obj(item, "label", label, error);
        jset_int(item, "kind", kind, a, error);

        vigil_json_array_push(result, item, error);
    }

    /* Add string method completions */
    for (i = 0; i < STRING_METHOD_COUNT; i++)
    {
        vigil_json_value_t *item = NULL;
        vigil_json_value_t *label = NULL;
        vigil_json_value_t *detail = NULL;
        vigil_json_value_t *doc = NULL;

        vigil_json_object_new(a, &item, error);
        vigil_json_string_new(a, string_method_completions[i].name, strlen(string_method_completions[i].name), &label,
                              error);
        vigil_json_string_new(a, string_method_completions[i].detail, strlen(string_method_completions[i].detail),
                              &detail, error);
        vigil_json_string_new(a, string_method_completions[i].doc, strlen(string_method_completions[i].doc), &doc,
                              error);

        jset_obj(item, "label", label, error);
        jset_obj(item, "detail", detail, error);
        jset_int(item, "kind", 2, a, error); /* Method */
        jset_obj(item, "documentation", doc, error);

        vigil_json_array_push(result, item, error);
    }

    /* Add native module function completions */
    {
        vigil_native_registry_t natives;
        vigil_native_registry_init(&natives);
        if (vigil_stdlib_register_all(&natives, error) == VIGIL_STATUS_OK)
        {
            size_t mi;
            for (mi = 0; mi < natives.module_count; mi++)
            {
                const vigil_native_module_t *mod = natives.modules[mi];
                size_t fi;
                for (fi = 0; fi < mod->function_count; fi++)
                {
                    const vigil_native_module_function_t *fn = &mod->functions[fi];
                    vigil_json_value_t *item = NULL;
                    vigil_json_value_t *label = NULL;
                    vigil_json_value_t *detail = NULL;

                    /* Build qualified label: "module.function" */
                    char qualified[256];
                    snprintf(qualified, sizeof(qualified), "%.*s.%.*s", (int)mod->name_length, mod->name,
                             (int)fn->name_length, fn->name);

                    vigil_json_object_new(a, &item, error);
                    vigil_json_string_new(a, qualified, strlen(qualified), &label, error);
                    jset_obj(item, "label", label, error);
                    jset_int(item, "kind", 3, a, error); /* Function */

                    /* Build detail string with module name */
                    char det[128];
                    snprintf(det, sizeof(det), "%.*s module", (int)mod->name_length, mod->name);
                    vigil_json_string_new(a, det, strlen(det), &detail, error);
                    jset_obj(item, "detail", detail, error);

                    vigil_json_array_push(result, item, error);
                }
            }
        }
        vigil_native_registry_free(&natives);
    }

    return lsp_make_response(a, id, result, out, error);
}

static vigil_status_t handle_definition(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                        const vigil_json_value_t *params, vigil_json_value_t **out,
                                        vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    const vigil_json_value_t *text_doc;
    const vigil_json_value_t *position;
    const vigil_json_value_t *uri_val;
    const vigil_json_value_t *line_val;
    const vigil_json_value_t *char_val;
    const vigil_source_file_t *src;
    vigil_source_span_t def_span;
    vigil_source_id_t source_id;
    size_t line, col, offset;

    text_doc = vigil_json_object_get(params, "textDocument");
    position = vigil_json_object_get(params, "position");
    if (text_doc == NULL || position == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = vigil_json_object_get(text_doc, "uri");
    line_val = vigil_json_object_get(position, "line");
    char_val = vigil_json_object_get(position, "character");
    if (uri_val == NULL || line_val == NULL || char_val == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, vigil_json_string_value(uri_val), vigil_json_string_length(uri_val));
    if (source_id == 0)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = vigil_source_registry_get(&server->sources, source_id);
    line = (size_t)vigil_json_number_value(line_val);
    col = (size_t)vigil_json_number_value(char_val);
    offset = line_col_to_offset(vigil_string_c_str(&src->text), vigil_string_length(&src->text), line, col);

    if (vigil_semantic_index_definition_at(server->index, source_id, offset, &def_span, error) == VIGIL_STATUS_OK)
    {
        vigil_json_value_t *result = NULL;
        vigil_json_value_t *range = NULL;
        vigil_json_value_t *start_pos = NULL;
        vigil_json_value_t *end_pos = NULL;
        const vigil_source_file_t *def_src;
        size_t start_line, start_col, end_line, end_col;

        def_src = vigil_source_registry_get(&server->sources, def_span.source_id);
        if (def_src == NULL)
        {
            return lsp_make_response(a, id, NULL, out, error);
        }

        offset_to_line_col(vigil_string_c_str(&def_src->text), vigil_string_length(&def_src->text),
                           def_span.start_offset, &start_line, &start_col);
        offset_to_line_col(vigil_string_c_str(&def_src->text), vigil_string_length(&def_src->text), def_span.end_offset,
                           &end_line, &end_col);

        vigil_json_object_new(a, &result, error);
        vigil_json_object_new(a, &range, error);
        vigil_json_object_new(a, &start_pos, error);
        vigil_json_object_new(a, &end_pos, error);

        {
            vigil_json_value_t *uri_out = NULL;
            vigil_json_string_new(a, vigil_string_c_str(&def_src->path), vigil_string_length(&def_src->path), &uri_out,
                                  error);
            jset_obj(result, "uri", uri_out, error);
        }

        jset_int(start_pos, "line", (int64_t)start_line, a, error);
        jset_int(start_pos, "character", (int64_t)start_col, a, error);
        jset_int(end_pos, "line", (int64_t)end_line, a, error);
        jset_int(end_pos, "character", (int64_t)end_col, a, error);

        jset_obj(range, "start", start_pos, error);
        jset_obj(range, "end", end_pos, error);
        jset_obj(result, "range", range, error);

        return lsp_make_response(a, id, result, out, error);
    }

    return lsp_make_response(a, id, NULL, out, error);
}

static vigil_status_t handle_hover(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                   const vigil_json_value_t *params, vigil_json_value_t **out, vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    const vigil_json_value_t *text_doc;
    const vigil_json_value_t *position;
    const vigil_json_value_t *uri_val;
    const vigil_json_value_t *line_val;
    const vigil_json_value_t *char_val;
    const vigil_source_file_t *src;
    const vigil_semantic_file_t *sem_file;
    const vigil_semantic_node_t *node;
    vigil_semantic_type_t type;
    vigil_source_id_t source_id;
    char *hover_text = NULL;
    size_t line, col, offset;

    text_doc = vigil_json_object_get(params, "textDocument");
    position = vigil_json_object_get(params, "position");
    if (text_doc == NULL || position == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = vigil_json_object_get(text_doc, "uri");
    line_val = vigil_json_object_get(position, "line");
    char_val = vigil_json_object_get(position, "character");
    if (uri_val == NULL || line_val == NULL || char_val == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, vigil_json_string_value(uri_val), vigil_json_string_length(uri_val));
    if (source_id == 0)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    sem_file = find_semantic_file(server, source_id);
    if (sem_file == NULL)
    {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = vigil_source_registry_get(&server->sources, source_id);
    line = (size_t)vigil_json_number_value(line_val);
    col = (size_t)vigil_json_number_value(char_val);
    offset = line_col_to_offset(vigil_string_c_str(&src->text), vigil_string_length(&src->text), line, col);

    /* Try to get node at position for doc lookup */
    node = vigil_semantic_file_node_at(sem_file, offset);
    if (node != NULL)
    {
        const char *name = NULL;
        if (node->kind == VIGIL_NODE_IDENTIFIER_EXPR)
        {
            name = node->data.identifier.name;
        }
        else if (node->kind == VIGIL_NODE_CALL_EXPR && node->data.call.callee != NULL &&
                 node->data.call.callee->kind == VIGIL_NODE_IDENTIFIER_EXPR)
        {
            name = node->data.call.callee->data.identifier.name;
        }
        if (name != NULL)
        {
            const vigil_doc_entry_t *doc = vigil_doc_lookup(name);
            if (doc != NULL)
            {
                vigil_doc_entry_render(doc, &hover_text, NULL, error);
            }
        }
    }

    /* Fall back to type info if no doc */
    if (hover_text == NULL)
    {
        type = vigil_semantic_file_type_at(sem_file, offset);
        if (vigil_semantic_type_is_valid(type))
        {
            hover_text = vigil_semantic_type_to_string(server->index, type);
        }
    }

    if (hover_text != NULL)
    {
        vigil_json_value_t *result = NULL;
        vigil_json_value_t *contents = NULL;

        vigil_json_object_new(a, &result, error);
        vigil_json_object_new(a, &contents, error);

        jset_str(contents, "kind", "plaintext", a, error);
        jset_str(contents, "value", hover_text, a, error);
        jset_obj(result, "contents", contents, error);

        {
            void *tmp = hover_text;
            vigil_runtime_free(server->runtime, &tmp);
        }
        return lsp_make_response(a, id, result, out, error);
    }

    return lsp_make_response(a, id, NULL, out, error);
}

/* Forward declaration — encode_tokens_to_json is defined after offset_to_line_col in the notifications section. */
static vigil_status_t encode_tokens_to_json(const sem_token_list_t *list, const vigil_source_file_t *src,
                                            const vigil_allocator_t *a, vigil_json_value_t *data_array,
                                            vigil_error_t *error);

static vigil_status_t handle_semantic_tokens_full(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                                  const vigil_json_value_t *params, vigil_json_value_t **out,
                                                  vigil_error_t *error)
{
    const vigil_allocator_t *a = &server->allocator;
    const vigil_json_value_t *td, *uri_val;
    const char *uri;
    size_t uri_len;
    vigil_source_id_t source_id;
    const vigil_source_file_t *src;
    const vigil_semantic_file_t *sem_file;
    sem_token_list_t list = {NULL, 0, 0, server->runtime};
    vigil_json_value_t *result = NULL;
    vigil_json_value_t *data_array = NULL;

    /* Parse URI and resolve to a tracked source file. */
    td = vigil_json_object_get(params, "textDocument");
    uri_val = (td != NULL) ? vigil_json_object_get(td, "uri") : NULL;
    uri = (uri_val != NULL) ? vigil_json_string_value(uri_val) : NULL;
    if (uri == NULL)
        goto empty;
    uri_len = vigil_json_string_length(uri_val);
    source_id = find_source_by_uri(server, uri, uri_len);
    src = (source_id != 0) ? vigil_source_registry_get(&server->sources, source_id) : NULL;
    sem_file = (src != NULL) ? find_semantic_file(server, source_id) : NULL;
    if (sem_file == NULL)
        goto empty;

    /* Gather tokens; bail out on allocation failure. */
    if (!collect_declaration_tokens(&list, server->index, source_id))
        goto empty;
    if (!collect_reference_tokens(&list, sem_file))
        goto empty;

    if (list.count > 1)
        qsort(list.data, list.count, sizeof(*list.data), sem_token_cmp);

    vigil_json_object_new(a, &result, error);
    vigil_json_array_new(a, &data_array, error);
    encode_tokens_to_json(&list, src, a, data_array, error);
    {
        void *tmp = list.data;
        vigil_runtime_free(server->runtime, &tmp);
    }
    jset_obj(result, "data", data_array, error);
    return lsp_make_response(a, id, result, out, error);

empty: {
    void *tmp = list.data;
    vigil_runtime_free(server->runtime, &tmp);
}
    vigil_json_object_new(a, &result, error);
    vigil_json_array_new(a, &data_array, error);
    jset_obj(result, "data", data_array, error);
    return lsp_make_response(a, id, result, out, error);
}

static vigil_status_t handle_document_symbol(vigil_lsp_server_t *server, const vigil_json_value_t *id,
                                             const vigil_json_value_t *params, vigil_json_value_t **out,
                                             vigil_error_t *error)
{
    vigil_json_value_t *result = NULL;
    const vigil_allocator_t *a = &server->allocator;
    size_t i, count;

    (void)params;

    vigil_json_array_new(a, &result, error);

    /* Return symbols from index. */
    count = vigil_debug_symbol_table_count(&server->index->symbols);
    for (i = 0; i < count; i++)
    {
        const vigil_debug_symbol_t *sym = vigil_debug_symbol_table_get(&server->index->symbols, i);
        vigil_json_value_t *symbol_info = NULL;
        vigil_json_value_t *location = NULL;
        vigil_json_value_t *range = NULL;
        vigil_json_value_t *start_pos = NULL;
        vigil_json_value_t *end_pos = NULL;
        int kind;

        /* Map symbol kind to LSP SymbolKind. */
        switch (sym->kind)
        {
        case VIGIL_DEBUG_SYMBOL_FUNCTION:
            kind = 12;
            break;
        case VIGIL_DEBUG_SYMBOL_CLASS:
            kind = 5;
            break;
        case VIGIL_DEBUG_SYMBOL_INTERFACE:
            kind = 11;
            break;
        case VIGIL_DEBUG_SYMBOL_ENUM:
            kind = 10;
            break;
        case VIGIL_DEBUG_SYMBOL_FIELD:
            kind = 8;
            break;
        case VIGIL_DEBUG_SYMBOL_METHOD:
            kind = 6;
            break;
        case VIGIL_DEBUG_SYMBOL_GLOBAL_CONST:
            kind = 14;
            break;
        case VIGIL_DEBUG_SYMBOL_GLOBAL_VAR:
            kind = 13;
            break;
        default:
            kind = 1;
            break;
        }

        vigil_json_object_new(a, &symbol_info, error);
        vigil_json_object_new(a, &location, error);
        vigil_json_object_new(a, &range, error);
        vigil_json_object_new(a, &start_pos, error);
        vigil_json_object_new(a, &end_pos, error);

        {
            vigil_json_value_t *name_val = NULL;
            vigil_json_string_new(a, sym->name, sym->name_length, &name_val, error);
            jset_obj(symbol_info, "name", name_val, error);
        }
        jset_int(symbol_info, "kind", kind, a, error);

        jset_int(start_pos, "line", 0, a, error);
        jset_int(start_pos, "character", (int64_t)sym->span.start_offset, a, error);
        jset_int(end_pos, "line", 0, a, error);
        jset_int(end_pos, "character", (int64_t)sym->span.end_offset, a, error);

        jset_obj(range, "start", start_pos, error);
        jset_obj(range, "end", end_pos, error);
        jset_str(location, "uri", "", a, error);
        jset_obj(location, "range", range, error);
        jset_obj(symbol_info, "location", location, error);

        vigil_json_array_push(result, symbol_info, error);
    }

    return lsp_make_response(a, id, result, out, error);
}

/* ── Message Dispatch ─────────────────────────────────────── */

/*
 * All request and notification handlers share the same signature so they
 * can be stored in a flat dispatch table, keeping lsp_handle_message simple.
 */
typedef vigil_status_t (*lsp_handler_fn)(vigil_lsp_server_t *, const vigil_json_value_t *, const vigil_json_value_t *,
                                         vigil_json_value_t **, vigil_error_t *);

typedef struct
{
    const char *method;
    lsp_handler_fn handler;
} lsp_dispatch_entry_t;

static const lsp_dispatch_entry_t lsp_dispatch_table[] = {
    {"initialize", handle_initialize},
    {"shutdown", handle_shutdown},
    {"textDocument/didOpen", handle_did_open},
    {"textDocument/didChange", handle_did_change},
    {"textDocument/didClose", handle_did_close},
    {"textDocument/documentSymbol", handle_document_symbol},
    {"textDocument/hover", handle_hover},
    {"textDocument/definition", handle_definition},
    {"textDocument/completion", handle_completion},
    {"textDocument/references", handle_references},
    {"textDocument/rename", handle_rename},
    {"textDocument/formatting", handle_formatting},
    {"textDocument/signatureHelp", handle_signature_help},
    {"textDocument/semanticTokens/full", handle_semantic_tokens_full},
};

#define LSP_DISPATCH_COUNT (sizeof(lsp_dispatch_table) / sizeof(lsp_dispatch_table[0]))

static vigil_status_t lsp_handle_message(vigil_lsp_server_t *server, const vigil_json_value_t *message,
                                         vigil_error_t *error)
{
    const vigil_json_value_t *method_val;
    const vigil_json_value_t *id;
    const vigil_json_value_t *params;
    vigil_json_value_t *response = NULL;
    const char *method;
    size_t method_len;
    vigil_status_t status = VIGIL_STATUS_OK;
    size_t i;

    method_val = vigil_json_object_get(message, "method");
    if (method_val == NULL)
        return VIGIL_STATUS_OK;

    method = vigil_json_string_value(method_val);
    if (method == NULL)
        return VIGIL_STATUS_OK;
    method_len = vigil_json_string_length(method_val);

    /* "exit" is handled inline — it requires no response and terminates the loop. */
    if (method_len == 4 && strncmp(method, "exit", 4) == 0)
    {
        server->shutdown_requested = 1;
        return VIGIL_STATUS_OK;
    }

    id = vigil_json_object_get(message, "id");
    params = vigil_json_object_get(message, "params");

    for (i = 0; i < LSP_DISPATCH_COUNT; i++)
    {
        const char *entry_method = lsp_dispatch_table[i].method;
        size_t entry_len = strlen(entry_method);
        if (method_len == entry_len && strncmp(method, entry_method, method_len) == 0)
        {
            status = lsp_dispatch_table[i].handler(server, id, params, &response, error);
            break;
        }
    }

    if (status != VIGIL_STATUS_OK)
    {
        vigil_json_free(&response);
        return status;
    }

    if (response != NULL)
    {
        status = vigil_jsonrpc_write(&server->transport, response, error);
        vigil_json_free(&response);
    }

    return status;
}

/* ── Server Lifecycle ─────────────────────────────────────── */

vigil_status_t vigil_lsp_server_create(vigil_lsp_server_t **out, FILE *in, FILE *out_stream,
                                       const vigil_allocator_t *allocator, vigil_error_t *error)
{
    vigil_lsp_server_t *server;
    vigil_status_t status;
    vigil_allocator_t a;

    if (out == NULL || in == NULL || out_stream == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "lsp: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    a = (allocator != NULL && vigil_allocator_is_valid(allocator)) ? *allocator : vigil_default_allocator();

    {
        void *mem = a.allocate(a.user_data, sizeof(vigil_lsp_server_t));
        if (mem == NULL)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        memset(mem, 0, sizeof(vigil_lsp_server_t));
        server = mem;
    }

    server->allocator = a;

    vigil_jsonrpc_transport_init(&server->transport, in, out_stream, &server->allocator);

    status = vigil_runtime_open(&server->runtime, NULL, error);
    if (status != VIGIL_STATUS_OK)
    {
        a.deallocate(a.user_data, server);
        return status;
    }

    vigil_source_registry_init(&server->sources, server->runtime);

    status = vigil_semantic_index_create(&server->index, server->runtime, &server->sources, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_source_registry_free(&server->sources);
        vigil_runtime_close(&server->runtime);
        a.deallocate(a.user_data, server);
        return status;
    }

    *out = server;
    return VIGIL_STATUS_OK;
}

void vigil_lsp_server_destroy(vigil_lsp_server_t **server)
{
    vigil_lsp_server_t *s;

    if (server == NULL || *server == NULL)
        return;
    s = *server;

    vigil_semantic_index_destroy(&s->index);
    vigil_source_registry_free(&s->sources);
    vigil_runtime_close(&s->runtime);
    s->allocator.deallocate(s->allocator.user_data, s);
    *server = NULL;
}

vigil_status_t vigil_lsp_server_process_one(vigil_lsp_server_t *server, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_json_value_t *message = NULL;

    if (server == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_jsonrpc_read(&server->transport, &message, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = lsp_handle_message(server, message, error);
    vigil_json_free(&message);
    return status;
}

vigil_status_t vigil_lsp_server_run(vigil_lsp_server_t *server, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_json_value_t *message = NULL;

    if (server == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    while (!server->shutdown_requested)
    {
        status = vigil_jsonrpc_read(&server->transport, &message, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        status = lsp_handle_message(server, message, error);
        vigil_json_free(&message);

        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}
