# Log-family Gappa proof

Gappa 1.4.1 models the shared `log1p` evaluation in
`src/exact/log_kernel.h` on the actual table-reduced interval
`u in [0, 2^-7]`. The coefficient literals are the stored Q.127 words from
`sollya/log_kernel.out`, scaled to real values. Each Horner product is
rounded down with `fixed<127, dn>`; the square is rounded down with
`fixed<128, dn>`; and the final product is rounded down with
`fixed<127, dn>`, as in the source's unsigned high-product shifts.

Gappa proves the arithmetic evaluation error between the rounded kernel and
the quantized polynomial. The polynomial-versus-`log1p` supnorm is a
hypothesis from Sollya's `supnorm` output in `sollya/log_kernel.out`,
not reproved by Gappa. The reduction/table accuracy terms and function
conversions are represented by the remaining explicit terms in the design's
per-function budget formulas; they are not kernel rounding operations.

Run `make -C proofs/gappa/log` with Gappa 1.4.1 or compatible.
