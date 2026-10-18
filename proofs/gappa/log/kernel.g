# Gappa proof of the log1p kernel's accumulated fixed-point error.
# Values are real-valued scaled integers: a_k are stored Q.127 coefficient
# magnitudes divided by 2^127; x is u128 / 2^128.
#
# This recurrence matches src/exact/log_kernel.h:
#   w10 = a10
#   wk = ak - fixed<127,dn>(w(k+1) * x), k = 9..2
#   qq = fixed<128,dn>(x * x)
#   vv = fixed<127,dn>(qq * w2)
#   out = x - 2*vv
# Exact polynomial Horner omits fixed<k,dn>. Define each local residue as
# rounded-minus-unrounded. The difference d_k = wk - p_k obeys
# d_k = -x*d(k+1) - rk. The final difference uses rq=qq-x^2 and rv=vv-qq*w2.
# The Sollya polynomial-versus-log1p supnorm h is a hypothesis.

{
  x in [0, 0.0078125]
  /\ a2 in [0.499, 0.5]
  /\ a3 in [0.333, 0.334]
  /\ a4 in [0.249, 0.251]
  /\ a5 in [0.199, 0.201]
  /\ a6 in [0.166, 0.168]
  /\ a7 in [0.142, 0.144]
  /\ a8 in [0.124, 0.126]
  /\ a9 in [0.11, 0.112]
  /\ a10 in [0.096, 0.097]

  /\ w10 = a10
  /\ w9 = a9 - fixed<127, dn>(w10 * x)
  /\ w8 = a8 - fixed<127, dn>(w9 * x)
  /\ w7 = a7 - fixed<127, dn>(w8 * x)
  /\ w6 = a6 - fixed<127, dn>(w7 * x)
  /\ w5 = a5 - fixed<127, dn>(w6 * x)
  /\ w4 = a4 - fixed<127, dn>(w5 * x)
  /\ w3 = a3 - fixed<127, dn>(w4 * x)
  /\ w2 = a2 - fixed<127, dn>(w3 * x)
  /\ qq = fixed<128, dn>(x * x)
  /\ vv = fixed<127, dn>(qq * w2)
  /\ computed = x - 2 * vv

  /\ r9 = fixed<127, dn>(w10 * x) - w10 * x
  /\ r8 = fixed<127, dn>(w9 * x) - w9 * x
  /\ r7 = fixed<127, dn>(w8 * x) - w8 * x
  /\ r6 = fixed<127, dn>(w7 * x) - w7 * x
  /\ r5 = fixed<127, dn>(w6 * x) - w6 * x
  /\ r4 = fixed<127, dn>(w5 * x) - w5 * x
  /\ r3 = fixed<127, dn>(w4 * x) - w4 * x
  /\ r2 = fixed<127, dn>(w3 * x) - w3 * x
  /\ rq = qq - x * x
  /\ rv = vv - qq * w2

  /\ d10 = 0
  /\ d9 = -d10 * x - r9
  /\ d8 = -d9 * x - r8
  /\ d7 = -d8 * x - r7
  /\ d6 = -d7 * x - r6
  /\ d5 = -d6 * x - r5
  /\ d4 = -d5 * x - r4
  /\ d3 = -d4 * x - r3
  /\ d2 = -d3 * x - r2
  /\ eh = 2 * (|rq| * |w2| + x * x * |d2| + |rv|)

  /\ h in [0, 3.83734e-31]
  /\ total = eh + h
  /\ ln_raw = total * 65536
  /\ log2_raw = total * 1.4427 * 65536 + 4.930380657631324e-32
  /\ log10_raw = (total * 0.4343 + 33 * 1.469367938527860e-39 + 7.523163845262641e-37) * 65536

  ->
  r9 in [-5.877471754111438e-39, 0] /\
  r8 in [-5.877471754111438e-39, 0] /\
  r7 in [-5.877471754111438e-39, 0] /\
  r6 in [-5.877471754111438e-39, 0] /\
  r5 in [-5.877471754111438e-39, 0] /\
  r4 in [-5.877471754111438e-39, 0] /\
  r3 in [-5.877471754111438e-39, 0] /\
  r2 in [-5.877471754111438e-39, 0] /\
  rq in [-2.938735877055719e-39, 0] /\
  rv in [-5.877471754111438e-39, 0] /\
  w2 in [0, 0.501] /\
  |d2| <= 6e-39 /\
  eh <= 1.5e-38 /\
  total <= 3.838e-31 /\
  ln_raw <= 2.515e-26 /\
  log2_raw <= 3.63e-26 /\
  log10_raw <= 1.10e-26
}
