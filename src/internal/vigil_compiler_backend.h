#ifndef VIGIL_COMPILER_BACKEND_H
#define VIGIL_COMPILER_BACKEND_H

#include "vigil_compiler_types.h"

vigil_status_t vigil_compile_materialize_lowered_function_body(vigil_program_state_t *program,
                                                               const vigil_function_decl_t *decl,
                                                               vigil_lowered_function_body_t *body,
                                                               vigil_object_t **out_object);

#endif
