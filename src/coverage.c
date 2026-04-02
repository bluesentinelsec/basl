#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_coverage.h"
#include "internal/vigil_internal.h"
#include "platform/platform.h"
#include "vigil/chunk.h"

typedef enum vigil_coverage_branch_kind
{
    VIGIL_COVERAGE_BRANCH_CONDITION = 0,
    VIGIL_COVERAGE_BRANCH_LOOP = 1
} vigil_coverage_branch_kind_t;

typedef struct vigil_coverage_file
{
    char *path;
    char *display_path;
    size_t line_count;
    size_t *line_starts;
    uint8_t *track_lines;
    uint8_t *hit_lines;
} vigil_coverage_file_t;

typedef struct vigil_coverage_branch
{
    size_t file_index;
    size_t line;
    size_t ordinal;
    vigil_coverage_branch_kind_t kind;
    uint8_t hit_mask;
} vigil_coverage_branch_t;

typedef struct vigil_coverage_chunk_map
{
    const vigil_chunk_t *chunk;
    size_t code_size;
    size_t *file_indexes;
    size_t *line_indexes;
    size_t *branch_indexes;
} vigil_coverage_chunk_map_t;

typedef struct vigil_coverage_vm_state
{
    vigil_vm_t *vm;
    const vigil_source_registry_t *registry;
    size_t pending_branch_index;
    size_t pending_fallthrough_ip;
    size_t pending_frame_depth;
    const vigil_chunk_t *pending_chunk;
    int has_pending;
} vigil_coverage_vm_state_t;

static const size_t k_coverage_none = (size_t)-1;

static int coverage_scan_chunk(vigil_coverage_session_t *session, const vigil_source_registry_t *registry,
                               const vigil_chunk_t *chunk);

static double coverage_percent(size_t covered, size_t total)
{
    if (total == 0U)
        return 100.0;
    return ((double)covered * 100.0) / (double)total;
}

static char *coverage_strdup(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL)
        return NULL;
    length = strlen(text);
    copy = malloc(length + 1U);
    if (copy == NULL)
        return NULL;
    memcpy(copy, text, length + 1U);
    return copy;
}

static int coverage_is_separator(char ch)
{
    return ch == '/' || ch == '\\';
}

static size_t coverage_init_normalized_root(const char *path, char *normalized, size_t *out_index, int *out_absolute)
{
    size_t index;
    size_t root_length;

    *out_index = 0U;
    *out_absolute = 0;
    root_length = 0U;
    if (path[0] == '\0')
        return root_length;

    if (path[1] == ':' && isalpha((unsigned char)path[0]) != 0)
    {
        normalized[(*out_index)++] = path[0];
        normalized[(*out_index)++] = ':';
        root_length = *out_index;
        index = 2U;
        if (coverage_is_separator(path[index]))
        {
            normalized[(*out_index)++] = '/';
            *out_absolute = 1;
            root_length = *out_index;
            while (coverage_is_separator(path[index]))
                index += 1U;
        }
        return root_length;
    }

    if (!coverage_is_separator(path[0]))
        return root_length;

    normalized[(*out_index)++] = '/';
    *out_absolute = 1;
    root_length = *out_index;
    index = 0U;
    while (coverage_is_separator(path[index]))
        index += 1U;
    return root_length;
}

static size_t coverage_skip_root(const char *path)
{
    size_t index;

    index = 0U;
    if (path[0] == '\0')
        return 0U;
    if (path[1] == ':' && isalpha((unsigned char)path[0]) != 0)
    {
        index = 2U;
        while (coverage_is_separator(path[index]))
            index += 1U;
        return index;
    }
    while (coverage_is_separator(path[index]))
        index += 1U;
    return index;
}

static size_t coverage_next_segment(const char *path, size_t length, size_t *index, size_t *segment_length)
{
    size_t segment_start;

    while (*index < length && coverage_is_separator(path[*index]))
        *index += 1U;
    if (*index >= length)
    {
        *segment_length = 0U;
        return length;
    }
    segment_start = *index;
    while (*index < length && !coverage_is_separator(path[*index]))
        *index += 1U;
    *segment_length = *index - segment_start;
    return segment_start;
}

static void coverage_append_segment(char *normalized, size_t *segments, size_t *segment_count, size_t *out_length,
                                    const char *segment, size_t segment_length)
{
    if (*out_length != 0U && normalized[*out_length - 1U] != '/')
        normalized[(*out_length)++] = '/';
    segments[(*segment_count)++] = *out_length;
    memcpy(normalized + *out_length, segment, segment_length);
    *out_length += segment_length;
}

static int coverage_handle_dot_segment(char *normalized, size_t *segments, size_t *segment_count, size_t *out_length,
                                       int absolute, const char *segment, size_t segment_length)
{
    if (segment_length == 1U && segment[0] == '.')
        return 1;
    if (!(segment_length == 2U && segment[0] == '.' && segment[1] == '.'))
        return 0;
    if (*segment_count > 0U)
    {
        *out_length = segments[*segment_count - 1U];
        *segment_count -= 1U;
        return 1;
    }
    if (!absolute)
        coverage_append_segment(normalized, segments, segment_count, out_length, segment, segment_length);
    return 1;
}

static char *coverage_normalize_path(const char *path)
{
    char *normalized;
    size_t length;
    size_t *segments;
    size_t segment_count;
    size_t out_length;
    size_t index;
    size_t root_length;
    int absolute;

    if (path == NULL)
        return NULL;
    /* Coverage needs stable path identity across source registration and CLI
     * scope filtering. The platform layer does not expose a canonicalization
     * helper for synthetic import paths, so normalize separators and dot
     * segments here before comparing paths. */
    length = strlen(path);
    normalized = malloc(length + 3U);
    segments = malloc((length + 1U) * sizeof(size_t));
    if (normalized == NULL || segments == NULL)
    {
        free(normalized);
        free(segments);
        return NULL;
    }

    segment_count = 0U;
    out_length = 0U;
    root_length = coverage_init_normalized_root(path, normalized, &out_length, &absolute);
    index = coverage_skip_root(path);

    while (index < length)
    {
        size_t segment_start;
        size_t segment_length;

        segment_start = coverage_next_segment(path, length, &index, &segment_length);
        if (segment_length == 0U)
            break;
        if (coverage_handle_dot_segment(normalized, segments, &segment_count, &out_length, absolute,
                                        path + segment_start, segment_length))
            continue;
        coverage_append_segment(normalized, segments, &segment_count, &out_length, path + segment_start,
                                segment_length);
    }

    if (out_length == 0U)
    {
        normalized[out_length++] = '.';
    }
    else if (out_length > root_length && normalized[out_length - 1U] == '/')
    {
        out_length -= 1U;
    }
    normalized[out_length] = '\0';
    free(segments);
    return normalized;
}

static int coverage_has_suffix(const char *path, const char *suffix)
{
    size_t path_length;
    size_t suffix_length;

    if (path == NULL || suffix == NULL)
        return 0;
    path_length = strlen(path);
    suffix_length = strlen(suffix);
    if (path_length < suffix_length)
        return 0;
    return strcmp(path + path_length - suffix_length, suffix) == 0;
}

static int coverage_path_has_prefix(const char *path, const char *prefix)
{
    size_t prefix_length;

    if (path == NULL || prefix == NULL)
        return 0;
    prefix_length = strlen(prefix);
    if (prefix_length == 0U)
        return 1;
    if (strncmp(path, prefix, prefix_length) != 0)
        return 0;
    if (path[prefix_length] == '\0')
        return 1;
    if (prefix[prefix_length - 1U] == '/' || prefix[prefix_length - 1U] == '\\')
        return 1;
    return path[prefix_length] == '/' || path[prefix_length] == '\\';
}

static const char *coverage_relative_display_path(const char *path, const char *scope_root)
{
    size_t root_length;

    if (path == NULL || scope_root == NULL || !coverage_path_has_prefix(path, scope_root))
        return path;
    root_length = strlen(scope_root);
    while (path[root_length] == '/' || path[root_length] == '\\')
        root_length += 1U;
    return path + root_length;
}

static size_t coverage_count_lines(const char *text, size_t length)
{
    size_t count;
    size_t index;

    count = 1U;
    for (index = 0U; index < length; index += 1U)
    {
        if (text[index] == '\n')
            count += 1U;
    }
    return count;
}

static int coverage_build_line_starts(vigil_coverage_file_t *file, const char *text, size_t length)
{
    size_t index;
    size_t line_index;

    file->line_count = coverage_count_lines(text, length);
    file->line_starts = calloc(file->line_count + 1U, sizeof(size_t));
    file->track_lines = calloc(file->line_count, sizeof(uint8_t));
    file->hit_lines = calloc(file->line_count, sizeof(uint8_t));
    if (file->line_starts == NULL || file->track_lines == NULL || file->hit_lines == NULL)
        return 0;

    line_index = 0U;
    file->line_starts[line_index++] = 0U;
    for (index = 0U; index < length; index += 1U)
    {
        if (text[index] == '\n' && line_index < file->line_count)
            file->line_starts[line_index++] = index + 1U;
    }
    file->line_starts[file->line_count] = length;
    return 1;
}

static size_t coverage_find_line_index(const vigil_coverage_file_t *file, size_t offset)
{
    size_t left;
    size_t right;

    if (file == NULL || file->line_count == 0U)
        return 0U;
    left = 0U;
    right = file->line_count;
    while (left + 1U < right)
    {
        size_t middle;

        middle = left + ((right - left) / 2U);
        if (file->line_starts[middle] <= offset)
            left = middle;
        else
            right = middle;
    }
    return left;
}

static void coverage_free_file(vigil_coverage_file_t *file)
{
    if (file == NULL)
        return;
    free(file->path);
    free(file->display_path);
    free(file->line_starts);
    free(file->track_lines);
    free(file->hit_lines);
    memset(file, 0, sizeof(*file));
}

static void coverage_free_chunk_map(vigil_coverage_chunk_map_t *chunk_map)
{
    if (chunk_map == NULL)
        return;
    free(chunk_map->file_indexes);
    free(chunk_map->line_indexes);
    free(chunk_map->branch_indexes);
    memset(chunk_map, 0, sizeof(*chunk_map));
}

static int coverage_grow_array(void **items, size_t item_size, size_t *capacity, size_t minimum_capacity)
{
    size_t next_capacity;
    void *memory;

    if (*capacity >= minimum_capacity)
        return 1;
    next_capacity = *capacity == 0U ? 8U : *capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / item_size)
        return 0;
    memory = realloc(*items, next_capacity * item_size);
    if (memory == NULL)
        return 0;
    *items = memory;
    *capacity = next_capacity;
    return 1;
}

static size_t coverage_find_file_index(const vigil_coverage_session_t *session, const char *path)
{
    size_t index;

    if (session == NULL || path == NULL)
        return k_coverage_none;
    for (index = 0U; index < session->file_count; index += 1U)
    {
        if (strcmp(session->files[index].path, path) == 0)
            return index;
    }
    return k_coverage_none;
}

static size_t coverage_find_or_add_file(vigil_coverage_session_t *session, const vigil_source_file_t *source,
                                        const char *scope_root)
{
    size_t file_index;
    vigil_coverage_file_t *file;
    char *normalized_path;
    char *normalized_root;
    const char *text;
    size_t text_length;

    normalized_path = coverage_normalize_path(vigil_string_c_str(&source->path));
    normalized_root = coverage_normalize_path(scope_root);
    if (normalized_path == NULL)
    {
        free(normalized_root);
        return k_coverage_none;
    }

    file_index = coverage_find_file_index(session, normalized_path);
    if (file_index != k_coverage_none)
    {
        free(normalized_path);
        free(normalized_root);
        return file_index;
    }

    if (!coverage_grow_array((void **)&session->files, sizeof(*session->files), &session->file_capacity,
                             session->file_count + 1U))
    {
        free(normalized_path);
        free(normalized_root);
        return k_coverage_none;
    }

    file_index = session->file_count++;
    file = &session->files[file_index];
    memset(file, 0, sizeof(*file));
    file->path = normalized_path;
    file->display_path = coverage_strdup(coverage_relative_display_path(normalized_path, normalized_root));
    text = vigil_string_c_str(&source->text);
    text_length = vigil_string_length(&source->text);
    if (file->path == NULL || file->display_path == NULL || !coverage_build_line_starts(file, text, text_length))
    {
        coverage_free_file(file);
        session->file_count -= 1U;
        free(normalized_root);
        return k_coverage_none;
    }

    free(normalized_root);
    return file_index;
}

static size_t coverage_find_branch_index(const vigil_coverage_session_t *session, size_t file_index, size_t line,
                                         vigil_coverage_branch_kind_t kind, size_t ordinal)
{
    size_t index;

    for (index = 0U; index < session->branch_count; index += 1U)
    {
        const vigil_coverage_branch_t *branch;

        branch = &session->branches[index];
        if (branch->file_index == file_index && branch->line == line && branch->kind == kind &&
            branch->ordinal == ordinal)
        {
            return index;
        }
    }
    return k_coverage_none;
}

static size_t coverage_find_or_add_branch(vigil_coverage_session_t *session, size_t file_index, size_t line,
                                          vigil_coverage_branch_kind_t kind, size_t ordinal)
{
    size_t branch_index;

    branch_index = coverage_find_branch_index(session, file_index, line, kind, ordinal);
    if (branch_index != k_coverage_none)
        return branch_index;

    if (!coverage_grow_array((void **)&session->branches, sizeof(*session->branches), &session->branch_capacity,
                             session->branch_count + 1U))
    {
        return k_coverage_none;
    }
    branch_index = session->branch_count++;
    session->branches[branch_index].file_index = file_index;
    session->branches[branch_index].line = line;
    session->branches[branch_index].kind = kind;
    session->branches[branch_index].ordinal = ordinal;
    session->branches[branch_index].hit_mask = 0U;
    return branch_index;
}

static size_t coverage_find_chunk_map_index(const vigil_coverage_session_t *session, const vigil_chunk_t *chunk)
{
    size_t index;

    for (index = 0U; index < session->chunk_count; index += 1U)
    {
        if (session->chunks[index].chunk == chunk)
            return index;
    }
    return k_coverage_none;
}

static int coverage_is_conditional_branch_opcode(vigil_opcode_t opcode)
{
    switch (opcode)
    {
    case VIGIL_OPCODE_JUMP_IF_FALSE:
    case VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_EQUAL_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE:
    case VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_EQUAL_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE:
    case VIGIL_OPCODE_FORLOOP_I32:
    case VIGIL_OPCODE_FORLOOP_I64:
        return 1;
    default:
        return 0;
    }
}

static vigil_coverage_branch_kind_t coverage_branch_kind_for_opcode(vigil_opcode_t opcode)
{
    if (opcode == VIGIL_OPCODE_FORLOOP_I32 || opcode == VIGIL_OPCODE_FORLOOP_I64)
        return VIGIL_COVERAGE_BRANCH_LOOP;
    return VIGIL_COVERAGE_BRANCH_CONDITION;
}

static size_t coverage_branch_ordinal_for_line(const vigil_coverage_chunk_map_t *chunk_map, size_t line_index,
                                               size_t ip)
{
    size_t count;
    size_t cursor;

    count = 0U;
    for (cursor = 0U; cursor < ip; cursor += 1U)
    {
        size_t branch_index;

        branch_index = chunk_map->branch_indexes[cursor];
        if (branch_index == k_coverage_none)
            continue;
        if (chunk_map->line_indexes[cursor] == line_index)
            count += 1U;
    }
    return count;
}

static vigil_coverage_vm_state_t *coverage_find_vm_state(vigil_coverage_session_t *session, vigil_vm_t *vm)
{
    size_t index;

    for (index = 0U; index < session->vm_state_count; index += 1U)
    {
        if (session->vm_states[index].vm == vm)
            return &session->vm_states[index];
    }
    return NULL;
}

static vigil_coverage_vm_state_t *coverage_find_or_add_vm_state(vigil_coverage_session_t *session, vigil_vm_t *vm)
{
    vigil_coverage_vm_state_t *state;

    state = coverage_find_vm_state(session, vm);
    if (state != NULL)
        return state;
    if (!coverage_grow_array((void **)&session->vm_states, sizeof(*session->vm_states), &session->vm_state_capacity,
                             session->vm_state_count + 1U))
    {
        return NULL;
    }
    state = &session->vm_states[session->vm_state_count++];
    memset(state, 0, sizeof(*state));
    state->vm = vm;
    state->pending_branch_index = k_coverage_none;
    return state;
}

static void coverage_remove_vm_state(vigil_coverage_session_t *session, vigil_vm_t *vm)
{
    size_t index;

    for (index = 0U; index < session->vm_state_count; index += 1U)
    {
        if (session->vm_states[index].vm == vm)
        {
            if (index + 1U < session->vm_state_count)
            {
                memmove(&session->vm_states[index], &session->vm_states[index + 1U],
                        (session->vm_state_count - index - 1U) * sizeof(*session->vm_states));
            }
            session->vm_state_count -= 1U;
            return;
        }
    }
}

static void coverage_write_json_string(FILE *stream, const char *text)
{
    const unsigned char *cursor;

    fputc('"', stream);
    for (cursor = (const unsigned char *)text; *cursor != '\0'; cursor += 1)
    {
        switch (*cursor)
        {
        case '"':
            fputs("\\\"", stream);
            break;
        case '\\':
            fputs("\\\\", stream);
            break;
        case '\b':
            fputs("\\b", stream);
            break;
        case '\f':
            fputs("\\f", stream);
            break;
        case '\n':
            fputs("\\n", stream);
            break;
        case '\r':
            fputs("\\r", stream);
            break;
        case '\t':
            fputs("\\t", stream);
            break;
        default:
            if (*cursor < 0x20U)
                fprintf(stream, "\\u%04x", (unsigned int)*cursor);
            else
                fputc((int)*cursor, stream);
            break;
        }
    }
    fputc('"', stream);
}

static void coverage_mark_branch_arm(vigil_coverage_session_t *session, size_t branch_index, int took_fallthrough)
{
    uint8_t mask;

    if (branch_index == k_coverage_none || branch_index >= session->branch_count)
        return;
    mask = took_fallthrough ? 0x1U : 0x2U;
    session->branches[branch_index].hit_mask |= mask;
}

static vigil_coverage_chunk_map_t *coverage_debug_chunk_map(vigil_coverage_session_t *session,
                                                            vigil_coverage_vm_state_t *vm_state,
                                                            const vigil_chunk_t *chunk)
{
    size_t chunk_index;

    chunk_index = coverage_find_chunk_map_index(session, chunk);
    if (chunk_index == k_coverage_none && vm_state->registry != NULL)
    {
        if (!coverage_scan_chunk(session, vm_state->registry, chunk))
            return NULL;
        chunk_index = coverage_find_chunk_map_index(session, chunk);
    }
    if (chunk_index == k_coverage_none)
        return NULL;
    return &session->chunks[chunk_index];
}

static void coverage_debug_resolve_pending(vigil_coverage_session_t *session, vigil_coverage_vm_state_t *vm_state,
                                           const vigil_chunk_t *chunk, size_t frame_depth, size_t ip)
{
    if (!vm_state->has_pending || vm_state->pending_branch_index == k_coverage_none)
        return;

    if (vm_state->pending_chunk == chunk && vm_state->pending_frame_depth == frame_depth)
    {
        coverage_mark_branch_arm(session, vm_state->pending_branch_index, ip == vm_state->pending_fallthrough_ip);
    }
    else
    {
        /* If control moved to a different frame or chunk before the next
         * instruction hook, the conditional must have taken the non-fallthrough
         * arm. This covers common cases like function calls in the taken branch. */
        coverage_mark_branch_arm(session, vm_state->pending_branch_index, 0);
    }
    vm_state->has_pending = 0;
    vm_state->pending_branch_index = k_coverage_none;
}

static void coverage_debug_mark_line(vigil_coverage_session_t *session, const vigil_coverage_chunk_map_t *chunk_map,
                                     size_t ip)
{
    size_t line_index;
    size_t file_index;

    if (ip >= chunk_map->code_size || chunk_map->line_indexes[ip] == k_coverage_none)
        return;

    line_index = chunk_map->line_indexes[ip];
    file_index = chunk_map->file_indexes[ip];
    if (file_index != k_coverage_none && file_index < session->file_count &&
        line_index < session->files[file_index].line_count)
    {
        session->files[file_index].hit_lines[line_index] = 1U;
    }
}

static void coverage_debug_begin_branch(vigil_coverage_vm_state_t *vm_state,
                                        const vigil_coverage_chunk_map_t *chunk_map, const vigil_chunk_t *chunk,
                                        size_t frame_depth, size_t ip)
{
    if (ip >= chunk_map->code_size || chunk_map->branch_indexes[ip] == k_coverage_none)
        return;
    vm_state->has_pending = 1;
    vm_state->pending_branch_index = chunk_map->branch_indexes[ip];
    vm_state->pending_fallthrough_ip = ip + vigil_opcode_size((vigil_opcode_t)vigil_chunk_code(chunk)[ip]);
    vm_state->pending_frame_depth = frame_depth;
    vm_state->pending_chunk = chunk;
}

static int coverage_debug_hook(vigil_vm_t *vm, void *userdata)
{
    vigil_coverage_session_t *session;
    vigil_coverage_vm_state_t *vm_state;
    size_t frame_depth;
    size_t frame_index;
    const vigil_chunk_t *chunk;
    size_t ip;
    vigil_coverage_chunk_map_t *chunk_map;

    session = userdata;
    if (session == NULL)
        return 0;

    vigil_platform_mutex_lock(session->lock);
    vm_state = coverage_find_or_add_vm_state(session, vm);
    frame_depth = vigil_vm_frame_depth(vm);
    if (vm_state == NULL || frame_depth == 0U)
        goto done;

    frame_index = frame_depth - 1U;
    chunk = vigil_vm_frame_chunk(vm, frame_index);
    ip = vigil_vm_frame_ip(vm, frame_index);
    chunk_map = coverage_debug_chunk_map(session, vm_state, chunk);
    if (chunk_map == NULL)
        goto done;

    coverage_debug_resolve_pending(session, vm_state, chunk, frame_depth, ip);
    coverage_debug_mark_line(session, chunk_map, ip);
    coverage_debug_begin_branch(vm_state, chunk_map, chunk, frame_depth, ip);

done:
    vigil_platform_mutex_unlock(session->lock);
    return 0;
}

static size_t coverage_file_index_for_span(const vigil_coverage_session_t *session,
                                           const vigil_source_registry_t *registry, vigil_source_span_t span)
{
    const vigil_source_file_t *source;
    char *normalized_path;
    size_t file_index;

    source = vigil_source_registry_get(registry, span.source_id);
    if (source == NULL)
        return k_coverage_none;
    normalized_path = coverage_normalize_path(vigil_string_c_str(&source->path));
    if (normalized_path == NULL)
        return k_coverage_none;
    file_index = coverage_find_file_index(session, normalized_path);
    free(normalized_path);
    return file_index;
}

static int coverage_scan_chunk(vigil_coverage_session_t *session, const vigil_source_registry_t *registry,
                               const vigil_chunk_t *chunk)
{
    const uint8_t *code;
    size_t code_size;
    size_t chunk_index;
    vigil_coverage_chunk_map_t *chunk_map;
    size_t ip;

    chunk_index = coverage_find_chunk_map_index(session, chunk);
    if (chunk_index != k_coverage_none)
        return 1;

    if (!coverage_grow_array((void **)&session->chunks, sizeof(*session->chunks), &session->chunk_capacity,
                             session->chunk_count + 1U))
    {
        return 0;
    }

    chunk_index = session->chunk_count++;
    chunk_map = &session->chunks[chunk_index];
    memset(chunk_map, 0, sizeof(*chunk_map));
    chunk_map->chunk = chunk;
    chunk_map->code_size = vigil_chunk_code_size(chunk);
    chunk_map->file_indexes = malloc(chunk_map->code_size * sizeof(size_t));
    chunk_map->line_indexes = malloc(chunk_map->code_size * sizeof(size_t));
    chunk_map->branch_indexes = malloc(chunk_map->code_size * sizeof(size_t));
    if (chunk_map->file_indexes == NULL || chunk_map->line_indexes == NULL || chunk_map->branch_indexes == NULL)
        return 0;
    for (ip = 0U; ip < chunk_map->code_size; ip += 1U)
    {
        chunk_map->file_indexes[ip] = k_coverage_none;
        chunk_map->line_indexes[ip] = k_coverage_none;
        chunk_map->branch_indexes[ip] = k_coverage_none;
    }

    code = vigil_chunk_code(chunk);
    code_size = chunk_map->code_size;
    for (ip = 0U; ip < code_size;)
    {
        vigil_source_span_t span;
        size_t file_index;
        size_t line_index;
        uint8_t size;

        span = vigil_chunk_span_at(chunk, ip);
        file_index = coverage_file_index_for_span(session, registry, span);
        size = vigil_opcode_size((vigil_opcode_t)code[ip]);
        if (size > code_size - ip)
            size = (uint8_t)(code_size - ip);
        if (file_index != k_coverage_none)
        {
            vigil_coverage_file_t *file;

            file = &session->files[file_index];
            line_index = coverage_find_line_index(file, span.start_offset);
            if (line_index < file->line_count)
            {
                size_t branch_index;
                size_t ordinal;

                chunk_map->file_indexes[ip] = file_index;
                file->track_lines[line_index] = 1U;
                chunk_map->line_indexes[ip] = line_index;
                if (coverage_is_conditional_branch_opcode((vigil_opcode_t)code[ip]))
                {
                    ordinal = coverage_branch_ordinal_for_line(chunk_map, line_index, ip);
                    branch_index =
                        coverage_find_or_add_branch(session, file_index, line_index + 1U,
                                                    coverage_branch_kind_for_opcode((vigil_opcode_t)code[ip]), ordinal);
                    if (branch_index != k_coverage_none)
                        chunk_map->branch_indexes[ip] = branch_index;
                }
            }
        }
        ip += size;
    }

    return 1;
}

static int coverage_scan_callable(vigil_coverage_session_t *session, const vigil_source_registry_t *registry,
                                  const vigil_object_t *callable)
{
    const vigil_object_t *function;
    size_t sibling_index;
    size_t global_index;

    if (callable == NULL)
        return 1;
    function = vigil_callable_object_function(callable);
    if (function == NULL)
        return 1;

    for (sibling_index = 0U;; sibling_index += 1U)
    {
        const vigil_object_t *sibling;
        const vigil_chunk_t *chunk;

        sibling = sibling_index == 0U ? function : vigil_function_object_sibling(function, sibling_index);
        if (sibling == NULL)
            break;
        chunk = vigil_callable_object_chunk(sibling);
        if (chunk != NULL && !coverage_scan_chunk(session, registry, chunk))
            return 0;
    }

    for (global_index = 0U; global_index < vigil_function_object_global_count(function); global_index += 1U)
    {
        vigil_value_t global_value;

        vigil_value_init_nil(&global_value);
        if (!vigil_function_object_get_global(function, global_index, &global_value))
            continue;
        if (vigil_value_kind(&global_value) == VIGIL_VALUE_OBJECT)
        {
            vigil_object_t *global_object;

            global_object = vigil_value_as_object(&global_value);
            if (global_object != NULL &&
                (vigil_object_type(global_object) == VIGIL_OBJECT_FUNCTION ||
                 vigil_object_type(global_object) == VIGIL_OBJECT_CLOSURE) &&
                !coverage_scan_callable(session, registry, global_object))
            {
                vigil_value_release(&global_value);
                return 0;
            }
        }
        vigil_value_release(&global_value);
    }

    return 1;
}

static void coverage_collect_counts(const vigil_coverage_session_t *session, size_t file_index, size_t *line_covered,
                                    size_t *line_total, size_t *branch_covered, size_t *branch_total)
{
    size_t index;

    *line_covered = 0U;
    *line_total = 0U;
    *branch_covered = 0U;
    *branch_total = 0U;

    for (index = 0U; index < session->files[file_index].line_count; index += 1U)
    {
        if (session->files[file_index].track_lines[index] == 0U)
            continue;
        *line_total += 1U;
        if (session->files[file_index].hit_lines[index] != 0U)
            *line_covered += 1U;
    }

    for (index = 0U; index < session->branch_count; index += 1U)
    {
        const vigil_coverage_branch_t *branch;

        branch = &session->branches[index];
        if (branch->file_index != file_index)
            continue;
        *branch_total += 2U;
        if ((branch->hit_mask & 0x1U) != 0U)
            *branch_covered += 1U;
        if ((branch->hit_mask & 0x2U) != 0U)
            *branch_covered += 1U;
    }
}

static const char *coverage_branch_description(const vigil_coverage_branch_t *branch, int fallthrough)
{
    if (branch->kind == VIGIL_COVERAGE_BRANCH_LOOP)
        return fallthrough ? "loop exit branch not taken" : "loop body branch not taken";
    return fallthrough ? "true branch not taken" : "false branch not taken";
}

void vigil_coverage_session_init(vigil_coverage_session_t *session)
{
    if (session == NULL)
        return;
    memset(session, 0, sizeof(*session));
}

void vigil_coverage_session_free(vigil_coverage_session_t *session)
{
    size_t index;

    if (session == NULL)
        return;
    for (index = 0U; index < session->file_count; index += 1U)
        coverage_free_file(&session->files[index]);
    for (index = 0U; index < session->chunk_count; index += 1U)
        coverage_free_chunk_map(&session->chunks[index]);
    free(session->files);
    free(session->branches);
    free(session->chunks);
    free(session->vm_states);
    if (session->lock != NULL)
        vigil_platform_mutex_destroy(session->lock);
    memset(session, 0, sizeof(*session));
}

vigil_status_t vigil_coverage_session_track_registry(vigil_coverage_session_t *session,
                                                     const vigil_source_registry_t *registry, const char *scope_root,
                                                     const char *wrapper_path, int include_deps, vigil_error_t *error)
{
    size_t index;

    if (session == NULL || registry == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "coverage registry input must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (session->lock == NULL)
    {
        vigil_status_t status;

        status = vigil_platform_mutex_create(&session->lock, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    for (index = 0U; index < vigil_source_registry_count(registry); index += 1U)
    {
        const vigil_source_file_t *source;
        const char *path;
        char *normalized_path;
        char *normalized_root;

        source = vigil_source_registry_get(registry, (vigil_source_id_t)index);
        if (source == NULL)
            continue;
        path = vigil_string_c_str(&source->path);
        normalized_path = coverage_normalize_path(path);
        normalized_root = coverage_normalize_path(scope_root);
        if (normalized_path == NULL)
        {
            free(normalized_root);
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "failed to normalize coverage source path");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        if (wrapper_path != NULL && strcmp(path, wrapper_path) == 0)
        {
            free(normalized_path);
            free(normalized_root);
            continue;
        }
        if (coverage_has_suffix(normalized_path, "_test.vigil"))
        {
            free(normalized_path);
            free(normalized_root);
            continue;
        }
        if (!include_deps && normalized_root != NULL && !coverage_path_has_prefix(normalized_path, normalized_root))
        {
            free(normalized_path);
            free(normalized_root);
            continue;
        }
        free(normalized_path);
        free(normalized_root);
        if (coverage_find_or_add_file(session, source, scope_root) == k_coverage_none)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "failed to track coverage source file");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
    }

    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_coverage_session_register_function(vigil_coverage_session_t *session,
                                                        const vigil_source_registry_t *registry,
                                                        const vigil_object_t *function, vigil_error_t *error)
{
    if (session == NULL || registry == NULL || function == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "coverage function input must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_platform_mutex_lock(session->lock);
    if (!coverage_scan_callable(session, registry, function))
    {
        vigil_platform_mutex_unlock(session->lock);
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "failed to record coverage chunk metadata");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    vigil_platform_mutex_unlock(session->lock);
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

void vigil_coverage_session_attach_vm(vigil_coverage_session_t *session, vigil_vm_t *vm,
                                      const vigil_source_registry_t *registry)
{
    vigil_coverage_vm_state_t *state;

    if (session == NULL || vm == NULL)
        return;
    vigil_platform_mutex_lock(session->lock);
    state = coverage_find_or_add_vm_state(session, vm);
    if (state != NULL)
        state->registry = registry;
    vigil_platform_mutex_unlock(session->lock);
    vigil_vm_set_debug_hook(vm, coverage_debug_hook, session);
}

void vigil_coverage_session_detach_vm(vigil_coverage_session_t *session, vigil_vm_t *vm)
{
    if (session == NULL || vm == NULL)
        return;
    vigil_vm_set_debug_hook(vm, NULL, NULL);
    vigil_platform_mutex_lock(session->lock);
    coverage_remove_vm_state(session, vm);
    vigil_platform_mutex_unlock(session->lock);
}

size_t vigil_coverage_session_file_count(const vigil_coverage_session_t *session)
{
    return session == NULL ? 0U : session->file_count;
}

vigil_coverage_summary_t vigil_coverage_session_total_line_summary(const vigil_coverage_session_t *session)
{
    vigil_coverage_summary_t summary;
    size_t index;

    memset(&summary, 0, sizeof(summary));
    if (session == NULL)
        return summary;
    for (index = 0U; index < session->file_count; index += 1U)
    {
        size_t covered;
        size_t total;
        size_t branch_covered;
        size_t branch_total;

        coverage_collect_counts(session, index, &covered, &total, &branch_covered, &branch_total);
        summary.covered += covered;
        summary.total += total;
    }
    summary.percent = coverage_percent(summary.covered, summary.total);
    return summary;
}

void vigil_coverage_session_print_text(const vigil_coverage_session_t *session, FILE *stream, int verbose,
                                       size_t test_file_count)
{
    size_t index;
    size_t width;
    size_t total_line_covered;
    size_t total_line_total;
    size_t total_branch_covered;
    size_t total_branch_total;

    if (session == NULL || stream == NULL)
        return;

    total_line_covered = 0U;
    total_line_total = 0U;
    total_branch_covered = 0U;
    total_branch_total = 0U;
    width = strlen("total");
    for (index = 0U; index < session->file_count; index += 1U)
    {
        size_t display_length;

        display_length = strlen(session->files[index].display_path);
        if (display_length > width)
            width = display_length;
    }

    fprintf(stream, "\ncoverage: %zu files, %zu test files\n\n", session->file_count, test_file_count);
    for (index = 0U; index < session->file_count; index += 1U)
    {
        size_t line_covered;
        size_t line_total;
        size_t branch_covered;
        size_t branch_total;
        size_t line_index;
        size_t branch_index;

        coverage_collect_counts(session, index, &line_covered, &line_total, &branch_covered, &branch_total);
        total_line_covered += line_covered;
        total_line_total += line_total;
        total_branch_covered += branch_covered;
        total_branch_total += branch_total;

        fprintf(stream, "%-*s %5.1f%% lines (%zu/%zu)   %5.1f%% branches (%zu/%zu)\n", (int)width,
                session->files[index].display_path, coverage_percent(line_covered, line_total), line_covered,
                line_total, coverage_percent(branch_covered, branch_total), branch_covered, branch_total);

        if (!verbose)
            continue;

        for (line_index = 0U; line_index < session->files[index].line_count; line_index += 1U)
        {
            if (session->files[index].track_lines[line_index] != 0U &&
                session->files[index].hit_lines[line_index] == 0U)
                fprintf(stream, "  line %zu: not covered\n", line_index + 1U);
        }
        for (branch_index = 0U; branch_index < session->branch_count; branch_index += 1U)
        {
            const vigil_coverage_branch_t *branch;

            branch = &session->branches[branch_index];
            if (branch->file_index != index)
                continue;
            if ((branch->hit_mask & 0x1U) == 0U)
                fprintf(stream, "  line %zu: %s\n", branch->line, coverage_branch_description(branch, 1));
            if ((branch->hit_mask & 0x2U) == 0U)
                fprintf(stream, "  line %zu: %s\n", branch->line, coverage_branch_description(branch, 0));
        }
        fprintf(stream, "\n");
    }

    fprintf(stream, "%-*s %5.1f%% lines (%zu/%zu)   %5.1f%% branches (%zu/%zu)\n", (int)width, "total",
            coverage_percent(total_line_covered, total_line_total), total_line_covered, total_line_total,
            coverage_percent(total_branch_covered, total_branch_total), total_branch_covered, total_branch_total);
}

vigil_status_t vigil_coverage_session_print_json(const vigil_coverage_session_t *session, FILE *stream,
                                                 vigil_error_t *error)
{
    size_t index;
    size_t total_line_covered;
    size_t total_line_total;
    size_t total_branch_covered;
    size_t total_branch_total;

    if (session == NULL || stream == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "coverage json output requires a stream");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    total_line_covered = 0U;
    total_line_total = 0U;
    total_branch_covered = 0U;
    total_branch_total = 0U;

    fputs("{\n  \"files\": [\n", stream);
    for (index = 0U; index < session->file_count; index += 1U)
    {
        size_t line_covered;
        size_t line_total;
        size_t branch_covered;
        size_t branch_total;
        size_t line_index;
        size_t branch_index;
        int first_line;
        int first_branch;

        coverage_collect_counts(session, index, &line_covered, &line_total, &branch_covered, &branch_total);
        total_line_covered += line_covered;
        total_line_total += line_total;
        total_branch_covered += branch_covered;
        total_branch_total += branch_total;

        fprintf(stream, "    {\n      \"path\": ");
        coverage_write_json_string(stream, session->files[index].display_path);
        fprintf(stream,
                ",\n      \"lines\": {\"covered\": %zu, \"total\": %zu, \"percent\": %.1f},\n"
                "      \"branches\": {\"covered\": %zu, \"total\": %zu, \"percent\": %.1f},\n"
                "      \"uncovered_lines\": [",
                line_covered, line_total, coverage_percent(line_covered, line_total), branch_covered, branch_total,
                coverage_percent(branch_covered, branch_total));

        first_line = 1;
        for (line_index = 0U; line_index < session->files[index].line_count; line_index += 1U)
        {
            if (session->files[index].track_lines[line_index] == 0U ||
                session->files[index].hit_lines[line_index] != 0U)
                continue;
            fprintf(stream, "%s%zu", first_line ? "" : ", ", line_index + 1U);
            first_line = 0;
        }

        fputs("],\n      \"missed_branches\": [", stream);
        first_branch = 1;
        for (branch_index = 0U; branch_index < session->branch_count; branch_index += 1U)
        {
            const vigil_coverage_branch_t *branch;

            branch = &session->branches[branch_index];
            if (branch->file_index != index)
                continue;
            if ((branch->hit_mask & 0x1U) == 0U)
            {
                fprintf(stream, "%s{\"line\": %zu, \"kind\": ", first_branch ? "" : ", ", branch->line);
                coverage_write_json_string(stream, branch->kind == VIGIL_COVERAGE_BRANCH_LOOP ? "loop-exit" : "true");
                fputs(", \"description\": ", stream);
                coverage_write_json_string(stream, coverage_branch_description(branch, 1));
                fputc('}', stream);
                first_branch = 0;
            }
            if ((branch->hit_mask & 0x2U) == 0U)
            {
                fprintf(stream, "%s{\"line\": %zu, \"kind\": ", first_branch ? "" : ", ", branch->line);
                coverage_write_json_string(stream, branch->kind == VIGIL_COVERAGE_BRANCH_LOOP ? "loop-body" : "false");
                fputs(", \"description\": ", stream);
                coverage_write_json_string(stream, coverage_branch_description(branch, 0));
                fputc('}', stream);
                first_branch = 0;
            }
        }
        fprintf(stream, "]\n    }%s\n", index + 1U == session->file_count ? "" : ",");
    }

    fprintf(stream,
            "  ],\n"
            "  \"total\": {\n"
            "    \"lines\": {\"covered\": %zu, \"total\": %zu, \"percent\": %.1f},\n"
            "    \"branches\": {\"covered\": %zu, \"total\": %zu, \"percent\": %.1f}\n"
            "  }\n"
            "}\n",
            total_line_covered, total_line_total, coverage_percent(total_line_covered, total_line_total),
            total_branch_covered, total_branch_total, coverage_percent(total_branch_covered, total_branch_total));

    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}
