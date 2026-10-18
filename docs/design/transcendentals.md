# The exp, log and tan families: reduction, tables, kernels and error budgets

This is the design the function cards build from: `fixed-exp-exact`, `fixed-tanh-exact`,
`fixed-sinh-cosh-exact`, `fixed-log-exact` and `fixed-tan-exact`. Each section gives the
reduction, the tables, the kernel, the final rounding, the saturated and out-of-domain results
and an error budget, so each function card is a mechanical implementation against a stated
number. Every constant, table entry and polynomial coefficient below comes from one of three
committed Sollya scripts (`sollya/exp_kernel.sollya`, `sollya/log_kernel.sollya`,
`sollya/tan_kernel.sollya`, outputs in the matching `.out`), and the tan reduction's bit count
comes from `tools/tan_reduction_bound.c`. Section 0 says how the numbers fit together.

Ruling, for every card here: these are **new functions only**. No existing function, test,
expected value or frozen hash changes (`fixExp2`, `fixLog2`, `fixPow`, `fixSin`, `fixCos`,
`fixComputeCosSin*`, `fixAtan2`, `fixSqrt` stay bit for bit). Integer arithmetic only, through
the `fixUInt128` / `fixInt128` / `fixUInt256` vocabulary of `include/fixed/fixed_int128.h` and
`include/fixed/fixed_int256.h`, so the native and emulated 128-bit arms, gcc and clang, give
the same bits. No gcc builtin outside the library's own wrappers and no variable-length array,
so plain MSVC builds it.

## 0. Notation, the target, and how a budget proves correct rounding

* A Q48.16 value is `raw * 2^-16`. "1 raw" is 2^-16. `y` is the exact result scaled to raw
  units, `y = f(raw * 2^-16) * 2^16`; the correctly rounded result is `RN(y)`, round to
  nearest, ties to even. For every function and input here `y` is transcendental (Lindemann:
  exp, ln, tan of a non-zero rational), except the trivial points `f(0)` and `ln 1`, `log2 2^k`,
  `log10 10^k`, which are integers in raw units and come out exactly. So `y` is never a
  midpoint and the tie rule never fires on an exact value.
* **The rounding test.** Each function computes an approximation `Y` with `|Y - y| <= eps`
  (its budget, in raw units) and returns the nearest integer to `Y`. That equals `RN(y)` for
  every input whose `y` is farther than `eps` from a midpoint (an odd multiple of 1/2). Call
  the smallest such distance over a domain its **margin**. `eps < margin` on a domain proves
  correct rounding on that domain. And `eps < 1/2` alone proves the result within 1 raw of
  `y` everywhere, which is the "within 1 raw elsewhere" floor.
* **Exhaustive domains** (exp, sinh, cosh, tanh, tan below 32 rad, ln/log2/log10 on
  [1, 2^20] raw): the margin is measured, every input, with MPFR at 256 bits. Measured on
  amd-1 (MPFR 4.2.2) on 2026-10-18 for this design, and measured again by each function card's
  exhaustive sweep piece (its `notcr=0` line is the same fact):

  | function | exhaustive domain (raw) | inputs | margin (raw) | at raw |
  |---|---|---|---|---|
  | exp | [-772243, 2135026] | 2,907,270 | 2^-21.15 | -129660 |
  | sinh | 1 <= \|raw\| <= 2180452 | 4,360,904 (+0) | 2^-32.75 | 441619 |
  | cosh | \|raw\| <= 2180452 | 4,360,905 | 2^-20.58 | 256 |
  | tanh | 1 <= \|raw\| <= 408834 | 817,668 (+0) | 2^-20.28 | 144246 |
  | tan | 1 <= \|raw\| < 2^21 (\|x\| < 32 rad) | 4,194,302 (+0) | 2^-22.24 | 557409 |
  | ln | [1, 2^20] | 1,048,576 | 2^-21.44 | 541866 |
  | log2 | [1, 2^20] | 1,048,576 | 2^-20.50 | 692513 |
  | log10 | [1, 2^20] | 1,048,576 | 2^-26.00 | 967694 |

  The odd functions are measured on the positive half; the negative half is the same set of
  distances by symmetry (every implementation below computes on |x| and applies the sign, so
  oddness is exact by construction).
* **Domains too large to sweep** (ln, log2, log10, tan above 2^20 raw): the margin there is
  not small by accident, it is small by counting. In the top input binade [2^62, 2^63) the
  inputs are 2^-62 apart in relative terms, so the outputs of ln are about 2^-46 raw apart, and
  every output midpoint has an input within about 2^-46 raw of it. Any `eps` above ~2^-46 raw
  therefore fails on thousands of inputs (section 2.2 shows this happening to `fixLog2`).
  The proof there is the BaCSeL list of card `fixed-bacsel-hard-cases`: for each input binade
  it is the complete list of inputs whose `y` lies within 2^-k raw of a midpoint (k chosen per
  binade, about e + 9 for the binade [2^e, 2^(e+1)) of x, 55 at the top). Every input not on
  the list is at least 2^-k from a midpoint, so `eps < 2^-k` makes it correctly rounded, and
  every input on the list is a sweep piece checked exactly. So the condition each budget
  below states is `eps < 2^-k` for every k the list uses, i.e. `eps <= 2^-62` raw is enough
  for every list with k <= 61 (the BaCSeL card expects k near e + 9, 55 at the top binade).

Shared arithmetic. The kernels need the high 128 bits of a 128 x 128 product. The library has
one, `fixUInt128Mul128` in `src/fixed_math.c`, but it is `static` there and that file is not
edited by these cards. Each new file builds the same product from the public
`fixUInt256MulU128ByU64` (two calls: `a * b.lo + (a * b.hi) << 64`, then the shift), as a
six-line `static inline` in `src/exact/exp_kernel.h` for the exp family and the same six
lines in `src/exact/log.c` and `src/exact/tan.c` (which cannot include the exp header without
a card dependency). Unifying them into one public `fixUInt128MulWide` is a follow-up that
edits `fixed_int256.h`, Glenn's call, not part of these cards. Every truncating multiply in a
budget below is this helper: its error is less than one unit of the format it truncates to.

## 1. The exp family: exp, sinh, cosh, tanh

### 1.1 exp, `fixExpExact`

**Saturation and zero, decided first.** `RN(y) = 0` exactly when `y < 1/2`, which is every
raw <= -772244 (x < -17 ln 2): return 0. `RN(y) >= INT64_MAX` exactly when
`y >= 2^63 - 3/2`, which is every raw >= 2135027 (x > 47 ln 2): return INT64_MAX.
Both thresholds were found by the MPFR scan above (`first_inside=-772243`,
`last_inside=2135026`); the exp sweep's edge pieces (every raw within 4096 of each end)
check them exactly. Between them, raw is in [-772243, 2135026].

**Reduction (Tang, table size 2^7).** `N = floor( x * 128 / ln 2 )`, `m = N >> 7` (arithmetic),
`j = N & 127`, `r = x - N * ln2 / 128` in [0, ln2/128). Then `e^x = 2^m * 2^(j/128) * e^r`.
Integer form, exact in the stored-constant world:

* `LN2_128 = round( ln 2 * 2^128 )` = 235865763225513294137944142764154484399 (u128;
  `exp_kernel.out`), 128 bits of ln 2, error below 2^-129.
* `N0 = ( raw * INV_LN2_64 ) >> 73` in fixInt128 (floor shift), with
  `INV_LN2_64 = round( 2^64 / ln 2 )` = 26613026195688644983. This is floor(raw * 2^-9 / ln 2)
  or one off.
* `D = raw * 2^119 - N0 * LN2_128` in 256 bits (raw * 2^119 is x * 2^135). Correct `N0` by
  one in either direction until `0 <= D < LN2_128` (at most one step). Then `N = N0` and
  `r = D * 2^-135` exactly, `D < 2^128`.
* `r128 = D >> 7`, r in unsigned Q0.128 (r < 2^-7.53, so r128 < 2^121).
* N is in [-2176, 6015] on the exp domain, m in [-17, 46].

r is never negative, so the whole kernel runs unsigned, with all coefficients positive.

**Table.** `T_j = round( 2^(j/128) * 2^126 )`, j = 0..127, unsigned Q2.126 in [2^126, 2^127),
128 entries of two u64 words (2 KB). Generated by `sollya/exp_kernel.sollya` (the `T j hi lo`
lines of `exp_kernel.out`), checked against Python `decimal` at 120 digits (0 mismatches).

**Kernel.** `P(r) = 1 + r + sum_{k=2..8} C_k * 2^-127 * r^k`, degree 8, each `C_k` an unsigned
integer (127 fraction bits), the 1 and r terms exact. fpminimax in the absolute error, fixed
format, on [0, ln2/128 rounded up to 2^-130]; supnorm certifies
`|P - e^r| <= 2^-102.674` on that interval (`exp_kernel.out`). Evaluation, Horner, every
accumulator Q1.127 unsigned:

    q = C_8;  for k = 7 .. 2:  q = C_k + mulhi( q, r128 )      // mulhi(a,b) = (a*b) >> 128
    s = 2^127 + mulhi( q, r128 )                               // 1 + r*q
    P = 2^127 + mulhi( s, r128 )                               // 1 + r*(1 + r*q)
    M = ( T_j * P ) >> 127                                     // Q2.126, M in [2^126, 2^127)

**Final rounding.** `raw_out = ( M + 2^(s-1) ) >> s` with `s = 110 - m` in [64, 127] (the same
shape as `fixExp2Exact`); `M + 2^(s-1) < 2^128`. Rounding the approximation half-up is fine:
a computed tie would mean `y` within `eps` of a midpoint, which the margin excludes.

**Error budget** (absolute, on M as a real number in [1, 2)):

| step | bound |
|---|---|
| ln 2 to 128 bits: \|N\| * 2^-129 / 128 on r, times M <= 2 | 2^-122.44 |
| r truncated to Q0.128, times 2 | 2^-127 |
| kernel, Sollya supnorm, times T_j < 2 | 2^-101.67 |
| Horner truncations (8 steps, each < 2^-127, damped by r < 2^-7.5), times 2 | 2^-124 |
| T_j rounding 2^-127, times P < 1.006 | 2^-126.99 |
| T_j * P truncated to Q2.126 | 2^-126 |
| **sum on M** | **2^-101.67** |

**exp budget: eps <= 2^-101.67 * 2^(m+16) <= 2^-101.67 * 2^62 = 2^-39.67 raw, against the
exhaustive margin 2^-21.15 raw (a factor of 2^18.5).** So exp is correctly rounded on every
input; the exhaustive sweep piece [-772243, 2135026] confirms it with `notcr=0`.

### 1.2 The shared kernel, `src/exact/exp_kernel.h`

Created by `fixed-exp-exact`, included (never copied) by `fixed-tanh-exact` and
`fixed-sinh-cosh-exact`. Internal to src/exact/, not installed:

    // e^(a * 2^-16) = *mantissa * 2^(*exponent - 126), *mantissa in [2^126, 2^127), for
    // |a| <= 2^22 (|x| <= 64). |*mantissa * 2^-126 - e^r * 2^(j/128)| <= 2^-101.67
    // (docs/design/transcendentals.md, 1.1). Integer only.
    static inline void fixExpKernel( int64_t a, fixUInt128* mantissa, int* exponent );

    // ( a * b ) >> 128 from two fixUInt256MulU128ByU64 calls (section 0).
    static inline fixUInt128 fixExactMulHi128( fixUInt128 a, fixUInt128 b );

`fixExpKernel` is steps 2-4 of 1.1 (reduction, table, kernel, `M`), `*exponent = m`.
`fixExpExact` is the saturation tests plus `fixExpKernel` plus the final shift. Over
|a| <= 2^22, |N| < 2^13.53 and the ln 2 term grows to 2^-121.5, still nothing next to the
kernel's 2^-101.67, so the same bound holds over the whole kernel domain.

### 1.3 sinh and cosh, `fixSinhExact`, `fixCoshExact`

Both work on `|raw|` (sinh negates at the end: odd exactly; cosh even exactly; INT64_MIN
takes the saturated branch, so the magnitude is never negated as a signed value).

* **Saturation.** `RN(sinh) >= INT64_MAX` and `RN(cosh) >= INT64_MAX` both start at
  |raw| = 2180453 (x >= asinh(2^47), measured): return INT64_MAX (sinh: with the input's
  sign, so -INT64_MAX for negative inputs, never INT64_MIN).
* **Small |x|, sinh.** `RN(sinh x * 2^16) = raw` for every 0 <= raw <= 2344 (measured; the
  cubic term raw^3 * 2^-32 / 6 stays under the distance to the midpoint): return raw. This is
  the "small kernel" near 0, and it is exact. cosh needs none.
* **Otherwise.** `fixExpKernel( |raw| )` gives `(Ma, ma)`, `fixExpKernel( -|raw| )` gives
  `(Mb, mb)`, `ma >= 0 > mb`. With `d = ma - mb` (at most 95):
  `Y = 2^(ma + 15 - 126) * ( Ma -/+ (Mb >> d) )`, minus for sinh, plus for cosh, in 128 bits
  (Ma + Mb < 2^128), then one round to nearest by the shift `111 - ma`.

**Why no separate odd kernel near 0 is needed.** Cancellation in `e^x - e^-x` costs relative
bits, but the output is fixed point: what has to be small is the *absolute* error in raw
units. Each kernel call is good to 2^-101.67 absolute on its mantissa, so the difference is
good to 2^-101.67 * 2^(ma + 15) (+ half that from the e^-x side), whatever the size of
sinh x. Near 0 that is 2^-86 raw; the identity cut-off above handles the region where the
result is just `raw`. If a sweep ever showed `notcr` near 0, the named fix is the odd kernel
`x + x^3 q(x^2)` on |x| < 2^-4 from a fourth Sollya script; the budget says it will not.

**sinh and cosh budget: eps <= 2^62 * ( 2^-101.67 + 2^-102.67 + 2^-126 ) = 2^-39.09 raw,
against margins 2^-32.75 (sinh) and 2^-20.58 (cosh).** sinh's margin is the tightest of the
exp family, still a factor 2^6.3.

### 1.4 tanh, `fixTanhExact`

On |raw|, sign applied at the end (odd exactly; `fixTanhExact(INT64_MIN) = -65536`).

* **Saturation.** `RN(tanh x * 2^16) = 65536` for every |raw| >= 408835 (measured,
  x >= 6.2383): return 65536 with the sign.
* **Small |x|.** `RN = raw` for every 0 <= raw <= 1860 (measured): return raw.
* **Otherwise.** `E = e^(2x)`: `fixExpKernel( 2 * |raw| )` gives `(M, m)`, m in [0, 18].
  With `a = 2^(126 - m)` (exact): `num = M - a`, `den = M + a` (both < 2^128),
  `Q = floor( num * 2^128 / den )` by `fixUInt256DivMod`, Q in Q0.128 (tanh < 1), and
  `raw_out = ( Q + 2^111 ) >> 112`.

No separate small-|x| polynomial is needed, for the same reason as sinh: the quotient's
absolute error is `0.5 * err(M) + 2^-128` because d/dM of (M - a)/(M + a) is at most 1/2.

**tanh budget: eps <= 2^16 * ( 2^-102.67 + 2^-128 ) = 2^-86.67 raw, against the margin
2^-20.28.**

## 2. The log family: ln, log2, log10

### 2.1 ln, `fixLnExact`

**Out of domain.** raw <= 0 returns INT64_MIN, as `fixLog2` does. No saturation: |ln x| < 33,
the output is at most 2,135,026 raw in magnitude.

**Reduction (Tang, 2^7 cells).** `msb` = index of the top set bit of raw (portable, no builtin),
`e = msb - 16`, `m = raw << (62 - msb)` as u64 (exact: raw < 2^63), m in [2^62, 2^63) is the
mantissa in Q2.62. `i = (m >> 55) & 127`, the top 7 fraction bits.

* **Tables** (`sollya/log_kernel.sollya`, the `L i INV hi lo` lines of `log_kernel.out`):
  `INV_i = ceil( 2^24 / (128 + i) )`, an 18-bit integer, `1/c_i` rounded **up** to 2^-17
  (c_i = 1 + i/128); and `L_i = round( ln( 2^17 / INV_i ) * 2^128 )`, u128, in [0, ln 2).
  128 entries. `INV_0 = 2^17` and `L_0 = 0`, so ln 1 is exactly 0. Checked against Python
  `decimal` (0 mismatches), and every cell's u range checked to lie in [0, 2^-7].
* `u = m * INV_i - 2^79`, exact in u128 (Q.79; the product is below 2^80). Because INV_i
  was rounded up, u >= 0, and u <= 2^-7 (max over the cells, computed).
  `u128 = u << 49`, u in unsigned Q0.128, exact.
* `ln x = e * ln 2 + L_i + log1p( u )`.

**Kernel.** `log1p(u) = u + sum_{k=2..10} C_k * 2^-127 * u^k`, degree 10, signed coefficients
alternating in sign (C_k < 0 for even k), fixed with 127 fraction bits; fpminimax absolute on
[0, 2^-7 + 2^-16], supnorm certifies `|p - log1p| <= 2^-101.04` (`log_kernel.out`). Because
u >= 0 and the signs alternate, Horner runs unsigned on magnitudes:
`w = |C_10|; for k = 9..2: w = |C_k| - mulhi( w, u128 )`, every w positive, then
`log1p(u) = u - mulhi( mulhi( u128, u128 ), w )` (the last product realigned from Q.127 to
Q0.128 by one exact shift).

**Sum and rounding.** The three terms are added in signed Q7.120 (fixInt128; |ln| < 2^5.05):
`e * LN2_128` as a 192-bit product (|e| <= 46) shifted right 8, `L_i >> 8`, and the kernel
shifted right 8. Then `raw_out = ( S + 2^103 ) >> 104` (floor shift). Here
`LN2_128` is the same digits as the exp section (`log_kernel.out` prints it again).

**ln budget** (absolute, on ln x):

| step | bound |
|---|---|
| e * ln 2 with ln 2 to 128 bits (\|e\| <= 46), shift to Q.120 | 2^-123.5 + 2^-120 |
| L_i to 128 bits, shift | 2^-129 + 2^-120 |
| kernel, Sollya supnorm | 2^-101.04 |
| Horner truncations, shift | 2^-125 + 2^-120 |
| **sum** | **2^-101.04** (the shifts add 2^-118.4) |

**ln budget: eps <= 2^-101.04 * 2^16 = 2^-85.04 raw**, against the exhaustive margin
2^-21.44 on [1, 2^20], and below 2^-k for every BaCSeL k <= 85, so the hard-case argument of
section 0 proves correct rounding in every binade the list covers.

### 2.2 log2: `fixLog2` is kept and checked; a `fixLog2Exact` is needed

`fixLog2` (src/fixed_math.c) is not edited. Its digits come from 32 repeated squarings of the
mantissa in Q2.62, each truncated, so its log2 is low by up to about
`sum_j 2^-62 / ln2 * 2^-j < 2^-61.47` (2^-45.47 raw), always in the same direction. Rounding
`fraction32` to 16 bits is correct except where log2 x lies in [mid, mid + 2^-45.47 raw) above
a midpoint. That is no input on [1, 2^20] (margin 2^-20.50), but it is many inputs higher up,
where inputs are closer together than that band. Measured for this design:

* exhaustive [1, 2^20] raw: 1,048,576 inputs, `notcr=0`; 16,384 seeded raws per binade
  2^20..2^62 (xorshift64, seed 0x9E3779B97F4A7C15): 704,512 inputs, `notcr=0`;
* the **midpoint-adjacent probe** (for each binade e and each k in 0..65535, the raw
  `ceil( 2^(16 + e + (2k+1)/2^17) )`, the first input above each log2 midpoint): `notcr=0`
  for e <= 30, then 1, 2, 3, 7, 15, 29, 55, 111, 256, 494, 1008, 2068, 4095, 8220, 16783 and
  **33,781 of 65,536 in e = 46**. Two witnesses, each 1 raw low:
  `fixLog2(208707216768631) = 2068871`, exact `2068871.500000000000010603...`, RN 2068872;
  `fixLog2(4611710406440186476) = 3014656`, exact `3014656.500000000000009311...`, RN 3014657.

So `fixed-log-exact` adds `fixLog2Exact` beside it (the card's own condition, "only if it is
not correctly rounded on some input", is met by these witnesses). Method, through the ln
reduction and kernel: `log2 x = e + ( L_i + log1p(u) ) * log2(e)`, the integer e added exactly
in raw units after the rounding of the second term, with
`LOG2E_127 = round( log2(e) * 2^127 )` = 245461841629398282873184673143046618761 (128-bit
multiply, `log_kernel.out`). **log2 budget: (L_i + log1p) < 0.7, so eps <=
1.4427 * 2^-101.04 * 2^16 + 2^-104 = 2^-84.5 raw.** The midpoint-adjacent probe becomes a
sweep piece for `log2` (and for `log2_exact`), so the witnesses are in the scoreboard.

### 2.3 log10, `fixLog10Exact`

`log10 x = ln x * log10(e)`, the ln sum S (Q7.120, before rounding) times
`LOG10E_128 = round( log10(e) * 2^128 )` = 147782754242741207861553459020638580249 as a
128 x 128 product on |S| (sign applied after), shifted right 128 to Q.120, then rounded as
ln is. The brief asks for at least 64 fraction bits in the constant; 64 would be
33 * 2^-65 = 2^-60 absolute, 2^-44 raw: enough for the exhaustive margin (2^-26.00) but not
for the hard-case argument (2^-k with k near 55 at the top), so the constant carries 128.
**log10 budget: eps <= ( 0.4343 * 2^-101.04 + 33 * 2^-129 + 2^-120 ) * 2^16 = 2^-86.2 raw.**

## 3. tan, `fixTanExact`

### 3.1 How close a Q48.16 angle gets to a pole (`tools/tan_reduction_bound.c`)

Every input is an integer times 2^-16, so the quarter-turn count is `q = raw * alpha` with
`alpha = 2^-15 / pi`, and the raw closest to a multiple of pi/2 minimises `||raw * alpha||`.
By the best-approximation property of continued fractions those minima are attained at the
convergent denominators of alpha; the tool lists them up to 2^63, checks the property by
brute force over every raw below 2^21, and prints (MPFR 4.2.2, 2048 bits, gcc and clang
agree):

    worst raw (closest to a multiple of pi/2, 1 <= raw <= 2^63): 6173953965003744831, multiple 59974077877939 of pi/2 (a pole), log2 f_min = -64.956, log2 r_min = -64.305
    worst raw with |x| < 32 rad (raw < 2^21): 1750043, log2 f = -21.399
    saturation edge: 2^16 cot r = 2^63 - 1.5 at log2 r_sat = -47.000, log2 f_sat = -47.651
    bits of 2/pi needed for relative precision 2^-120 of r: J >= 47 + 120 + 47.651 = 215 (f_floor = max(f_min, f_sat))
    bits of 2/pi without saturation (the floating-point count, f_floor = f_min): 232
    tan_reduction_bound: worst_raw=6173953965003744831 bits=215

Truncating 2/pi after J fraction bits moves q by at most 2^63 * 2^-16 * 2^-J = 2^(47-J), so
r is known to relative 2^-p wherever |f| >= f_floor when `J >= 47 + p + log2(1/f_floor)`. In
Q48.16, no input needs relative precision closer to a pole than the saturation edge, so
f_floor is f_sat = 2^-47.65 and **J >= 215** for the p = 120 this design asks. The design
carries **240** bits, which also covers the 232 a floating-point tan would need at f_min.

**Saturation.** `fixTanExact` returns +/-INT64_MAX (never INT64_MIN) where
`|y| >= 2^63 - 3/2`, i.e. |r| below r_sat = 2^-47.000 on an odd quarter (a pole). Signed by
the input's sign and by the side of the pole: on an odd k, tan x = -cot r, so r > 0 gives
-INT64_MAX. Because the convergents are the records of closeness, the smallest |raw| that
saturates is the first pole-side convergent past f_sat, **raw 195734331562903**
(x = 2.99e9 rad, tan x * 2^16 = +1.18e20). The closest approach that does not saturate is
**raw 130146176612260** (tan x * 2^16 = -8558550789933375681.40, 2^62.89). Below 32 rad nothing saturates
(the largest |y| is 2^36.75, at raw 1750043). All three are named test points.

### 3.2 Reduction: Payne-Hanek, shared with fixed-sincos-exact

`fixComputeCosSinExact` (fixed-sincos-exact, PR #41, src/fixed_math.c on its branch) reduces
by `m * C` with `C = floor( 2^113 / pi ) = HI * 2^64 + LO`, the first 112 fraction bits of
2/pi (`FIX_TWO_OVER_PI_HI = 0x0000A2F9836E4E44`, `FIX_TWO_OVER_PI_LO = 0x1529FC2757D1F534`),
which gives the quarter-turn fraction to < 2 units of 2^-64. That is the right reduction for
sine and cosine (absolute error 2^-61 rad) and for tan wherever |tan| <= 1, but near a pole
cot r needs r to relative 2^-120 down to 2^-47.65, which is 215 bits.

What is shared: the method (`m = |raw|` as u64, the product with the words of 2/pi, the
quarter count from the high part, the signed fraction from the low part, sign applied at the
end) and the first two words. `tan_kernel.out` prints `floor( 2^240 * 2/pi )` as four words
and checks words 0 and 1 against sincos's HI and LO (`true`):

    W0 = 179192535600708          (= FIX_TWO_OVER_PI_HI)
    W1 = 1525027194748925236      (= FIX_TWO_OVER_PI_LO)
    W2 = 15979012694373645379
    W3 = 10394869040465829563

What tan adds: words W2 and W3 (bits 113..240) and the parity of k instead of k mod 4. The
sincos constants are file-local `#define`s in src/fixed_math.c and `fixComputeCosSinExact`
returns rounded Q48.16 values, so there is nothing to call: `src/exact/tan.c` holds its own
four words, cites the sincos constants for W0:W1, and its unit test checks all four against
the `tan_kernel.out` digits.

Integer form: `t = m * (W0:W1:W2:W3)`, four 64 x 64 -> 128 partial products summed exactly into
five words (no truncation). With 2^-16 for the input scale, `q = t * 2^-256`: the integer part
`t >> 256` (< 2^47), its parity and the fraction's top bit give k's parity (round to nearest
quarter), and the fraction as a signed 256-bit value is `f` in [-1/2, 1/2), error below
2^-193 from the 2/pi truncation. Normalise |f|: `z` = its leading zeros (z <= 65 by the
tool), `F` = the 128 bits from the leading one (relative truncation 2^-127). Then
`R = ( F * PIO2_127 ) >> 127` with `PIO2_127 = round( pi/2 * 2^127 )` =
267257146016241686964920093290467695825 gives `|r| = R * 2^(-128-z)`, a 128-bit mantissa and
an exponent, relative error below 2^-125.7 (2/pi, F, pi/2 and the product).

### 3.3 Table and kernel

`|r| <= pi/4`. Two paths, on the size of |r|:

* **Table path, |r| >= 1/256.** r to fixed Q0.128 (shift; abs truncation 2^-128),
  `j = round( r * 128 )` in 1..101, `s = r - j/128`, |s| <= 2^-8, exact.
  `TAN_j = round( tan(j/128) * 2^127 )`, j = 0..101, unsigned Q1.127 (`T j hi lo` lines of
  `tan_kernel.out`, 1.6 KB; checked against a Python decimal Taylor series, 0 mismatches).
  `t = tan(s)`, then `tan r = (TAN_j + t) / (1 - TAN_j * t)` and
  `cot r = (1 - TAN_j * t) / (TAN_j + t)`: one `fixUInt256DivMod` either way, the numerator
  shifted up 128. The odd quarters use cot.
* **Pole-and-zero path, |r| < 1/256 (j = 0).** r stays as mantissa and exponent.
  `tan r = r * (1 + w)`, `w = r^2 * q(r^2)` in fixed Q0.127 (|w| < 2^-16). Even quarter:
  `y = 2^16 * tan r`, under 256 raw. Odd quarter: `y = 2^16 / ( r * (1 + w) )` as
  `2^255 / ( R * (1 + w) )` (one divide) and a shift by the exponent; |r| < 2^-48 saturates
  without dividing, and a computed `|Y| >= 2^63 - 3/2` saturates.

**Kernel.** `tan(s) = s + sum_{k=3,5,..,13} C_k * 2^-127 * s^k`, odd degree 13, the s
coefficient exactly 1, the others unsigned with 127 fraction bits. This kernel feeds a
reciprocal near the poles, so its error has to be small relative to tan(s) down to s = 2^-65,
not only absolutely: fpminimax runs in the **relative** error (the absolute fit of the
odd basis does not converge in Sollya 8.0), and supnorm certifies both, on
[-(2^-8 + 2^-100), 2^-8 + 2^-100]: **relative 2^-134.23, absolute 2^-142.32**
(`tan_kernel.out`). Horner in s^2: `w = C_13; for k = 11..3: w = C_k + mulhi( w, s2 )`, then
`t = s + mulhi( s, mulhi( s2, w ) )` (sign-magnitude on s; `s2 = mulhi( s, s )`, Q0.128; products realigned to Q0.128 by one
exact shift where a Q.127 factor enters).

### 3.4 tan budget

Relative error of the value before the final rounding:

| step | table path | pole path |
|---|---|---|
| 2/pi to 240 bits, relative at \|f\| >= f_sat | 2^-184 (\|f\| >= 2^-8.6) | 2^-145.35 |
| F, pi/2, product: r relative | 2^-125.7 | 2^-125.7 |
| r to Q0.128 (abs 2^-128 at r >= 2^-8) | 2^-120 | - |
| kernel (Sollya), Horner, TAN_j | 2^-125 abs | 2^-126 |
| combination, divide | 2^-127 | 2^-127 + 2^-127 |
| **total** | abs 2^-123.6 on tan r (tan' <= 2) | **relative 2^-124** |

**tan budget: eps <= 2^-91.6 raw on the table path (|y| <= 2^24, the odd-quarter case
2^16 * 2^-123.6 / tan^2 r) and eps <= 2^63 * 2^-124 = 2^-61 raw on the pole path, against
the exhaustive margin 2^-22.24 below 32 rad and below 2^-k for every BaCSeL k <= 61.** Above
32 rad the claim is correct rounding wherever `tan_big.txt` covers the binade and within 1
raw everywhere (eps < 1/2).

## 4. The exhaustive pieces and counts (as `fixed-sweep-harness-scoreboard` defines them)

| function | exhaustive piece (raw) | inputs | other pieces |
|---|---|---|---|
| exp | [ceil(2^16 ln 2^-17), floor(2^16 ln 2^47)] = [-772243, 2135026] | 2,907,270 | every raw within 4096 of each end; 2^20 seeded raws over the rest (0 or INT64_MAX exactly) |
| sinh | \|raw\| <= 2180452 | 4,360,905 | 2^20 seeded beyond (+/-INT64_MAX exactly) |
| cosh | \|raw\| <= 2180452 | 4,360,905 | 2^20 seeded beyond (INT64_MAX exactly) |
| tanh | \|raw\| <= 408834 | 817,669 | 2^20 seeded beyond (+/-65536 exactly) |
| ln, log2, log10 | [1, 2^20] | 1,048,576 each | 2^18 seeded per binade above 2^20; the BaCSeL list per function; the midpoint-adjacent probe (2.2), 17 x 65,536 raws for e = 30..46, for log2 and log2_exact |
| tan | \|raw\| < 2^21 (\|x\| < 32 rad) | 4,194,303 | 2^18 seeded per binade 32 rad..2^47 rad; every raw within 64 of each multiple of pi/2 up to 2^20 rad; `tan_big.txt`; the three named points of 3.1 |

The sweep card's README numbers ("about 2.9M", "about 4.4M", "about 0.8M") are these.

## 5. Test plan per function

Each card's fast test (`test/exact/<fn>_test.c`, under 1 s at -O2, no MPFR, no clock) reads
the points header `fixed_sweep --fn <fn> --emit-points` writes (every edge, 4096 seeded draws,
the 256 worst inputs of the sweep) and checks each exactly, plus these named cases:

* **exp**: raw -772244 -> 0, -772243 -> 1 (the first non-zero), 2135026 (largest finite),
  2135027 -> INT64_MAX, INT64_MAX -> INT64_MAX, INT64_MIN -> 0, 0 -> 65536 exactly, the margin raw -129660.
  Negative control (`test/exact/exp_negative_control.c`, WILL_FAIL): the same points with
  `LN2_128` replaced by a 64-bit ln 2 (`round(ln 2 * 2^64) << 64`): at |N| ~ 6000 that moves r by
  2^-65 * 6000 / 128 = 2^-59.4 and y by up to 2^63 * 2^-59.4 = 12 raw, so it must fail.
* **sinh / cosh**: 0 -> 0 / 65536; 2344 -> 2344 and 2345 (the identity edge); +/-2180452 and
  +/-2180453 (saturation edges, sign); INT64_MIN -> -INT64_MAX / INT64_MAX; the sinh margin
  raw 441619. Oddness and evenness on every point.
* **tanh**: 1860, 1861; 408834, 408835 -> 65536; INT64_MIN -> -65536; margin raw 144246;
  oddness on every point.
* **ln / log10 / log2_exact**: raw <= 0 -> INT64_MIN; 1, 65536 (ln 1 = 0 exactly),
  2^k raws (log2 exactly k - 16), 655360 for log10 (exactly 1); the margin raws (541866,
  967694, 692513); the two fixLog2 witnesses of 2.2 for log2_exact (and a check that
  `fixLog2` still returns its frozen values there); INT64_MAX.
* **tan**: 0, +/-1, the three points of 3.1 (195734331562903 -> INT64_MAX,
  130146176612260 -> -8558550789933375681, 6173953965003744831 -> INT64_MAX), raw 1750043, the
  margin raw 557409, INT64_MIN (odd, and saturation sign), raws on both sides of pi/4
  (the j = 101 cell) and of 1/256 (the path switch). A unit test that the four 2/pi words and
  the first entries of each table match `tan_kernel.out`. Negative control: W2 and W3
  zeroed (112 bits, the sincos reduction): it must fail at 130146176612260.

Every card also runs the whole existing suite unchanged on both arms and both compilers and
quotes the four sweep hashes.

## 6. The build cards' specs, in one place

| card | functions (new header) | method | constants and tables | budget, margin |
|---|---|---|---|---|
| fixed-exp-exact | `fixExpExact` (fixed_exp.h); `src/exact/exp_kernel.h` | 1.1, 1.2 | LN2_128, INV_LN2_64, T_j (128) from exp_kernel.out; C_2..C_8 | 2^-39.67 vs 2^-21.15 |
| fixed-sinh-cosh-exact | `fixSinhExact`, `fixCoshExact` (fixed_sinh_cosh.h) | 1.3, includes exp_kernel.h | none of its own (identity cut-off 2344, saturation 2180453) | 2^-39.09 vs 2^-32.75 / 2^-20.58 |
| fixed-tanh-exact | `fixTanhExact` (fixed_tanh.h) | 1.4, includes exp_kernel.h | none (cut-offs 1860, 408835) | 2^-86.67 vs 2^-20.28 |
| fixed-log-exact | `fixLnExact`, `fixLog10Exact`, `fixLog2Exact` (fixed_log.h) | 2.1-2.3 | INV_i, L_i (128), LN2_128, LOG2E_127, LOG10E_128; C_2..C_10 | ln 2^-85.04, log2 2^-84.5, log10 2^-86.2; margins 2^-21.44 / 2^-20.50 / 2^-26.00 and BaCSeL k <= 84 |
| fixed-tan-exact | `fixTanExact` (fixed_tan.h) | 3.1-3.4 | W0..W3, PIO2_127, TAN_j (102); C_3..C_13 | 2^-61 (pole) / 2^-91.6 vs 2^-22.24 and k <= 61 |

Each card's own `sollya/<fn>.sollya` is its kernel script: for exp, log and tan it can be the
matching `*_kernel.sollya` here (same fpminimax and supnorm), so the coefficients pasted in
its source are the ones this budget certifies; sinh_cosh and tanh have no polynomial of their
own (their script prints the cut-offs' checks or restates the exp kernel's certificate).

Where this design differs from a card's text, the design is the method and the card text
yields: (1) sinh and tanh need no odd/small-|x| polynomial, only the exact identity cut-off
(1.3, 1.4); (2) `fixLog2Exact` is required, with witnesses (2.2); (3) the tan kernel is fit
and certified in the relative error, with the absolute bound printed too (3.3); (4) log10's
constant carries 128 bits, not 64 (2.3).

Not designed here, each carded on its own: `fixed-asin-acos-exact`, `fixed-rsqrt-exact`,
`fixed-mod-exact`, `fixed-cbrt-exact`.

## 7. Peer check: FixPointCS

FixPointCS (MIT, github.com/XMunkki/FixPointCS, read at a852f05) covers Exp, Exp2, Log, Log2,
Pow, Sqrt, RSqrt, Rcp, Sin, Cos, Tan, Asin, Acos, Atan, Atan2 and Mod in S32.32, each in three
precisions (24, 16 and 10 relative bits), with polynomials fitted by its own Remez script. Its
structure is the usual one and matches ours where it overlaps: normalise to [1, 2) and fit a
polynomial for Log/Log2; Exp as Exp2 of `x * (1/ln 2)`; Tan as sin/cos of a unit-angle
reduction in 32 bits. What this design does that it does not: tables (Tang) so the
polynomials stay degree 8-13 at 100+ bits; ln 2 and 2/pi to 128 and 240 bits; correct rounding
proved by budget plus exhaustive sweep plus hard cases, where FixPointCS states relative
precision measured on random inputs. Its exp via exp2(x/ln 2) double-rounds, which is why
`fixExpExact` does not go through `fixExp2Exact`. Coverage it has that this family does not
design: Rcp, Pow, Atan and Mod (the latter carded separately); coverage we add: sinh, cosh,
tanh, log10, and saturation defined exactly at every edge.

## 8. Regenerating every number

    sollya sollya/exp_kernel.sollya > sollya/exp_kernel.out
    sollya sollya/log_kernel.sollya > sollya/log_kernel.out
    sollya sollya/tan_kernel.sollya > sollya/tan_kernel.out
    cc -O2 tools/tan_reduction_bound.c -lmpfr -lgmp -o tan_reduction_bound && ./tan_reduction_bound

The margins and thresholds of section 0 and the fixLog2 measurements of 2.2 were taken with a
throwaway MPFR program (256 bits, linked against libfixed at BASE 4ad7979) that is not
committed: the function cards' exhaustive sweep pieces measure the same facts and commit
them to `docs/scoreboard/`.
