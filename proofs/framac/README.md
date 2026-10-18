# Frama-C WP RTE proofs

This directory was intended to hold the Frama-C WP (`-wp-rte`) proofs that the
integer kernels (fixMul, fixDiv, fixISqrt128High and the int128/int256
primitives) hold no undefined behaviour and no overflow, on the emulated
128-bit path and on the native path.

The proofs could not be run on the bench: the toolchain is missing.

- `frama-c` is not on PATH (`command -v frama-c` prints nothing).
- `alt-ergo` and `z3` are not on PATH either.

Per the card's toolchain gate, this is a HOLD. No WP goals were produced or
proved.
