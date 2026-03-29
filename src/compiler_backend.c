#include <string.h>

#include "internal/vigil_binding.h"
#include "internal/vigil_compiler_backend.h"
#include "internal/vigil_compiler_semantics.h"
#include "internal/vigil_internal.h"

static size_t backend_effective_call_return_count(vigil_parser_type_t return_type, size_t return_count)
{
    return vigil_parser_type_is_void(return_type) ? 0U : return_count;
}

static size_t lowered_operand_byte_size(vigil_lowered_operand_kind_t kind)
{
    return kind == VIGIL_LOWERED_OPERAND_U32 ? 4U : 1U;
}

static size_t lowered_instruction_byte_size(const vigil_lowered_instruction_t *instruction)
{
    size_t size = 1U;
    uint8_t operand_index;

    for (operand_index = 0U; operand_index < instruction->operand_count; operand_index += 1U)
        size += lowered_operand_byte_size(instruction->operands[operand_index].kind);
    return size;
}

static vigil_lowered_instruction_t *lowered_last_instruction(vigil_parser_state_t *state)
{
    vigil_lowered_function_body_t *body = state->lowered_body;

    if (body == NULL || body->instruction_count == 0U)
        return NULL;
    return &body->instructions[body->instruction_count - 1U];
}

void vigil_backend_lowered_replace_last_opcode(vigil_parser_state_t *state, vigil_opcode_t opcode)
{
    vigil_lowered_instruction_t *instruction = lowered_last_instruction(state);

    if (instruction != NULL)
        instruction->opcode = opcode;
}

void vigil_backend_lowered_fuse_last_two_get_local(vigil_parser_state_t *state, vigil_opcode_t opcode)
{
    vigil_lowered_function_body_t *body = state->lowered_body;
    vigil_lowered_instruction_t *left;
    vigil_lowered_instruction_t *right;

    if (body == NULL || body->instruction_count < 2U)
        return;

    left = &body->instructions[body->instruction_count - 2U];
    right = &body->instructions[body->instruction_count - 1U];
    if (left->opcode != VIGIL_OPCODE_GET_LOCAL || right->opcode != VIGIL_OPCODE_GET_LOCAL ||
        left->operand_count != 1U || right->operand_count != 1U)
        return;

    left->opcode = opcode;
    left->operands[1] = right->operands[0];
    left->operand_count = 2U;
    body->instruction_count -= 1U;
}

vigil_status_t vigil_backend_lowered_patch_u32(vigil_parser_state_t *state, size_t operand_offset, uint32_t value)
{
    vigil_lowered_function_body_t *body = state->lowered_body;
    size_t offset = 0U;
    size_t instruction_index;
    int retry = 0;

    if (body == NULL)
        return VIGIL_STATUS_OK;

retry_scan:
    offset = 0U;
    for (instruction_index = 0U; instruction_index < body->instruction_count; instruction_index += 1U)
    {
        vigil_lowered_instruction_t *instruction = &body->instructions[instruction_index];
        size_t instruction_offset = offset;
        size_t operand_offset_in_instruction = instruction_offset + 1U;
        uint8_t operand_index;

        for (operand_index = 0U; operand_index < instruction->operand_count; operand_index += 1U)
        {
            size_t operand_size = lowered_operand_byte_size(instruction->operands[operand_index].kind);

            if (instruction->operands[operand_index].kind == VIGIL_LOWERED_OPERAND_U32 &&
                operand_offset == operand_offset_in_instruction)
            {
                instruction->operands[operand_index].value = value;
                return VIGIL_STATUS_OK;
            }

            operand_offset_in_instruction += operand_size;
        }

        offset += lowered_instruction_byte_size(instruction);
    }

    if (!retry)
    {
        vigil_status_t sync_status =
            vigil_lowered_function_body_sync_from_chunk(body, &state->chunk, state->program->error);
        if (sync_status != VIGIL_STATUS_OK)
            return sync_status;
        retry = 1;
        goto retry_scan;
    }

    vigil_error_set_literal(state->program->error, VIGIL_STATUS_INTERNAL, "lowered jump patch offset is out of range");
    return VIGIL_STATUS_INTERNAL;
}

static void lowered_remove_trailing_instructions(vigil_parser_state_t *state, size_t remove_count)
{
    vigil_lowered_function_body_t *body = state->lowered_body;

    if (body == NULL || remove_count > body->instruction_count)
        return;
    body->instruction_count -= remove_count;
}

void vigil_backend_lowered_rewrite_tail_call_self(vigil_parser_state_t *state)
{
    vigil_lowered_function_body_t *body = state->lowered_body;
    vigil_lowered_instruction_t *call_instruction;

    if (body == NULL || body->instruction_count < 2U)
        return;

    call_instruction = &body->instructions[body->instruction_count - 2U];
    if (call_instruction->opcode != VIGIL_OPCODE_CALL_SELF || call_instruction->operand_count != 2U)
        return;

    call_instruction->opcode = VIGIL_OPCODE_TAIL_CALL;
    call_instruction->operands[1].value = call_instruction->operands[0].value;
    call_instruction->operands[0].value = (uint32_t)state->function_index;
    lowered_remove_trailing_instructions(state, 1U);
}

void vigil_backend_lowered_rewrite_tail_call(vigil_parser_state_t *state)
{
    vigil_lowered_function_body_t *body = state->lowered_body;
    vigil_lowered_instruction_t *call_instruction;

    if (body == NULL || body->instruction_count < 2U)
        return;

    call_instruction = &body->instructions[body->instruction_count - 2U];
    if (call_instruction->opcode != VIGIL_OPCODE_CALL || call_instruction->operand_count != 3U)
        return;

    call_instruction->opcode = VIGIL_OPCODE_TAIL_CALL;
    call_instruction->operand_count = 2U;
    lowered_remove_trailing_instructions(state, 1U);
}

void vigil_backend_lowered_rewrite_increment_local(vigil_parser_state_t *state, vigil_opcode_t opcode, int8_t delta)
{
    vigil_lowered_function_body_t *body = state->lowered_body;
    vigil_lowered_instruction_t *instruction;

    if (body == NULL || body->instruction_count < 5U)
        return;

    instruction = &body->instructions[body->instruction_count - 5U];
    instruction->opcode = opcode;
    instruction->operand_count = 2U;
    instruction->operands[1].kind = VIGIL_LOWERED_OPERAND_I8;
    instruction->operands[1].value = (uint8_t)delta;
    lowered_remove_trailing_instructions(state, 4U);
}

void vigil_backend_lowered_rewrite_locals_store(vigil_parser_state_t *state, vigil_opcode_t opcode)
{
    vigil_lowered_function_body_t *body = state->lowered_body;
    vigil_lowered_instruction_t *instruction;
    vigil_lowered_instruction_t *set_local;

    if (body == NULL || body->instruction_count < 3U)
        return;

    instruction = &body->instructions[body->instruction_count - 3U];
    set_local = &body->instructions[body->instruction_count - 2U];
    if (set_local->operand_count != 1U || instruction->operand_count != 2U)
        return;

    instruction->opcode = opcode;
    instruction->operands[2] = instruction->operands[1];
    instruction->operands[1] = instruction->operands[0];
    instruction->operands[0] = set_local->operands[0];
    instruction->operand_count = 3U;
    lowered_remove_trailing_instructions(state, 2U);
}

void vigil_backend_lowered_rewrite_f64_store(vigil_parser_state_t *state, vigil_opcode_t opcode)
{
    vigil_lowered_function_body_t *body = state->lowered_body;
    vigil_lowered_instruction_t *instruction;
    vigil_lowered_instruction_t *set_local;

    if (body == NULL || body->instruction_count < 3U)
        return;

    instruction = &body->instructions[body->instruction_count - 3U];
    set_local = &body->instructions[body->instruction_count - 2U];
    if (set_local->operand_count != 1U)
        return;

    instruction->opcode = opcode;
    instruction->operands[0] = set_local->operands[0];
    instruction->operand_count = 1U;
    lowered_remove_trailing_instructions(state, 2U);
}

static int lowered_find_instruction_index_by_offset(const vigil_lowered_function_body_t *body, size_t target_offset,
                                                    size_t *out_index)
{
    size_t offset = 0U;
    size_t instruction_index;

    if (body == NULL)
        return 0;

    for (instruction_index = 0U; instruction_index < body->instruction_count; instruction_index += 1U)
    {
        if (offset == target_offset)
        {
            *out_index = instruction_index;
            return 1;
        }
        offset += lowered_instruction_byte_size(&body->instructions[instruction_index]);
    }

    return 0;
}

void vigil_backend_lowered_rewrite_forloop(vigil_parser_state_t *state, size_t loop_start, vigil_opcode_t opcode,
                                           uint8_t cmp_type)
{
    vigil_lowered_function_body_t *body = state->lowered_body;
    size_t condition_index;
    vigil_lowered_instruction_t *condition_constant;
    vigil_lowered_instruction_t *forloop_instruction;
    size_t body_start_offset;
    size_t forloop_end;
    uint32_t back_off;

    if (body == NULL || body->instruction_count < 2U)
        return;
    if (!lowered_find_instruction_index_by_offset(body, loop_start, &condition_index))
        return;
    if (condition_index + 1U >= body->instruction_count)
        return;

    condition_constant = &body->instructions[condition_index + 1U];
    forloop_instruction = &body->instructions[body->instruction_count - 2U];
    body_start_offset = loop_start + lowered_instruction_byte_size(&body->instructions[condition_index]) +
                        lowered_instruction_byte_size(condition_constant);
    if (body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_LESS_I32_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_LESS_EQUAL_I32_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_GREATER_I32_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_GREATER_EQUAL_I32_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_NOT_EQUAL_I32_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_LESS_I64_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_LESS_EQUAL_I64_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_GREATER_I64_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_GREATER_EQUAL_I64_JUMP_IF_FALSE ||
        body->instructions[condition_index + 2U].opcode == VIGIL_OPCODE_NOT_EQUAL_I64_JUMP_IF_FALSE)
    {
        body_start_offset += lowered_instruction_byte_size(&body->instructions[condition_index + 2U]) +
                             lowered_instruction_byte_size(&body->instructions[condition_index + 3U]);
    }
    else
    {
        body_start_offset += lowered_instruction_byte_size(&body->instructions[condition_index + 2U]) +
                             lowered_instruction_byte_size(&body->instructions[condition_index + 3U]) +
                             lowered_instruction_byte_size(&body->instructions[condition_index + 4U]) +
                             lowered_instruction_byte_size(&body->instructions[condition_index + 5U]);
    }

    forloop_end = state->chunk.code.length;
    back_off = (uint32_t)(forloop_end - body_start_offset);

    forloop_instruction->opcode = opcode;
    forloop_instruction->operand_count = 5U;
    forloop_instruction->operands[1].kind = VIGIL_LOWERED_OPERAND_I8;
    forloop_instruction->operands[2] = condition_constant->operands[0];
    forloop_instruction->operands[3].kind = VIGIL_LOWERED_OPERAND_U8;
    forloop_instruction->operands[3].value = cmp_type;
    forloop_instruction->operands[4].kind = VIGIL_LOWERED_OPERAND_U32;
    forloop_instruction->operands[4].value = back_off;
    lowered_remove_trailing_instructions(state, 1U);
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
