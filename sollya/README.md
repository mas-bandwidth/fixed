# Sollya scripts

Sollya (https://www.sollya.org) is a tool here and nothing more: it is never vendored, no
Sollya source or library is in this repository, and the fixed library never links it. The
scripts compute polynomial coefficients (`fpminimax` with fixed-point formats), certify their
error (`supnorm`), and print the tables and constants of docs/design/transcendentals.md; their
outputs are committed beside them, and the C sources paste the numbers with a comment naming
the script.

Version used: Sollya 8.0 (Ubuntu package sollya 8.0+ds-3build2 on Ubuntu 26.04, amd-1,
2026-10-18). Each output is byte-for-byte reproducible: rerun the command and `git diff` shows
nothing.

| script | output | what it prints |
|---|---|---|
| exp_kernel.sollya | exp_kernel.out | degree-8 kernel for e^r on [0, ln2/128], its certified bound (2^-102.67), LN2_128, INV_LN2_63, the 2^(j/128) table |
| log_kernel.sollya | log_kernel.out | degree-10 kernel for log1p(u) on [0, 2^-7 + 2^-16], its certified bound (2^-101.04), LN2_128, LOG10E_128, LOG2E_127, the INV_i and L_i tables |
| tan_kernel.sollya | tan_kernel.out | odd degree-13 kernel for tan(s) on \|s\| <= 2^-8 + 2^-100, its certified relative (2^-134.23) and absolute (2^-142.32) bounds, PIO2_127, 240 bits of 2/pi as four words, the tan(j/128) table |

One command per script, from the repository root:

    sollya sollya/exp_kernel.sollya > sollya/exp_kernel.out
    sollya sollya/log_kernel.sollya > sollya/log_kernel.out
    sollya sollya/tan_kernel.sollya > sollya/tan_kernel.out
