VERDICT: LEARNED (fixMulBatch IFMA: DROP, slower than avx512; fixDotBatch3 IFMA: bit-exact and leaning 1.05x to 1.28x faster, but not resolved beyond the A/A spread on a loaded host)

# spike-fixed-batch-ifma-zen4

Host: hetzner2, AMD Ryzen 9 7950X3D (Zen 4), Linux 6.8, gcc 13.3 `-O3`, pinned with `taskset -c 14`. THE HOST WAS NOT QUIET: load average 29 to 39 on 32 threads and every core near 90 percent busy with other sprint workers before and during every run (b1 was at load 43). perf stat could not run (`perf_event_paranoid=4`, no sudo). Everything below carries that caveat. Raw output: `benchmark/experiments/spike-fixed-batch-ifma-zen4/raw.txt`.

## Hypothesis
`vpmadd52luq/huq` (4 cycle latency, accumulating add free) beats the `vpmullq` + `vpmuldq` + `vpmuludq` recipe of the avx512 variant, most of all for the dot, where three products share one accumulator pair.

## The change (one paragraph)
New `src/fixed_isa_avx512ifma.c` (target attribute `avx512f,avx512dq,avx512ifma`), table entries and `fixIsaCompiled` in `src/fixed_isa.c`, CMake source and test registration, `test/batch_ifma_test.c` (BatchIfmaEquivalenceTest, the batch lane test with the variant forced by name, plus a mixed-magnitude lane generator), and the isa_dispatch test updated (IFMA no longer reserved; best variant for all facts is now IFMA). Maths, with the unsigned 64-bit patterns split as a = a0 + a1*2^52 (a0 52 bits, a1 12 bits): modulo 2^80, a*b = lo52(a0*b0) + 2^52*(hi52(a0*b0) + lo52(a0*b1) + lo52(a1*b0)); the signed product differs from the unsigned by 2^64*(sa*b + sb*a). So (a*b + half) >> 16 = ((lo52 + half) >> 16) + ((M - (c << 12)) << 36) mod 2^64, with M the bracket and c the sign correction. Per 8 lanes: 4 IFMA, 2 shifts to get a1/b1, 2 arithmetic shifts + 2 ands + add + shift for the correction, 3 shifts and 2 add/sub to finish (23 loop instructions). The dot accumulates all three products into one (low, mid, corr) triple and rounds once (12 IFMA, 51 loop instructions), as fixDot does, including when the 128-bit sum wraps. Proposed layout for a port: the patch as it stands; no production file is touched by this card.

## Before / after (ns per element, median of 11 interleaved repeats, spread = (max-min)/median)
Micro (ifma_micro.c, one binary, FIXED_ISA switched in-process, two runs):

| op | n | scalar | avx512 | avx512ifma | ifma vs avx512 (run 1 / run 2) | spread (ifma) |
|---|---|---|---|---|---|---|
| fixMulBatch | 16 | 0.881 | 0.237 | 0.251 | 0.945x / 1.006x | 81% / 43% |
| fixMulBatch | 256 | 1.318 | 0.188 | 0.249 | 0.755x / 0.760x | 11% / 9% |
| fixMulBatch | 4096 | 1.153 | 0.200 | 0.240 | 0.833x / 0.802x | 10% / 17% |
| fixDotBatch3 | 16 | 2.508 | 0.854 | 0.764 | 1.117x / 1.127x | 22% / 26% |
| fixDotBatch3 | 256 | 2.555 | 0.760 | 0.651 | 1.167x / 1.150x | 22% / 19% |
| fixDotBatch3 | 4096 | 1.900 | 0.720 | 0.564 | 1.278x / 1.177x | 24% / 27% |

Application harness (`ab.sh`, op_bench batch rows, n = 4096, 11 repeats): avx512 vs avx512ifma: fixMulBatch 0.2728 vs 0.3261 = 0.837x (spreads 8.5%/7.9%, a real loss); fixDotBatch3 0.7010 vs 0.6648 = 1.054x (spreads 7.5%/14.3%; min 0.6953 vs 0.6579 = 1.057x). Scalar vs ifma: mul 2.98x, dot 2.72x (that run's scalar side was disturbed, 37 to 51% spread; the micro's 3.5x to 3.9x is the better figure).

A/A floor (5 passes of the same harness, avx512 vs avx512 both sides): median ratios between the two copies ranged about 0.90x to 1.12x on the dot (per-side spreads 13% to 75%), 0.93x to 1.00x on the mul; MINIMA agreed to about 1 to 2 percent in every pass. So medians on this host cannot resolve a 5 percent dot gain, and the micro's 12 to 28 percent dot gain sits at or inside the median spread (17 to 27 percent) although it points the same way in all six cells of two runs.

## Bit-exact evidence (THE JUDGE, part a: PASSED)
- Red at BASE, then green: `BatchIfmaEquivalenceTest FAIL: variant avx512ifma absent (not compiled into this build)` at BASE; `BatchIfmaEquivalenceTest PASS: variant avx512ifma; 252 structured edges crossed; 100000000 seeded lanes; mul and dot` with the patch, native and `-DFIX_FORCE_EMULATED_INT128`. The structured edge grid is crossed with itself, tails 0 to 9 and 4095, unaligned pointers, half the seeded lanes full width and half mixed magnitude and sign.
- Reversed witness: changing the correction shift from 12 to 13 makes it fail at lane 1 (`a=1 b=-15`).
- `isa_dispatch_test`: `VARIANT avx512ifma RAN: batch oracle rows and 100000000 seeded lanes bit-identical to scalar` (native and emulated). FIX_SATURATE builds report the variant not compiled (scalar only); `batch_ifma_saturated` is a WILL_FAIL test pinning that.
- Frozen oracle under `FIXED_ISA=avx512ifma` via ab.sh: all operations `ORACLE OK`, `fixMulBatch hash=de6fc21fb01df7f2`, `fixDotBatch3 hash=7fb109655c1745da`, `all operations match benchmark/oracle-baseline.txt (base_random=50000000)`.
- ctest: 45 of 45 pass in the native build and 45 of 45 in build-emu.

## Prediction against measurement (llvm-mca -mcpu=znver4)
Predicted block throughput per 8-lane iteration: mul avx512 3.0 cycles, ifma 11.5 (ifma 0.26x); dot avx512 12.0, ifma 31.5 (0.38x). Measured: mul ifma 0.76x to 0.84x of avx512 at n >= 256, dot ifma 1.05x to 1.28x. The model has the right sign for the mul and the wrong sign for the dot, and the magnitudes are off by 3x or more. The znver4 scheduling model charges every zmm vpmadd52 about 2 cycles of FP0/FP1 time and treats vpmullq zmm as one cheap uop; the measurements say vpmullq is far costlier in practice (the avx512 dot spends three of them) and IFMA cheaper than the model. I did not have uops.info open (not fetched here), so the cause is a hypothesis; it is the first thing a follow-up should check.

## What to try next
1. Re-measure the dot on a QUIET Zen 4 host (idle core and idle SMT sibling, `perf stat -e cycles,instructions` available) with 21 repeats; a gain that holds there is the KEEP case for the dot only. The mul stays on avx512.
2. If the dot keeps its gain, the port card should ship the dot with IFMA and keep the mul on the avx512 table entry: the table already holds one pointer per operation, so a per-operation choice costs nothing.
3. Cheaper mul sign correction: fold `srai/and` into mask registers (`vpmovq2m`, masked add), or pre-bias the inputs, to drop 6 of 23 instructions; only worth doing if the mul is to be revisited (it needs to find 20 percent).
4. Cover Intel Ice Lake / Sapphire Rapids, where vpmullq is 3 uops and IFMA is cheaper, before deciding IFMA is "Zen 4 only".
5. Note for a port: `fixIsaBest` takes the highest index, so adding the IFMA table entry makes IFMA the DEFAULT on every IFMA host, mul included. A port must either order the dot and the mul separately or leave the mul on avx512.

## What not to repeat
- Do not call the dot gain a result from this measurement: the host was at load 30+, the A/A median ratio wanders by +-12 percent, and perf counters were unavailable.
- Do not re-try IFMA for the mul with this recipe on Zen 4 (4 IFMA + 6 correction ops lose to 3 multiplies by 17 to 24 percent at n >= 256).
- Do not trust llvm-mca znver4 for IFMA or vpmullq throughput without checking uops.info.

## Not done
perf stat/record (blocked by `perf_event_paranoid=4`); uops.info lookup; a quiet-host measurement; clang build of the kernel; 21-repeat runs; arm. The finding's table is from a loaded shared host and is stated as such.
