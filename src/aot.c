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
#include "internal/vigil_vm_internal.h"
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

/* Lightweight frame push for AOT self-calls.  Skips the cache lookup and
   indirect entry call that made the old helper slow.  Only sets the fields
   that matter for numeric-subset recursion. */
static vigil_status_t vigil_aot_push_self_frame(vigil_vm_t *vm, size_t new_base, size_t needed_stack,
                                                vigil_error_t *error)
{
    vigil_vm_frame_t *prev;
    vigil_vm_frame_t *frame;
    vigil_status_t status;

    if (__builtin_expect(vm->frame_count >= vm->frame_capacity, 0))
    {
        status = vigil_vm_push_frame(vm, NULL, NULL, NULL, 0U, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        vm->frame_count -= 1U;
    }

    if (__builtin_expect(vm->stack_capacity < needed_stack, 0))
    {
        status = vigil_vm_grow_stack(vm, needed_stack, error);
        if (status != VIGIL_STATUS_OK)
            return status;
    }

    prev = &vm->frames[vm->frame_count - 1U];
    frame = &vm->frames[vm->frame_count];
    frame->callable = prev->callable;
    frame->function = prev->function;
    frame->chunk = prev->chunk;
    frame->base_slot = new_base;
    vm->frame_count += 1U;

    if (vm->stack_count < needed_stack)
        vm->stack_count = needed_stack;

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

static vigil_status_t vigil_aot_numeric_call_native(vigil_vm_t *vm, const vigil_reg_chunk_t *rc, uint8_t arg_base_r,
                                                    uint8_t arg_count, uint32_t const_idx, vigil_error_t *error)
{
    if (vm == NULL || vm->frame_count == 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "AOT native call requires an active frame");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_vm_frame_t *frame = &vm->frames[vm->frame_count - 1U];
    size_t base = frame->base_slot;
    vm->stack_count = base + (size_t)arg_base_r + (size_t)arg_count;

    const vigil_value_t *native_val = VIGIL_VM_CHUNK_CONSTANT(rc->stack_chunk, (size_t)const_idx);
    if (native_val == NULL || !vigil_nanbox_has_object(*native_val))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "AOT native call: invalid constant");
        return VIGIL_STATUS_INTERNAL;
    }

    vigil_native_fn_t native_fn = vigil_native_function_get((vigil_object_t *)vigil_nanbox_decode_ptr(*native_val));
    if (native_fn == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "AOT native call: not a native function");
        return VIGIL_STATUS_UNSUPPORTED;
    }

    vigil_status_t status = native_fn(vm, (size_t)arg_count, error);
    if (status != VIGIL_STATUS_OK)
        return status;

    if (vm->stack_count < base + (size_t)rc->max_registers)
        vm->stack_count = base + (size_t)rc->max_registers;

    return VIGIL_STATUS_OK;
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
    case VREG_CALL_NATIVE:
        return 2U;
    case VREG_FORLOOP_I32:
    case VREG_FORLOOP_I64:
        return 3U;
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

/* Check that every CALL_NATIVE in the chunk targets a native function
   whose return type is numeric.  Returns 0 if any native call returns
   a string, error, or object type. */
static int vigil_aot_native_calls_are_numeric(const vigil_reg_chunk_t *rc)
{
    size_t ip = 0U;
    while (ip < rc->code_count)
    {
        vigil_reg_instr_t ci = rc->code[ip];
        if (VREG_GET_OP(ci) == VREG_CALL_NATIVE && ip + 1U < rc->code_count)
        {
            uint32_t idx = rc->code[ip + 1U];
            const vigil_value_t *nv = VIGIL_VM_CHUNK_CONSTANT(rc->stack_chunk, (size_t)idx);
            if (nv == NULL || !vigil_nanbox_has_object(*nv))
                return 0;
            int rt = vigil_native_function_get_return_type(
                (vigil_object_t *)vigil_nanbox_decode_ptr(*nv));
            if (rt == VIGIL_TYPE_STRING || rt == VIGIL_TYPE_ERR || rt == VIGIL_TYPE_OBJECT ||
                rt == VIGIL_TYPE_INVALID)
                return 0;
        }
        ip += vigil_aot_instr_words(rc, ip);
    }
    return 1;
}

static int vigil_aot_chunk_is_numeric_subset(const vigil_reg_chunk_t *rc)
{
    size_t ip = 0U;
    int has_native_call = 0;
    int has_call_self = 0;
    int has_tail_call = 0;

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
        case VREG_MUL_I32:
        case VREG_DIV_I32:
        case VREG_MOD_I32:
        case VREG_ADD_I64:
        case VREG_SUB_I64:
        case VREG_MUL_I64:
        case VREG_DIV_I64:
        case VREG_MOD_I64:
        case VREG_ADDI:
        case VREG_SUBI:
        case VREG_ADDI_I64:
        case VREG_SUBI_I64:
        case VREG_INC_I32:
        case VREG_INC_I64:
        case VREG_FORLOOP_I32:
        case VREG_FORLOOP_I64:
        case VREG_NEG:
        case VREG_NOT:
        case VREG_BNOT:
        case VREG_BAND:
        case VREG_BOR:
        case VREG_BXOR:
        case VREG_SHL:
        case VREG_SHR:
        case VREG_EQ_I32:
        case VREG_NE_I32:
        case VREG_LT_I32:
        case VREG_LE_I32:
        case VREG_GT_I32:
        case VREG_GE_I32:
        case VREG_DUP:
        case VREG_TESTSET:
        case VREG_JMP:
        case VREG_TEST:
        case VREG_LT_I32_JMP:
        case VREG_LE_I32_JMP:
        case VREG_GT_I32_JMP:
        case VREG_GE_I32_JMP:
        case VREG_EQ_I32_JMP:
        case VREG_NE_I32_JMP:
        case VREG_LT_I64_JMP:
        case VREG_LE_I64_JMP:
        case VREG_GT_I64_JMP:
        case VREG_GE_I64_JMP:
        case VREG_EQ_I64_JMP:
        case VREG_NE_I64_JMP:
        case VREG_LT_I32_IMM_JMP:
        case VREG_TO_I32:
        case VREG_TO_I64:
        case VREG_CALL:
        case VREG_CALL_SELF:
        case VREG_CALL_NATIVE:
        case VREG_TAIL_CALL:
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

        /* CALL_NATIVE is only safe when the callee deals exclusively with
           numeric values.  If the function also contains RELEASE instructions
           it manipulates object-typed registers (strings, arrays, …) and
           must fall back to the interpreter. */
        if (op == VREG_CALL_NATIVE)
            has_native_call = 1;
        if (op == VREG_CALL_SELF)
            has_call_self = 1;
        if (op == VREG_TAIL_CALL)
            has_tail_call = 1;

        ip += vigil_aot_instr_words(rc, ip);
    }

    if (has_native_call && !vigil_aot_native_calls_are_numeric(rc))
        return 0;

    if (has_call_self && has_tail_call)
        return 0;

    return 1;
}

/* Forward declaration removed — wrapper disabled, see vigil_aot_build. */

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







static vigil_status_t vigil_aot_build_numeric(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache,
                                              vigil_error_t *error)
{
    vigil_aot_cache_t *cache = NULL;
    MIR_context_t ctx = NULL;
    MIR_module_t module;
    MIR_item_t func;
    MIR_item_t grow_stack_proto;
    MIR_item_t grow_stack_import;
    MIR_item_t grow_proto;
    MIR_item_t grow_import;
    MIR_item_t call_proto;
    MIR_item_t call_import;
    MIR_item_t call_native_proto;
    MIR_item_t call_native_import;
    MIR_item_t fail_proto;
    MIR_item_t fail_import;
    MIR_item_t self_proto;
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
    /* Dedicated MIR registers for FORLOOP counters — persist across iterations. */
    MIR_reg_t forloop_regs[256];
    memset(forloop_regs, 0, sizeof(forloop_regs));
    /* Register promotion: slots written ONLY by typed ops get MIR virtual regs. */
    MIR_reg_t vreg[256];
    uint8_t promoted[256]; /* 0=no, 1=i32, 2=i64 */
    int has_self_call = 0;
    memset(vreg, 0, sizeof(vreg));
    memset(promoted, 0, sizeof(promoted));
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
    grow_stack_proto = MIR_new_proto(ctx, "vigil_vm_grow_stack_proto", 1U, &res_type, 3U, MIR_T_P, "vm", MIR_T_I64,
                                     "min_cap", MIR_T_P, "error");
    grow_stack_import = MIR_new_import(ctx, "vigil_vm_grow_stack");
    grow_proto = MIR_new_proto(ctx, "vigil_aot_push_self_frame_proto", 1U, &res_type, 4U, MIR_T_P, "vm", MIR_T_I64,
                               "new_base", MIR_T_I64, "needed_stack", MIR_T_P, "error");
    grow_import = MIR_new_import(ctx, "vigil_aot_push_self_frame");
    self_proto = MIR_new_proto(ctx, "vigil_aot_self_proto", 1U, &res_type, 3U, MIR_T_P, "vm", MIR_T_P, "out_value",
                               MIR_T_P, "error");
    call_proto = MIR_new_proto(ctx, "vigil_aot_numeric_call_proto", 1U, &res_type, 6U, MIR_T_P, "vm", MIR_T_P, "rc",
                               MIR_T_I64, "arg_base", MIR_T_I64, "func_idx", MIR_T_I64, "arg_count", MIR_T_P,
                               "error");
    call_import = MIR_new_import(ctx, "vigil_aot_numeric_call");
    call_native_proto = MIR_new_proto(ctx, "vigil_aot_numeric_call_native_proto", 1U, &res_type, 6U,
                                      MIR_T_P, "vm", MIR_T_P, "rc", MIR_T_I64, "arg_base", MIR_T_I64,
                                      "arg_count", MIR_T_I64, "const_idx", MIR_T_P, "error");
    call_native_import = MIR_new_import(ctx, "vigil_aot_numeric_call_native");
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

    /* Allocate dedicated MIR registers for FORLOOP counters. */
    {
        char rname[32];
        size_t sip = 0U;
        while (sip < rc->code_count)
        {
            uint8_t sop = VREG_GET_OP(rc->code[sip]);
            if (sop == VREG_FORLOOP_I32 || sop == VREG_FORLOOP_I64)
            {
                uint8_t idx = VREG_GET_A(rc->code[sip]);
                if (forloop_regs[idx] == 0)
                {
                    (void)snprintf(rname, sizeof(rname), "$fl%u", (unsigned)idx);
                    forloop_regs[idx] = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, rname);
                }
            }
            sip += vigil_aot_instr_words(rc, sip);
        }
    }

    /* Promotion analysis: scan bytecode to find slots safe to promote. */
    {
        uint8_t typed_w[256] = {0}; /* bit 0: i32 typed write, bit 1: i64 typed write */
        uint8_t other_w[256] = {0}; /* any non-typed write */
        size_t sip = 0U;
        while (sip < rc->code_count)
        {
            vigil_reg_instr_t si = rc->code[sip];
            uint8_t sop = VREG_GET_OP(si);
            uint8_t sa = VREG_GET_A(si);
            switch (sop)
            {
            case VREG_ADD_I32: case VREG_SUB_I32: case VREG_MUL_I32: case VREG_DIV_I32: case VREG_MOD_I32:
            case VREG_ADDI: case VREG_SUBI: case VREG_NEG: case VREG_BNOT:
            case VREG_BAND: case VREG_BOR: case VREG_BXOR: case VREG_SHL: case VREG_SHR:
            case VREG_INC_I32: case VREG_FORLOOP_I32:
            case VREG_TO_I32:
                typed_w[sa] |= 1U;
                break;
            case VREG_ADD_I64: case VREG_SUB_I64: case VREG_MUL_I64: case VREG_DIV_I64: case VREG_MOD_I64:
            case VREG_ADDI_I64: case VREG_SUBI_I64: case VREG_INC_I64: case VREG_FORLOOP_I64:
            case VREG_TO_I64:
                typed_w[sa] |= 2U;
                break;
            case VREG_LOAD_K: {
                const vigil_value_t *kv = VIGIL_VM_CHUNK_CONSTANT(rc->stack_chunk, (size_t)VREG_GET_Bx(si));
                if (kv != NULL && vigil_nanbox_is_int_inline(*kv))
                    typed_w[sa] |= 1U;
                else
                    other_w[sa] = 1;
                break;
            }
            case VREG_CALL_SELF:
                has_self_call = 1;
                break;
            default:
                /* Only mark A as non-typed write for ops that actually write to A.
                   JMP, TEST, comparison JMPs, RETURN, RELEASE don't write to A. */
                switch (sop)
                {
                case VREG_JMP: case VREG_TEST: case VREG_RETURN: case VREG_RELEASE:
                case VREG_TAIL_CALL:
                case VREG_LT_I32_JMP: case VREG_LE_I32_JMP: case VREG_GT_I32_JMP:
                case VREG_GE_I32_JMP: case VREG_EQ_I32_JMP: case VREG_NE_I32_JMP:
                case VREG_LT_I64_JMP: case VREG_LE_I64_JMP: case VREG_GT_I64_JMP:
                case VREG_GE_I64_JMP: case VREG_EQ_I64_JMP: case VREG_NE_I64_JMP:
                case VREG_LT_I32_IMM_JMP:
                case VREG_SET_FIELD: case VREG_SET_INDEX: case VREG_SET_GLOBAL:
                case VREG_SET_CAPTURE:
                    break; /* these don't write to A */
                default:
                    other_w[sa] = 1;
                    break;
                }
                break;
            }
            sip += vigil_aot_instr_words(rc, sip);
        }
        {
            char rn[32];
            for (uint16_t ri = 0; ri < rc->max_registers && ri < 256; ri++)
            {
                if ((typed_w[ri] & 1U) && !other_w[ri])
                    promoted[ri] = 1;
                /* i64 promotion disabled: raw values can exceed 48-bit NaN-box payload. */
                if (promoted[ri])
                {
                    (void)snprintf(rn, sizeof(rn), "$p%u", (unsigned)ri);
                    vreg[ri] = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, rn);
                }
            }
        }
    }

    for (ip = 0U; ip <= rc->code_count; ++ip)
    {
        labels[ip] = MIR_new_label(ctx);
    }
    error_label = MIR_new_label(ctx);

    /* Inline prepare_frame: check stack capacity, grow on slow path. */
    {
        MIR_label_t prep_grow = MIR_new_label(ctx);
        MIR_label_t prep_done = MIR_new_label(ctx);

        /* Load base_slot from current frame */
        vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg, status_reg);
        /* tmp3_reg = base_slot, regs_reg = &stack[base_slot] */

        /* needed = base + max_registers + 16 */
        MIR_append_insn(ctx, func,
                        MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp3_reg),
                                     MIR_new_int_op(ctx, (int64_t)rc->max_registers + 16)));
        /* if stack_capacity < needed → grow */
        MIR_append_insn(ctx, func,
                        MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp1_reg),
                                     MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, stack_capacity), vm_reg, 0U,
                                                    1U)));
        MIR_append_insn(ctx, func,
                        MIR_new_insn(ctx, MIR_BLT, MIR_new_label_op(ctx, prep_grow), MIR_new_reg_op(ctx, tmp1_reg),
                                     MIR_new_reg_op(ctx, tmp0_reg)));
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, prep_done)));

        MIR_append_insn(ctx, func, prep_grow);
        MIR_append_insn(ctx, func,
                        MIR_new_call_insn(ctx, 6U, MIR_new_ref_op(ctx, grow_stack_proto),
                                          MIR_new_ref_op(ctx, grow_stack_import), MIR_new_reg_op(ctx, status_reg),
                                          MIR_new_reg_op(ctx, vm_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                          MIR_new_reg_op(ctx, error_reg)));
        MIR_append_insn(ctx, func,
                        MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, error_label),
                                     MIR_new_reg_op(ctx, status_reg), MIR_new_int_op(ctx, VIGIL_STATUS_OK)));

        MIR_append_insn(ctx, func, prep_done);

        /* Update stack_count if needed: base + max_registers */
        MIR_append_insn(ctx, func,
                        MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp3_reg),
                                     MIR_new_int_op(ctx, (int64_t)rc->max_registers)));
        MIR_append_insn(ctx, func,
                        MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp1_reg),
                                     MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, stack_count), vm_reg, 0U,
                                                    1U)));
        {
            MIR_label_t sc_ok = MIR_new_label(ctx);
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BGE, MIR_new_label_op(ctx, sc_ok),
                                         MIR_new_reg_op(ctx, tmp1_reg), MIR_new_reg_op(ctx, tmp0_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV,
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, stack_count), vm_reg, 0U,
                                                        1U),
                                         MIR_new_reg_op(ctx, tmp0_reg)));
            MIR_append_insn(ctx, func, sc_ok);
        }
    }
    vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg, status_reg);

    /* ── Full SSA register promotion ─────────────────────────────
       Every VREG slot gets a MIR virtual register (v[slot]).
       All typed ops operate on v[] directly — no memory access.
       Non-typed ops (NOT, TEST, TESTSET) flush to memory first.
       Calls flush all, then reload all after return. */

    /* Allocate v[] regs for all slots.
       Skip for recursive functions — the per-call V_LOAD overhead
       exceeds the benefit when calls dominate the runtime. */
    MIR_reg_t *v = has_self_call ? NULL : (MIR_reg_t *)calloc((size_t)rc->max_registers, sizeof(*v));
    if (v != NULL)
    {
        char rn[32];
        for (uint16_t ri = 0; ri < rc->max_registers; ri++)
        {
            (void)snprintf(rn, sizeof(rn), "$v%u", (unsigned)ri);
            v[ri] = MIR_new_func_reg(ctx, func->u.func, MIR_T_I64, rn);
        }
    }

/* Flush v[slot] to NaN-boxed memory. Promoted slots need encode; others are already NaN-boxed. */
#define V_FLUSH(slot) do { \
    if (v) { \
        if (promoted[(slot)]) { \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, tmp3_reg), MIR_new_reg_op(ctx, v[(slot)]), \
                             MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK))); \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, tmp3_reg), MIR_new_reg_op(ctx, tmp3_reg), \
                             MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT))); \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, \
                             MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)(slot) * sizeof(vigil_value_t)), regs_reg, 0U, 1U), \
                             MIR_new_reg_op(ctx, tmp3_reg))); \
        } else { \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, \
                             MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)(slot) * sizeof(vigil_value_t)), regs_reg, 0U, 1U), \
                             MIR_new_reg_op(ctx, v[(slot)]))); \
        } \
    } \
} while (0)
/* Reload v[slot] from NaN-boxed memory. Promoted slots need decode; others load as-is. */
#define V_LOAD(slot) do { \
    if (v) { \
        if (promoted[(slot)] == 1) { \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[(slot)]), \
                             MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)(slot) * sizeof(vigil_value_t)), regs_reg, 0U, 1U))); \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_EXT32, MIR_new_reg_op(ctx, v[(slot)]), MIR_new_reg_op(ctx, v[(slot)]))); \
        } else if (promoted[(slot)] == 2) { \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[(slot)]), \
                             MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)(slot) * sizeof(vigil_value_t)), regs_reg, 0U, 1U))); \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, v[(slot)]), MIR_new_reg_op(ctx, v[(slot)]), \
                             MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK))); \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_LSH, MIR_new_reg_op(ctx, v[(slot)]), MIR_new_reg_op(ctx, v[(slot)]), MIR_new_int_op(ctx, 16))); \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_RSH, MIR_new_reg_op(ctx, v[(slot)]), MIR_new_reg_op(ctx, v[(slot)]), MIR_new_int_op(ctx, 16))); \
        } else { \
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[(slot)]), \
                             MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)(slot) * sizeof(vigil_value_t)), regs_reg, 0U, 1U))); \
        } \
    } \
} while (0)
/* Flush ALL slots to memory. */
#define V_FLUSH_ALL() do { \
    if (v) for (uint16_t _i = 0; _i < rc->max_registers; _i++) V_FLUSH(_i); \
} while (0)
/* Reload ALL slots from memory. */
#define V_LOAD_ALL() do { \
    if (v) for (uint16_t _i = 0; _i < rc->max_registers; _i++) V_LOAD(_i); \
} while (0)
/* Get the MIR reg for a slot (v[slot] if available, else tmp fallback). */
#define V(slot) (v ? v[(slot)] : tmp0_reg)

/* Decode i32 from v[slot] or memory. For promoted slots, v[] already holds raw value. */
#define V_DEC_I32(dst, slot) do { \
    if (v && promoted[(slot)]) \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, v[(slot)]))); \
    else if (v) { \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, v[(slot)]))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_EXT32, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, (dst)))); \
    } else { \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, (dst)), \
                         MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)(slot) * sizeof(vigil_value_t)), regs_reg, 0U, 1U))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_EXT32, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, (dst)))); \
    } \
} while (0)
/* Decode i64 from v[slot] or memory. For promoted slots, v[] already holds raw value. */
#define V_DEC_I64(dst, slot) do { \
    if (v && promoted[(slot)]) \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, v[(slot)]))); \
    else if (v) { \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, v[(slot)]), \
                         MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_LSH, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, (dst)), MIR_new_int_op(ctx, 16))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_RSH, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, (dst)), MIR_new_int_op(ctx, 16))); \
    } else { \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, (dst)), \
                         MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)(slot) * sizeof(vigil_value_t)), regs_reg, 0U, 1U))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, (dst)), \
                         MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_LSH, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, (dst)), MIR_new_int_op(ctx, 16))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_RSH, MIR_new_reg_op(ctx, (dst)), MIR_new_reg_op(ctx, (dst)), MIR_new_int_op(ctx, 16))); \
    } \
} while (0)
/* Encode result into v[slot]. For promoted slots, store raw value directly. */
#define V_ENC(val, slot) do { \
    if (v && promoted[(slot)]) \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[(slot)]), MIR_new_reg_op(ctx, (val)))); \
    else if (v) { \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, v[(slot)]), MIR_new_reg_op(ctx, (val)), \
                         MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, v[(slot)]), MIR_new_reg_op(ctx, v[(slot)]), \
                         MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT))); \
    } else { \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, tmp3_reg), MIR_new_reg_op(ctx, (val)), \
                         MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, tmp3_reg), MIR_new_reg_op(ctx, tmp3_reg), \
                         MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT))); \
        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, \
                         MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)(slot) * sizeof(vigil_value_t)), regs_reg, 0U, 1U), \
                         MIR_new_reg_op(ctx, tmp3_reg))); \
    } \
} while (0)

    /* Initial load: load all slots from NaN-boxed memory into v[]. */
    if (v != NULL)
    {
        uint16_t entry_load_count = rc->max_registers;
        if (has_self_call && rc->arity < entry_load_count)
            entry_load_count = rc->arity;
        for (uint16_t ri = 0; ri < entry_load_count; ri++)
            V_LOAD(ri);
    }

    for (ip = 0U; ip < rc->code_count; ip += vigil_aot_instr_words(rc, ip))
    {
        vigil_reg_instr_t instr = rc->code[ip];
        uint8_t op = VREG_GET_OP(instr);
        int16_t off;

        MIR_append_insn(ctx, func, labels[ip]);

        switch (op)
        {
        case VREG_MOVE:
            if (v)
            {
                uint8_t dst = VREG_GET_A(instr), src = VREG_GET_B(instr);
                if (promoted[src] && !promoted[dst])
                {
                    /* Source is raw, dest expects NaN-boxed — encode. */
                    MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, v[dst]),
                                    MIR_new_reg_op(ctx, v[src]), MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK)));
                    MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, v[dst]),
                                    MIR_new_reg_op(ctx, v[dst]), MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT)));
                }
                else if (!promoted[src] && promoted[dst])
                {
                    /* Source is NaN-boxed, dest expects raw — decode. */
                    MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[dst]),
                                    MIR_new_reg_op(ctx, v[src])));
                    if (promoted[dst] == 1)
                        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_EXT32, MIR_new_reg_op(ctx, v[dst]),
                                        MIR_new_reg_op(ctx, v[dst])));
                    else
                    {
                        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, v[dst]),
                                        MIR_new_reg_op(ctx, v[dst]), MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK)));
                        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_LSH, MIR_new_reg_op(ctx, v[dst]),
                                        MIR_new_reg_op(ctx, v[dst]), MIR_new_int_op(ctx, 16)));
                        MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_RSH, MIR_new_reg_op(ctx, v[dst]),
                                        MIR_new_reg_op(ctx, v[dst]), MIR_new_int_op(ctx, 16)));
                    }
                }
                else
                {
                    /* Same representation — direct copy. */
                    MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[dst]),
                                    MIR_new_reg_op(ctx, v[src])));
                }
            }
            else
            {
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                             MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)VREG_GET_B(instr) * sizeof(vigil_value_t)), regs_reg, 0U, 1U)));
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_MOV,
                                             MIR_new_mem_op(ctx, MIR_T_I64, (MIR_disp_t)((size_t)VREG_GET_A(instr) * sizeof(vigil_value_t)), regs_reg, 0U, 1U),
                                             MIR_new_reg_op(ctx, tmp0_reg)));
            }
            break;
        case VREG_LOAD_K: {
            uint8_t dst = VREG_GET_A(instr);
            const vigil_value_t *k = VIGIL_VM_CHUNK_CONSTANT(rc->stack_chunk, (size_t)VREG_GET_Bx(instr));
            vigil_value_t kv = k != NULL ? *k : VIGIL_NANBOX_NIL;
            if (v)
            {
                if (promoted[dst] == 1 && vigil_nanbox_is_int_inline(kv))
                    MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[dst]),
                                    MIR_new_int_op(ctx, (int64_t)vigil_nanbox_decode_i32(kv))));
                else
                    MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[dst]),
                                    MIR_new_uint_op(ctx, kv)));
            }
            else
                vigil_aot_emit_store_constant(ctx, func, regs_reg, dst, kv);
            break;
        }
        case VREG_LOAD_NIL:
        case VREG_LOAD_TRUE:
        case VREG_LOAD_FALSE: {
            vigil_value_t cv = (op == VREG_LOAD_NIL) ? VIGIL_NANBOX_NIL : (op == VREG_LOAD_TRUE) ? VIGIL_NANBOX_TRUE : VIGIL_NANBOX_FALSE;
            if (v)
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, v[VREG_GET_A(instr)]),
                                MIR_new_uint_op(ctx, cv)));
            else
                vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), cv);
            break;
        }
        /* ── i32 binary arithmetic (with overflow check) ──────────── */
        case VREG_ADD_I32:
        case VREG_SUB_I32:
        case VREG_MUL_I32: {
            MIR_insn_code_t mo = (op == VREG_ADD_I32) ? MIR_ADDO : (op == VREG_SUB_I32) ? MIR_SUBO : MIR_MULO;
            V_DEC_I32(tmp0_reg, VREG_GET_B(instr));
            V_DEC_I32(tmp1_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, mo, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        case VREG_DIV_I32:
        case VREG_MOD_I32: {
            MIR_insn_code_t mo = (op == VREG_DIV_I32) ? MIR_DIV : MIR_MOD;
            V_DEC_I32(tmp0_reg, VREG_GET_B(instr));
            V_DEC_I32(tmp1_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[rc->code_count]),
                            MIR_new_reg_op(ctx, tmp1_reg), MIR_new_int_op(ctx, 0)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, mo, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        /* ── i32 immediate arithmetic ────────────────────────────── */
        case VREG_ADDI:
        case VREG_SUBI: {
            MIR_insn_code_t mo = (op == VREG_ADDI) ? MIR_ADDO : MIR_SUBO;
            V_DEC_I32(tmp0_reg, VREG_GET_B(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, mo, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_int_op(ctx, (int32_t)(int8_t)VREG_GET_C(instr))));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        /* ── i32 unary ───────────────────────────────────────────── */
        case VREG_NEG:
            V_DEC_I32(tmp0_reg, VREG_GET_B(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_NEG, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg)));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        case VREG_NOT: {
            MIR_label_t not_true = MIR_new_label(ctx);
            MIR_label_t not_done = MIR_new_label(ctx);
            /* NOT needs NaN-boxed value. Encode promoted slot if needed. */
            if (v && promoted[VREG_GET_B(instr)])
            {
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, tmp0_reg),
                                MIR_new_reg_op(ctx, v[VREG_GET_B(instr)]), MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK)));
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, tmp0_reg),
                                MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT)));
            }
            else
            {
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                MIR_new_reg_op(ctx, V(VREG_GET_B(instr)))));
            }
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, not_true),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_FALSE)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, not_true),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_NIL)));
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_FALSE);
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, V(VREG_GET_A(instr))),
                            MIR_new_uint_op(ctx, VIGIL_NANBOX_FALSE)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, not_done)));
            MIR_append_insn(ctx, func, not_true);
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_TRUE);
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, V(VREG_GET_A(instr))),
                            MIR_new_uint_op(ctx, VIGIL_NANBOX_TRUE)));
            MIR_append_insn(ctx, func, not_done);
            break;
        }
        case VREG_BNOT:
            V_DEC_I32(tmp0_reg, VREG_GET_B(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_XOR, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_int_op(ctx, -1)));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        /* ── Bitwise binary ──────────────────────────────────────── */
        case VREG_BAND: case VREG_BOR: case VREG_BXOR: case VREG_SHL: case VREG_SHR: {
            MIR_insn_code_t mo;
            switch (op) {
            case VREG_BAND: mo = MIR_AND; break;
            case VREG_BOR:  mo = MIR_OR;  break;
            case VREG_BXOR: mo = MIR_XOR; break;
            case VREG_SHL:  mo = MIR_LSH; break;
            default:        mo = MIR_RSH; break;
            }
            V_DEC_I32(tmp0_reg, VREG_GET_B(instr));
            V_DEC_I32(tmp1_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, mo, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        /* ── i64 binary arithmetic ───────────────────────────────── */
        case VREG_ADD_I64: case VREG_SUB_I64: case VREG_MUL_I64: {
            MIR_insn_code_t mo = (op == VREG_ADD_I64) ? MIR_ADDO : (op == VREG_SUB_I64) ? MIR_SUBO : MIR_MULO;
            V_DEC_I64(tmp0_reg, VREG_GET_B(instr));
            V_DEC_I64(tmp1_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, mo, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        case VREG_DIV_I64: case VREG_MOD_I64: {
            MIR_insn_code_t mo = (op == VREG_DIV_I64) ? MIR_DIV : MIR_MOD;
            V_DEC_I64(tmp0_reg, VREG_GET_B(instr));
            V_DEC_I64(tmp1_reg, VREG_GET_C(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[rc->code_count]),
                            MIR_new_reg_op(ctx, tmp1_reg), MIR_new_int_op(ctx, 0)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, mo, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp1_reg)));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        case VREG_ADDI_I64: case VREG_SUBI_I64: {
            MIR_insn_code_t mo = (op == VREG_ADDI_I64) ? MIR_ADDO : MIR_SUBO;
            V_DEC_I64(tmp0_reg, VREG_GET_B(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, mo, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_int_op(ctx, (int64_t)(int8_t)VREG_GET_C(instr))));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        /* ── Increment ───────────────────────────────────────────── */
        case VREG_INC_I32: {
            V_DEC_I32(tmp0_reg, VREG_GET_A(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_ADDO, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_int_op(ctx, (int32_t)(int8_t)VREG_GET_B(instr))));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        case VREG_INC_I64: {
            V_DEC_I64(tmp0_reg, VREG_GET_A(instr));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_ADDO, MIR_new_reg_op(ctx, tmp2_reg),
                            MIR_new_reg_op(ctx, tmp0_reg), MIR_new_int_op(ctx, (int64_t)(int8_t)VREG_GET_B(instr))));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            V_ENC(tmp2_reg, VREG_GET_A(instr));
            break;
        }
        /* ── Type conversions ────────────────────────────────────── */
        case VREG_TO_I32: {
            V_DEC_I32(tmp0_reg, VREG_GET_B(instr));
            /* i32→i32 is identity; i64→i32 truncates (EXT32 already done by V_DEC_I32). */
            V_ENC(tmp0_reg, VREG_GET_A(instr));
            break;
        }
        case VREG_TO_I64: {
            V_DEC_I64(tmp0_reg, VREG_GET_B(instr));
            V_ENC(tmp0_reg, VREG_GET_A(instr));
            break;
        }
        /* ── For-loop ────────────────────────────────────────────── */
        case VREG_FORLOOP_I32: case VREG_FORLOOP_I64: {
            uint8_t idx = VREG_GET_A(instr);
            int64_t delta = (int64_t)(int8_t)VREG_GET_B(instr);
            uint8_t cmp = VREG_GET_C(instr);
            vigil_reg_instr_t i2 = rc->code[ip + 1U];
            uint16_t ci = VREG_GET_Bx(i2);
            const vigil_value_t *kv = VIGIL_VM_CHUNK_CONSTANT(rc->stack_chunk, (size_t)ci);
            int64_t limit = (op == VREG_FORLOOP_I32)
                                ? (int64_t)vigil_nanbox_decode_i32(kv != NULL ? *kv : VIGIL_NANBOX_NIL)
                                : vigil_nanbox_decode_int(kv != NULL ? *kv : VIGIL_NANBOX_NIL);
            MIR_label_t loop_cont = MIR_new_label(ctx);
            MIR_reg_t fl = forloop_regs[idx];
            if (op == VREG_FORLOOP_I32)
                V_DEC_I32(fl, idx);
            else
                V_DEC_I64(fl, idx);
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_ADDO, MIR_new_reg_op(ctx, fl),
                            MIR_new_reg_op(ctx, fl), MIR_new_int_op(ctx, delta)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BO, MIR_new_label_op(ctx, labels[rc->code_count])));
            if (op == VREG_FORLOOP_I32)
                V_ENC(fl, idx);
            else
                V_ENC(fl, idx);
            switch (cmp) {
            case 0: MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BLT, MIR_new_label_op(ctx, loop_cont), MIR_new_reg_op(ctx, fl), MIR_new_int_op(ctx, limit))); break;
            case 1: MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BLE, MIR_new_label_op(ctx, loop_cont), MIR_new_reg_op(ctx, fl), MIR_new_int_op(ctx, limit))); break;
            case 2: MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BGT, MIR_new_label_op(ctx, loop_cont), MIR_new_reg_op(ctx, fl), MIR_new_int_op(ctx, limit))); break;
            case 3: MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BGE, MIR_new_label_op(ctx, loop_cont), MIR_new_reg_op(ctx, fl), MIR_new_int_op(ctx, limit))); break;
            default: MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, loop_cont), MIR_new_reg_op(ctx, fl), MIR_new_int_op(ctx, limit))); break;
            }
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, labels[ip + 3U])));
            MIR_append_insn(ctx, func, loop_cont);
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, labels[ip + 2U])));
            continue;
        }
        case VREG_EQ_I32:
        case VREG_NE_I32:
        case VREG_LT_I32:
        case VREG_LE_I32:
        case VREG_GT_I32:
        case VREG_GE_I32: {
            MIR_label_t cmp_true = MIR_new_label(ctx);
            MIR_label_t cmp_done = MIR_new_label(ctx);
            MIR_insn_code_t cmp_op;
            V_DEC_I32(tmp0_reg, VREG_GET_B(instr));
            V_DEC_I32(tmp1_reg, VREG_GET_C(instr));
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
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, V(VREG_GET_A(instr))),
                            MIR_new_uint_op(ctx, VIGIL_NANBOX_FALSE)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, cmp_done)));
            MIR_append_insn(ctx, func, cmp_true);
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_TRUE);
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, V(VREG_GET_A(instr))),
                            MIR_new_uint_op(ctx, VIGIL_NANBOX_TRUE)));
            MIR_append_insn(ctx, func, cmp_done);
            break;
        }
        case VREG_DUP: {
            uint8_t dst = VREG_GET_A(instr), src = VREG_GET_B(instr);
            if (v && promoted[src] && !promoted[dst])
            {
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, v[dst]),
                                MIR_new_reg_op(ctx, v[src]), MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK)));
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, v[dst]),
                                MIR_new_reg_op(ctx, v[dst]), MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT)));
            }
            else
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, V(dst)),
                                MIR_new_reg_op(ctx, V(src))));
            break;
        }
        case VREG_TESTSET: {
            MIR_label_t ts_skip = MIR_new_label(ctx);
            /* TESTSET needs NaN-boxed value for comparison. */
            MIR_reg_t ts_src = V(VREG_GET_B(instr));
            if (v && promoted[VREG_GET_B(instr)])
            {
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, tmp0_reg),
                                MIR_new_reg_op(ctx, v[VREG_GET_B(instr)]), MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK)));
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, tmp0_reg),
                                MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT)));
                ts_src = tmp0_reg;
            }
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, ts_skip),
                                         MIR_new_reg_op(ctx, ts_src), MIR_new_uint_op(ctx, VIGIL_NANBOX_FALSE)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, ts_skip),
                                         MIR_new_reg_op(ctx, ts_src), MIR_new_uint_op(ctx, VIGIL_NANBOX_NIL)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, V(VREG_GET_A(instr))),
                            MIR_new_reg_op(ctx, V(VREG_GET_B(instr)))));
            MIR_append_insn(ctx, func, ts_skip);
            break;
        }
        case VREG_JMP:
            off = VREG_GET_sBx(instr);
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_JMP,
                                         MIR_new_label_op(ctx, labels[(size_t)((int32_t)ip + 1 + (int32_t)off)])));
            continue;
        case VREG_TEST: {
            /* TEST needs NaN-boxed value for comparison. */
            MIR_reg_t t_src = V(VREG_GET_A(instr));
            if (v && promoted[VREG_GET_A(instr)])
            {
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_AND, MIR_new_reg_op(ctx, tmp0_reg),
                                MIR_new_reg_op(ctx, v[VREG_GET_A(instr)]), MIR_new_uint_op(ctx, VIGIL_NANBOX_PAYLOAD_MASK)));
                MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_OR, MIR_new_reg_op(ctx, tmp0_reg),
                                MIR_new_reg_op(ctx, tmp0_reg), MIR_new_uint_op(ctx, VIGIL_NANBOX_TAG_INT)));
                t_src = tmp0_reg;
            }
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[ip + 1U]),
                                         MIR_new_reg_op(ctx, t_src), MIR_new_uint_op(ctx, VIGIL_NANBOX_FALSE)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, labels[ip + 1U]),
                                         MIR_new_reg_op(ctx, t_src), MIR_new_uint_op(ctx, VIGIL_NANBOX_NIL)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, labels[ip + 2U])));
            continue;
        }
        case VREG_LT_I32_JMP:
        case VREG_LE_I32_JMP:
        case VREG_GT_I32_JMP:
        case VREG_GE_I32_JMP:
        case VREG_EQ_I32_JMP:
        case VREG_NE_I32_JMP:
            V_DEC_I32(tmp0_reg, VREG_GET_A(instr));
            V_DEC_I32(tmp1_reg, VREG_GET_B(instr));
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
            V_DEC_I32(tmp0_reg, VREG_GET_A(instr));
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
            V_DEC_I64(tmp0_reg, VREG_GET_A(instr));
            V_DEC_I64(tmp1_reg, VREG_GET_B(instr));
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
            V_FLUSH_ALL();
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
            V_LOAD_ALL();
            break;
        case VREG_CALL_SELF: {
            /* Only flush argument slots and reload return slot — the callee
               operates on a different stack frame, so our other slots are safe. */
            uint8_t ret_r = VREG_GET_A(instr);
            uint8_t arg_count = VREG_GET_B(instr);
            uint8_t arg_base_r = (uint8_t)(rc->code[ip + 1U] & 0xFFU);
            /* Flush only the argument slots so the callee can read them. */
            if (v)
            {
                for (uint8_t ai = 0; ai < arg_count && (arg_base_r + ai) < rc->max_registers; ai++)
                    V_FLUSH(arg_base_r + ai);
            }
            MIR_label_t grow_slow = MIR_new_label(ctx);
            MIR_label_t push_done = MIR_new_label(ctx);

            /* Reload to get current base_slot into tmp3_reg */
            vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg,
                                       status_reg);
            /* tmp3_reg = current frame's base_slot */

            /* new_base = base_slot + arg_base_r */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp3_reg),
                                         MIR_new_int_op(ctx, (int64_t)arg_base_r)));
            /* tmp0_reg = new_base */

            /* Check: frame_count < frame_capacity AND stack_capacity >= needed */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp1_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, frame_count), vm_reg, 0U,
                                                        1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp2_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, frame_capacity), vm_reg,
                                                        0U, 1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BGE, MIR_new_label_op(ctx, grow_slow),
                                         MIR_new_reg_op(ctx, tmp1_reg), MIR_new_reg_op(ctx, tmp2_reg)));
            /* needed_stack = new_base + max_registers + 16 */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp2_reg),
                                         MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_int_op(ctx, (int64_t)rc->max_registers + 16)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, status_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, stack_capacity), vm_reg,
                                                        0U, 1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BLT, MIR_new_label_op(ctx, grow_slow),
                                         MIR_new_reg_op(ctx, status_reg), MIR_new_reg_op(ctx, tmp2_reg)));

            /* === FAST PATH: inline frame push === */
            /* frame_ptr = frames + frame_count * sizeof(frame) */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MUL, MIR_new_reg_op(ctx, tmp2_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg),
                                         MIR_new_int_op(ctx, (int64_t)sizeof(vigil_vm_frame_t))));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, status_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, frames), vm_reg, 0U,
                                                        1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp2_reg),
                                         MIR_new_reg_op(ctx, status_reg), MIR_new_reg_op(ctx, tmp2_reg)));
            /* frames[frame_count].base_slot = new_base */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV,
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_frame_t, base_slot),
                                                        tmp2_reg, 0U, 1U),
                                         MIR_new_reg_op(ctx, tmp0_reg)));
            /* frame_count++ */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp1_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg), MIR_new_int_op(ctx, 1)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV,
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, frame_count), vm_reg, 0U,
                                                        1U),
                                         MIR_new_reg_op(ctx, tmp1_reg)));
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, push_done)));

            /* === SLOW PATH: call C helper for growth === */
            MIR_append_insn(ctx, func, grow_slow);
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp2_reg),
                                         MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_int_op(ctx, (int64_t)rc->max_registers + 16)));
            MIR_append_insn(ctx, func,
                            MIR_new_call_insn(ctx, 7U, MIR_new_ref_op(ctx, grow_proto),
                                              MIR_new_ref_op(ctx, grow_import), MIR_new_reg_op(ctx, status_reg),
                                              MIR_new_reg_op(ctx, vm_reg), MIR_new_reg_op(ctx, tmp0_reg),
                                              MIR_new_reg_op(ctx, tmp2_reg),
                                              MIR_new_reg_op(ctx, error_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, error_label),
                                         MIR_new_reg_op(ctx, status_reg), MIR_new_int_op(ctx, VIGIL_STATUS_OK)));

            MIR_append_insn(ctx, func, push_done);

            /* Compute out_ptr = &vm->stack[caller_base + ret_r] */
            /* tmp3_reg still has caller's base_slot (set before the branch) */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp1_reg),
                                         MIR_new_reg_op(ctx, tmp3_reg),
                                         MIR_new_int_op(ctx, (int64_t)ret_r)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_LSH, MIR_new_reg_op(ctx, tmp1_reg),
                                         MIR_new_reg_op(ctx, tmp1_reg), MIR_new_int_op(ctx, 3)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp2_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, stack), vm_reg, 0U,
                                                        1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp1_reg),
                                         MIR_new_reg_op(ctx, tmp2_reg), MIR_new_reg_op(ctx, tmp1_reg)));

            /* Direct self-call: self(vm, out_ptr, error) */
            MIR_append_insn(ctx, func,
                            MIR_new_call_insn(ctx, 6U, MIR_new_ref_op(ctx, self_proto),
                                              MIR_new_ref_op(ctx, func), MIR_new_reg_op(ctx, status_reg),
                                              MIR_new_reg_op(ctx, vm_reg), MIR_new_reg_op(ctx, tmp1_reg),
                                              MIR_new_reg_op(ctx, error_reg)));

            /* Inline frame pop: frame_count-- */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, frame_count), vm_reg, 0U,
                                                        1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_SUB, MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_reg_op(ctx, tmp0_reg), MIR_new_int_op(ctx, 1)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV,
                                         MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, frame_count), vm_reg, 0U,
                                                        1U),
                                         MIR_new_reg_op(ctx, tmp0_reg)));

            /* Check self-call status */
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, error_label),
                                         MIR_new_reg_op(ctx, status_reg), MIR_new_int_op(ctx, VIGIL_STATUS_OK)));
            vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg,
                                       status_reg);
            /* Only reload the return slot — other slots weren't modified. */
            V_LOAD(ret_r);
            break;
        }
        case VREG_CALL_NATIVE:
            V_FLUSH_ALL();
            MIR_append_insn(ctx, func,
                            MIR_new_call_insn(ctx, 9U, MIR_new_ref_op(ctx, call_native_proto),
                                              MIR_new_ref_op(ctx, call_native_import), MIR_new_reg_op(ctx, status_reg),
                                              MIR_new_reg_op(ctx, vm_reg),
                                              MIR_new_uint_op(ctx, (uint64_t)(uintptr_t)rc),
                                              MIR_new_int_op(ctx, VREG_GET_A(instr)),
                                              MIR_new_int_op(ctx, VREG_GET_C(instr)),
                                              MIR_new_int_op(ctx, (int64_t)rc->code[ip + 1U]),
                                              MIR_new_reg_op(ctx, error_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, error_label),
                                         MIR_new_reg_op(ctx, status_reg), MIR_new_int_op(ctx, VIGIL_STATUS_OK)));
            vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg,
                                       status_reg);
            V_LOAD_ALL();
            break;
        case VREG_TAIL_CALL: {
            V_FLUSH_ALL();
            vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg,
                                       status_reg);
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
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                         MIR_new_mem_op(ctx, MIR_T_I64,
                                                        (MIR_disp_t)((size_t)VREG_GET_A(instr) * sizeof(vigil_value_t)),
                                                        regs_reg, 0U, 1U)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV,
                                         MIR_new_mem_op(ctx, MIR_T_I64, 0, out_reg, 0U, 1U),
                                         MIR_new_reg_op(ctx, tmp0_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV,
                                         MIR_new_mem_op(ctx, MIR_T_I64, 0, regs_reg, 0U, 1U),
                                         MIR_new_reg_op(ctx, tmp0_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, tmp0_reg), MIR_new_reg_op(ctx, tmp3_reg),
                                         MIR_new_int_op(ctx, 1)));
            MIR_append_insn(ctx, func,
                            MIR_new_insn(ctx, MIR_MOV, MIR_new_mem_op(ctx, MIR_T_I64, offsetof(vigil_vm_t, stack_count),
                                                                      vm_reg, 0U, 1U),
                                         MIR_new_reg_op(ctx, tmp0_reg)));
            MIR_append_insn(ctx, func,
                            MIR_new_ret_insn(ctx, 1U, MIR_new_int_op(ctx, VIGIL_STATUS_OK)));
            continue;
        }
        case VREG_RELEASE:
            vigil_aot_emit_store_constant(ctx, func, regs_reg, VREG_GET_A(instr), VIGIL_NANBOX_NIL);
            MIR_append_insn(ctx, func, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, V(VREG_GET_A(instr))),
                            MIR_new_uint_op(ctx, VIGIL_NANBOX_NIL)));
            break;
        case VREG_RETURN:
            vigil_aot_emit_reload_regs(ctx, func, regs_reg, vm_reg, tmp0_reg, tmp1_reg, tmp2_reg, tmp3_reg,
                                       status_reg);
            V_FLUSH_ALL();
            if (VREG_GET_B(instr) > 0U)
            {
                uint8_t ret_slot = VREG_GET_A(instr);
                /* Read return value from memory (V_FLUSH_ALL already wrote it). */
                MIR_append_insn(ctx, func,
                                MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, tmp0_reg),
                                             MIR_new_mem_op(ctx, MIR_T_I64,
                                                            (MIR_disp_t)((size_t)ret_slot * sizeof(vigil_value_t)),
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
            free(v); free(labels); /* alloc-check: exempt - calloc-allocated */
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
    MIR_load_external(ctx, "vigil_vm_grow_stack", (void *)vigil_vm_grow_stack);
    MIR_load_external(ctx, "vigil_aot_push_self_frame", (void *)vigil_aot_push_self_frame);
    MIR_load_external(ctx, "vigil_aot_numeric_call", (void *)vigil_aot_numeric_call);
    MIR_load_external(ctx, "vigil_aot_numeric_call_native", (void *)vigil_aot_numeric_call_native);
    MIR_load_external(ctx, "vigil_aot_numeric_fail", (void *)vigil_aot_numeric_fail);
    MIR_gen_init(ctx);
    cache->generator_initialized = 1;
    MIR_link(ctx, MIR_set_gen_interface, NULL);

    cache->entry = (vigil_aot_entry_fn)MIR_gen(ctx, func);
    free(v); free(labels); /* alloc-check: exempt - calloc-allocated */
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
#if 0 /* wrapper disabled — correctness issue with multi-file class programs */
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
#endif /* wrapper disabled */

static vigil_status_t vigil_aot_build(const vigil_reg_chunk_t *rc, vigil_aot_cache_t **out_cache,
                                      vigil_error_t *error)
{
    if (vigil_aot_chunk_is_numeric_subset(rc))
    {
        return vigil_aot_build_numeric(rc, out_cache, error);
    }

    /* Non-numeric functions fall back to interpreter at call time. */
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "AOT: non-numeric function");
    return VIGIL_STATUS_UNSUPPORTED;
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

    if (status == VIGIL_STATUS_UNSUPPORTED)
        return vigil_regvm_execute(vm, rc, out_value, error);
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
