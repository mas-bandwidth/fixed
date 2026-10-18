# Log-family Gappa proof

Gappa models the shared `log1p` Horner evaluation from
`src/exact/log_kernel.h` over the table-reduced input interval
`u in [0, 2^-7]`. The model uses the stored Q.127 coefficient magnitudes,
Q.127 truncating products (`fixed<127, dn>`), the Q.128 truncated square
(`fixed<128, dn>`), and the final Q.127 truncation.

The proof is structured around each local rounding residue
`fixed<k, dn>(v) - v`, whose interval is [-2^-k, 0]. It accumulates the
Horner residues and the final square/product residues, then adds Sollya's
polynomial-versus-`log1p` supnorm as hypothesis `h`. Gappa proves the
arithmetic recurrence and the stated per-function upper bounds. It does not
re-prove Sollya's supnorm or the downstream design's table/constant analysis;
those are hypotheses and terms in `docs/design/transcendentals.md`.

The natural-log kernel feeds `fixLnExact`, `fixLog2Exact`, and
`fixLog10Exact`. `results.md` compares each conservative raw-unit bound
with the design budget. A bound above budget is an acceptance finding: it
means this proof does not establish the card's DONE-WHEN condition.

Run `make -C proofs/gappa/log` with Gappa 1.4.1 or compatible.
