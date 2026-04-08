# Phase 3: MIR Optimization Passes — Technical Analysis

## Finding: No Untapped Optimization Levels

Vigil does not call `MIR_gen_set_optimize_level()`. MIR defaults to level 2.

### MIR Optimization Levels (from mir-gen.c:209)

| Level | Features |
|-------|----------|
| 0 | Fast generation, no optimization |
| 1 | Register allocation + instruction combiner |
| 2 | + GVN/CCP, BB cloning, pressure relief **(DEFAULT)** |
| ≥3 | Same as level 2 (no additional passes in source) |

### Verification

Searched `deps/mir/mir-gen.c` for all `optimize_level` references.
The highest threshold checked is `optimize_level >= 2`. There are no
`optimize_level >= 3` or `optimize_level > 2` checks. The comment
">=3: everything" is misleading — level 3 enables nothing beyond level 2.

### Level 2 Passes (already active)

From the MIR source, level 2 enables:
- Global Value Numbering (GVN) — eliminates redundant computations
- Conditional Constant Propagation (CCP) — folds constants
- Basic Block cloning — improves branch prediction
- Register pressure relief — reduces spills
- Dead code elimination
- Unreachable BB removal

### Why MIR Can't Help More

MIR's optimizer operates on MIR IR, not on Vigil semantics. The current
codegen emits NaN-box encode/decode as explicit AND/OR/MOV instructions.
From MIR's perspective, these are meaningful operations that cannot be
eliminated — it doesn't know they're just type tagging overhead.

Example: MIR sees this for `x = a + b` (i32):
```
MOV  t0, [mem+B*8]    ; load
EXT32 t0, t0          ; "compute" (sign extend)
MOV  t1, [mem+C*8]    ; load
EXT32 t1, t1          ; "compute"
ADDO t2, t0, t1       ; the actual add
AND  t3, t2, MASK     ; "compute" (tag)
OR   t3, t3, TAG      ; "compute" (tag)
MOV  [mem+A*8], t3    ; store
```

GVN can eliminate redundant loads if the same register is read twice.
CCP can fold if operands are constants. But the AND/OR tagging sequence
is unique per operation and cannot be optimized away.

## Recommendation

Phase 3 is not a standalone optimization. The correct approach is:

1. Improve the quality of MIR IR generated (Phase 2)
2. MIR's existing level-2 optimizer will automatically benefit

No code changes needed for this phase. Just verify that `MIR_gen_set_optimize_level`
is not accidentally called with a lower level anywhere.

## Potential Future MIR Improvements

If Vigil's AOT becomes more sophisticated, these MIR features could help:

- **`MIR_set_lazy_gen_interface`**: Lazy compilation — only JIT functions
  when first called. Could reduce startup time for large programs.
- **`MIR_set_lazy_bb_gen_interface`**: Even lazier — compile basic blocks
  on demand. Useful for rarely-taken error paths.
- **Debug output**: `MIR_gen_set_debug_file` + `MIR_gen_set_debug_level`
  can dump the optimization pipeline for debugging codegen quality.
