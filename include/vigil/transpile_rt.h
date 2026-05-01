/*
 * vigil_transpile_rt.h — Runtime helpers for transpiled C code.
 *
 * These functions bridge between the transpiler's register-based code
 * and the Vigil runtime.  They are linked into the generated project
 * alongside libvigil.
 */
#ifndef VIGIL_TRANSPILE_RT_H
#define VIGIL_TRANSPILE_RT_H

#include "vigil/runtime.h"
#include "vigil/status.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Execution context passed to every transpiled function. */
    typedef struct vigil_tc
    {
        vigil_vm_t *vm;
        vigil_runtime_t *runtime;
        const vigil_value_t *constants;
        size_t constant_count;
        const vigil_object_t *function;
        uint64_t ret_buf[8]; /* Multi-return buffer for VREG_CALL */
        uint8_t ret_count;   /* Number of return values in ret_buf */
    } vigil_tc_t;

    /* Convert a value to its string representation. */
    vigil_status_t vigil_tc_to_string(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                      vigil_error_t *error);

    /* Call a native function by constant-pool index. */
    vigil_status_t vigil_tc_call_native(vigil_tc_t *tc, vigil_value_t *regs, uint8_t arg_base, uint8_t arg_count,
                                        uint32_t const_idx, vigil_error_t *error);

    /* Create a new error object. */
    vigil_status_t vigil_tc_new_error(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *msg,
                                      const vigil_value_t *kind, vigil_error_t *error);

    /* Get error kind (i64). */
    vigil_status_t vigil_tc_get_error_kind(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *err_val,
                                           vigil_error_t *error);

    /* Get error message (string). */
    vigil_status_t vigil_tc_get_error_msg(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *err_val,
                                          vigil_error_t *error);

    /* Format f64 with precision. */
    vigil_status_t vigil_tc_format_f64(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *val, uint8_t precision,
                                       vigil_error_t *error);

    /* Parse string to i32 (returns value + error in dst[0], dst[1]). */
    vigil_status_t vigil_tc_parse_i32(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                      vigil_error_t *error);

    /* Parse string to f64 (returns value + error in dst[0], dst[1]). */
    vigil_status_t vigil_tc_parse_f64(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                      vigil_error_t *error);

    /* Parse string to bool (returns value + error in dst[0], dst[1]). */
    vigil_status_t vigil_tc_parse_bool(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *src,
                                       vigil_error_t *error);

    /**
     * Execute a VM stack-based operation.  Syncs the given registers to the
     * VM stack, calls the operation, and copies results back.
     *
     * @param tc        Transpiler context.
     * @param regs      Register file (vigil_reg_t[] cast to vigil_value_t*).
     * @param opcode    The VREG_* opcode to execute.
     * @param a         A operand from the instruction.
     * @param b         B operand from the instruction.
     * @param c         C operand from the instruction.
     * @param error     Error output.
     */
    vigil_status_t vigil_tc_vm_op(vigil_tc_t *tc, vigil_value_t *regs, uint8_t opcode, uint8_t a, uint8_t b, uint8_t c,
                                  vigil_error_t *error);

    /* Create a new class instance with nanbox-encoded field values. */
    vigil_status_t vigil_tc_new_instance(vigil_tc_t *tc, vigil_value_t *regs, uint8_t dest, uint16_t class_idx,
                                         uint8_t fields_base, uint8_t field_count, vigil_error_t *error);

    /* Call a function value (closure/function object in a register). */
    vigil_status_t vigil_tc_call_value(vigil_tc_t *tc, vigil_value_t *regs, uint8_t ret, uint16_t arg_count,
                                       uint8_t arg_base, vigil_error_t *error);

    /* Call an extern function by descriptor constant index. */
    vigil_status_t vigil_tc_call_extern(vigil_tc_t *tc, vigil_value_t *regs, uint8_t ret, uint8_t const_idx,
                                        uint8_t arg_count, uint8_t arg_base, vigil_error_t *error);

    /* Call an interface method on a receiver object. */
    vigil_status_t vigil_tc_call_interface(vigil_tc_t *tc, vigil_value_t *regs, uint8_t ret, uint8_t iface_idx,
                                           uint8_t arg_count, uint32_t method_idx, uint8_t arg_base,
                                           vigil_error_t *error);

    /* Format a value with a format specifier (f-string {x:spec}). */
    vigil_status_t vigil_tc_format_spec(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *val, uint32_t word1,
                                        uint32_t word2, vigil_error_t *error);

    /* Compare two register values for equality (handles objects). */
    int vigil_tc_values_equal(const vigil_value_t *regs, uint8_t b, uint8_t c);

    /* Compare two register values for ordering, mirroring generic VM comparisons. */
    int vigil_tc_values_lt(const vigil_value_t *regs, uint8_t b, uint8_t c);
    int vigil_tc_values_le(const vigil_value_t *regs, uint8_t b, uint8_t c);

    /* Test if a register value is truthy (handles nanboxed bools, nil, raw ints). */
    int vigil_tc_is_truthy(vigil_value_t v);

    /* Coerce a raw or nanboxed value to an i32, following VM conversion rules. */
    int64_t vigil_tc_to_i32_value(vigil_value_t src);

    /* Negate a raw or nanboxed numeric value, preserving f64 operands. */
    vigil_value_t vigil_tc_negate(vigil_value_t src);

    /* Move a value between registers, retaining objects and releasing the old value. */
    void vigil_tc_move_reg(vigil_value_t *dst, vigil_value_t src);

    /* Call a sibling transpiled function with multi-return support. */
    vigil_status_t vigil_tc_call_self(vigil_tc_t *tc, vigil_value_t *regs, uint8_t ret, size_t func_idx,
                                      uint16_t arg_count, uint8_t arg_base, vigil_error_t *error);

    /* Execute a string operation (to_upper, reverse, trim, etc.). */
    vigil_status_t vigil_tc_string_op(vigil_tc_t *tc, vigil_value_t *regs, uint8_t dest, uint8_t top_reg,
                                      uint8_t sub_op, vigil_error_t *error);

    /* Capture a deferred action for execution when the current function returns. */
    vigil_status_t vigil_tc_defer(vigil_tc_t *tc, vigil_value_t *regs, uint8_t defer_op, uint8_t top_reg,
                                  uint32_t operand_a, uint32_t operand_b, uint32_t operand_c, vigil_error_t *error);

    /* Drain deferred actions for the current VM frame in LIFO order. */
    vigil_status_t vigil_tc_drain_defers(vigil_tc_t *tc, vigil_error_t *error);

    /* Generic add: string concatenation if both objects, integer add otherwise. */
    vigil_status_t vigil_tc_generic_add(vigil_tc_t *tc, vigil_value_t *dst, const vigil_value_t *lhs,
                                        const vigil_value_t *rhs, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* VIGIL_TRANSPILE_RT_H */
