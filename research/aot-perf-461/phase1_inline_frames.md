# Phase 1: Inline Frame Management — Technical Analysis

## Problem

`VREG_CALL_SELF` in AOT codegen calls the C helper `vigil_aot_numeric_call_self()`.
This function performs a full frame push/pop cycle through the C runtime for every
recursive call. The native→C→native round-trip is the dominant cost in recursion-heavy
benchmarks.

## Current Call Path (aot.c:79-141)

```c
vigil_aot_numeric_call_self(vm, rc, ret, arg_count, arg_base_r, error)
{
    frame = &vm->frames[vm->frame_count - 1];
    base = frame->base_slot;
    arg_base = base + arg_base_r;

    // 1. Full frame push through C runtime
    vigil_vm_push_frame(vm, frame->callable, frame->function,
                        frame->chunk, arg_base, error);

    // 2. Stack capacity check + potential realloc
    if (vm->stack_capacity < arg_base + rc->max_registers + 16)
        vigil_vm_grow_stack(vm, ...);

    // 3. Redundant cache lookup on hot path
    vigil_aot_ensure(rc, &cache, error);

    // 4. Call back into MIR-generated code
    status = cache->entry(vm, &vm->stack[base + ret], error);

    // 5. Frame pop
    vigil_vm_pop_frame(vm);
}
```

## MIR Codegen for CALL_SELF (aot.c, VREG_CALL_SELF case)

```
; Current: emit a C function call with 6 arguments
CALL  call_self_proto, call_self_import, status_reg,
      vm_reg, rc_ptr, ret_imm, arg_count_imm, arg_base_imm, error_reg
BNE   error_label, status_reg, VIGIL_STATUS_OK
; reload regs pointer (may have moved due to stack growth)
reload_regs(...)
```

## Proposed Inline Codegen

Replace the C helper call with direct MIR instructions:

```
; --- frame push (inline) ---
; Load frame_count
MOV   fc_reg, [vm_reg + offsetof(vigil_vm_t, frame_count)]
; Check frame limit
BGE   stack_overflow_label, fc_reg, MAX_FRAMES

; Set up new frame: frames[frame_count]
; frame_size = sizeof(vigil_vm_frame_t)
MUL   frame_off, fc_reg, FRAME_SIZE
ADD   frame_ptr, vm_reg, offsetof(vigil_vm_t, frames)
ADD   frame_ptr, frame_ptr, frame_off

; Copy callable/function/chunk from current frame
MOV   [frame_ptr + offsetof(callable)], [prev_frame + offsetof(callable)]
MOV   [frame_ptr + offsetof(function)], [prev_frame + offsetof(function)]
MOV   [frame_ptr + offsetof(chunk)],    [prev_frame + offsetof(chunk)]
MOV   [frame_ptr + offsetof(base_slot)], new_base

; Increment frame_count
ADD   fc_reg, fc_reg, 1
MOV   [vm_reg + offsetof(frame_count)], fc_reg

; --- stack capacity check ---
ADD   needed, new_base, max_registers + 16
MOV   cap, [vm_reg + offsetof(stack_capacity)]
BLT   grow_label, cap, needed
; (grow_label calls vigil_vm_grow_stack and jumps back)

; --- recursive call (direct MIR call to self) ---
CALL  self_proto, self_func_ref, status_reg, vm_reg, out_ptr, error_reg
BNE   error_label, status_reg, VIGIL_STATUS_OK

; --- frame pop (inline) ---
MOV   fc_reg, [vm_reg + offsetof(frame_count)]
SUB   fc_reg, fc_reg, 1
MOV   [vm_reg + offsetof(frame_count)], fc_reg

; reload regs pointer
reload_regs(...)
```

## Key Implementation Details

### Self-reference in MIR

The generated function needs to call itself. MIR supports this — the function
item can be referenced before `MIR_finish_func()`. Use `MIR_new_ref_op(ctx, func)`
where `func` is the current function being built.

### Frame struct layout

Need to access `vigil_vm_frame_t` fields from MIR. Required offsets:
- `offsetof(vigil_vm_t, frame_count)`
- `offsetof(vigil_vm_t, frames)`
- `offsetof(vigil_vm_t, stack)`
- `offsetof(vigil_vm_t, stack_count)`
- `offsetof(vigil_vm_t, stack_capacity)`
- `offsetof(vigil_vm_frame_t, base_slot)`
- `offsetof(vigil_vm_frame_t, callable)`
- `offsetof(vigil_vm_frame_t, function)`
- `offsetof(vigil_vm_frame_t, chunk)`
- `sizeof(vigil_vm_frame_t)`

These can be computed at compile time with `offsetof()` and embedded as
immediate constants in the MIR instructions.

### Stack growth slow path

Stack growth (`vigil_vm_grow_stack`) is rare but must be handled. Emit it
as a cold path:
1. Check capacity inline (fast path: branch not taken)
2. On capacity miss, call `vigil_vm_grow_stack` via C import
3. After growth, reload the regs pointer and continue

### Stack overflow check

Must check `frame_count < VIGIL_VM_MAX_FRAMES` before pushing. On overflow,
call the fail helper to set the error and return.

## Estimated Instruction Count

Current: 1 CALL instruction (but the callee does ~20 operations)
Proposed: ~15 MIR instructions for frame push/pop + 1 CALL for self

Net effect: eliminates 2 ABI boundary crossings and the redundant cache
lookup per recursive call. The MIR optimizer can also schedule the frame
setup instructions more efficiently than the C compiler can optimize the
helper function (since MIR sees the full context).

## Risks

1. **Stack pointer invalidation**: `vigil_vm_grow_stack` may realloc the
   stack array. After any call that might trigger growth, the `regs` pointer
   must be reloaded. The current code already handles this.

2. **Frame count overflow**: Must check before incrementing. Current C helper
   relies on `vigil_vm_push_frame` for this check.

3. **Defer actions**: The current `vigil_vm_pop_frame` may run defer actions.
   For self-recursive numeric functions, defers are unlikely, but the inline
   pop must handle this or assert no defers are pending.
