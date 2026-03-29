#include <string.h>

#include "internal/vigil_binding.h"
#include "internal/vigil_compiler_backend.h"
#include "internal/vigil_compiler_semantics.h"
#include "internal/vigil_internal.h"

static size_t backend_effective_call_return_count(vigil_parser_type_t return_type, size_t return_count)
{
    return vigil_parser_type_is_void(return_type) ? 0U : return_count;
}

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

static vigil_status_t backend_compile_synthetic_constructor(vigil_program_state_t *program, size_t function_index,
                                                            size_t class_index, size_t init_function_index)
{
    vigil_status_t status;
    vigil_parser_state_t state;
    vigil_function_decl_t *decl;
    const vigil_class_decl_t *class_decl;
    vigil_object_t *object;
    size_t field_index;
    size_t param_index;
    uint32_t init_arg_count;

    decl = &program->functions.functions[function_index];
    if (class_index >= program->class_count || init_function_index >= program->functions.count)
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_INTERNAL, "synthetic constructor metadata is invalid");
        return VIGIL_STATUS_INTERNAL;
    }

    class_decl = &program->classes[class_index];
    if (decl->param_count > UINT32_MAX - 1U)
    {
        vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY, "constructor arity overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    init_arg_count = (uint32_t)(decl->param_count + 1U);

    memset(&state, 0, sizeof(state));
    state.program = program;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
    vigil_chunk_init(&state.chunk, program->registry->runtime);
    vigil_binding_scope_stack_init(&state.locals, program->registry->runtime);

    for (field_index = 0U; field_index < class_decl->field_count; field_index += 1U)
    {
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_NIL, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
    }
    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_NEW_INSTANCE, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_u32(&state, (uint32_t)class_index, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_u32(&state, (uint32_t)class_decl->field_count, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_DUP, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    for (param_index = 0U; param_index < decl->param_count; param_index += 1U)
    {
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_GET_LOCAL, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
        status = vigil_parser_emit_u32(&state, (uint32_t)param_index, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
    }

    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_CALL, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_u32(&state, (uint32_t)init_function_index, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_u32(&state, init_arg_count, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_u32(
        &state,
        (uint32_t)backend_effective_call_return_count(program->functions.functions[init_function_index].return_type,
                                                      program->functions.functions[init_function_index].return_count),
        decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_RETURN, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_u32(&state, (uint32_t)decl->return_count, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    object = NULL;
    status = vigil_function_object_new(program->registry->runtime, decl->name, decl->name_length, decl->param_count,
                                       decl->return_count, &state.chunk, &object, program->error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    vigil_parser_state_free(&state);
    decl->object = object;
    return VIGIL_STATUS_OK;

cleanup:
    vigil_chunk_free(&state.chunk);
    vigil_parser_state_free(&state);
    return status;
}

static vigil_status_t backend_compile_extern_fn(vigil_program_state_t *program, size_t function_index,
                                                const vigil_extern_fn_decl_t *ext)
{
    vigil_status_t status;
    vigil_parser_state_t state;
    vigil_function_decl_t *decl;
    vigil_object_t *object;

    decl = &program->functions.functions[function_index];

    memset(&state, 0, sizeof(state));
    state.program = program;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
    vigil_chunk_init(&state.chunk, program->registry->runtime);
    vigil_binding_scope_stack_init(&state.locals, program->registry->runtime);

    for (size_t i = 0; i < decl->param_count; i += 1U)
    {
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_GET_LOCAL, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
        status = vigil_parser_emit_u32(&state, (uint32_t)i, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
    }

    {
        size_t lib_len = strlen(ext->lib_path);
        size_t name_len = strlen(ext->c_name);
        size_t sig_len = strlen(ext->sig);
        size_t desc_len = lib_len + 1U + name_len + 1U + sig_len;
        char *desc = NULL;
        const vigil_allocator_t *alloc = vigil_runtime_allocator(program->registry->runtime);
        vigil_object_t *str_obj = NULL;
        vigil_value_t str_val;
        size_t const_idx;

        desc = (char *)alloc->allocate(alloc->user_data, desc_len + 1U);
        if (desc == NULL)
        {
            status = VIGIL_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }

        char *p = desc;
        memcpy(p, ext->lib_path, lib_len);
        p += lib_len;
        *p++ = '\0';
        memcpy(p, ext->c_name, name_len);
        p += name_len;
        *p++ = '\0';
        memcpy(p, ext->sig, sig_len);
        p += sig_len;
        *p = '\0';

        status = vigil_string_object_new(program->registry->runtime, desc, desc_len, &str_obj, program->error);
        alloc->deallocate(alloc->user_data, desc);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;

        vigil_value_init_object(&str_val, &str_obj);
        status = vigil_chunk_add_constant(&state.chunk, &str_val, &const_idx, program->error);
        vigil_value_release(&str_val);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;

        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_CALL_EXTERN, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
        status = vigil_parser_emit_u32(&state, (uint32_t)const_idx, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
        status = vigil_parser_emit_u32(&state, (uint32_t)decl->param_count, decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
        status = vigil_parser_emit_u32(
            &state, (uint32_t)backend_effective_call_return_count(decl->return_type, decl->return_count),
            decl->name_span);
        if (status != VIGIL_STATUS_OK)
            goto cleanup;
    }

    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_RETURN, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;
    status = vigil_parser_emit_u32(&state, vigil_parser_type_is_void(decl->return_type) ? 0U : 1U, decl->name_span);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    object = NULL;
    status = vigil_function_object_new(program->registry->runtime, decl->name, decl->name_length, decl->param_count,
                                       decl->return_count, &state.chunk, &object, program->error);
    if (status != VIGIL_STATUS_OK)
        goto cleanup;

    vigil_parser_state_free(&state);
    decl->object = object;
    return VIGIL_STATUS_OK;

cleanup:
    vigil_chunk_free(&state.chunk);
    vigil_parser_state_free(&state);
    return status;
}

vigil_status_t vigil_compile_backend_try_materialize_special_function(vigil_program_state_t *program,
                                                                      size_t function_index, int *handled)
{
    size_t class_index;

    *handled = 0;
    for (class_index = 0U; class_index < program->class_count; class_index += 1U)
    {
        const vigil_class_decl_t *class_decl = &program->classes[class_index];
        const vigil_class_method_t *init_method = NULL;

        if (class_decl->constructor_function_index != function_index)
            continue;
        if (!vigil_class_decl_find_method(class_decl, "init", 4U, NULL, &init_method) || init_method == NULL)
        {
            vigil_error_set_literal(program->error, VIGIL_STATUS_INTERNAL, "class init declaration is missing");
            return VIGIL_STATUS_INTERNAL;
        }

        *handled = 1;
        return backend_compile_synthetic_constructor(program, function_index, class_index, init_method->function_index);
    }

    for (size_t ei = 0U; ei < program->extern_fn_count; ei += 1U)
    {
        if (program->extern_fns[ei].function_index != function_index)
            continue;
        *handled = 1;
        return backend_compile_extern_fn(program, function_index, &program->extern_fns[ei]);
    }

    return VIGIL_STATUS_OK;
}
