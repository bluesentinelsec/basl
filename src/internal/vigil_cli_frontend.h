#ifndef VIGIL_INTERNAL_CLI_FRONTEND_H
#define VIGIL_INTERNAL_CLI_FRONTEND_H

#include <stddef.h>

#include "vigil/lexer.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/token.h"

/* Path helpers shared across CLI sources. */
int cli_path_has_vigil_extension(const char *path, size_t length);
int cli_path_is_absolute(const char *path, size_t length);

/* Source token text accessor. */
const char *cli_source_token_text(const vigil_source_file_t *source, const vigil_token_t *token, size_t *out_length);

/* Import path resolver. */
vigil_status_t resolve_import_path(vigil_runtime_t *runtime, const char *base_path, const char *import_text,
                                   size_t import_length, vigil_string_t *out_path, vigil_error_t *error);

/* Project discovery and source loading. */
int find_project_root(const char *start_path, char *out_buf, size_t buf_size);
int register_source_tree(vigil_source_registry_t *registry, const char *path, const char *project_root,
                         vigil_source_id_t *out_source_id, vigil_error_t *error);

/* Test runner entry point. */
int vigil_cli_run_test_command(int argc, char **argv);

#endif
