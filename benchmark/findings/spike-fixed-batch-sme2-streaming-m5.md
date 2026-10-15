VERDICT: DROP — the SME2 streaming-SVE batch kernels are bit-identical to scalar (oracle, 100 million lanes, all three builds) but never faster on the M5 Pro: there is no break-even n, the smstart/smstop pair costs 8.9 ns and the first streaming vector iteration about 23 ns more, and at n = 4096 the streaming loop only reaches scalar's throughput (0.404 ns per lane, mul) while losing 0.66x to 0.82x once the caller reads the result, and 0.34x to 0.57x with 2 to 6 threads on the shared SME unit.

# SME2 streaming-SVE batch kernels on the M5: do long batches amortise the mode switch?

**Host.** `ssh mini-m5-1`: Apple M5 Pro (18 logical CPUs), macOS arm64 (Darwin 27.0.0), Apple clang 21.0.0 (clang-2100.3.34.2), FEAT_SME, FEAT_SME2, FEAT_SME2p1, FEAT_SME_I16I64 all 1, `hw.optional.arm.sme_max_svl_b` = 64 (a 512-bit streaming vector, 8 lanes of 64 bits). Not a quiet box: nova friend daemons were resident (load average 1.9 to 2.9 on 18 CPUs), no benchmark process ran (`benchmarks_running: no` before and after). macOS has no thread pinning (no taskset), so nothing was pinned; the interleaving and the A/A arm are the only defences. No cmake on that host (a prior card said the same): the suite was built with written clang lines (below), so the cmake/ctest gate is not done.

**Hypothesis.** The batch API is the best fit for streaming mode: one `smstart` per call over n lanes, MUL plus SMULH per 64-bit lane giving the exact 128-bit product, so for large enough n the mode switch is amortised and the 512-bit streaming vectors beat the scalar kernels.

**The change (one paragraph).** `src/fixed_isa_sme2.c` adds `fixIsaMulBatchSme2` and `fixIsaDotBatch3Sme2`, each `__arm_locally_streaming` with a per-function `target("sme2")` attribute (no global flag), no ZA, one `smstart sm` and one `smstop sm` per call; the loop is VL-agnostic (`whilelt` predicate, so the tail is the last predicated iteration, no scalar tail). Per lane: `mul` + `smulh` give the 128-bit product halves, `+ 2^15` carries into the high half when the low sum wraps (`cmphi` + predicated `add`), and the result is `(lo >> 16) | (hi << 48)`; dot sums three products with the same carrying add and rounds once. `src/fixed_isa.c` registers the variant in the dispatch (enum slot `sme2` already reserved; compiled only for arm64 clang on Apple with `<arm_sme.h>`, never under FIX_SATURATE), `test/batch_sme2_test.c` is `BatchSme2EquivalenceTest`, `test/isa_dispatch_test.c` stops listing `sme2` as reserved, `benchmark/sme2_batch_bench.c` is the microbenchmark (iteration counts as arguments). All in `experiment.patch`.

**Red first (STEP 2), at BASE d7f6ac5.** The new test copied onto an unmodified BASE tree, built and run on the M5:
`BatchSme2EquivalenceTest FAIL: host has sme/sme2 but variant sme2 is absent from this build` (exit 1). A host without SME2, or a FIX_SATURATE build, prints `SKIP`, which the test says is not a pass.

## Prediction before measuring (and why it is an estimate)

llvm-mca is not installed on mini-m5-1 and does not know the M5, so the prediction is op-count arithmetic, not an mca run. The mul loop is 11 streaming instructions per 8 lanes (2 loads, `mul`, `smulh`, `add`, `cmphi`, `mov`+predicated `add`, `lsr`, `lsl`, `orr`, store) against scalar's 8 instructions per lane (disassembly in raw.txt: scalar uses `smulh`, `mul`, `adds`, `cinc`, `extr` plus load/store, because AArch64 has a 128-bit funnel shift, `extr`, that SVE does not). If the streaming unit retires one vector instruction per cycle, 11 cycles per 8 lanes is about 1.4 cycles per lane, about 1.3x faster than scalar's measured 0.404 ns per lane (about 1.8 cycles per lane at the M5's clock). Predicted: a gain at large n, with the smstart/smstop entry cost as the only loss. Measured: parity at best. The gap says the streaming 64-bit multiplies (`mul`, `smulh`) are not retired at one per cycle on the SME unit, or the shared SME unit serialises; the card's "published M4 SME streaming-SVE throughput" figures were not available to cross-check and no Instruments or `sample` counters were taken over ssh (not done).

## Before/after: ns per CALL, `sme2_batch_bench 11 4000000` (11 interleaved repeats of scalar, sme2, scalar again)

fixMulBatch (consume=0: nothing reads the output):

| n | scalar med | sme2 med | speedup | A/A gap | sme2 spread |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 (smstart+smstop alone) | 0.870 | 8.919 | 0.097 | 0.9% | 16.8% |
| 1 | 0.669 | 32.727 | 0.020 | 2.1% | 150.6% |
| 16 | 6.072 | 34.608 | 0.175 | 3.8% | 24.9% |
| 64 | 25.152 | 51.632 | 0.487 | 0.4% | 158.4% |
| 256 | 104.640 | 129.600 | 0.807 | 0.1% | 42.6% |
| 1024 | 417.307 | 440.604 | 0.947 | 0.6% | 3.5% |
| 4096 | 1653.689 | 1655.738 | 0.999 | 0.6% | 1.9% |

fixDotBatch3 (consume=0):

| n | scalar med | sme2 med | speedup | A/A gap |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0.723 | 8.982 | 0.080 | 20.2% |
| 16 | 13.724 | 37.944 | 0.362 | 2.3% |
| 64 | 54.816 | 80.208 | 0.683 | 0.5% |
| 256 | 222.848 | 248.512 | 0.897 | 1.8% |
| 1024 | 868.920 | 915.259 | 0.949 | 0.1% |
| 4096 | 3720.287 | 3582.992 | 1.038 | 1.9% |

The only cell above 1.0 is dot at n = 4096, 1.038, with an A/A gap of 1.9% and per-arm spreads of 2.7% and 1.6%: inside the noise floor the JUDGE sets (a gain inside the A/A spread is noise), and it reverses when the output is read (next).

**The smstart/smstop cost alone:** 8.9 ns per call (n = 0, both kernels; scalar's n = 0 call is 0.7 to 0.9 ns). **The first streaming vector iteration costs about 23 ns more:** n = 1 is 32.7 ns and n = 8 is 32.3 ns for mul (one iteration), so the fixed cost of a streaming call is about 32 ns, not 9. Beyond that the slope is 0.404 ns per lane for mul, equal to scalar's 0.404, so the gap closes only as 1/n and never turns into a lead (mul 0.947 at 1024, 0.999 at 4096). **Break-even n: none** for mul within n <= 4096 (asymptotically parity); for dot, parity at n = 4096 inside noise.

## When the caller reads the result (consume=1), which is also why ab.sh disagrees with the table above

`ab.sh` (`op_bench`) sums every output lane on the normal core after each batch call; the microbenchmark with consume=0 does not. Rerun with consume=1 (`sme2_batch_bench 11 4000000 1`), speedup medians: mul n = 64: 0.229; n = 256: 0.501; n = 1024: 0.525; n = 4096: 0.741; dot n = 64: 0.362; n = 1024: 0.596; n = 4096: 0.818. A single-lane call followed by a read costs 75 ns (mul) against 0.93 ns scalar. The data stored in streaming mode is evidently not cheaply visible to the normal core (a read-back penalty of about 0.2 ns (mul) to 0.35 ns (dot) per lane at 4096), and that cost is part of any real use of a batch result. This does not change the verdict; it makes the loss larger.

**ab.sh, the card's gate A/B** (`AB_ISA_A=scalar AB_ISA_B=sme2 AB_REPEATS=11 AB_ITERS=2000000 sh benchmark/ab.sh . . fixMulBatch fixDotBatch3`, 4096 lanes per call, median ns per lane):

| op | mode | scalar med | sme2 med | speedup |
| --- | --- | ---: | ---: | ---: |
| fixMulBatch | scalar chain (control) | 1.3040 | 1.3380 | 0.975 |
| fixMulBatch | batch | 0.4239 | 0.6396 | 0.663 |
| fixDotBatch3 | scalar chain (control) | 2.4240 | 2.4310 | 0.997 |
| fixDotBatch3 | batch | 0.9406 | 1.2596 | 0.747 |

(The spreads that ab.sh prints are 34% to 131% for the batch and control rows, from a few outlier repeats among 11; the medians are stable across the two harnesses once consume is matched, and the A/A arm of the microbenchmark, 0.1% to 3.8% gaps at n >= 16, is the noise floor used here. Five separate ab.sh A/A passes were not run: the in-harness scalar-against-scalar arm was used instead.)

## Threads and the shared SME unit (aggregate lanes per microsecond, n = 1024 and 4096, median of 11, consume=0)

| op, n | threads | scalar | sme2 | speedup |
| --- | ---: | ---: | ---: | ---: |
| mul, 4096 | 1 | 2518.7 | 2517.9 | 1.000 |
| mul, 4096 | 2 | 4955.5 | 2811.8 | 0.567 |
| mul, 4096 | 4 | 9759.9 | 3798.9 | 0.389 |
| mul, 4096 | 6 | 14606.5 | 5152.9 | 0.353 |
| dot, 4096 | 1 | 1102.4 | 1146.6 | 1.040 |
| dot, 4096 | 2 | 2181.8 | 1683.5 | 0.772 |
| dot, 4096 | 4 | 4333.7 | 2080.2 | 0.480 |
| dot, 4096 | 6 | 6151.5 | 2841.2 | 0.462 |

n = 1024 gives the same shape (mul 0.979, 0.557, 0.379, 0.340; dot 0.951, 0.721, 0.445, 0.420). Scalar scales almost linearly with threads (one core's worth each); the sme2 variant scales to about 2x at 6 threads: the SME unit is shared and is the bottleneck, which is exactly the risk the card named. A server running batch kernels on several cores would be throttled by this variant.

## Bit-exact evidence (THE JUDGE, part a: passed; part b: failed)

All on mini-m5-1, with clang lines mirroring CMakeLists.txt (-O3 -DNDEBUG -ffp-contract=off):
- Native: 24 of 24 (the whole suite plus the six WILL_FAIL negative controls failing as they must), including `BatchMulLaneEquivalenceTest PASS: variant sme2; 252 structured edges crossed; 100000000 seeded lanes; mul and dot` (the default selection on this host is now sme2), `BatchSme2EquivalenceTest PASS: variant sme2 forced; every length 0..130 over edge lanes` and `IsaDispatchTest PASS` with `VARIANT sme2 RAN: batch oracle rows and 100000000 seeded lanes bit-identical to scalar`.
- Emulated (-DFIX_FORCE_EMULATED_INT128): 24 of 24, `VARIANT sme2 RAN ... 10000000 seeded lanes` (the dispatch test's emulated count, as its CMake registration sets).
- Saturated (-DFIX_SATURATE): 4 of 4 (batch, batch_sme2, isa_dispatch, numerical_regression); the variant is not compiled there: `VARIANT sme2 SKIP not compiled into this build`, `BatchSme2EquivalenceTest SKIP: this build does not carry the sme2 variant`. Scalar route kept, as the card requires.
- The frozen output oracle under `FIXED_ISA=sme2` (ab.sh): `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)`, including `fixMulBatch hash=de6fc21fb01df7f2` and `fixDotBatch3 hash=7fb109655c1745da`.
- Disassembly (raw.txt): exactly one `smstart sm` and one `smstop sm` per kernel, no `za`, `zero`, `mova`, `rdsvl` or `tpidr2` instruction, hence no ZA use and no lazy-save. The compiler does spill and restore `d8` to `d15` around each call (the streaming-mode callee-save rule): 4 `stp` and 4 `ldp`.
- The speed gate (THE JUDGE b) is failed: no cell beats the A/A spread by measured median, and most are far below 1.

## Not done

- cmake and ctest: no cmake on mini-m5-1, so the suite gate was run as written clang commands (listed above), not through CMakeLists.txt; the CMake registration in the patch (`batch_sme2`, `batch_sme2_emulated`, `batch_sme2_saturated`, the bench target) was therefore never configured or built by CMake.
- A neon variant does not exist on BASE (the name is reserved), so there is no neon column.
- llvm-mca (absent), Instruments CPU Counters, `sample` and perf: not available over ssh here; the prediction is op-count arithmetic and the gap is a hypothesis, not a measurement.
- Five separate ab.sh A/A passes, a pinned core (macOS has none), and a quiet box (daemons resident, load 1.9 to 2.9).
- No tuning past the first measurement (no unrolling, no software pipelining of the loads, no prefetch).

## What to try next (sentences only)

1. A kernel whose result stays in streaming-visible memory, or whose consumer is also in streaming mode (a fused solver that stays in one smstart for many batch operations: the 32 ns entry and the 70 ns read-back are paid once). This is the only shape in which the 9 ns switch cost amortises; it is a fixed3d-solver question, not a `fixMulBatch` question.
2. A NEON variant for mul and dot: scalar here already issues about 2.5 lanes per nanosecond with `extr`, so NEON would need a 64x64 to 128 product that NEON lacks (no 64-bit lane multiply-high); the existing spike-batch-ops-neon (M3) result for mul 1.16x and dot below 1 predicts a small or no gain.
3. If a future M-series ships a wider SVL or an unshared SME unit, re-run this exact card; `sme_max_svl_b` is the first fact to read.

## What not to repeat

Do not offer the SME2 variant of `fixMulBatch` or `fixDotBatch3` as a general dispatch choice on M-series (it is slower to equal at every n up to 4096 and 0.34x to 0.58x multi-threaded), and do not default `fixIsaBest` to it when the facts say SME2: if a streaming variant is ever selected it must be opt-in and per whole solver, never per batch call. Do not compare a streaming kernel on a harness that never reads the output.
