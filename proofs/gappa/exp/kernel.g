# kernel.g
#
# Gappa proof of the evaluation error of the shared exp kernel fixExpKernel
# (src/exact/exp_kernel.h) in its integer fixed formats, and of the budget the
# exp family (fixExpExact, fixSinhExact/fixCoshExact, fixTanhExact) inherits from
# it (docs/design/transcendentals.md section 1).
#
# METHOD (src/exact/exp_kernel.h, docs/design/transcendentals.md 1.1/1.2, sollya/
# exp_kernel.out). On the reduced interval r in [0, ln2/128] the kernel evaluates
#
#     P(r) = 1 + r + sum_{k=2..8} C_k * 2^-127 * r^k,
#
# with C_k stored as unsigned integers with 127 fraction bits (c_k = C_k/2^-127),
# and T = T_j/2^126 = 2^(j/128) in [1, 2). The source's Horner is
#
#     q = C_8; for k = 7..2: q = C_k + mulhi(q, r128)   # Q.127
#     s  = 2^127 + mulhi(q, r128)                       # 1 + q*r
#     Pv = 2^127 + mulhi(s, r128)                       # 1 + s*r
#     M  = (T_j * Pv) >> 127                            # Q2.126 mantissa
#
# where mulhi(a,b) = (a*b) >> 128 is a truncating high multiply. In real units
# (q/2^127, r128/2^128, M/2^126) every mulhi is fixed<-127, dn> except the last,
# which is fixed<-126, dn>; dn rounds down to a multiple of 2^-k and loses less
# than one unit of that format (2^-127, and 2^-126 for M). All quantities are
# positive, so the truncations are downward.
#
# The residues rho_k are defined as the exact difference between the unrounded
# product and the fixed-operator result, so they are not free variables: Gappa
# proves from the fixed operator that each lies in [0, 2^-k>. The Horner error
# then decouples from the coefficients and obeys the backward recurrence
#
#     e_8 = 0,  e_k = e_{k+2}*r - rho_k,  e_s = e_2*r - rho_s,
#     e_P = e_s*r - rho_P,  E = T*e_P - rho_M,
#
# where E = M - T*P(r) is the evaluation error against the exact stored-coefficient
# polynomial (checked by expanding the definitions; the recurrence is the Horner
# form of that difference). The polynomial-versus-function error is supplied as a
# certified hypothesis from sollya/exp_kernel.out (the supnorm certificate covers
# the whole reduced interval, coefficient quantisation included):
#
#     |P(r) - e^r| <= 1.236209838752694113105510039539433637113639803289646403008807013447042878301419192191865209492533254646400253656704902499e-31
#                  = 2^-102.6738433837890625.
#
# So |M - T*e^r| <= |E| + T*|P(r) - e^r| <= 2.4725e-31 (about 2^-101.67), the
# kernel's budget line on M in docs/design/transcendentals.md 1.1.
#
# FAMILY BUDGETS. The kernel bound dM <= 2.4725e-31 is the result proved in the
# first block; the remaining goals are the per-function translations that section 1
# of the design quotes (they reuse the proved bound as a hypothesis, since Gappa has
# no power operator for the variable exponents and the scaling is done by the
# worst-case powers below):
#
#   exp        m <= 46, so raw error <= 2^(m+16)*dM <= 2^62*dM <= 2^-39.67.
#   sinh/cosh  y_raw = 2^(ma+15)*(Ma -/+ Mb*2^-d). If ma >= 1 then d = ma - mb >= 2
#              (the two kernel calls use N and -N, so mb = -ceil(N/128) <= -1), so
#              the Mb term is damped by 1/4 and ma <= 47 gives 2^(ma+15) <= 2^62.
#              If ma = 0 the scaling is 2^15 and no damping is needed. Both cases
#              are below the design's 2^-39.09 raw exp-path budget.
#   tanh       d/dM of (M-a)/(M+a) is at most 1/2, so the raw error is at most
#              2^15*dM plus the 2^-128 quotient truncation (2^-112 raw), below the
#              design's 2^-86.67 raw budget.
#
# All bounds are absolute errors on the pre-final-rounding value (the design's eps);
# the final round to nearest is a separate, exact half-up shift.
{
  # Exact stored coefficients C_k / 2^127 (sollya/exp_kernel.out).
  c2 = 85070591730234615865844285925257417007/170141183460469231731687303715884105728
  /\ c3 = 28356863910078205286577426134019248193/170141183460469231731687303715884105728
  /\ c4 = 7089215977519553812536390431947563211/170141183460469231731687303715884105728
  /\ c5 = 1417843195502364686515137541331572158/170141183460469231731687303715884105728
  /\ c6 = 236307199786718878054036503198843823/170141183460469231731687303715884105728
  /\ c7 = 33758066009536759454161585696416255/170141183460469231731687303715884105728
  /\ c8 = 4230727913899129129119028149777696/170141183460469231731687303715884105728

  # Reduced input r = r128/2^128 in [0, ln2/128] (2^-7.53), and T_j/2^126 in [1,2).
  /\ r in [0, 0.0054153]
  /\ T in [1, 2]

  # Kernel intermediates exactly as src/exact/exp_kernel.h computes them.
  /\ q8 = c8
  /\ q7 = c7 + fixed<-127, dn>(q8 * r)
  /\ q6 = c6 + fixed<-127, dn>(q7 * r)
  /\ q5 = c5 + fixed<-127, dn>(q6 * r)
  /\ q4 = c4 + fixed<-127, dn>(q5 * r)
  /\ q3 = c3 + fixed<-127, dn>(q4 * r)
  /\ q2 = c2 + fixed<-127, dn>(q3 * r)
  /\ s = 1 + fixed<-127, dn>(q2 * r)
  /\ Pv = 1 + fixed<-127, dn>(s * r)
  /\ M = fixed<-126, dn>(T * Pv)

  # Truncation residues, exactly the loss of each fixed operator.
  /\ rho7 = c7 + q8*r - q7
  /\ rho6 = c6 + q7*r - q6
  /\ rho5 = c5 + q6*r - q5
  /\ rho4 = c4 + q5*r - q4
  /\ rho3 = c3 + q4*r - q3
  /\ rho2 = c2 + q3*r - q2
  /\ rho_s = 1 + q2*r - s
  /\ rho_P = 1 + s*r - Pv
  /\ rho_M = T*Pv - M

  # Horner error against the exact stored-coefficient polynomial.
  /\ e8 = 0
  /\ e7 = e8*r - rho7
  /\ e6 = e7*r - rho6
  /\ e5 = e6*r - rho5
  /\ e4 = e5*r - rho4
  /\ e3 = e4*r - rho3
  /\ e2 = e3*r - rho2
  /\ es = e2*r - rho_s
  /\ ep = es*r - rho_P
  /\ E = T*ep - rho_M
  /\ P = 1 + r + c2*r*r + c3*r*r*r + c4*r*r*r*r + c5*r*r*r*r*r
        + c6*r*r*r*r*r*r + c7*r*r*r*r*r*r*r + c8*r*r*r*r*r*r*r*r

  # Per-function scalings. dM is the proved kernel bound |M - T*e^r| <= 2.4725e-31.
  /\ dM in [0, 2.4725e-31]
  /\ dMa in [0, 2.4725e-31]
  /\ dMb in [0, 2.4725e-31]
  /\ Yexp = dM * 4611686018427387904
  /\ Ysinh1 = 4611686018427387904 * (dMa + dMb/4 + 1.1754943508222875e-38)
  /\ Ysinh0 = 32768 * (dMa + dMb + 1.1754943508222875e-38)
  /\ Ytanh = 32768*dM + 1.92592994438723585e-34
  ->
  # Evaluation error against the exact stored-coefficient polynomial.
  |E| <= 2.4e-38
  # Total kernel error against the function (Sollya supnorm is the hypothesis).
  /\ |E| + T*1.236209838752694113105510039539433637113639803289646403008807013447042878301419192191865209492533254646400253656704902499e-31 <= 2.4725e-31
  # exp: eps <= 2^62 * dM <= 2^-39.67 raw (design budget 2^-39.67).
  /\ Yexp <= 1.143247004185311e-12
  # sinh/cosh, ma >= 1 and ma = 0: eps <= 2^-39.09 raw (design budget 2^-39.09).
  /\ Ysinh1 <= 1.7089819252105546e-12
  /\ Ysinh0 <= 1.7089819252105546e-12
  # tanh: eps <= 2^16*(dM/2 + 2^-128) <= 2^-86.67 raw (design budget 2^-86.67).
  /\ Ytanh <= 8.123258540033696e-27
}
