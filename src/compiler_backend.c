#include <string.h>

#include "internal/vigil_compiler_backend.h"
#include "internal/vigil_compiler_semantics.h"
#include "internal/vigil_internal.h"

vigil_status_t vigil_compile_materialize_lowered_function_body(vigil_program_state_t *program,
                                                               const vigil_function_decl_t *decl,
                                                               vigil_lowered_function_body_t *body,
                                                               vigil_object_t **out_object)
{
    vigil_chunk_t chunk;
    vigil_status_t status;
    size_t instruction_index;
    size_t operand_index;

    vigil_chunk_init(&chunk, program->registry->runtime);
    chunk.constants = body->constants;
    chunk.constant_count = body->constant_count;
    chunk.constant_capacity = body->constant_capacity;
    body->constants = NULL;
    body->constant_count = 0U;
    body->constant_capacity = 0U;

    chunk.debug_locals = body->debug_locals;
    memset(&body->debug_locals, 0, sizeof(body->debug_locals));

    for (instruction_index = 0U; instruction_index < body->instruction_count; instruction_index += 1U)
    {
        const vigil_lowered_instruction_t *instruction = &body->instructions[instruction_index];

        status = vigil_chunk_write_opcode(&chunk, instruction->opcode, instruction->span, program->error);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;

        for (operand_index = 0U; operand_index < instruction->operand_count; operand_index += 1U)
        {
            const vigil_lowered_operand_t *operand = &instruction->operands[operand_index];

            if (operand->kind == VIGIL_LOWERED_OPERAND_U32)
            {
                status = vigil_chunk_write_u32(&chunk, operand->value, instruction->span, program->error);
            }
            else
            {
                status = vigil_chunk_write_byte(&chunk, (uint8_t)operand->value, instruction->span, program->error);
            }

            if (status != VIGIL_STATUS_OK)
                goto cleanup;
        }
    }

    *out_object = NULL;
    status = vigil_function_object_new(program->registry->runtime, decl->name, decl->name_length, decl->param_count,
                                       decl->return_count, &chunk, out_object, program->error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    vigil_lowered_function_body_free(body);
    return VIGIL_STATUS_OK;

cleanup:
    vigil_chunk_free(&chunk);
    vigil_lowered_function_body_free(body);
    return status;
}
