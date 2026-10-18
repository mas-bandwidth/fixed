# Results

The proof source is committed at the card head. The Linux Hulk Gappa gate has
not passed yet; do not treat these output values as proved until
`make -C proofs/gappa/log` completes successfully.

The proposed conservative bounds for the shared natural-log kernel are
`<= 3.838e-31` before conversion to raw units. Conversions in the Gappa goals
are:

| function | conservative raw bound | design budget | expected comparison |
| --- | ---: | ---: | --- |
| fixLnExact | <= 2.515e-26 | 2^-85.04 = 2.51425411001008e-26 | above budget |
| fixLog2Exact | <= 3.63e-26 | 2^-84.5 = 3.65565637751430e-26 | below budget |
| fixLog10Exact | <= 1.10e-26 | 2^-86.2 = 1.12516023157136e-26 | below budget |

The ln comparison is a likely HOLD finding if Gappa proves the conservative
bound. It does not imply that the actual error exceeds the budget; it says the
certified upper bound is too coarse to establish the design budget.
