VERDICT: LEARNED — AVX-512 passes this pass's bit and speed checks; AVX2 native fails the 1.5x vector gate, so the complete patch must not be adopted.

# Batched Q48.16 multiply and dot product

**Hypothesis.** AVX-512's 64-bit lane multiply can make independent 4096-element `fixMulBatch` and `fixDotBatch3` calls faster than scalar loops while returning exactly the same bits. AVX2's several 32-bit partial products may cost more than scalar multiply. The experiment ran once on Zen 4 and AVX2-only Zen+, with no tuning pass afterward.

**Change.** The patch adds two SoA array APIs, scalar fallbacks, AVX2 and AVX-512DQ kernels, and separate function pointers selected once at library startup by the compiler's CPUID/XGETBV-aware `__builtin_cpu_supports` checks. `FIX_SATURATE` builds use only the scalar route; other platforms use scalar. The same extended `op_bench.c` source is compiled against baseline and candidate, using dependent scalar chains and independent 4096-lane batch calls. The new oracle rows are frozen from the scalar baseline; all pre-existing frozen rows remain untouched. New CTests compare both APIs against `fixMul` and `fixDot` for the structured edge grid crossed with itself, tails and unaligned pointers, plus 100 million seeded full-width lanes per build.

The bit argument is `b = (b >> 16) * 2^16 + (b & 65535)`, so `(a*b + half) >> 16 = a*(b >> 16) + ((a*(b & 65535) + half) >> 16)` modulo the low 64 result bits. Write `a = a_hi*2^32 + a_lo`, with signed `a_hi` and unsigned `a_lo`; the second term becomes `(a_hi*(b & 65535) << 16) + ((a_lo*(b & 65535) + half) >> 16)`. AVX2 reconstructs the first term's low 64 bits from three unsigned 32-bit products; AVX-512DQ uses `vpmullq` and mixed signed/unsigned 32-bit products. For dot product, each exact product is `q_i*2^16 + r_i` with `0 <= r_i < 2^16`; summing three `q_i` and adding `(r_x+r_y+r_z+half)>>16` rounds once, matching `fixDot` even when the 128-bit signed sum wraps.

## Before and after

All numbers are ns per scalar call or per element in a 4096-element batch. Cells are **minimum / median / spread**, where spread is `(max-min)/median` over seven interleaved A/B repeats; speedup is baseline median divided by candidate median. Both trees used the same benchmark source and `-O3 -g -DNDEBUG -ffp-contract=off`, 200,000 iterations per repeat. Scalar rows are dependent-chain controls. Full commands, compiler versions, ISA flags, affinity, load and per-operation oracle lines are in [raw.txt](../experiments/spike-batch-mul-avx2-avx512/raw.txt).

| CPU / 128-bit arm | Operation | Mode | Before min / med / spread | After min / med / spread | Speedup |
| --- | --- | --- | ---: | ---: | ---: |
| Zen 4 / native | fixMulBatch | scalar | 2.5250 / 2.5398 / 1.9% | 2.5245 / 2.5493 / 17.1% | 0.996x |
| Zen 4 / native | fixMulBatch | batch | 0.7203 / 0.7206 / 0.5% | 0.2476 / 0.2493 / 1.8% | 2.890x |
| Zen 4 / native | fixDotBatch3 | scalar | 3.8840 / 3.8957 / 3.0% | 3.8838 / 3.9275 / 1.7% | 0.992x |
| Zen 4 / native | fixDotBatch3 | batch | 1.3821 / 1.3837 / 1.6% | 0.6436 / 0.6480 / 3.2% | 2.135x |
| Zen 4 / emulated | fixMulBatch | scalar | 3.4828 / 3.4970 / 26.4% | 3.5440 / 3.6058 / 10.7% | 0.970x |
| Zen 4 / emulated | fixMulBatch | batch | 2.4756 / 2.4793 / 1.0% | 0.2490 / 0.2499 / 1.2% | 9.921x |
| Zen 4 / emulated | fixDotBatch3 | scalar | 7.4019 / 7.4528 / 59.3% | 7.2619 / 7.4389 / 47.1% | 1.002x |
| Zen 4 / emulated | fixDotBatch3 | batch | 6.5279 / 6.5532 / 0.8% | 0.6350 / 0.6376 / 79.4% | 10.278x |
| Zen+ / native | fixMulBatch | scalar | 4.0798 / 4.1026 / 91.0% | 4.0799 / 4.1176 / 91.2% | 0.996x |
| Zen+ / native | fixMulBatch | batch | 1.2748 / 1.2829 / 90.4% | 1.0836 / 1.1050 / 88.3% | 1.161x |
| Zen+ / native | fixDotBatch3 | scalar | 7.1960 / 7.2472 / 90.9% | 7.2050 / 7.3298 / 92.5% | 0.989x |
| Zen+ / native | fixDotBatch3 | batch | 2.4114 / 2.4187 / 57.8% | 3.5094 / 3.5850 / 88.4% | 0.675x |
| Zen+ / emulated | fixMulBatch | scalar | 6.5009 / 6.5303 / 5.4% | 6.5387 / 6.5733 / 6.3% | 0.993x |
| Zen+ / emulated | fixMulBatch | batch | 3.3131 / 3.3373 / 5.6% | 1.0772 / 1.0919 / 3.6% | 3.056x |
| Zen+ / emulated | fixDotBatch3 | scalar | 13.1424 / 13.2142 / 5.0% | 13.6938 / 13.7635 / 4.1% | 0.960x |
| Zen+ / emulated | fixDotBatch3 | batch | 7.9374 / 7.9715 / 5.3% | 3.4805 / 3.5548 / 4.5% | 2.242x |

Zen 4 was an AMD Ryzen 7 PRO 8700GE (`avx2`, `avx512f`, `avx512dq`), GCC 13.3.0, affinity 0-15, one-minute load 0.43 and 0.96 at native and emulated starts. Zen+ was an AMD Ryzen Threadripper 2990WX (`avx2`, no AVX-512), GCC 15.2.0, affinity 0-63, loads 1.08 and 1.17. `cpu_info.sh` reported no competing benchmark at each start. The wide Zen+ native spreads prevent a precise speed estimate; its medians are below the 1.5x AVX2 gate for multiply and slower for dot. The emulated arm's speedups do not establish a native AVX2 win. All scalar controls and existing `fixMul` and `fixDot` rows stayed within their measured spread.

## Bit-exact evidence

The baseline test was red with `BatchMulLaneEquivalenceTest FAIL: batch API absent at baseline`; it was a real failure, not a skip. CTest then passed **39/39** on each Linux CPU, including `BatchMulLaneEquivalenceTest`, its emulated twin, its saturated build, and both frozen-hash suites. The full `ab.sh` oracle passed all 33 operations in each of four runs at `base_random=50000000`, including original hashes and these new rows from the scalar baseline:

```text
ORACLE OK   fixMulBatch  count=50063756  hash=de6fc21fb01df7f2
ORACLE OK   fixDotBatch3  count=50063756  hash=7fb109655c1745da
ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)
```

The same first-1,000-input unit hashes were checked independently with scalar baseline source (`fixMulBatch f74d919d55ad89c8`, `fixDotBatch3 a967efd82587b494`). Native, emulated and saturated 100-million-lane tests passed. This supports bit identity for every tested input; untested compiler and OS combinations remain unmeasured.

## Decision and next step

The complete patch fails the keep rule because AVX2 native did not reach 1.5x, so no source change is adopted by this spike. A follow-up can isolate the AVX-512DQ path, keep scalar fallback on AVX2, and validate that smaller patch on CI and a consuming workload. Do not repeat this AVX2 32-bit partial-product dot kernel as a shipping dispatch branch based on emulated-only speedup; do not tune this experiment after its first complete pass.
