#ifndef VIGIL_LEXER_H
#define VIGIL_LEXER_H

#include "vigil/diagnostic.h"
#include "vigil/export.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/token.h"

#ifdef __cplusplus
extern "C"
{
#endif

    VIGIL_API vigil_status_t vigil_lex_source(const vigil_source_registry_t *registry, vigil_source_id_t source_id,
                                              vigil_token_list_t *tokens, vigil_diagnostic_list_t *diagnostics,
                                              vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
