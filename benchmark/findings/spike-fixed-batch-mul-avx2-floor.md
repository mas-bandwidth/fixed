VERDICT: KEEP (with a stated noise caveat) — an avx2 `fixMulBatch` is bit-identical to scalar in all three builds and faster by median in 7 of 7 A/B passes on the AVX2-only Threadripper 2990WX (1.27x to 1.34x, mean about 1.30x), beyond the typical A/A spread but NOT beyond the worst A/A spreads, because the host was shared and not quiet. The dot kernel stays scalar.

# An avx2 fixMulBatch on an AVX2-only CPU, measured quietly

**Hypothesis.** The first batch spike (benchmark/findings/spike-batch-mul-avx2-avx512.md) measured AVX2 `fixMulBatch` at 1.16x on Zen+ with 90 percent spreads, which cannot be judged. NEW reason for this pass: Glenn's judge is "faster by median", not 1.5x, and that measurement was not quiet. With only the mul kernel (dot stays scalar, it lost at 0.675x) on one pinned core, with an A/A first, the median should be readable.

**Change (one paragraph).** `src/fixed_isa_avx2.c` adds `fixIsaMulBatchAvx2`, four lanes per iteration, `target("avx2")` on the function, scalar tail. Same bit argument as the AVX-512 kernel: `(a*b + half) >> 16 = a*c + (ah*blo << 16) + ((al*blo + half) >> 16)` modulo 2^64 with `c = b >> 16` (arithmetic), `blo = b & 65535`. AVX2 has no 64-bit multiply and no 64-bit arithmetic shift, so `c` is sign-extended from its 48-bit field as `((b >>> 16) ^ 2^47) - 2^47`, and `low64(a*c)` is `al*cl + ((ah*cl + al*ch) << 32)` from three `vpmuludq`; `ah*blo` is `vpmuldq`, `al*blo` is `vpmuludq`: five multiplies per four lanes. `src/fixed_isa.c` compiles the variant in on GNU/clang x86-64 (not `FIX_SATURATE`), `fixIsaCompiled(AVX2)` is true, and installing it sets `mulBatch` to the avx2 kernel and `dotBatch3` to the scalar kernel. `test/isa_dispatch_test.c` stops reserving the name `avx2` and expects `avx2` as the best variant for facts with AVX2 and no AVX-512. `test/batch_mul_avx2_test.c` (CTest `batch_mul_avx2`, `_emulated`, `_saturated`) is the new equivalence test. Everything is in `benchmark/experiments/spike-fixed-batch-mul-avx2-floor/experiment.patch` (applies cleanly with `git apply` on BASE be0df4e).

## Host and hygiene

Headline host: `vision`, AMD Ryzen Threadripper 2990WX (Zen+, `avx2 fma bmi2`, no AVX-512), GCC 15.2.0, clang 21.1.8, Linux 7.0, `taskset -c 15` with sibling 47 idle (mpstat 1 percent before and after). It is a shared fleet box: load average 8 to 16 during the runs, other jobs on other cores, so the core was not isolated. `perf_event_paranoid=4` and no sudo: **perf stat/record counters were not available (not done)**, so no cycle counts, no frequency read, no branch-miss counters. `cpu_info.sh` and `ab.sh` both reported no `op_bench|numerical_bench|oracle_check` process; a plain `pgrep -af bench` matches `nova-bench` paths and postgres of fleet jobs, which are not benchmarks of this tree. Cross-check host: `b2`, Ryzen 7 255 (Zen 4 class, AVX-512), idle, `taskset -c 2`. macOS has no taskset; neither host was macOS. All commands, cpu_info output and loads are in raw.txt.

## Prediction first (llvm-mca-21, znver1) and the gap

The loop was extracted from the compiled objects (disassembly in raw.txt). llvm-mca `-mcpu=znver1`: avx2 loop 12.0 cycles per 4 lanes = 3.0 cycles per lane (the 256-bit multiplies split into two 128-bit uops on Zen 1's single multiplier pipe); the scalar loop (`imulq`, `adc`, `shrd`) 2.3 cycles per lane. **Predicted speedup 0.78x, a loss.** `-mcpu=znver4`: avx2 4.0 cycles per 4 lanes = 1.0 per lane; scalar 3.0 per lane (predicted 3.0x).

Measured on Zen+: 1.27x to 1.34x, a win, so the Zen 1 prediction is wrong. The most likely cause is the scalar side: at 1.354 ns per lane the scalar loop runs at about 4.6 cycles per lane if the clock is near 3.4 GHz (clock not read, no counters), twice mca's 2.3, so the znver1 scheduling model is optimistic for `imulq` (one-operand 64x64 to 128) plus `shrd`; the avx2 side, 1.075 ns, is about 3.65 cycles per lane against mca's 3.0. This was NOT checked against uops.info or the vendor guide in this session. Zen 4 measured 2.5x to 3.1x against a predicted 3.0x, in line.

## Before and after

ns per element of a 4096-element `fixMulBatch` (op_bench batch mode, 200,000 iterations per repeat, `-O3 -g -DNDEBUG -ffp-contract=off`), 15 interleaved repeats per pass. Cells are min / median over the 15 repeats; spread is (max - min) / median.

| Pass (Threadripper 2990WX, taskset 15) | Scalar min / med / spread | avx2 min / med / spread | Median speedup |
| --- | ---: | ---: | ---: |
| ab.sh A/B (gcc) | 1.3878 / 1.5047 / 23.9% | 1.0517 / 1.1262 / 241.2% | 1.336x |
| gcc pass 1 | 1.3511 / 1.4255 / 15.9% | 1.0239 / 1.0752 / 8.2% | 1.326x |
| gcc pass 2 | 2.5637 / 2.5691 / 1.9% | 1.9348 / 1.9607 / 3.1% | 1.310x |
| gcc pass 3 | 2.5620 / 2.5701 / 8.6% | 1.9302 / 1.9848 / 15.9% | 1.295x |
| clang pass 1 | 1.3535 / 1.3800 / 3.7% | 1.0762 / 1.0902 / 3.8% | 1.266x |
| clang pass 2 | 1.3550 / 1.3803 / 24.8% | 1.0750 / 1.0831 / 3.9% | 1.274x |
| clang pass 3 | 1.3630 / 1.3833 / 22.8% | 1.0764 / 1.0833 / 21.7% | 1.277x |

`fixDotBatch3` in the ab.sh pass (dot stays scalar, so it is a control): batch 2.6419 vs 2.6265, 1.006x, within noise as it must be. The scalar-chain `fixMulBatch scalar` row (the dependent `fixMul` loop, which the variant does not touch) is 1.001x.

**A/A (noise floor), same box, same core.** 5 gcc and 5 clang scalar-vs-scalar passes (15 repeats per side, interleaved, so 20 sides) plus the ab.sh A/A passes: the ratio of the two sides' medians is 0.985 to 1.012 (one outlier, the ab.sh avx2-vs-avx2 pass, 0.936). The max-min spread of an A/A side ranged from 3.2 percent to 195 percent; its median over the 20 sides is about 20 percent. The runs also show a bimodal clock-like state: whole passes ran at 1.35 ns and others at 2.56 ns on BOTH sides of an interleaved pass, with the ratio unchanged (1.31x in the slow state, 1.33x in the fast); the cause is not identified (SMT sibling and frequency were the suspects the card named, sibling was idle, frequency could not be read without perf or sudo).

**Reading against the judge.** The paired median gain (1.27x to 1.34x, 26 to 34 percent) exceeds the A/A median-ratio drift (at most 1.5 percent in the ten gcc/clang A/A passes, 6.4 percent in the one ab.sh outlier) and exceeds the median A/A max-min spread (about 20 percent). It does NOT exceed the worst A/A max-min spreads (up to 195 percent) which come from isolated outlier repeats on a loaded host. The keep call rests on the paired median ratio being stable across seven A/B passes, two compilers and both clock states; a quieter host (an isolated core, or one with perf counters) should be used for the port card's confirmation.

**Cross-check, AVX-512 CPU (b2, Ryzen 7 255), avx2 against scalar, gcc, 15 repeats:** pass 1 0.9152 to 0.3643 (2.51x), pass 2 1.4764 to 0.4786 (3.09x), pass 3 1.4761 to 0.4779 (3.09x); its A/A medians agree within 2.3 percent. On an AVX-512 machine the avx512 variant still wins over avx2 (0.2493 ns on Zen 4 in the earlier spike); the dispatch prefers avx512 there, so the avx2 variant serves AVX2-only CPUs (Zen 1 to 3, Skylake client, Haswell on).

## Bit-exact evidence

- RED at BASE: `BatchMulAvx2EquivalenceTest FAIL: avx2 variant absent (not compiled)`, exit 1 (a real failure, not a SKIP).
- GREEN: `BatchMulAvx2EquivalenceTest PASS: variant avx2, edge grid, tails, unaligned, 100000000 seeded lanes` (native, 252 x 252 structured edge grid crossed with itself, every tail 0..33 on random and edge lanes, pointers one element off vector alignment, 100 million seeded full-width lanes); the emulated build passes at 10 million lanes (`-DFIX_FORCE_EMULATED_INT128`); the `FIX_SATURATE` build compiles scalar only and the test requires `avx2` to be refused by name (`NOT_COMPILED`), which passes.
- `isa_dispatch` (native 100 million lanes, emulated 10 million): `VARIANT avx2 RAN: batch oracle rows and N seeded lanes bit-identical to scalar`; saturated: `VARIANT avx2 SKIP not compiled into this build`, as designed.
- ctest: 45 of 45 pass in the native build and in the emulated build.
- Oracle under `FIXED_ISA=avx2` at `base_random=50000000`, the native and the emulated oracle binaries, in both builds, all operations OK, including the frozen rows:

```text
ORACLE OK   fixMulBatch  count=50063756  hash=de6fc21fb01df7f2
ORACLE OK   fixDotBatch3  count=50063756  hash=7fb109655c1745da
ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)
```

## What to try next (a sentence each, not done)

- Unroll to 8 lanes per iteration and hoist the `c` build: the loop is multiply-port bound on Zen 1 (5 vpmuludq per 4 lanes, two uops each), so the ceiling is small, but the scalar tail and loop overhead are not.
- Skip the `ch` multiply when the high half of `b >> 16` is known: for Q48.16 values below 2^47 the sign bits make `ch` a shift of the sign, saving one of the five multiplies (needs a range argument and its own equivalence test).
- A 128-bit (SSE4.1) variant for pre-AVX2 CPUs, measured, in the same table.

## What not to repeat

- Do not read a Zen 1 llvm-mca prediction as the verdict: it predicted a 0.78x loss and the measurement is 1.3x; measure.
- Do not draw a conclusion from a single `ab.sh` run on a shared fleet box: the spread (max - min) is dominated by isolated outlier repeats; use paired median ratios over several passes and the A/A ratios beside them.
- Do not run the headline on a box with perf locked down if cycles matter: ask for `perf_event_paranoid<=1` on the Threadripper bench first.
- Not done: perf stat cycles/instructions/branch-misses, perf record, an isolated (isolcpus) core, frequency readout, uops.info/vendor-guide check, macOS (no taskset) and Skylake/Haswell Intel measurements, MSVC (the variant is compiled for GNU/clang only).
