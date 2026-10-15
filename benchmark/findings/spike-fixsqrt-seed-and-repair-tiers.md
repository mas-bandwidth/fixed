# Spike: fixSqrt and the 128-bit integer square root, a table seed and fewer repair steps

VERDICT: LEARNED

## Hypothesis

The >104-bit arm of `fixISqrt128High` runs a restoring shift-subtract square
root (~64 dependent 128-bit iterations). A hardware double seed is already
correctly rounded to ~2^-53 relative error, so for a 128-bit input its absolute
error is at most ~2^11 raw units. One integer Newton step in 128-bit collapses
that to a unit or two, after which the exact repair loop runs once or twice.
The restoring algorithm should then be needed only for the rare tail where the
seed rounds up to 2^64 (overflow) or the Newton quotient would not fit 64 bits.

## The change

In `include/fixed/fixed.h`, the `hi >= 2^40` arm of `fixISqrt128High` now seeds
with `FIX_SQRT_SEED((double)hi * 2^64 + (double)lo)`, takes one integer Newton
step `r = (r + n/r) / 2` (the 128/64 quotient via `fixUInt128DivRemBy64`, whose
`hi < r` precondition is enforced by a fallback), then repairs exactly with the
same two while-loops as the <=104-bit arm. The restoring algorithm is kept as
the fallback for the rare tail (`r == 0 || hi >= r`). The Newton half-sum is
computed as `(r>>1)+(q>>1)+((r&q)&1)` to avoid a 64-bit overflow in `r+q`.

## Before / after (ns/op, native x86-64, cc 15.2.0, -O3 -DNDEBUG -ffp-contract=off)

AMD Ryzen 7 255, avx2/avx512f, 7 interleaved repeats, 200000 iters. `a` is the
baseline (restoring), `b` is the candidate (seed+Newton). speedup = a_med/b_med.

| op | mode | a_med | b_med | speedup |
| --- | --- | ---: | ---: | ---: |
| fixMul | scalar | 1.5085 | 1.5082 | 1.000 |
| fixMul | batch | 1.2368 | 1.2378 | 0.999 |
| fixSqrt_small | scalar | 7.3072 | 7.3064 | 1.000 |
| fixSqrt_small | batch | 2.3204 | 2.4819 | 0.935 |
| fixSqrt_raw_below2p32 | scalar | 7.3262 | 7.3198 | 1.001 |
| fixSqrt_raw_below2p32 | batch | 2.1535 | 2.6455 | 0.814 |
| fixISqrt128High_u64 | scalar | 80.5390 | 84.9592 | 0.948 |
| fixISqrt128High_u64 | batch | 2.8002 | 2.7151 | 1.031 |
| fixISqrt128High_above104bits | scalar | 79.9271 | 83.3573 | 0.959 |
| fixISqrt128High_above104bits | batch | 58.5595 | 3.9866 | 14.689 |

The target operation, `fixISqrt128High_above104bits`, is **14.7x faster in the
batch (throughput) loop** (58.56 -> 3.99 ns) but **4% slower in the scalar
(latency) loop** (79.93 -> 83.36 ns), outside its ~1% spread. The scalar
regression is the `divq` 128/64 division latency on the dependent chain. Every
other row is inside its spread (the small-op rows carry >20% spreads).

## Bit-exact evidence

- `./build/fixed_oracle_check`: `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)`.
- `ctest --test-dir build`: 36/36 pass, including the new `sqrt_tier` and
  `sqrt_tier_emulated` suites (exhaustive below 2^32, 100M seeded 64/128-bit,
  perfect-square neighbours, tier boundaries 2^104-1 and 2^104), all against the
  reference restoring algorithm.
- The emulated full-set oracle was not completed within the wall (killed to run
  the A/B); the emulated `sqrt_tier` suite passed, so the emulated arm is
  bit-exact on the tier sweep.

## What to try next

The batch win is real and large. The scalar latency is the open problem: the
`divq` sits on the dependent chain. A follow-up could (a) seed on the high half
(`sqrt(hi) << 32`) and use a 64-bit-only Newton step, or (b) a small leading-bit
table seed, to cut the division latency. The emulated arm (plain MSVC) also
needs measuring: its 128/64 division is a 128-iteration shift-subtract, so the
Newton step is likely slower there than the restoring loop.

## What not to repeat

Do not use `(r + q) >> 1` for the Newton half-sum: `r + q` overflows 64 bits
when the seed is above 2^63, silently halving the root and turning the repair
loop into a ~2^63-iteration hang. Do not run the full 50M-input oracle in the
foreground; it takes minutes and blocks the A/B.
