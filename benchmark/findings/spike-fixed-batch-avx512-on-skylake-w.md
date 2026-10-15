VERDICT: LEARNED — the dispatched AVX-512 batch kernels cannot execute on this Skylake-W Mac: macOS leaves XCR0 at 0x7 (no opmask/ZMM state), so detection refuses `avx512` by name and no speed number is obtainable here. The rule that requires XCR0 zmm state before selecting a 512-bit kernel is correct and should be kept.

# The AVX-512 batch kernels on a Skylake-W Xeon (macOS)

**Hypothesis.** On an Intel Xeon W-2191B (Skylake-W: AVX-512 F/DQ/BW/VL/CD, no IFMA) the runtime-dispatched `fixIsaMulBatchAvx512` and `fixIsaDotBatch3Avx512` (from `fixed-isa-dispatch-batch-kernels`) would beat the scalar batch kernels at 16, 256 and 4096 lanes, bit-identical to scalar. The spike ran end to end once: detection, the equivalence test, an A/B of `FIXED_ISA=scalar` against `FIXED_ISA=avx512`, and the frequency-licence cost.

**Change.** No change to `src/` or `include/` is needed or possible: the AVX-512 kernels already exist on BASE. The one experiment change is to `benchmark/op_bench.c`: an `OP_BENCH_LANES` environment variable (clamped to `[1, 4096]`) lets the same `fixMulBatch`/`fixDotBatch3` rows be measured at 16, 256 and 4096 lanes so the width sweep the card names can be run. Both the scalar and the SIMD variant pay the same per-call overhead, so the comparison stays fair. The patch is `benchmark/experiments/spike-fixed-batch-avx512-on-skylake-w/experiment.patch`.

## What happened

The first fact is the host: CPUID reports `AVX512F`, `AVX512DQ`, `AVX512VL`, `AVX512BW`, `AVX512CD` and no `AVX512IFMA`, exactly the card's toolchain. But `XGETBV(0)` returns **`0x7`** — bits 0,1,2 only (x87/SSE/AVX/YMM). The opmask (bit 5), ZMM_Hi256 (bit 6) and Hi16_ZMM (bit 7) bits are **not** set. macOS on this box does not enable AVX-512 register state, so the ZMM upper halves would not be saved on a context switch; executing a 512-bit instruction would `#UD` anyway.

The dispatch layer's rule is therefore decisive. `fixIsaRequiredFacts(FIX_ISA_AVX512) = AVX512F | AVX512DQ | OS_ZMM`, and `fixIsaParseX86` only sets `OS_ZMM` when `(xcr0 & 0xe6) == 0xe6`. Result, printed by the frozen equivalence test:

```text
host facts: avx2 os-ymm-state avx512f avx512dq avx512vl
default variant: scalar
VARIANT scalar RAN: batch oracle rows and 100000000 seeded lanes bit-identical to scalar
VARIANT avx512 SKIP host lacks os-zmm-state
```

`VARIANT avx512 SKIP` is **not** a pass — it means the AVX-512 kernels were never executed, including on the bit gate. Forcing the variant is refused by name and aborts (never silently replaced), so the A/B cannot even start:

```text
AB_ISA_A=scalar AB_ISA_B=avx512 benchmark/ab.sh . . fixMulBatch fixDotBatch3
fixed: FIXED_ISA=avx512 refused: host lacks os-zmm-state
Abort trap: 6
```

There is no way to force the measurement: this is architectural state the OS owns, and `xsetbv` from a user process is not available (and would be wrong to attempt). So the card's central comparison — scalar against avx512 at n = 16, 256, 4096 — **was not done**, because on this microarchitecture the AVX-512 variant is not runnable.

## Measured anyway: the scalar batch lane sweep

The same rows under the only runnable variant, 7 repeats of 200,000 iterations, median ns per lane (`benchmark/fixed_op_bench`, which is what `ab.sh` interleaves). This is the A-side baseline the AVX-512 variant would have had to beat; the A/A noise floor was not completed (see below).

| lanes | op | mode | min | median | max |
| ---: | --- | --- | ---: | ---: | ---: |
| 16 | fixMulBatch | batch | 1.2755 | 2.5510 | 3.8265 |
| 16 | fixDotBatch3 | batch | 5.1020 | 6.3776 | 7.6531 |
| 256 | fixMulBatch | batch | 1.4349 | 1.5944 | 1.9133 |
| 256 | fixDotBatch3 | batch | 4.2251 | 4.8629 | 33.8807 |
| 4096 | fixMulBatch | batch | 1.5695 | 1.7239 | 3.1290 |
| 4096 | fixDotBatch3 | batch | 4.7284 | 4.9575 | 10.6326 |

The 16-lane rows are dominated by call overhead and the spread is large; the 4096-lane rows are the stable ones. These are scalar numbers only and say nothing about AVX-512.

## Bit-exact evidence

- `ctest -R 'isa_dispatch|batch|oracle_unit|determinism'`: 10/10 passed in the native build, which also carries the `FIX_FORCE_EMULATED_INT128` twins (`batch_emulated`, `isa_dispatch_emulated`) and the `FIX_SATURATE` twins (`batch_saturated`, `isa_dispatch_saturated`).
- `VARIANT scalar RAN: batch oracle rows and 100000000 seeded lanes bit-identical to scalar` on the real host.
- The AVX-512 variant's own bit gate **did not run** (`SKIP host lacks os-zmm-state`); its bit-identity to scalar on this host is therefore unproven, and under `FIX_SATURATE` it is not compiled at all.

## What to try next

1. Re-run this card's exact A/B on a Linux host with AVX-512 where the kernel enables zmm state in XCR0 (`0xe7`), or a Windows Skylake-W box. The card's BENCH HYGIENE already names Linux `taskset`; the macOS host cannot be that bench.
2. If macOS AVX-512 is ever wanted, the detection change would be to also accept an OS that reports zmm state through some other channel — but on this box there is none, and the safe default (refuse) is right. Do **not** weaken the `OS_ZMM` requirement.
3. `vpmullq` on Skylake is 3 uops with ~15-cycle latency (uops.info); if a runnable host appears, predict the mul-batch kernel's throughput from that before measuring.

## What not to repeat

- Do not treat `VARIANT avx512 SKIP host lacks os-zmm-state` as evidence that the AVX-512 kernels are correct on this host; the bit gate did not run.
- Do not add a `FIXED_ISA` bypass that ignores XCR0: on macOS/Skylake-W that would execute 512-bit instructions the OS does not save, which is a correctness and crash hazard, not a speedup.
- Do not re-measure AVX-512 batch kernels on a macOS Intel host expecting a number; check `XCR0` first.

## Not done

- The scalar-vs-avx512 A/B at n = 16, 256, 4096 (impossible here; variant refused).
- The scalar A/A noise floor over 7 full interleaved repeats: the `ab.sh` run reached `oracle FIXED_ISA=scalar` and its `oracle_check` at `base_random=50000000` outlived the 300 s wall before printing the summary table, so no `speedup`/spread row was produced.
- The frequency-licence cost (calibrated dependent scalar loop before and after a kernel burst): no AVX-512 kernel burst is possible here.
- `llvm-mca` of the kernel for `skylake-avx512` (the toolchain here has no `llvm-mca`); only a static `otool -tV` excerpt of the kernels is in `raw.txt`.
