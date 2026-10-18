# kernel.g
#
# Gappa proof of the kernel evaluation error of fixTanKernel
# (src/exact/tan_kernel.h) and of the pole-path Horner fixTanExactPoleW
# (src/exact/tan.c), in their integer fixed formats.
#
# METHOD (src/exact/tan_kernel.h, docs/design/transcendentals.md 3.3, sollya/
# tan_kernel.out). On the reduced interval |s| <= 2^-8 + 2^-100 the kernel is the
# odd degree-13 polynomial
#
#     tan(s) = s + sum_{k=3,5,..,13} C_k * 2^-127 * s^k,
#
# with the s coefficient exactly 1 and C_k = round(C_k * 2^127) stored as unsigned
# 127-fraction-bit integers (Q0.127). Evaluation by Horner in s^2, all products the
# high half of a 128x128 product, mulhi(a,b) = (a*b) >> 128, a truncation:
#
#     s2 = fixed<-128, dn>(x*x)                    # Q0.128, truncating shift
#     w  = C_13; for k = 11..3: w = C_k + fixed<-127, dn>(w*s2)   # Q0.127
#     c  = fixed<-127, dn>(s2*w)                   # Q0.127
#     t  = x + fixed<-127, dn>(x*c)                # x*c Q0.127; the (<<1) in the
#                                                  # source only realigns Q0.127 to
#                                                  # Q0.128, one exact left shift
#
# The coefficients are added exactly (they already sit on the 2^-127 grid). Every
# product truncates by less than one unit of its format, i.e. the fixed rounding
# operator fixed<-k, dn> (downward rounding to a multiple of 2^-k). Because the
# truncations are the only source of error, the evaluation error against the exact
# polynomial decouples from the coefficients: write each Horner step as
#
#     v_k = C_k + fixed<-127, dn>(v_{k+2} * s2),
#
# and let q(s2) = C_3 + C_5 s2 + ... be the exact polynomial in s2. Then, with
# e_5 = 0 and e_k = e_{k+2} * s2 - rho_k, where rho_k in [0, 2^-127) is the fixed
# rounding residue of that step, the accumulated error is
#
#     e_0 = v_0 - q(s2) = -(rho_0 + rho_1*s2 + rho_2*s2^2 + rho_3*s2^3 + rho_4*s2^4).
#
# The final products truncate the same way: c = s2*v_0 - rho_c and
# t = x + x*c - rho_t. The total kernel evaluation error against the exact stored-
# coefficient polynomial p(x) = x + x^3 q(x^2) is therefore
#
#     E = t - p(x) = x*(s2*q(s2) - x^2*q(x^2)) + x*s2*e_0 - x*rho_c - rho_t.
#
# The first term is the replacement of s2 by x^2 (the x^2 truncation); with
# delta = s2 - x^2 in [-2^-128, 0] and q(s2) - q(x^2) = delta * B it equals
# x*delta*(q(s2) + x^2*B). The pole path (fixTanExactPoleW) computes w = r^2 q(r^2)
# by the same Horner on |r| < 1/256, so w - r^2 q(r^2) = delta_r*(q(s2r) + rq^2*B_r)
# + s2r*e_0r - rho_cr.
#
# The polynomial-versus-function error is supplied as a certified hypothesis from
# sollya/tan_kernel.out (supnorm of the quantised polynomial against tan over the
# whole kernel interval, coefficient quantisation included):
#
#     |p(x) - tan(x)|      <= 8.460112648223753998e-44    (absolute, log2 -143.084)
#     |p(x)/tan(x) - 1|    <= 9.100984361674279281e-41    (relative, log2 -133.013)
#
# so the total kernel error is bounded by that hypothesis plus the proved E. The
# table-path design budget line (docs/design/transcendentals.md 3.4) for the kernel
# is 2^-125 absolute on tan r; the pole-path "Horner for w" line is 2^-126 absolute
# on 1 + w. Bounds below are written as decimals (Gappa 1.4.1 has no power
# operator): 2^-125 = 2.3509887016445750e-38, 2^-126 = 1.1754943508222875e-38,
# 2^-127 = 5.8774717541114375e-39, 2^-128 = 2.9387358770557188e-39.
{
  # Exact stored coefficients C_k / 2^127 (sollya/tan_kernel.out).
  c3  = 56713727820156410577229101238627985601/170141183460469231731687303715884105728
  /\ c5 = 22685491128062564230891640546445447242/170141183460469231731687303715884105728
  /\ c7 = 9182222599453895045818339859637447938/170141183460469231731687303715884105728
  /\ c9 = 3720900661216614364217111164977437442/170141183460469231731687303715884105728
  /\ c11 = 1508001382045491119827481516662477816/170141183460469231731687303715884105728
  /\ c13 = 611182642321922348734831470419262855/170141183460469231731687303715884105728
  /\ h_abs = 8.460112648223753998e-44

  # ---------------------------------------------------------------- table path
  # x = |s| in Q0.128 over [0, 2^-8 + 2^-100] = [0, 0.0039062500000000007886].
  # s2, v5..v0, c and t are the kernel's fixed-operator intermediates exactly as
  # the source computes them; the residues rho_* are their truncation losses.
  /\ x in [0, 0.0039062500000000007886]
  /\ s2 = fixed<-128, dn>(x*x)
  /\ v5 = c13
  /\ v4 = c11 + fixed<-127, dn>(v5*s2)
  /\ v3 = c9  + fixed<-127, dn>(v4*s2)
  /\ v2 = c7  + fixed<-127, dn>(v3*s2)
  /\ v1 = c5  + fixed<-127, dn>(v2*s2)
  /\ v0 = c3  + fixed<-127, dn>(v1*s2)
  /\ c = fixed<-127, dn>(s2*v0)
  /\ t = x + fixed<-127, dn>(x*c)
  /\ rho4 in [0, 5.8774717541114375e-39]
  /\ rho3 in [0, 5.8774717541114375e-39]
  /\ rho2 in [0, 5.8774717541114375e-39]
  /\ rho1 in [0, 5.8774717541114375e-39]
  /\ rho0 in [0, 5.8774717541114375e-39]
  /\ e5 = 0
  /\ e4 = e5*s2 - rho4
  /\ e3 = e4*s2 - rho3
  /\ e2 = e3*s2 - rho2
  /\ e1 = e2*s2 - rho1
  /\ e0 = e1*s2 - rho0
  /\ rho_c in [0, 5.8774717541114375e-39]
  /\ rho_t in [0, 5.8774717541114375e-39]
  /\ q_s2 = c3 + c5*s2 + c7*s2*s2 + c9*s2*s2*s2 + c11*s2*s2*s2*s2
          + c13*s2*s2*s2*s2*s2
  /\ B = c5 + c7*(s2+x*x)
        + c9*(s2*s2+s2*x*x+x*x*x*x)
        + c11*(s2*s2*s2+s2*s2*x*x+s2*x*x*x*x+x*x*x*x*x*x)
        + c13*(s2*s2*s2*s2+s2*s2*s2*x*x+s2*s2*x*x*x*x+s2*x*x*x*x*x*x
          + x*x*x*x*x*x*x*x)
  /\ delta = s2 - x*x
  /\ Dq = delta*(q_s2 + x*x*B)
  /\ E = x*Dq + x*s2*e0 - x*rho_c - rho_t

  # ------------------------------------------------------------------ pole path
  # rq = |r| in Q0.128 over [0, 2^-8] (|r| < 1/256); w = r^2 q(r^2).
  /\ rq in [0, 0.00390625]
  /\ s2r = fixed<-128, dn>(rq*rq)
  /\ v5r = c13
  /\ v4r = c11 + fixed<-127, dn>(v5r*s2r)
  /\ v3r = c9  + fixed<-127, dn>(v4r*s2r)
  /\ v2r = c7  + fixed<-127, dn>(v3r*s2r)
  /\ v1r = c5  + fixed<-127, dn>(v2r*s2r)
  /\ v0r = c3  + fixed<-127, dn>(v1r*s2r)
  /\ wr = fixed<-127, dn>(s2r*v0r)
  /\ rho4r in [0, 5.8774717541114375e-39]
  /\ rho3r in [0, 5.8774717541114375e-39]
  /\ rho2r in [0, 5.8774717541114375e-39]
  /\ rho1r in [0, 5.8774717541114375e-39]
  /\ rho0r in [0, 5.8774717541114375e-39]
  /\ e5r = 0
  /\ e4r = e5r*s2r - rho4r
  /\ e3r = e4r*s2r - rho3r
  /\ e2r = e3r*s2r - rho2r
  /\ e1r = e2r*s2r - rho1r
  /\ e0r = e1r*s2r - rho0r
  /\ rho_cr in [0, 5.8774717541114375e-39]
  /\ q_s2r = c3 + c5*s2r + c7*s2r*s2r + c9*s2r*s2r*s2r + c11*s2r*s2r*s2r*s2r
           + c13*s2r*s2r*s2r*s2r*s2r
  /\ Br = c5 + c7*(s2r+rq*rq)
        + c9*(s2r*s2r+s2r*rq*rq+rq*rq*rq*rq)
        + c11*(s2r*s2r*s2r+s2r*s2r*rq*rq+s2r*rq*rq*rq*rq+rq*rq*rq*rq*rq*rq)
        + c13*(s2r*s2r*s2r*s2r+s2r*s2r*s2r*rq*rq+s2r*s2r*rq*rq*rq*rq
          + s2r*rq*rq*rq*rq*rq*rq+rq*rq*rq*rq*rq*rq*rq*rq)
  /\ deltar = s2r - rq*rq
  /\ Dqr = deltar*(q_s2r + rq*rq*Br)
  /\ Er = Dqr + s2r*e0r - rho_cr
  /\ W = rq*rq*(c3 + c5*rq*rq + c7*rq*rq*rq*rq + c9*rq*rq*rq*rq*rq*rq
        + c11*rq*rq*rq*rq*rq*rq*rq*rq + c13*rq*rq*rq*rq*rq*rq*rq*rq*rq*rq)

  ->
  |E| <= 7.0e-39
  /\ |E| + h_abs <= 2.3509887016445750e-38
  /\ |Er| <= 7.0e-39
  /\ |Er| / (1 + W) <= 7.0e-39
}
