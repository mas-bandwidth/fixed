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
# a_k = |C_k| * 2^-127, in Q0.127:
#
#     w_10 = a_10
#     w_k  = a_k - fixed<-127, dn>(w_{k+1} * u)      k = 9..2
#     qq   = fixed<-128, dn>(u*u)                    Q0.128
#     vv   = fixed<-127, dn>(qq * w_2)               Q0.127
#     comp = u - vv
#
# The realignment (vv << 1) in the source only moves the Q0.127 factor w_2
# back onto the Q0.128 scale of u; in the real-valued model below the final
# value is u - vv.
#
# fixed<-k, dn> is downward rounding to a multiple of 2^-k, i.e. the
# truncating shift the source uses for positive operands. Every rounding
# residue is non-negative:
#
#     rho_k = w_{k+1}*u - fixed<-127,dn>(w_{k+1}*u)  in [0, 2^-127)
#     rho_q = u*u       - qq                         in [0, 2^-128)
#     rho_v = qq*w_2    - vv                         in [0, 2^-127)
#
# With H_10 = a_10 and H_k = a_k - u*H_{k+1} the exact Horner value
# (H_2 = q(u), p(u) = u - u^2 q(u)), the local errors d_k = w_k - H_k obey
#
#     d_10 = 0,   d_k = -u*d_{k+1} + rho_k.
#
# The kernel evaluation error against the stored-coefficient polynomial is
# then
#
#     E = comp - p(u) = rho_q*w_2 + rho_v - u^2*d_2.
#
# The polynomial-versus-log1p error is a certified hypothesis from
# sollya/log_kernel.out (the fpminimax supnorm, coefficient quantisation
# included): |p(u) - log1p(u)| <= h, with the printed interval's upper
# endpoint
#
#     h = 3.837335479148857891302978686748981565437784801207944958327721352762597264308934632253007019224816838110169786977841947941e-31
#
# (log2 of the certified bound: -101.03966522216796875). So the total kernel
# error is |comp - log1p(u)| <= |E| + h, and the per-function bounds below
# carry the design's conversion to raw units (docs/design/transcendentals.md
# 2.1-2.3):
#
#   ln     : (|E| + h) * 2^16
#   log2   : (|E| + h) * log2(e) * 2^16 + 2^-104
#   log10  : ((|E| + h) * log10(e) + 33*2^-129 + 2^-120) * 2^16
#
# The design budgets are 2^-85.04, 2^-84.5 and 2^-86.2 raw respectively;
# results.md compares each proved bound with its budget.

{
  # Exact stored coefficient magnitudes a_k = |C_k| / 2^127.
  a2  = 85070591730234615865841526850016844886/170141183460469231731687303715884105728
  /\ a3  = 56713727820156410570044514409808717995/170141183460469231731687303715884105728
  /\ a4  = 42535295865117298470636450618141527806/170141183460469231731687303715884105728
  /\ a5  = 34028236692087275764462364288807969266/170141183460469231731687303715884105728
  /\ a6  = 28356863907371577920262375446842104005/170141183460469231731687303715884105728
  /\ a7  = 24305882655336387207562327824050622546/170141183460469231731687303715884105728
  /\ a8  = 21267534873952615706011558485088099485/170141183460469231731687303715884105728
  /\ a9  = 18893294931513468741888555143192483885/170141183460469231731687303715884105728
  /\ a10 = 16381085491173456875485386199713526109/170141183460469231731687303715884105728

  # Reduced input, the whole kernel interval of sollya/log_kernel.out.
  /\ u in [0, 0.0078277587890625]

  # Horner chain in Q0.127, each product truncated to its format.
  /\ w10 = a10
  /\ w9 = a9 - fixed<-127, dn>(w10 * u)
  /\ w8 = a8 - fixed<-127, dn>(w9 * u)
  /\ w7 = a7 - fixed<-127, dn>(w8 * u)
  /\ w6 = a6 - fixed<-127, dn>(w7 * u)
  /\ w5 = a5 - fixed<-127, dn>(w6 * u)
  /\ w4 = a4 - fixed<-127, dn>(w5 * u)
  /\ w3 = a3 - fixed<-127, dn>(w4 * u)
  /\ w2 = a2 - fixed<-127, dn>(w3 * u)

  # Final square and product, each truncated to its format.
  /\ qq = fixed<-128, dn>(u * u)
  /\ vv = fixed<-127, dn>(qq * w2)
  /\ comp = u - vv

  # Truncation residues of each fixed rounding.
  /\ rho9 in [0, 5.8774717541114375e-39]
  /\ rho8 in [0, 5.8774717541114375e-39]
  /\ rho7 in [0, 5.8774717541114375e-39]
  /\ rho6 in [0, 5.8774717541114375e-39]
  /\ rho5 in [0, 5.8774717541114375e-39]
  /\ rho4 in [0, 5.8774717541114375e-39]
  /\ rho3 in [0, 5.8774717541114375e-39]
  /\ rho2 in [0, 5.8774717541114375e-39]
  /\ rho_q in [0, 2.9387358770557188e-39]
  /\ rho_v in [0, 5.8774717541114375e-39]

  # Local error recurrence d_k = w_k - H_k.
  /\ d10 = 0
  /\ d9 = -u * d10 + rho9
  /\ d8 = -u * d9  + rho8
  /\ d7 = -u * d8  + rho7
  /\ d6 = -u * d7  + rho6
  /\ d5 = -u * d6  + rho5
  /\ d4 = -u * d5  + rho4
  /\ d3 = -u * d4  + rho3
  /\ d2 = -u * d3  + rho2

  /\ E = rho_q * w2 + rho_v - u * u * d2

  # Polynomial-versus-log1p supnorm, certified by sollya/log_kernel.out.
  /\ h = 3.837335479148857891302978686748981565437784801207944958327721352762597264308934632253007019224816838110169786977841947941e-31

  # Per-function totals in raw units.
  /\ ln_raw = (h + |E|) * 65536
  /\ log2_raw = (h + |E|) * 1.4427 * 65536 + 4.930380657631324e-32
  /\ log10_raw = ((h + |E|) * 0.4343 + 33 * 1.469367938527860e-39 + 7.523163845262641e-37) * 65536

  ->
  |E| <= 7.35e-39
  /\ ln_raw <= 2.5149e-26
  /\ log2_raw <= 3.6282e-26
  /\ log10_raw <= 1.0922e-26
}
