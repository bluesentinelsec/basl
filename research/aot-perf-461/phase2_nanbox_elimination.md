# Phase 2: Eliminate NaN-Boxing in AOT — Technical Analysis

## Problem

Every typed arithmetic operation in AOT codegen performs a full NaN-box
decode→operate→encode cycle, even though the compiler already knows the
operand types at compile time (e.g., `VREG_ADD_I32` guarantees both
operands are i32).

## Current Codegen: VREG_ADD_I32

From `aot.c`, the `VREG_ADD_I32` case emits:

```
; Decode B: load uint64, sign-extend to extract i32
MOV   tmp0, [regs + B*8]           ; load 8-byte NaN-boxed value
EXT32 tmp0, tmp0                    ; sign-extend low 32 bits

; Decode C: same
MOV   tmp1, [regs + C*8]
EXT32 tmp1, tmp1

; Operate
ADDO  tmp2, tmp0, tmp1              ; add with overflow detection
BO    overflow_label                ; branch on overflow

; Encode result: mask + tag + store
AND   tmp3, tmp2, 0x0000FFFFFFFFFFFF  ; PAYLOAD_MASK
OR    tmp3, tmp3, 0x7FFC010000000000  ; TAG_INT
MOV   [regs + A*8], tmp3             ; store 8-byte NaN-boxed result
```

**Total: 9 MIR instructions per i32 add.**

## Proposed Approach: Typed Register Slots

### Concept

During AOT compilation, maintain a type map for each VREG register.
When a register is known to hold an i32 (from a typed opcode), store
the raw i32 value without NaN-boxing. Only encode when the value must
cross a type boundary (function return, call argument, etc.).

### Implementation Strategy

**Sub-approach A: Shadow typed slots (simpler)**

Allocate a parallel array of MIR virtual registers, one per VREG slot.
For typed operations, operate on the MIR virtual registers directly.
Only flush to the NaN-boxed register file at call boundaries and returns.

```c
// During codegen setup:
MIR_reg_t typed_regs[256];  // one per VREG slot
int       typed_state[256]; // 0=unknown, 1=i32, 2=i64, 3=f64

// For VREG_ADD_I32:
if (typed_state[B] == TYPE_I32 && typed_state[C] == TYPE_I32) {
    // Both already in MIR regs — no load needed
    MIR_append_insn(ctx, func,
        MIR_new_insn(ctx, MIR_ADDS, typed_regs[A], typed_regs[B], typed_regs[C]));
    typed_state[A] = TYPE_I32;
} else {
    // Fallback: decode from NaN-boxed slot
    emit_decode(B);
    emit_decode(C);
    // ... operate ...
    typed_state[A] = TYPE_I32;
}

// At CALL boundaries:
for each live typed reg:
    emit_encode_and_store(reg);
// After CALL:
    invalidate all typed_state[];
```

**Sub-approach B: Full SSA promotion (harder, better)**

Build SSA form over the VREG bytecode. Each typed opcode produces a
new SSA value. Phi nodes at control flow joins. MIR's register allocator
handles the rest.

This is significantly more complex but produces optimal code — values
stay in machine registers across basic blocks.

### Proposed Codegen: VREG_ADD_I32 (typed slots)

```
; If B and C are already in MIR regs (common case in loops):
ADDS  reg_A, reg_B, reg_C
BO    overflow_label
; Done. No memory access at all.
```

**Total: 2 MIR instructions per i32 add (vs 9 currently).**

### Boundary Encoding

Values must be NaN-box encoded when:
1. Passed as arguments to a non-AOT function call
2. Returned from the AOT function
3. Stored to a global variable
4. At control flow joins where one path has typed and another has boxed

Encoding is the reverse of the current decode:
```
AND   tmp, value, PAYLOAD_MASK
OR    tmp, tmp, TAG_INT
MOV   [regs + slot*8], tmp
```

### Adding i64 Ops to AOT Subset

Before NaN-box elimination helps `arith`, the i64 opcodes must be added
to the AOT subset filter and codegen. Required additions:

**Subset filter (`vigil_aot_chunk_is_numeric_subset`):**
```c
case VREG_ADD_I64:
case VREG_SUB_I64:
case VREG_MUL_I64:
case VREG_DIV_I64:
case VREG_MOD_I64:
case VREG_ADDI_I64:
case VREG_SUBI_I64:
case VREG_INC_I64:
case VREG_FORLOOP_I64:
case VREG_FORLOOP_I32:
```

**Codegen:** Similar to i32 but using 64-bit MIR operations and the
full NaN-box int encoding (48-bit payload with sign extension).

### Impact on Bitwise Operations

The bitwise benchmark is dominated by `VREG_BAND`, `VREG_BOR`, `VREG_BXOR`,
`VREG_SHL`, `VREG_SHR`. Each currently does decode→op→encode. With typed
slots, the inner loop becomes:

```
; Current (per iteration, ~6 ops per bitwise op × 5 ops = 30 MIR insns):
loop:
  decode(i), decode(0xFF), AND, encode → x_xor_arg
  decode(x), decode(x_xor_arg), XOR, encode → x
  decode(i), decode(3), RSH, encode → shift_arg
  decode(x), decode(shift_arg), OR, encode → x
  decode(x), decode(0x7FFFFFFF), AND, encode → x
  ; loop increment + branch

; Proposed (per iteration, ~8 MIR insns total):
loop:
  AND   t0, i_reg, 0xFF
  XOR   x_reg, x_reg, t0
  RSH   t1, i_reg, 3
  OR    x_reg, x_reg, t1
  AND   x_reg, x_reg, 0x7FFFFFFF
  ADDS  i_reg, i_reg, 1
  BLT   loop, i_reg, 10000000
```

This is close to what Dart AOT generates.

## Complexity Assessment

| Sub-task | Effort | Impact |
|----------|--------|--------|
| Add i64 ops to subset + codegen | Low | Enables arith AOT |
| Add FORLOOP_I32/I64 to subset | Low | Enables loop AOT |
| Typed slot tracking (approach A) | Medium | 3-5x on bitwise |
| Boundary encode/decode | Medium | Correctness critical |
| Full SSA promotion (approach B) | High | Optimal codegen |

## Recommended Sequence

1. Add i64 + forloop ops (PR 2) — low risk, enables measurement
2. Typed slot tracking for i32 ops (PR 3a) — medium risk, big payoff
3. Extend typed slots to i64/f64 (PR 3b) — incremental
4. (Stretch) SSA promotion (PR 4) — only if approach A is insufficient
