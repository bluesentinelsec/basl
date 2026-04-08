#include "internal/vigil_aot.h"

/* MIR requires function-to-object pointer casts for MIR_load_external
   and MIR_gen. This is inherent to any AOT/JIT system and unavoidable
   in ISO C. Suppress the pedantic warning for this translation unit. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "internal/vigil_regvm.h"
#include "platform/platform.h"

#if defined(VIGIL_ENABLE_AOT)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
#endif
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4244 4267)
#endif
#include "mir-gen.h"
#include "mir.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

#define VIGIL_AOT_CACHE_STATE_EMPTY 0LL
#define VIGIL_AOT_CACHE_STATE_BUILDING 1LL
#define VIGIL_AOT_CACHE_STATE_READY 2LL

#if defined(VIGIL_ENABLE_AOT)
static vigil_status_t vigil_aot_grow_frames(vigil_vm_t *vm, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= vm->frame_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = vm->frame_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*vm->frames)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm frame allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (vm->frames == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(vm->runtime, next_capacity * sizeof(*vm->frames), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        memset(memory, 0, next_capacity * sizeof(*vm->frames));
    }
    else
    {
        memory = vm->frames;
        status = vigil_runtime_realloc(vm->runtime, &memory, next_capacity * sizeof(*vm->frames), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        memset((vigil_vm_frame_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*vm->frames));
    }

    vm->frames = (vigil_vm_frame_t *)memory;
    vm->frame_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_aot_prepare_frame(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, vigil_error_t *error)
{
    vigil_vm_frame_t *frame;
    size_t base;
    vigil_status_t status;

    if (vm == NULL || rc == NULL || vm->frame_count == 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "AOT frame preparation requires an active frame");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    frame = &vm->frames[vm->frame_count - 1U];
    base = frame->base_slot;

    if (vm->stack_capacity < base + (size_t)rc->max_registers + 16U)
    {
        status = vigil_vm_grow_stack(vm, base + (size_t)rc->max_registers + 16U, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    if (vm->stack_count < base + (size_t)rc->max_registers)
    {
        vm->stack_count = base + (size_t)rc->max_registers;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_aot_numeric_call_self(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, uint8_t ret,
                                                  uint8_t arg_count, uint8_t arg_base_r, vigil_error_t *error)
{
    vigil_vm_frame_t *frame;
    vigil_vm_frame_t *child;
    vigil_aot_cache_t *cache = NULL;
    size_t base;
    size_t arg_base;
    vigil_status_t status;

    if (vm == NULL || vm->frame_count == 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "AOT self-call requires an active frame");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    frame = &vm->frames[vm->frame_count - 1U];
    base = frame->base_slot;
    arg_base = base + (size_t)arg_base_r;
    (void)arg_count;

    status = vigil_aot_grow_frames(vm, vm->frame_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    child = &vm->frames[vm->frame_count++];
    memset(child, 0, sizeof(*child));
    child->callable = frame->callable;
    child->function = frame->function;
    child->chunk = frame->chunk;
    child->base_slot = arg_base;

    if (vm->stack_capacity < arg_base + (size_t)rc->max_registers + 16U)
    {
        status = vigil_vm_grow_stack(vm, arg_base + (size_t)rc->max_registers + 16U, error);
        if (status != VIGIL_STATUS_OK)
        {
            vm->frame_count -= 1U;
            return status;
        }
    }

    if (vm->stack_count < arg_base + (size_t)rc->max_registers)
    {
        vm->stack_count = arg_base + (size_t)rc->max_registers;
    }

    status = vigil_aot_ensure(rc, &cache, error);
    if (status != VIGIL_STATUS_OK)
    {
        vm->frame_count -= 1U;
        return status;
    }
    if (cache == NULL || cache->entry == NULL)
    {
        vm->frame_count -= 1U;
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "AOT self-call cache missing native entry");
        return VIGIL_STATUS_INTERNAL;
    }

    status = cache->entry(vm, &vm->stack[base + (size_t)ret], error);
    vm->frame_count -= 1U;
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (vm->stack_count < base + (size_t)ret + 1U)
    {
        vm->stack_count = base + (size_t)ret + 1U;
    }
    if (vm->stack_count < base + (size_t)rc->max_registers)
    {
        vm->stack_count = base + (size_t)rc->max_registers;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_aot_numeric_call(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, uint8_t arg_base_r,
                                             uint8_t func_idx, uint8_t arg_count, vigil_error_t *error)
{
    vigil_vm_frame_t *frame;
    const vigil_object_t *callee;
    size_t base;
    size_t arg_base;

    (void)rc;

    if (vm == NULL || vm->frame_count == 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "AOT sibling call requires an active frame");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    frame = &vm->frames[vm->frame_count - 1U];
    callee = vigil_vm_function_sibling(frame->function, (size_t)func_idx);
    if (callee == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "missing sibling callee");
        return VIGIL_STATUS_INTERNAL;
    }

    base = frame->base_slot;
    arg_base = base + (size_t)arg_base_r;
    vm->stack_count = arg_base + (size_t)arg_count;
    return vigil_vm_execute_call(vm, callee, (size_t)arg_count, error);
}

static vigil_status_t vigil_aot_numeric_fail(vigil_status_t status, const char *message, vigil_error_t *error)
{
    vigil_error_set_literal(error, status, message);
    return status;
}

static size_t vigil_aot_instr_words(const vigil_reg_chunk_t *rc, size_t ip)
{
    uint8_t op;

    if (rc == NULL || ip >= rc->code_count)
    {
        return 1U;
    }

    op = VREG_GET_OP(rc->code[ip]);
    switch (op)
    {
    case VREG_CALL_SELF:
        return 2U;
    default:
        return 1U;
    }
}

static int vigil_aot_is_numeric_constant(const vigil_value_t *value)
{
    if (value == NULL)
    {
        return 0;
    }

    return !vigil_nanbox_has_object(*value);
}

static int vigil_aot_chunk_is_numeric_subset(const vigil_reg_chunk_t *rc)
{
    size_t ip = 0U;

    if (rc == NULL)
    {
        return 0;
    }
    if (rc->code == NULL || rc->code_count == 0U)
    {
        return 0;
    }

    while (ip < rc->code_count)
    {
        vigil_reg_instr_t instr = rc->code[ip];
        uint8_t op = VREG_GET_OP(instr);

        switch (op)
        {
        case VREG_MOVE:
        case VREG_LOAD_K:
        case VREG_LOAD_NIL:
        case VREG_LOAD_TRUE:
        case VREG_LOAD_FALSE:
        case VREG_ADD_I32:
        case VREG_SUB_I32:
        case VREG_ADDI:
        case VREG_SUBI:
        case VREG_JMP:
        case VREG_TEST:
        case VREG_LT_I32_JMP:
        case VREG_LE_I32_JMP:
        case VREG_GT_I32_JMP:
        case VREG_GE_I32_JMP:
        case VREG_EQ_I32_JMP:
        case VREG_NE_I32_JMP:
        case VREG_LT_I32_IMM_JMP:
        case VREG_CALL:
        case VREG_CALL_SELF:
        case VREG_RETURN:
        case VREG_RELEASE:
            break;
        default:
            return 0;
        }

        if (op == VREG_LOAD_K)
        {
            const vigil_value_t *k = VIGIL_VM_CHUNK_CONSTANT(rc->stack_chunk, (size_t)VREG_GET_Bx(instr));
            if (!vigil_aot_is_numeric_constant(k))
            {
                return 0;
            }
        }

        ip += vigil_aot_instr_words(rc, ip);
    }

    return 1;
}

static vigil_status_t vigil_aot_build_wrapper(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache,
                                              vigil_error_t *error);

static void vigil_aot_emit_reload_regs(MIR_context_t ctx, MIR_item_t func, MIR_reg_t regs_reg, MIR_reg_t vm_reg,
                                       MIR_reg_t frame_count_reg, MIR_reg_t frames_reg, MIR_reg_t frame_ptr_reg,
                                       MIR_reg_t base_reg, MIR_reg_t scratch_reg)
{
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, frame_count_reg),
                                 MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, frame_count), vm_reg, 0U, 1U)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_SUB, MIR_new_reg_op(ctx, frame_count_reg),
                                 MIR_new_reg_op(ctx, frame_count_reg), MIR_new_int_op(ctx, 1)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, frames_reg),
                                 MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, frames), vm_reg, 0U, 1U)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MUL, MIR_new_reg_op(ctx, scratch_reg),
                                 MIR_new_reg_op(ctx, frame_count_reg),
                                 MIR_new_int_op(ctx, (int64_t)sizeof(vigil_vm_frame_t))));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, frame_ptr_reg), MIR_new_reg_op(ctx, frames_reg),
                                 MIR_new_reg_op(ctx, scratch_reg)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, base_reg),
                                 MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_frame_t, base_slot), frame_ptr_reg,
                                                0U, 1U)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, regs_reg),
                                 MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, stack), vm_reg, 0U, 1U)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_LSH, MIR_new_reg_op(ctx, scratch_reg), MIR_new_reg_op(ctx, base_reg),
                                 MIR_new_int_op(ctx, 3)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, regs_reg), MIR_new_reg_op(ctx, regs_reg),
                                 MIR_new_reg_op(ctx, scratch_reg)));
}

static void vigil_aot_emit_store_constant(MIR_context_t ctx, MIR_item_t func, MIR_reg_t regs_reg, uint8_t dst,
                                          vigil_value_t value)
{
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV,
                                 MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)dst * sizeof(vigil_value_t)),
                                                regs_reg, 0U, 1U),
                                 MIR_new_uint_op(ctx, value)));
}

static void vigil_aot_emit_move(MIR_context_t ctx, MIR_item_t func, MIR_reg_t regs_reg, MIR_reg_t tmp_reg, uint8_t dst,
                                uint8_t src)
{
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp_reg),
                                 MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)src * sizeof(vigil_value_t)),
                                                regs_reg, 0U, 1U)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV,
                                 MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)dst * sizeof(vigil_value_t)),
                                                regs_reg, 0U, 1U),
                                 MIR_new_reg_op(ctx, tmp_reg)));
}

static void vigil_aot_emit_i32_decode(MIR_context_t ctx, MIR_item_t func, MIR_reg_t out_reg, MIR_reg_t regs_reg,
                                      uint8_t src)
{
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, out_reg),
                                 MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)src * sizeof(vigil_value_t)),
                                                regs_reg, 0U, 1U)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_EXT32, MIR_new_reg_op(ctx, out_reg), MIR_new_reg_op(ctx, out_reg)));
}

static void vigil_aot_emit_i32_encode_store(MIR_context_t ctx, MIR_item_t func, MIR_reg_t value_reg, MIR_reg_t tmp_reg,
                                            MIR_reg_t regs_reg, uint8_t dst)
{
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, tmp_reg), MIR_new_reg_op(ctx, value_reg),
                                 MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, tmp_reg), MIR_new_reg_op(ctx, tmp_reg),
                                 MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_MOV,
                                 MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)dst * sizeof(vigil_value_t)),
                                                regs_reg, 0U, 1U),
                                 MIR_new_reg_op(ctx, tmp_reg)));
}

static vigil_status_t vigil_aot_build_numeric(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache,
                                              vigil_error_t *error)
{
    vigil_aot_cache_t *cache = NULL;
    MIR_context_t ctx = NULL;
    MIR_module_t module;
    MIR_item_t func;
    MIR_item_t prepare_proto;
    MIR_item_t prepare_import;
    MIR_item_t call_self_proto;
    MIR_item_t call_self_import;
    MIR_item_t call_proto;
    MIR_item_t call_import;
    MIR_item_t fail_proto;
    MIR_item_t fail_import;
    MIR_type_t res_type = MIR_T_I64;
    MIR_reg_t status_reg;
    MIR_reg_t vm_reg;
    MIR_reg_t out_reg;
    MIR_reg_t error_reg;
    MIR_reg_t regs_reg;
    MIR_reg_t tmp0_reg;
    MIR_reg_t tmp1_reg;
    MIR_reg_t tmp2_reg;
    MIR_reg_t tmp3_reg;
    MIR_label_t *labels = NULL;
    MIR_label_t error_label;
    char module_name[64];
    char func_name[64];
    size_t ip;

    if (out_cache == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_cache must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_cache = NULL;
    cache = (vigil_aot_cache_t *)calloc(1U, sizeof(*cache));
    if (cache == NULL)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    labels = (MIR_label_t *)calloc(rc->code_count + 1U, sizeof(*labels));
    if (labels == NULL)
    {
        free(cache); /* alloc-check: exempt - calloc-allocated */
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    ctx = MIR_init();
    cache->context = ctx;

    (void)snprintf(module_name, sizeof(module_name), "vigil_aot_%p", (const void *)rc);
    (void)snprintf(func_name, sizeof(func_name), "vigil_entry_%p", (const void *)rc);

    module = MIR_new_module(ctx, module_name);
    prepare_proto = MIR_new_proto(ctx, "vigil_aot_prepare_frame_proto", 1U, &res_type, 3U, MIR_T_P, "vm", MIR_T_P,
                                  "rc", MIR_T_P, "error");
    prepare_import = MIR_new_import(ctx, "vigil_aot_prepare_frame");
    call_self_proto = MIR_new_proto(ctx, "vigil_aot_numeric_call_self_proto", 1U, &res_type, 6U, MIR_T_P, "vm",
                                    MIR_T_P, "rc", MIR_T_I64, "ret", MIR_T_I64, "arg_count", MIR_T_I64, "arg_base",
                                    MIR_T_P, "error");
    call_self_import = MIR_new_import(ctx, "vigil_aot_numeric_call_self");
    call_proto = MIR_new_proto(ctx, "vigil_aot_numeric_call_proto", 1U, &res_type, 6U, MIR_T_P, "vm", MIR_T_P, "rc",
                               MIR_T_I64, "arg_base", MIR_T_I64, "func_idx", MIR_T_I64, "arg_count", MIR_T_P,
                               "error");
    call_import = MIR_new_import(ctx, "vigil_aot_numeric_call");
    fail_proto = MIR_new_proto(ctx, "vigil_aot_numeric_fail_proto", 1U, &res_type, 3U, MIR_T_I64, "status", MIR_T_P,
                               "message", MIR_T_P, "error");
    fail_import = MIR_new_import(ctx, "vigil_aot_numeric_fail");
    func = MIR_new_func(ctx, func_name, 1U, &res_type, 3U, MIR_T_P, "vm", MIR_T_P, "out_value", MIR_T_P, "error");

    status_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "$status");
    vm_reg = MIR_reg(ctx, "vm", func->u.func);
    out_reg = MIR_reg(ctx, "out_value", func->u.func);
    error_reg = MIR_reg(ctx, "error", func->u.func);
    regs_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "$regs");
    tmp0_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "$tmp0");
    tmp1_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "$tmp1");
    tmp2_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "$tmp2");
    tmp3_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "$tmp3");

    for (ip = 0U; ip <= rc->code_count; ++ip)
    {
        labels[ip] = MIR_new_label(ctx);
    }
    error_label = MIR_new_label(ctx);

    MIR_append_insn(ctx, func,
                    MIR_new_call_insn(ctx, 6U, MIR_new_ref_op(ctx, prepare_proto),
                                      MIR_new_ref_op(ctx, prepare_import), MIR_new_reg_op(ctx, status_reg),
                                      MIR_new_reg_op(ctx, vm_reg), MIR_new_uint_op(ctx, (uint64_t)(uintptr_t)rc),
                                      MIR_new_reg_op(ctx, error_reg)));
    MIR_append_insn(ctx, func,
                    MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, error_label),
                                 MIR_new_reg_op(ctx, status_reg), MIR_new_int_op(ctx, VIGIL_STATUS_OK)));
    vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg, status_reg);

    for (ip = 0U; ip < rc->code_count; ip += vigil_aot_instr_words(rc, ip))
    {
        vigil_reg_instr_t instr = rc->code[ip];
        uint8_t op = VREG_GET_OP(instr);
        int16_t off;

        MIR_append_insn(ctx, func, labels[ip]);

        switch (op)
        {
        case VREG_MOVE:
            vigil_aot_emit_move(ctx, func, regs_reg, tmp0_reg, VREG_GET_A(instr), VREG_GET_B(instr));
            break;
        case VREG_LOAD_K: {
            const vigil_value_t *k = VIGIL_VM_CHUNK_CONSTANT(rc->stack_chunk, (size_t)VREG_GET_Bx(instr));
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), k != NULL ? *k : VIGIL_NANBOX_NIL);
            break;
        }
        case VREG_LOAD_NIL:
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_NIL);
            break;
        case VREG_LOAD_TRUE:
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_TRUE);
            break;
        case VREG_LOAD_FALSE:
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_FALSE);
            break;
        case VREG_ADD_I32:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADDO, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_SUB_I32:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_SUBO, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_ADDI:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADDO, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_int_op(ctx, (int32_t)(int8_t)VREG_GET_C(instr))));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_SUBI:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_SUBO, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_int_op(ctx, (int32_t)(int8_t)VREG_GET_C(instr))));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_MUL_I32:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MULO, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_DIV_I32:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[rc->code_count]),
                                         MIR_new_reg_op(ctx, tmp1_reg), MIR_new_int_op(ctx, 0)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_DIV, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_MOD_I32:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[rc->code_count]),
                                         MIR_new_reg_op(ctx, tmp1_reg), MIR_new_int_op(ctx, 0)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOD, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_NEG:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_NEG, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_NOT: {
            MIR_label_t not_true = MIR_new_label(ctx);
            MIR_label_t not_done = MIR_new_label(ctx);
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64,
                                                        (MIR_disp_t)((size_t)VREG_GET_B(instr) * sizeof(vigil_value_t)),
                                                        regs_reg, 0U, 1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, not_true),
                                         MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_FALSE)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, not_true),
                                         MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_NIL)));
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_FALSE);
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, not_done)));
            MIR_append_insn(ctx, func, not_true);
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_TRUE);
            MIR_append_insn(ctx, func, not_done);
            break;
        }
        case VREG_BNOT:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_XOR, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_int_op(ctx, -1)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_BAND:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_BOR:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_BXOR:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_XOR, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_SHL:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_LSH, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_SHR:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_RSH, MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            vigil_aot_emit_i32_encode_store(ctx, func, tmp2_reg, tmp3_reg, regs_reg, VREG_GET_A(instr));
            break;
        case VREG_EQ_I32:
        case VREG_NE_I32:
        case VREG_LT_I32:
        case VREG_LE_I32:
        case VREG_GT_I32:
        case VREG_GE_I32: {
            MIR_label_t cmp_true = MIR_new_label(ctx);
            MIR_label_t cmp_done = MIR_new_label(ctx);
            MIR_insn_code_t cmp_op;
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_B(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_C(instr));
            switch (op) {
            case VREG_EQ_I32: cmp_op = MIR_BEQ; break;
            case VREG_NE_I32: cmp_op = MIR_BNE; break;
            case VREG_LT_I32: cmp_op = MIR_BLT; break;
            case VREG_LE_I32: cmp_op = MIR_BLE; break;
            case VREG_GT_I32: cmp_op = MIR_BGT; break;
            default:          cmp_op = MIR_BGE; break;
            }
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, cmp_op, MIR_new_label_op(ctx, cmp_true),
                                         MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_FALSE);
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, cmp_done)));
            MIR_append_insn(ctx, func, cmp_true);
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_TRUE);
            MIR_append_insn(ctx, func, cmp_done);
            break;
        }
        case VREG_DUP:
            vigil_aot_emit_move(ctx, func, regs_reg, tmp0_reg, VREG_GET_A(instr), VREG_GET_B(instr));
            break;
        case VREG_TESTSET: {
            MIR_label_t ts_skip = MIR_new_label(ctx);
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64,
                                                        (MIR_disp_t)((size_t)VREG_GET_B(instr) * sizeof(vigil_value_t)),
                                                        regs_reg, 0U, 1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, ts_skip),
                                         MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_FALSE)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, ts_skip),
                                         MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_NIL)));
            vigil_aot_emit_move(ctx, func, regs_reg, tmp0_reg, VREG_GET_A(instr), VREG_GET_B(instr));
            MIR_append_insn(ctx, func, ts_skip);
            break;
        }
        case VREG_JMP:
            off = VREG_GET_sBx(instr);
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_JMP,
                                         MIR_new_label_op(ctx, labels[(size_t)((int32_t)ip + 1 + (int32_t)off)])));
            continue;
        case VREG_TEST:
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64,
                                                        (MIR_disp_t)((size_t)VREG_GET_A(instr) *
                                                                     sizeof(vigil_value_t)),
                                                        regs_reg, 0U, 1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[ip + 1U]),
                                         MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_FALSE)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[ip + 1U]),
                                         MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_NIL)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, labels[ip + 2U])));
            continue;
        case VREG_LT_I32_JMP:
        case VREG_LE_I32_JMP:
        case VREG_GT_I32_JMP:
        case VREG_GE_I32_JMP:
        case VREG_EQ_I32_JMP:
        case VREG_NE_I32_JMP:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_A(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_B(instr));
            switch (op)
            {
            case VREG_LT_I32_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BLT, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            case VREG_LE_I32_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BLE, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            case VREG_GT_I32_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BGT, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            case VREG_GE_I32_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BGE, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            case VREG_EQ_I32_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            default:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            }
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, labels[ip + 1U])));
            continue;
        case VREG_LT_I32_IMM_JMP:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_A(instr));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BLT, MIR_new_label_op(ctx, labels[ip + 2U]),
                                         MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_int_op(ctx, (int32_t)(int8_t)VREG_GET_C(instr))));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, labels[ip + 1U])));
            continue;
        case VREG_LT_I64_JMP:
        case VREG_LE_I64_JMP:
        case VREG_GT_I64_JMP:
        case VREG_GE_I64_JMP:
        case VREG_EQ_I64_JMP:
        case VREG_NE_I64_JMP:
            vigil_aot_emit_i32_decode(ctx, func, tmp0_reg, regs_reg, VREG_GET_A(instr));
            vigil_aot_emit_i32_decode(ctx, func, tmp1_reg, regs_reg, VREG_GET_B(instr));
            switch (op)
            {
            case VREG_LT_I64_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BLT, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            case VREG_LE_I64_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BLE, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            case VREG_GT_I64_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BGT, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            case VREG_GE_I64_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BGE, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            case VREG_EQ_I64_JMP:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            default:
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, labels[ip + 2U]),
                                             MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
                break;
            }
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, labels[ip + 1U])));
            continue;
        case VREG_CALL:
            MIR_append_insn(ctx, func,
                            MIR_new_call_insn(ctx, 9U, MIR_new_ref_op(ctx, call_proto),
                                              MIR_new_ref_op(ctx, call_import), MIR_new_reg_op(ctx, status_reg),
                                              MIR_new_reg_op(ctx, vm_reg),
                                              MIR_new_uint_op(ctx, (uint64_t)(uintptr_t)rc),
                                              MIR_new_int_op(ctx, VREG_GET_A(instr)),
                                              MIR_new_int_op(ctx, VREG_GET_B(instr)),
                                              MIR_new_int_op(ctx, VREG_GET_C(instr)),
                                              MIR_new_reg_op(ctx, error_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, error_label),
                                         MIR_new_reg_op(ctx, status_reg), MIR_new_int_op(ctx, VIGIL_STATUS_OK)));
            vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg,
                                       status_reg);
            break;
        case VREG_CALL_SELF:
            MIR_append_insn(ctx, func,
                            MIR_new_call_insn(ctx, 9U, MIR_new_ref_op(ctx, call_self_proto),
                                              MIR_new_ref_op(ctx, call_self_import), MIR_new_reg_op(ctx, status_reg),
                                              MIR_new_reg_op(ctx, vm_reg),
                                              MIR_new_uint_op(ctx, (uint64_t)(uintptr_t)rc),
                                              MIR_new_int_op(ctx, VREG_GET_A(instr)),
                                              MIR_new_int_op(ctx, VREG_GET_B(instr)),
                                              MIR_new_int_op(ctx, (int64_t)(rc->code[ip + 1U] & 0xFFU)),
                                              MIR_new_reg_op(ctx, error_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, error_label),
                                         MIR_new_reg_op(ctx, status_reg), MIR_new_int_op(ctx, VIGIL_STATUS_OK)));
            vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg,
                                       status_reg);
            break;
        case VREG_RELEASE:
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_NIL);
            break;
        case VREG_RETURN:
            vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg,
                                       status_reg);
            if (VREG_GET_B(instr) > 0U)
            {
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                             MIR_new_mem_op(ctx, MIR_T_I64,
                                                            (MIR_disp_t)((size_t)VREG_GET_A(instr) *
                                                                         sizeof(vigil_value_t)),
                                                            regs_reg, 0U, 1U)));
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_MOV,
                                             MIR_new_mem_op(ctx, MIR_T_I64, 0, out_reg, 0U, 1U),
                                             MIR_new_reg_op(ctx, tmp0_reg)));
            }
            else
            {
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_MOV, MIR_new_mem_op(ctx, MIR_T_I64, 0, out_reg, 0U, 1U),
                                             MIR_new_uint_op(ctx, VIGIL_NANBOX_NIL)));
            }
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp3_reg),
                                         MIR_new_int_op(ctx, (int64_t)(VREG_GET_A(instr) + VREG_GET_B(instr)))));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, stack_count),
                                                                      vm_reg, 0U, 1U),
                                         MIR_new_reg_op(ctx, tmp0_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_ret_insn(ctx, 1U, MIR_new_int_op(ctx, VIGIL_STATUS_OK)));
            continue;
        default:
            free(labels); /* alloc-check: exempt - calloc-allocated */
            vigil_aot_cache_free(cache);
            vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "numeric AOT builder encountered unsupported op");
            return VIGIL_STATUS_UNSUPPORTED;
        }

        if (ip + vigil_aot_instr_words(rc, ip) < rc->code_count)
        {
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_JMP,
                                         MIR_new_label_op(ctx, labels[ip + vigil_aot_instr_words(rc, ip)])));
        }
    }

    MIR_append_insn(ctx, func, labels[rc->code_count]);
    MIR_append_insn(ctx, func,
                    MIR_new_call_insn(ctx, 6U, MIR_new_ref_op(ctx, fail_proto), MIR_new_ref_op(ctx, fail_import),
                                      MIR_new_reg_op(ctx, status_reg),
                                      MIR_new_int_op(ctx, VIGIL_STATUS_INVALID_ARGUMENT),
                                      MIR_new_uint_op(ctx, (uint64_t)(uintptr_t)"integer arithmetic overflow"),
                                      MIR_new_reg_op(ctx, error_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1U, MIR_new_reg_op(ctx, status_reg)));
    MIR_append_insn(ctx, func, error_label);
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1U, MIR_new_reg_op(ctx, status_reg)));
    MIR_finish_func(ctx);
    MIR_finish_module(ctx);

    MIR_load_module(ctx, module);
    MIR_load_external(ctx, "vigil_aot_prepare_frame", (void *)vigil_aot_prepare_frame);
    MIR_load_external(ctx, "vigil_aot_numeric_call_self", (void *)vigil_aot_numeric_call_self);
    MIR_load_external(ctx, "vigil_aot_numeric_call", (void *)vigil_aot_numeric_call);
    MIR_load_external(ctx, "vigil_aot_numeric_fail", (void *)vigil_aot_numeric_fail);
    MIR_gen_init(ctx);
    cache->generator_initialized = 1;
    MIR_link(ctx, MIR_set_gen_interface, NULL);

    cache->entry = (vigil_aot_entry_fn)MIR_gen(ctx, func);
    free(labels); /* alloc-check: exempt - calloc-allocated */
    if (cache->entry == NULL)
    {
        vigil_aot_cache_free(cache);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to generate MIR native entry");
        return VIGIL_STATUS_INTERNAL;
    }

    *out_cache = cache;
    return VIGIL_STATUS_OK;
}
#endif

void vigil_aot_cache_free(vigil_aot_cache_t *cache)
{
    if (cache == NULL)
    {
        return;
    }

#if defined(VIGIL_ENABLE_AOT)
    if (cache->context != NULL)
    {
        MIR_context_t ctx = (MIR_context_t)cache->context;

        if (cache->generator_initialized)
        {
            MIR_gen_finish(ctx);
        }
        MIR_finish(ctx);
    }
#endif

    free(cache); /* alloc-check: exempt - calloc-allocated */
}

int vigil_aot_supported(void)
{
#if defined(VIGIL_ENABLE_AOT)
    return 1;
#else
    return 0;
#endif
}

#if defined(VIGIL_ENABLE_AOT)
static vigil_status_t vigil_aot_build_wrapper(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache,
                                              vigil_error_t *error)
{
    vigil_aot_cache_t *cache = NULL;
    MIR_context_t ctx = NULL;
    MIR_module_t module;
    MIR_item_t proto;
    MIR_item_t import_item;
    MIR_item_t func;
    MIR_type_t res_type = MIR_T_I64;
    MIR_reg_t status_reg;
    MIR_reg_t vm_reg;
    MIR_reg_t out_reg;
    MIR_reg_t error_reg;
    char module_name[64];
    char func_name[64];

    if (out_cache == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_cache must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_cache = NULL;

    cache = (vigil_aot_cache_t *)calloc(1U, sizeof(*cache));
    if (cache == NULL)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    ctx = MIR_init();
    cache->context = ctx;

    (void)snprintf(module_name, sizeof(module_name), "vigil_aot_%p", (const void *)rc);
    (void)snprintf(func_name, sizeof(func_name), "vigil_entry_%p", (const void *)rc);

    module = MIR_new_module(ctx, module_name);
    proto = MIR_new_proto(ctx, "vigil_regvm_execute_proto", 1U, &res_type, 4U, MIR_T_P, "vm", MIR_T_P, "rc",
                          MIR_T_P, "out_value", MIR_T_P, "error");
    import_item = MIR_new_import(ctx, "vigil_regvm_execute");
    func = MIR_new_func(ctx, func_name, 1U, &res_type, 3U, MIR_T_P, "vm", MIR_T_P, "out_value", MIR_T_P, "error");

    status_reg = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, "$status");
    vm_reg = MIR_reg(ctx, "vm", func->u.func);
    out_reg = MIR_reg(ctx, "out_value", func->u.func);
    error_reg = MIR_reg(ctx, "error", func->u.func);

    MIR_append_insn(ctx, func,
                    MIR_new_call_insn(ctx, 7U, MIR_new_ref_op(ctx, proto), MIR_new_ref_op(ctx, import_item),
                                      MIR_new_reg_op(ctx, status_reg), MIR_new_reg_op(ctx, vm_reg),
                                      MIR_new_uint_op(ctx, (uint64_t)(uintptr_t)rc), MIR_new_reg_op(ctx, out_reg),
                                      MIR_new_reg_op(ctx, error_reg)));
    MIR_append_insn(ctx, func, MIR_new_ret_insn(ctx, 1U, MIR_new_reg_op(ctx, status_reg)));
    MIR_finish_func(ctx);
    MIR_finish_module(ctx);

    MIR_load_module(ctx, module);
    MIR_load_external(ctx, "vigil_regvm_execute", (void *)vigil_regvm_execute);
    MIR_gen_init(ctx);
    cache->generator_initialized = 1;
    MIR_link(ctx, MIR_set_gen_interface, NULL);

    cache->entry = (vigil_aot_entry_fn)MIR_gen(ctx, func);
    if (cache->entry == NULL)
    {
        vigil_aot_cache_free(cache);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to generate MIR native entry");
        return VIGIL_STATUS_INTERNAL;
    }

    *out_cache = cache;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_aot_build(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache,
                                      vigil_error_t *error)
{
    if (vigil_aot_chunk_is_numeric_subset(rc))
    {
        return vigil_aot_build_numeric(rc, out_cache, error);
    }

    return vigil_aot_build_wrapper(rc, out_cache, error);
}
#endif

vigil_status_t vigil_aot_ensure(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache, vigil_error_t *error)
{
    vigil_reg_chunk_t *mutable_rc = (vigil_reg_chunk_t *)rc;

    if (out_cache == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_cache must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_cache = NULL;

    if (mutable_rc == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "reg chunk must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

#if !defined(VIGIL_ENABLE_AOT)
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "AOT is not enabled in this build");
    return VIGIL_STATUS_UNSUPPORTED;
#else
    for (;;)
    {
        int64_t state = vigil_atomic_load(&mutable_rc->aot_cache_state);

        if (state == VIGIL_AOT_CACHE_STATE_READY)
        {
            if (mutable_rc->aot_cache == NULL)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "reg chunk AOT cache ready state was null");
                return VIGIL_STATUS_INTERNAL;
            }
            *out_cache = mutable_rc->aot_cache;
            return VIGIL_STATUS_OK;
        }

        if (state == VIGIL_AOT_CACHE_STATE_EMPTY &&
            vigil_atomic_cas(&mutable_rc->aot_cache_state, VIGIL_AOT_CACHE_STATE_EMPTY, VIGIL_AOT_CACHE_STATE_BUILDING))
        {
            vigil_status_t status = vigil_aot_build(rc, out_cache, error);

            if (status != VIGIL_STATUS_OK)
            {
                vigil_atomic_store(&mutable_rc->aot_cache_state, VIGIL_AOT_CACHE_STATE_EMPTY);
                return status;
            }

            mutable_rc->aot_cache = *out_cache;
            vigil_atomic_store(&mutable_rc->aot_cache_state, VIGIL_AOT_CACHE_STATE_READY);
            return VIGIL_STATUS_OK;
        }

        while (vigil_atomic_load(&mutable_rc->aot_cache_state) == VIGIL_AOT_CACHE_STATE_BUILDING)
        {
        }
    }
#endif
}

vigil_status_t vigil_reg_execute_cached(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, vigil_value_t *out_value,
                                        vigil_error_t *error)
{
    if (vm == NULL || rc == NULL || out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "cached reg execute arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (!vm->aot_enabled || vm->debug_hook != NULL || !vigil_aot_supported())
    {
        return vigil_regvm_execute(vm, rc, out_value, error);
    }

#if defined(VIGIL_ENABLE_AOT)
    vigil_aot_cache_t *cache = NULL;
    vigil_status_t status = vigil_aot_ensure(rc, &cache, error);

    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (cache == NULL || cache->entry == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "AOT cache missing native entry");
        return VIGIL_STATUS_INTERNAL;
    }
    return cache->entry(vm, out_value, error);
#else
    return vigil_regvm_execute(vm, rc, out_value, error);
#endif
}
