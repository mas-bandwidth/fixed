# kernel.g
#
# Gappa proof of the log1p kernel evaluation error shared by fixLnExact,
# fixLog2Exact and fixLog10Exact (src/exact/log.c, src/exact/log_kernel.h,
# docs/design/transcendentals.md 2.1, sollya/log_kernel.out).
#
# METHOD. On the table-reduced interval u in [0, 2^-7 + 2^-16] the kernel
# evaluates the stored-coefficient polynomial
#
#     p(u) = u + sum_{k=2..10} C_k * 2^-127 * u^k,
#
# with C_k the Sollya fpminimax coefficients (C_k < 0 for even k and
# C_k > 0 for odd k). Horner runs on the coefficient magnitudes
# a_k = |C_k| * 2^-127, in Q0.127. mulhi(a, b) = (a*b) >> 128 on unsigned
# integers is a floor, so each product is the fixed rounding operator
# fixed<-k, dn> (downward rounding to a multiple of 2^-k) of the exact
# product of its operands:
#
#     w_10 = a_10 (the constant itself)
#     t_k  = fixed<-127, dn>(w_{k+1} * u)   w_k = a_k - t_k      k = 9..2
#     qq   = fixed<-128, dn>(u * u)         Q0.128, mulhi(u128, u128)
#     vv   = fixed<-127, dn>(qq * w_2)      Q0.127, mulhi(u2, w)
#     comp = u - vv                         u128 - (kw << 1)
#
# The realignment (kw << 1) in the source is an exact left shift that moves
# the Q0.127 product onto the Q0.128 scale of u, so the computed value is
# u - vv as a real number.
#
# The exact polynomial is written out beside it, as Horner in exact
# arithmetic on the same coefficients:
#
#     H_10 = a_10,   H_k = a_k - H_{k+1} * u,   p = u - u^2 * H_2.
#
# Gappa proves the enclosure of E = comp - p from these two chains, with
# no hint and no hand-derived error formula: its own rewriting splits each
# difference w_k - H_k into the rounding error of that step (fixed<>(x) - x,
# which it bounds from the operator) and the difference carried from the
# step before.
#
# The polynomial-versus-log1p error is a certified hypothesis from
# sollya/log_kernel.out (the fpminimax supnorm, coefficient quantisation
# included): with L the exact log1p(u), |p - L| <= h, the upper endpoint of
# the printed interval,
#
#     h = 3.837335479148857891302978686748981565437784801207944958327721352762597264308934632253007019224816838110169786977841947941e-31
#
# (log2 of the certified bound: -101.03966522216796875). The total kernel
# error comp - L is then proved, and the per-function bounds below carry
# the design's conversion to raw units (docs/design/transcendentals.md
# 2.1-2.3):
#
#   ln     : |comp - L| * 2^16
#   log2   : |comp - L| * log2(e) * 2^16 + 2^-104
#   log10  : ((|comp - L| + 2^-118) * log10(e) + 33*2^-129 + 2^-120) * 2^16
#            (2^-118 covers the ln sum's table and shift truncations)
#
# log2(e) and log10(e) enter as the upper bounds 1.4427 and 0.4343. The
# design budgets are 2^-85.039, 2^-84.5 and 2^-86.2 raw respectively;
# results.md compares each proved bound with its budget.

@rd127 = fixed<-127, dn>;
@rd128 = fixed<-128, dn>;

# Exact stored coefficient magnitudes a_k = |C_k| / 2^127.
a2  = 85070591730234615865841526850016844886/170141183460469231731687303715884105728;
a3  = 56713727820156410570044514409808717995/170141183460469231731687303715884105728;
a4  = 42535295865117298470636450618141527806/170141183460469231731687303715884105728;
a5  = 34028236692087275764462364288807969266/170141183460469231731687303715884105728;
a6  = 28356863907371577920262375446842104005/170141183460469231731687303715884105728;
a7  = 24305882655336387207562327824050622546/170141183460469231731687303715884105728;
a8  = 21267534873952615706011558485088099485/170141183460469231731687303715884105728;
a9  = 18893294931513468741888555143192483885/170141183460469231731687303715884105728;
a10 = 16381085491173456875485386199713526109/170141183460469231731687303715884105728;

# Computed: the Horner chain in Q0.127, each product truncated to its format.
t9 = rd127(a10 * u);  w9 = a9 - t9;
t8 = rd127(w9 * u);   w8 = a8 - t8;
t7 = rd127(w8 * u);   w7 = a7 - t7;
t6 = rd127(w7 * u);   w6 = a6 - t6;
t5 = rd127(w6 * u);   w5 = a5 - t5;
t4 = rd127(w5 * u);   w4 = a4 - t4;
t3 = rd127(w4 * u);   w3 = a3 - t3;
t2 = rd127(w3 * u);   w2 = a2 - t2;

# Computed: final square and product, each truncated to its format.
qq = rd128(u * u);
vv = rd127(qq * w2);
comp = u - vv;

# Exact: the same polynomial in exact arithmetic.
H9 = a9 - a10 * u;
H8 = a8 - H9 * u;
H7 = a7 - H8 * u;
H6 = a6 - H7 * u;
H5 = a5 - H6 * u;
H4 = a4 - H5 * u;
H3 = a3 - H4 * u;
H2 = a2 - H3 * u;
p = u - u * u * H2;

# Errors, all against the computed value.
E = comp - p;
K = comp - L;

# Per-function totals in raw units.
ln_raw = |K| * 65536;
log2_raw = |K| * 1.4427 * 65536 + 1b-104;
log10_raw = ((|K| + 1b-118) * 0.4343 + 33 * 1b-129 + 1b-120) * 65536;
# ln with the ln sum's table and shift terms (design 2.1: 2^-118.4 < 2^-118).
ln_full_raw = (|K| + 1b-118) * 65536;

{
  # Reduced input, the whole kernel interval of sollya/log_kernel.out.
  u in [0, 0.0078277587890625]
  # Hypothesis: Sollya's certified supnorm h of the polynomial against log1p,
  # |p - L| <= h, h the upper endpoint printed in sollya/log_kernel.out.
  /\ p - L in [-3.837335479148857891302978686748981565437784801207944958327721352762597264308934632253007019224816838110169786977841947941e-31, 3.837335479148857891302978686748981565437784801207944958327721352762597264308934632253007019224816838110169786977841947941e-31]

  ->
  E in ?
  /\ K in ?
  /\ ln_raw in ?
  /\ log2_raw in ?
  /\ log10_raw in ?
  /\ ln_full_raw in ?
  # Budget goals: ln 2^-85.039, log2 2^-84.5, log10 2^-86.2 (decimals rounded
  # down, so the goals are no weaker than the budgets); ln also keeps 2^-85,
  # the bound the hard-case argument uses.
  /\ |E| <= 7.35e-39
  /\ log2_raw <= 3.6556563775e-26
  /\ log10_raw <= 1.1251602315e-26
  /\ ln_full_raw <= 2.5159974622e-26
  /\ ln_full_raw <= 1b-85
}
