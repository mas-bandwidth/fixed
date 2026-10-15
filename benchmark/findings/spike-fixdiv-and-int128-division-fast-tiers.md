VERDICT: KEEP (emulated arm: fixDiv slow tier 4.9x faster, bit-exact, no native regression; native arm64's libcall path is NOT addressed by this patch and needs a mirror change)

## Hypothesis

The slow tier of `fixDiv` (numerator >= 2^47) and the 128/64 divide behind `fixInt128Div`
run as 128-iteration shift-subtract long division on the emulated arm (plain MSVC, or any
`FIX_FORCE_EMULATED_INT128` build), because that arm has no hardware 128/64 divide. A
64-bit reciprocal, seeded from one hardware double division and refined by three integer
Newton-Raphson steps, plus an exact remainder-compare repair, should give the same
truncating quotient for a fraction of the iterations.

## The change

In `include/fixed/fixed_int128.h`, the emulated `fixEmuUInt128DivMod` now routes the
64-bit-divisor case through a new `fixEmuUInt128DivRemBy64Recip`: normalize the divisor so
its top bit is set, seed `r ~ 2^127/d` with `2^127/(double)d`, refine it with three integer
Newton steps (`r_{k+1} = r * (2^128 - d*r) / 2^127`, assembled with a 64x64->128
`fixEmuUInt128MulU64`), take the quotient as the top 65 bits of `(n<<s)*r`, then drive it
exact with at most a few `+-1` remainder compares. The double is only a seed (as in
`fixSqrt`): the final quotient/remainder are exact integer results, so determinism cannot
move. The 64/64 hardware path, the zero divisor, and the full 128/128 shift-subtract (for
divisors that do not fit in 64 bits) are unchanged. Native `__int128` and the x86-64 `divq`
tier are never touched. A unit tier `Int128DivFastTierTest` (`sweepDivFastTier` in
`test/int128_test.c`) holds the reciprocal path to an independent shift-subtract reference
and to native `__int128` over the tier gates, power-of-two `+-1` divisors, and 1M seeded
pairs.

## Before / after (ns/op, median of 7 interleaved runs, 200k iterations)

AMD Ryzen 7 255, x86-64, avx512f, cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, `-O3 -g -DNDEBUG -ffp-contract=off`.

| op | mode | base-native | cand-native | base-emu | cand-emu | emu change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| fixDiv_above2p47 | batch | 3.03 | 3.06 | 178.11 | 36.35 | **4.90x faster** |
| fixDiv_above2p47 | scalar | 2.27 | 2.26 | 2.23 | 2.18 | (fast path, see note) |
| fixInt128Div | batch | 8.17 | 8.17 | 192.60 | 188.45 | 1.02x (unchanged) |
| fixInt128Div | scalar | 4.07 | 3.91 | 133.30 | 134.45 | 1.01x (unchanged) |
| fixMul (control) | batch | 1.23 | 1.24 | 2.87 | 2.88 | unchanged |

Native is the control and is untouched (the patch lives in the emulated layer): no
regression. The `fixDiv_above2p47` scalar row measures the fast path, not the slow tier --
the dependent chain feeds each quotient back as the next dividend, which is almost always
below 2^47. BATCH (independent inputs, all above 2^47) is the slow-tier number.

`fixInt128Div` does not move because the harness feeds it divisors up to ~2^104 (`hi < 2^40`),
so the 64-bit-divisor reciprocal never fires there; real fixed3d traffic divides by a 64-bit
`fixed_t`, which is exactly the `fixDiv_above2p47` shape.

## Bit-exactness

- `ctest --test-dir build --output-on-failure`: 36/36 pass, native and emulated; the frozen
  int128 hash `0x348cfaf5be984407` is unchanged.
- `./build/fixed_oracle_check_emulated` (full baseline, base_random=50000000):
  `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)`.
  Targeted rows also match exactly: `fixDiv_above2p47 hash=679bdcc4e27fd523`,
  `fixInt128Div hash=65e492d721fcd5f6`, `fixUInt256DivMod hash=d6c2bda0c1986a82`.
- `fixUInt256DivMod` is affected (Knuth D calls `fixUInt128DivRemBy64`) and still matches.

## What to try next

Mirror the same double-seeded Newton reciprocal into `fixInt128Div`'s non-x86 native branch
(`return a / b`, the `__udivti3` libcall arm64 pays) -- detect a 64-bit divisor and use the
reciprocal instead of the libcall; that is the arm64 win this card could not measure on an
x86-64 box. Also consider a reciprocal for the wide-divisor (128/128) case, which this patch
leaves as shift-subtract.

## What not to repeat

Do not seed the quotient directly from `(double)n/(double)d` (error is ~2^75 for small
divisors); do not skip the integer Newton steps (a bare double seed needs ~2^11 repair
iterations). Do not try to use the scalar dependent-chain number for `fixDiv_above2p47` as
the slow-tier measurement.
