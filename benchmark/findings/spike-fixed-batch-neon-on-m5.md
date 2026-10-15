VERDICT: KEEP for fixLengthBatch (1.77x to 1.96x) and fixCrossBatch (1.41x to 1.66x), bit-identical to scalar; DROP for fixMulBatch (loses 0.66x to 0.74x on full-width inputs, wins 1.12x to 1.34x only when every operand fits int32) and for fixDotBatch3 (wins 1.07x and 1.10x at 16 and 256 lanes, loses 0.88x at 4096). The ops are judged separately because the kernel table must choose per operation, not per ISA.

# The NEON batch kernels on the M5, measured quietly

**Hypothesis.** The archived `spike-batch-ops-neon` patch (length 2.03x, cross 1.72x, mul 1.16x, dot below 1 on a loaded M3 Ultra) holds on a quiet Apple M5 Pro when run as a `neon` variant behind the dispatch layer, except that the M5's wider scalar core takes away the narrow wins: prediction, reasoned before the sweep ran from the M3 table and the scalar-core width (and written down only now), is that mul and dot lose their win and length and cross keep theirs. New reason for a repeat of an already-measured idea (NOT REPEATING rule): the M3 numbers were taken on a loaded shared machine outside the dispatch layer; this is the first quiet, interleaved, bit-gated measurement on the host the card names, and the first with the exactness gate cost reported on both sides of the gate.

**Host.** Apple M5 Pro Mac mini (`ssh mini-m5`, Darwin 27.0.0 arm64, 6 P + 12 E cores, L1D 64 KiB, L2 8 MiB; FEAT_SME, FEAT_SME2, FEAT_SME2p1, FEAT_SME_I16I64, `sme_max_svl_b` 64), Apple clang 21.0.0, `-O3 -g -DNDEBUG -ffp-contract=off` (ab.sh's flags). Not the working Mac Studio and not the EPYC box. macOS has no `taskset`: the runs are UNPINNED and the OS could place them on either core type. Load average before the sweep was 2.3 (the box idles at about 2 under two `nova-swarm` members). Another worker's `benchmark/ab.sh` (cwd `~/rowan-jobs/fixdiv-cand`, pids 93859/95437, later 11368 and 12905 seen by my retry loop) ran on this same box during the card. ab.sh's own `pgrep` made each side refuse while the other's benchmark was alive and my driver waited (up to 200 x 3 s) and retried; nothing was killed. The shared box is the main threat to the numbers below; A/A shows it did not move them (see the noise floor).

## The change (one paragraph)

`experiment.patch` (applies cleanly with `git apply` on BASE `4f02608`, checked in a fresh clone that then built and ran the new test) adds `src/fixed_isa_neon.c`, the four batch kernels of the archived patch ported behind the dispatch layer: `fixIsaMulBatchNeon`, `fixIsaDotBatch3Neon`, `fixIsaLengthBatchNeon`, `fixIsaCrossBatchNeon`. NEON is baseline on aarch64, so the file needs no target attribute and no flag; the neon name stops being reserved (compiled on aarch64 outside FIX_SATURATE, requires only the NEON fact), the kernel table gains `lengthBatch` and `crossBatch` slots (new public `fixLengthBatch`, `fixCrossBatch` in `fixed_vec.h` with scalar kernels in `fixed_isa.c`; BASE carried only mul and dot), and every other variant leaves the new slots on scalar. Each kernel runs an EXACTNESS GATE first (one OR of `x ^ (x >> 63)` over every operand array; the whole call is the scalar kernel if any operand is outside int32, or outside |x| <= 2^30 for the three-term dot); inside the gate `smull/smlal` give exact 64-bit products and one `srshr` is fixMul's `(p + FIX_HALF) >> 16`. Length is an exact integer floor square root of the u64 sum of squares (a `fsqrt` double seed repaired by one `umull` down step and one up step), cross rounds each product on its own then subtracts. The patch also carries what the card names: `test/batch_neon_equivalence_test.c` (+ CMake registration in the native, emulated and FIX_SATURATE builds and two witnesses), `isa_dispatch_test.c` updated so neon is no longer a reserved name, two new frozen oracle rows (`fixLengthBatch`, `fixCrossBatch`: 16 identical lanes per input, hashes frozen from the scalar variant, existing rows unchanged), and bench-harness changes in `op_bench.c` (`OP_BENCH_LANES` width sweep, passes follow the lanes so a 16-lane row is as long as a 4096-lane row, `OP_BENCH_SHIFT` / `OP_BENCH_LSHIFT` to put operands inside or outside the gates, rows for length and cross).

## Red first (BASE)

`BatchNeonEquivalenceTest` against the unmodified BASE tree (test file copied in, nothing else):

```text
BASE 4f026088b90877c2d5dfe79d176a4cbc2b969372 land spike-fixed-fixmul-fits-64-on-m5 (sprint stream fixed-perf)
SUBTEST BatchNeonEquivalenceTest
FAIL test/batch_neon_equivalence_test.c:293: BatchNeonEquivalenceTest: variant neon ABSENT on aarch64 (fixForceIsa returned 2: not compiled into this build)
exit=1
```

That is a FAIL, not a SKIP. (The work started on `d7f6ac5` and was moved onto the newer tip `4f02608`; the commits between them add only findings and experiment files, no change to src, include, test, CMake or the benchmark harness, and the red line was re-run on the new tip with the same result.)

## Results (the judged table)

ns per lane, median of 15 interleaved repeats of 20,000,000 lane-operations, ONE binary run with `FIXED_ISA=scalar` (A) against `FIXED_ISA=neon` (B); speedup = A/B, above 1 means neon is faster. The A/A column is the same binary scalar against scalar: 0.997 to 1.006 everywhere on the batch rows, so a ratio outside about 0.99 to 1.01 is a real difference of medians. Full tables, spreads and the A/A runs are in `raw.txt`.

**Natural inputs** (the oracle's own fills: `fixMulBatch` operands are full 64-bit width, so its gate FAILS and neon pays the scan and then runs scalar; dot, length and cross operands are +-2^19, inside the gates):

| op | lanes | A scalar | B neon | speedup | B spread |
| --- | ---: | ---: | ---: | ---: | ---: |
| fixMulBatch | 16 | 0.4289 | 0.5816 | 0.737 | 1.4% |
| fixMulBatch | 256 | 0.4433 | 0.6440 | 0.688 | 1.6% |
| fixMulBatch | 4096 | 0.4503 | 0.6836 | 0.659 | 1.6% |
| fixDotBatch3 | 16 | 0.9183 | 0.8587 | 1.069 | 1.9% |
| fixDotBatch3 | 256 | 0.9442 | 0.8607 | 1.097 | 1.4% |
| fixDotBatch3 | 4096 | 0.9986 | 1.1339 | 0.881 | 1.3% |
| fixLengthBatch | 16 | 1.6172 | 0.8994 | 1.798 | 1.0% |
| fixLengthBatch | 256 | 1.6583 | 0.8480 | 1.956 | 0.3% |
| fixLengthBatch | 4096 | 1.6674 | 0.9324 | 1.788 | 1.1% |
| fixCrossBatch | 16 | 2.0250 | 1.3290 | 1.524 | 21.0% (outlier repeat) |
| fixCrossBatch | 256 | 2.0223 | 1.2180 | 1.660 | 1.2% |
| fixCrossBatch | 4096 | 2.1639 | 1.5081 | 1.435 | 2.0% |

**Mul with every operand inside int32** (`OP_BENCH_SHIFT=33`): 16 lanes 0.4278 -> 0.4111, 1.041 (B spread 13.1%, inside the noise, not a gain); 256 lanes 0.4477 -> 0.3330, 1.344; 4096 lanes 0.4501 -> 0.4021, 1.119.

**Operands pushed past the gates** (`OP_BENCH_LSHIFT=14`, the gate-failure cost: the scan plus the scalar kernel): mul 0.742 / 0.694 / 0.662, dot 0.628 / 0.640 / 0.590, length 0.911 / 0.874 / 0.849, cross 0.790 / 0.794 / 0.764 at 16 / 256 / 4096 lanes.

The `scalar` rows of the same tables (the dependent-chain control, which no variant should move) read 0.96 to 1.06 with spreads of 15% to 50% (interrupts on a shared box); they are not judged. The cross control row reads 0.31 ns because the chain feeds word 0 back and collapses to zero; it is a harness quirk, not a result.

The real `benchmark/ab.sh` run (with its oracle step) at 4096 lanes, natural inputs, 9 repeats, agrees with the sweep: mul 0.674, dot 0.878, length 1.770, cross 1.415, and it ends `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)` under `FIXED_ISA=neon`.

## Judge

(a) Bit-identity: held, in all three builds, for all four ops (below). (b) Faster by median beyond the A/A spread on this host: length yes at every width; cross yes at every width (at 16 lanes the min-max spread is 21% to 43% from one outlier repeat in the cross rows, but the median ratio repeats (1.524 in two separate invocations), so it is kept; the strict spread rule is only just beaten there); mul only when the operands fit int32 and n >= 256, and it LOSES on full-width operands, which is what the oracle's own fill draws; dot at 16 and 256 lanes by 7% and 10% (B spreads 1.4% to 1.9%), and it LOSES at 4096. Per THE JUDGE a path that fails (b) is dropped, so mul and dot are DROP and length and cross are KEEP. Note what this means for the dispatch default: after this patch the M5 auto-selects `neon` (fixIsaBest takes the highest compiled variant whose facts hold), which would REGRESS mul and dot as installed here. The port must install length and cross only, per operation.

## Bit-exact evidence

- `BatchNeonEquivalenceTest` (`test/batch_neon_equivalence_test.c`), native: `VARIANT neon RAN: 100377127 lanes compared bit-identical to fixMul/fixDot/fixLength/fixCross (184243642 lane-operations in calls whose gate passed: vector body)`. It walks tiers on both sides of the int32 and 2^30 gates (inside, exactly at, just past, full width), the structured grid (0, +-1, +-2^k, +-(2^k - 1), extremes) crossed with itself raw and clamped so the vector path sees every in-range pair, a boundary value at every lane position of an 11- and a 37-lane batch in every operand array, every tail length 0..33 at unaligned pointers, 400 batches of lanes built ON perfect squares and one above and below for the integer square root, and the seeded lanes. Words around each output are checked untouched.
- Emulated build (`-DFIX_FORCE_EMULATED_INT128`): the same test at the same 100 million lanes passes. FIX_SATURATE build: `fixForceIsa("neon")` is refused as not compiled and the test pins it (`VARIANT neon SKIP not compiled in a FIX_SATURATE build`); a SKIP is not a pass and this one is a refusal check.
- Witnesses, both MUST fail and do: `NEON_EQUIV_NEGATIVE_CONTROL` (reference off by one ulp on one lane per 1024) and `FIX_NEON_BROKEN_GATE` (each gate one bit too wide).
- `isa_dispatch_test` under each build prints `VARIANT neon RAN: batch oracle rows and N seeded lanes bit-identical to scalar` (100,000,000 native, 10,000,000 emulated).
- Frozen output oracle (`oracle_check`, all operations, base_random=50000000) matches `benchmark/oracle-baseline.txt` in the native and the emulated builds under `FIXED_ISA=scalar` and under `FIXED_ISA=neon`. The two new rows' hashes under neon equal the scalar variant's; all 33 existing rows are unchanged.
- Gate: the whole stand-in suite is 51 passed, 0 failed (the nine WILL_FAIL controls and witnesses included). `cmake` is NOT installed on this host and the card forbids installing system packages, so `cmake -S . -B build ... && ctest` was not run; `suite.sh` (its text is the last section of raw.txt) compiles every CMakeLists.txt test target with the flags a Release build uses, the same defines, sources and arguments, runs them, and treats WILL_FAIL targets as passing when they exit non-zero. The suite gate is therefore reported not-done as `ctest`, done as a hand-built equivalent.

## DEEPEN: what the raw material shows

Profile: not done. `sample` could not be attached over ssh (it produced no output), `xctrace`/Instruments counters were not attempted, and macOS has no `perf`; no cycles/instructions/branch-miss counts exist for this card. `llvm-mca` is not installed (also `llvm-objdump`), so the prediction below is an instruction count read from the disassembly, not an mca throughput; M-series latencies were not looked up and none is claimed.

Disassembly (full excerpts in `raw.txt`): the scalar mul kernel is 10 instructions per lane (`ldr`, `ldr`, `smulh`, `mul`, `adds`, `cinc`, `extr`, `str`, `subs`, `b.ne`). The NEON mul has a gate scan of `ldp`, 2 x (`cmlt`, `eor`, `orr`), 3 address adds, `cmp`, `b.ls` = 12 instructions per 4 lanes per array, i.e. 3 per lane per array, then a compute loop of `ldp`, `uzp1`, `ldp`, `uzp1`, `smull`, `smull2`, `srshr` x2, `stp` and loop overhead = 16 per 4 lanes. For mul that is (3 x 2) + 4 = 10 instructions per lane against the scalar 10: parity, so a gain can only come from the vector units being cheaper per instruction (measured 1.12x to 1.34x in gate). On a gate failure it is 6 + 10 = 16 against 10, and 10/16 = 0.63: measured 0.66 to 0.74. For the dot the scan is 3 x 6 = 18 instructions per lane against a scalar body I estimate at 25 (six loads, three `smulh`/`mul` pairs, the carry chain and the store; I did not disassemble it), so roughly parity again (measured 1.07 to 1.10 at small widths). Length and cross win because the scalar body is long: `fixLength` is a 128-bit sum of squares and `fixISqrt128High` (7.9 ns per call as a dependent chain), cross is six `mul`+`smulh` pairs, and the NEON body replaces them with 3 `smull`/`smlal` pairs and a vector `fsqrt` seed. This model was written after the sweep, from the disassembly, and agrees in direction with every row; I did not fit it and it is an explanation, not a prediction that was tested.

The gap that remains is the dot at 4096: 6 input arrays x 32 KiB = 192 KiB plus 32 KiB out, against a 64 KiB L1D (as `hw.l1dcachesize` reports; it may be the E-core figure), and the gate pass reads all six arrays before the compute pass reads them again, so the second read comes from L2 while the scalar kernel reads once. Hypothesis, not measured (no counters). The mul at 4096 shows the same shape (1.344 at 256 lanes, 1.119 at 4096).

## What to try next (one sentence each, none done: the spike stops at the first full measurement)

1. Make the gate three times cheaper: `(x + 2^31) as unsigned` OR-accumulated is one `add` and one `orr` per two lanes instead of `cmlt`, `eor`, `orr`; that takes the scan from 3 to 1 instruction per lane and moves mul and dot toward the length and cross numbers and the gate-failure cost from about 0.65x toward 0.85x.
2. Block the gate: scan 256 lanes, compute those 256, then the next, so the second read hits L1 and the dot stops losing at 4096.
3. Per-operation selection in the kernel table (neon carries length and cross; mul and dot stay scalar until 1 and 2 are measured), and a `fixLengthBatch` / `fixCrossBatch` oracle row and equivalence test must come with the port, as in this patch.
4. SME2 streaming mode (the next card, `spike-fixed-batch-sme2-streaming-m5`) is the only route to a true 64-bit lane product on this host; this finding gives it the NEON numbers to beat: mul 0.43 to 0.45, dot 0.92 to 1.00, length 1.62 to 1.67, cross 2.02 to 2.16 ns per lane scalar.

## What not to repeat

- Do not measure mul or dot with the oracle's own fills expecting a vector win: the mul fill is full 64-bit width and never passes the gate.
- Do not read a 16-lane `fixCrossBatch` spread of 21% to 43% as a failure of the median; look at the three runs.
- Do not trust the `scalar` (dependent chain) rows of the batch ops: cross collapses to zero in the chain.
- Do not run `benchmark/ab.sh` with its oracle step 36 times: it costs about a minute per call; `suite.sh`/ab.sh once is the bit gate, the sweep is the same source without the oracle step.
- `benchmark/cpu_info.sh` prints `isa_neon: unknown` for every flag on macOS because it asks for `hw.optional.arm.FEAT_neon` and the like, which do not exist (the real names are `hw.optional.arm.AdvSIMD`, `hw.optional.arm.FEAT_SME`, `FEAT_SME2`, `FEAT_I8MM`); proposed diff for a separate card, not committed here: query `AdvSIMD` for neon and keep the `FEAT_` names for the rest.

## Not done

- `cmake --build` / `ctest` (cmake absent); the hand-built equivalent ran instead.
- Instruments / `sample` profile and hardware counters, `llvm-mca` (not installed).
- CPU pinning (macOS has none), and a measurement with the box to itself: another worker's ab.sh shared it.
- AoS and `ld3` de-swizzle variants and `fixLengthBatch` / `fixCrossBatch` on AVX2/AVX-512 (out of this card's scope).
- The stale comment in `include/fixed/fixed_isa.h` ("today: avx2, avx512ifma, neon, sme2") still lists neon as reserved; a port should fix it.
