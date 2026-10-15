VERDICT: FIXED-INPUT. No operation differs between x86-64 and arm64 on any input its contract accepts; all seven rows were the oracle feeding the two machines different (or undefined) inputs.

# Seven oracle rows differed between x86-64 and arm64

Date: 2026-10-15. BASE: sprint/fixed-perf `ac247f7`. Benches: hetzner2 (x86-64, GCC 13.3.0, plus clang 18.1.3 as a cross-check) and the Studio (Apple M3 Ultra, Apple clang 21.0.0). Same source and flags on both (`-DCMAKE_BUILD_TYPE=Release -DFIX_BUILD_BENCHMARKS=ON`, `-ffp-contract=off` from CMakeLists). Evidence lines: `benchmark/experiments/oracle-rows-x86-arm64/raw.txt`.

## What differed, per row

| row | first differing input | cause |
|---|---|---|
| fixMulQuat | index 63757, the first random-stream input | input fill: unsequenced argument draws |
| fixNormalizeQuat | 63757 | input fill: unsequenced argument draws |
| fixRotateVector | 63757 | input fill: unsequenced argument draws |
| fixInvRotateVector | 63757 | input fill: unsequenced argument draws |
| fixNLerp | 63757 | input fill: unsequenced argument draws |
| fixAABB_Transform | 63757 | input fill: unsequenced argument draws |
| fixQuantize | 1767 (cross grid: in0=INT64_MAX, in1=65537) | input fill: out-of-domain input, undefined double-to-int64 conversion |

**The six quaternion rows.** Every random quaternion was drawn as `oracleQuat( NULL, oracleRngNext( rng ), oracleRngNext( rng ) )`. C leaves the order in which function arguments are evaluated unspecified (C11 6.5.2.2p10). GCC evaluates these right to left and clang left to right, so the two words of every random quaternion were swapped between the machines. At index 63757, x86-64 fed `(38755, 2967, -51958, -9192)` and arm64 fed `(-51958, -9192, 38755, 2967)`. Each machine's output is the correct output for the input it was actually given. The grid phases (indices 0 to 63756) use no draws in argument position, and they agree exactly.

This depends on the compiler, not the architecture. The unchanged BASE built with **clang on x86-64** gives exactly the arm64 hashes for all six rows: fixMulQuat 89460e7fdc0afa12, fixNormalizeQuat c5c7a65be194eb17, and so on. The baseline matched x86-64 only because it was written with GCC.

**fixQuantize.** Both machines got the same inputs but produced different outputs, and only where the scaled value is 2^63 or more. Converting such a double to int64 is undefined (C11 6.3.1.4p1). On x86-64, `cvttsd2si` returns INT64_MIN. On arm64, `fcvtzs` saturates, giving INT64_MAX, or -INT64_MAX on the negative branch. The oracle's cross grid pairs INT64_MAX/2^16 with scale 65537 and similar inputs. fixQuantize's own contract excludes them: the header calls it an authority-only double boundary helper. test/quantize_test.c says its sweeps stay inside the asserted domain, and fixQuantizeClamped is the out-of-range form. The oracle's own rule is that every fill produces inputs the operation's contract accepts, and this fill broke that rule.

## The fix (benchmark/oracle.c only)

- `oracleQuatDraw()` draws the two words into sequenced locals, r1 first and then r0. That is the GCC order the baseline was written with, so the x86-64 hashes of the six rows do not move.
- `fillQuantizeInDomain()` is used only by the fixQuantize row; fixDequantize keeps `fillQuantize`. It halves the raw value toward zero until `|value * scale| + 0.5 < 2^63`. The check uses the same IEEE-754 double arithmetic the operation does, so it gives the same answer on every target. The same clamp is applied in `oracleFillRest` for the grid phases.

Results with the fix:
- x86-64, both 128-bit arms, against the **unchanged** baseline: 30 of 31 rows match, including all six quaternion rows. Only fixQuantize moved, to 8100e7c61c94a956, because its input set changed.
- arm64, both arms: the same 30 rows match, and fixQuantize gives the same 8100e7c61c94a956 with the same 16 segment hashes.
- x86-64 with clang: the six rows match the baseline and fixQuantize gives 8100e7c61c94a956.

Only after both architectures agreed was the one fixQuantize row regenerated (`--op fixQuantize --write` on x86-64, spliced in). No other baseline row changed. ctest passes 36 of 36 on both machines. The frozen fixQuantize prefix hash in test/oracle_unit_test.c is untouched and still passes, because the first 1000 inputs all use scale 1 and are in domain.

No experiment.patch: no library operation differs on an input inside its contract.

## Do not repeat

- Never make more than one RNG draw inside a single function call's argument list or a single initializer list. Draw into locals first. Any `f( next(), next() )` in oracle.c is a cross-compiler hash split.
- A "differs on arm64" row is not proof of an architecture difference. Build the same tree with the other compiler on the same machine before blaming the hardware. Here clang on x86-64 reproduced the arm64 hashes exactly.
- Every oracle input to a double-to-integer conversion has to be inside the range where the conversion is defined. Outside it, x86-64 and arm64 legitimately disagree.

## Not done / open

- fixQuantize itself still has undefined behaviour for |value*scale| >= 2^63, with no assert in the way. The behaviour is inherited verbatim from delta (delta_quantize.h), which this library must match bit for bit. There are two ways to harden it: add a `FIX_ASSERT` on the range, or saturate like fixQuantizeClamped. Either one is a cross-repo decision for the owner and delta, not a change for this card.
- The full emulated-arm oracle was not re-run end to end against the regenerated baseline on either machine. Its other 30 rows were already OK against the unchanged baseline, and `--op fixQuantize` matches on both emulated builds.
