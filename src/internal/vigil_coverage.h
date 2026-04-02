#ifndef VIGIL_INTERNAL_COVERAGE_H
#define VIGIL_INTERNAL_COVERAGE_H

#include <stdio.h>

#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/vm.h"

typedef struct vigil_platform_mutex vigil_platform_mutex_t;

typedef enum vigil_coverage_output_format
{
    VIGIL_COVERAGE_OUTPUT_TEXT = 0,
    VIGIL_COVERAGE_OUTPUT_JSON = 1
} vigil_coverage_output_format_t;

typedef struct vigil_coverage_file vigil_coverage_file_t;
typedef struct vigil_coverage_branch vigil_coverage_branch_t;
typedef struct vigil_coverage_chunk_map vigil_coverage_chunk_map_t;
typedef struct vigil_coverage_vm_state vigil_coverage_vm_state_t;

typedef struct vigil_coverage_session
{
    vigil_coverage_file_t *files;
    size_t file_count;
    size_t file_capacity;
    vigil_coverage_branch_t *branches;
    size_t branch_count;
    size_t branch_capacity;
    vigil_coverage_chunk_map_t *chunks;
    size_t chunk_count;
    size_t chunk_capacity;
    vigil_coverage_vm_state_t *vm_states;
    size_t vm_state_count;
    size_t vm_state_capacity;
    vigil_platform_mutex_t *lock;
} vigil_coverage_session_t;

typedef struct vigil_coverage_summary
{
    size_t covered;
    size_t total;
    double percent;
} vigil_coverage_summary_t;

void vigil_coverage_session_init(vigil_coverage_session_t *session);
void vigil_coverage_session_free(vigil_coverage_session_t *session);
vigil_status_t vigil_coverage_session_track_registry(vigil_coverage_session_t *session,
                                                     const vigil_source_registry_t *registry, const char *scope_root,
                                                     const char *wrapper_path, int include_deps, vigil_error_t *error);
vigil_status_t vigil_coverage_session_register_function(vigil_coverage_session_t *session,
                                                        const vigil_source_registry_t *registry,
                                                        const vigil_object_t *function, vigil_error_t *error);
void vigil_coverage_session_attach_vm(vigil_coverage_session_t *session, vigil_vm_t *vm,
                                      const vigil_source_registry_t *registry);
void vigil_coverage_session_detach_vm(vigil_coverage_session_t *session, vigil_vm_t *vm);
size_t vigil_coverage_session_file_count(const vigil_coverage_session_t *session);
vigil_coverage_summary_t vigil_coverage_session_total_line_summary(const vigil_coverage_session_t *session);
void vigil_coverage_session_print_text(const vigil_coverage_session_t *session, FILE *stream, int verbose,
                                       size_t test_file_count);
vigil_status_t vigil_coverage_session_print_json(const vigil_coverage_session_t *session, FILE *stream,
                                                 vigil_error_t *error);

#endif
