# AOT Performance Research — Issue #461

Research findings for closing the performance gap between Vigil AOT and Dart AOT.

## Hardware

- Apple M1 Max (arm64)
- macOS

## Software Versions

- Vigil 0.2.3
- Lua 5.4.7
- Python 3.12.3
- Dart SDK 3.11.4 (AOT compiled)

## Baseline Measurements

All times are wall-clock seconds, best of 3 runs.

| Benchmark | Vigil AOT | Vigil Interp | Lua 5.4 | Python 3.12 | Dart AOT | Vigil/Dart Gap |
|-----------|-----------|-------------|---------|-------------|----------|----------------|
| fib(35) | 0.574 | 0.598 | 0.560 | 0.938 | 0.080 | 7.2x |
| arith(100M i64) | 2.516 | 2.523 | 3.561 | 10.559 | 0.070 | 35.9x |
| bitwise(10M) | 0.494 | 0.494 | 0.164 | 1.643 | 0.034 | 14.5x |
| nested(1M) | 0.040 | 0.039 | 0.029 | 0.125 | 0.031 | 1.3x |
| ack(3,10) | 2.848 | 2.159 | 0.772 | 2.818 | 0.221 | 12.9x |

## Key Observations

### 1. AOT provides zero benefit for most benchmarks

AOT and interpreter times are nearly identical for fib, arith, bitwise, and nested.
This means the MIR JIT overhead is being paid but the generated code is no faster
than the threaded interpreter.

### 2. AOT is a *regression* for ack

ack(3,10): AOT = 2.848s, Interpreter = 2.159s — AOT is **32% slower**.

Root cause: `VREG_CALL_SELF` in AOT emits a call to the C helper
`vigil_aot_numeric_call_self()`, which does:
1. `vigil_vm_push_frame()` — full frame setup through C runtime
2. `vigil_aot_ensure()` — cache lookup (redundant on hot path)
3. `cache->entry()` — call back into MIR-generated code
4. `vigil_vm_pop_frame()` — frame teardown

This native→C→native round-trip per recursive call adds ~300ns overhead
compared to the interpreter's inline frame management.

### 3. arith benchmark doesn't use AOT at all

The `arith` benchmark uses i64 types. The AOT subset filter
(`vigil_aot_chunk_is_numeric_subset`) only accepts i32 arithmetic ops.
`VREG_ADD_I64`, `VREG_SUB_I64`, `VREG_MUL_I64`, `VREG_DIV_I64` are all
rejected, so the entire function falls back to the interpreter.

### 4. bitwise benchmark: AOT active but no speedup

bitwise uses i32 ops that ARE in the AOT subset, but AOT (0.494s) equals
interpreter (0.494s). The NaN-box decode→operate→encode cycle in the MIR
codegen is the bottleneck — each i32 operation requires:
- Load uint64 from register slot
- EXT32 (sign-extend to extract i32)
- Perform operation
- AND with payload mask
- OR with tag bits
- Store uint64 back

That's 6 MIR instructions per i32 op. Without NaN-boxing, it would be 3
(load, op, store) — or potentially just 1 if values stay in MIR registers.

### 5. Vigil is slower than Lua on bitwise

Lua 5.4 (0.164s) is 3x faster than Vigil (0.494s) on bitwise. Lua uses
tagged values but its JIT-less interpreter is highly optimized with computed
goto dispatch. Vigil's register VM dispatch is competitive but the NaN-box
overhead on bitwise ops is significant.

## Phase Analysis

### Phase 1: Inline frame management into MIR codegen

**Target benchmarks:** fib, ack

**Current CALL_SELF path (6 steps, 2 ABI boundary crossings):**
```
MIR native code
  → C ABI call to vigil_aot_numeric_call_self()
    → vigil_vm_push_frame()
    → vigil_aot_ensure() (redundant cache lookup)
    → cache->entry() (MIR native)
    → vigil_vm_pop_frame()
  → return to MIR native
```

**Proposed CALL_SELF path (3 steps, 0 ABI boundary crossings):**
```
MIR native code:
  increment vm->frame_count
  set new frame base_slot = current_base + arg_base_r
  direct MIR call to self (same function pointer)
  decrement vm->frame_count
  continue
```

**Expected impact:**
- Eliminates ~300ns per recursive call
- fib(35) makes ~29M recursive calls → potential savings: ~8.7s of overhead
  removed. Current overhead is ~0.5s, so the C call overhead is ~17ns/call.
  Inlining should reduce this to ~2-5ns/call.
- Estimated fib improvement: 0.574s → ~0.15-0.25s (2-4x speedup)
- Estimated ack improvement: 2.848s → ~0.5-1.0s (3-6x speedup)
- ack should also fix the AOT regression vs interpreter

**Complexity:** Medium. Requires emitting MIR instructions for frame
push/pop inline, plus stack growth checks. The frame struct layout must
be accessed directly from MIR.

**Risk:** Stack overflow detection must still work. Need to check
`frame_count < max_frames` in MIR.

### Phase 2: Eliminate NaN-boxing in AOT code

**Target benchmarks:** bitwise, arith, nested, fib (secondary)

**Current i32 add codegen (6 MIR instructions):**
```
MOV  tmp0, [regs + B*8]        ; load nanboxed uint64
EXT32 tmp0, tmp0               ; sign-extend low 32 bits
MOV  tmp1, [regs + C*8]        ; load nanboxed uint64
EXT32 tmp1, tmp1               ; sign-extend low 32 bits
ADDO tmp2, tmp0, tmp1          ; add with overflow
BO   overflow_label             ; branch on overflow
AND  tmp3, tmp2, PAYLOAD_MASK  ; mask to 48 bits
OR   tmp3, tmp3, TAG_INT       ; apply NaN-box tag
MOV  [regs + A*8], tmp3        ; store nanboxed result
```

**Proposed i32 add codegen (3 MIR instructions):**
```
MOV  tmp0, [regs + B*4]        ; load raw i32 (4-byte slot)
MOV  tmp1, [regs + C*4]        ; load raw i32
ADDS tmp2, tmp0, tmp1          ; add (overflow check via ADDO)
BO   overflow_label
MOV  [regs + A*4], tmp2        ; store raw i32
```

Or even better — keep values in MIR virtual registers across operations,
only loading/storing at function entry/exit and across calls:

**Optimal codegen (register-resident):**
```
; values already in MIR regs from previous ops
ADDS result_reg, b_reg, c_reg
; result stays in MIR reg for next op
```

**Expected impact:**
- bitwise: 0.494s → ~0.05-0.10s (5-10x speedup) — eliminates tag overhead
- arith: requires adding i64 ops to AOT subset first, then same benefit
- nested: 0.040s → ~0.010-0.015s (marginal absolute improvement)

**Complexity:** High. Two sub-approaches:

**(a) Narrow slots (simpler):** Change the register file layout for AOT
functions to use typed slots (i32 = 4 bytes, i64 = 8 bytes) instead of
uniform 8-byte NaN-boxed slots. Encode/decode only at call boundaries.

**(b) Full register promotion (harder, better):** Track which VREG registers
hold typed values and keep them in MIR virtual registers. Only spill to
the stack-based register file when needed (calls, joins with different types).
This is essentially SSA construction over the register VM bytecode.

**Risk:** Values must be re-encoded when crossing to interpreter (calls to
non-AOT functions, native calls). Correctness of the boundary encoding is
the main risk.

### Phase 3: MIR optimization passes

**Current state:** Vigil does not call `MIR_gen_set_optimize_level()`.
MIR defaults to level 2 (GVN + CCP + register allocation + combiner).

**Available levels:**
- 0: Fast generation, no optimization
- 1: Register allocation + instruction combiner
- 2: + GVN/CCP, BB cloning, pressure relief (DEFAULT)
- 3+: Same as 2 (no additional passes in current MIR source)

**Finding:** Level 2 is already the default. Level 3 adds nothing.
Level 0 or 1 would be *worse*. There is no untapped optimization
from simply changing the level.

**However:** MIR's optimizer can only work with what it's given. The
current codegen emits very literal instruction sequences (load-decode-op-
encode-store for every bytecode op). MIR's GVN can eliminate redundant
loads, and CCP can fold constants, but it cannot eliminate the fundamental
NaN-box encode/decode pattern because those are semantically meaningful
operations from MIR's perspective.

**Expected impact:** Negligible from level changes alone. The real gains
come from giving MIR better input (Phase 2).

**Recommendation:** Not a standalone phase. Fold into Phase 2 — once
NaN-boxing is eliminated, MIR's existing level-2 optimizer will
automatically benefit from cleaner input IR.

## Recommended Priority Order

### Priority 1: Phase 1 — Inline frame management

**Rationale:**
- Fixes the ack AOT regression (AOT slower than interpreter)
- Largest single-benchmark improvement potential (ack: 12x gap to Dart)
- Medium complexity, well-scoped change
- Benefits fib and ack — the two recursion-heavy benchmarks
- No risk of regression on non-recursive benchmarks

### Priority 2: Phase 2 — Eliminate NaN-boxing + add i64 AOT ops

**Rationale:**
- Addresses the largest absolute gap (arith: 35.9x to Dart)
- Fixes bitwise (14.5x gap) and improves all numeric benchmarks
- Higher complexity but highest total impact across all benchmarks
- Should be split into sub-PRs:
  1. Add i64 arithmetic ops to AOT subset (enables arith benchmark)
  2. Eliminate NaN-box encode/decode for typed ops
  3. (Optional) Register promotion for cross-op value reuse

### Priority 3: Phase 3 — MIR optimization passes

**Rationale:**
- No standalone benefit from level changes
- Becomes relevant only after Phase 2 improves codegen quality
- Zero implementation effort (just verify level 2 is set)

## Projected Performance After All Phases

| Benchmark | Current | After P1 | After P1+P2 | Dart AOT | Target Met? |
|-----------|---------|----------|-------------|----------|-------------|
| fib(35) | 0.574 | ~0.20 | ~0.10-0.15 | 0.080 | Close |
| arith(100M) | 2.516 | 2.516 | ~0.10-0.20 | 0.070 | Close |
| bitwise(10M) | 0.494 | 0.494 | ~0.05-0.10 | 0.034 | Close |
| nested(1M) | 0.040 | 0.040 | ~0.010 | 0.031 | Yes |
| ack(3,10) | 2.848 | ~0.8 | ~0.3-0.5 | 0.221 | Close |

Note: Dart AOT uses a sophisticated optimizing compiler with type
specialization, inlining, and escape analysis. Matching Dart exactly
may require additional work beyond these three phases (e.g., function
inlining, loop unrolling). But these phases should close the gap from
7-36x down to 1.5-3x.

## Implementation Plan

### PR 1: Inline CALL_SELF in AOT codegen
1. Emit MIR instructions for frame_count increment/decrement
2. Emit stack capacity check with growth call on slow path
3. Emit direct MIR call to self entry point
4. Remove `vigil_aot_numeric_call_self` C helper usage from codegen
5. Benchmark: fib, ack must improve; arith, bitwise, nested must not regress

### PR 2: Add i64 ops to AOT subset
1. Add `VREG_ADD_I64`, `VREG_SUB_I64`, `VREG_MUL_I64`, `VREG_DIV_I64`,
   `VREG_MOD_I64`, `VREG_FORLOOP_I64`, `VREG_INC_I64`, `VREG_ADDI_I64`,
   `VREG_SUBI_I64` to the subset filter
2. Emit corresponding MIR codegen (initially with NaN-box encode/decode)
3. Benchmark: arith must now use AOT path

### PR 3: Eliminate NaN-box encode/decode for typed AOT ops
1. Change AOT codegen to operate on raw typed values
2. Only encode at function return and call boundaries
3. Only decode at function entry and after calls
4. Benchmark: bitwise, arith should see 5-10x improvement

### PR 4: (Stretch) Register promotion
1. Track live typed values in MIR virtual registers
2. Eliminate redundant loads/stores between consecutive typed ops
3. Benchmark: all numeric benchmarks should approach Dart-level performance
