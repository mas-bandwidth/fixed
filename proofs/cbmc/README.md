# CBMC: the native and emulated int128 paths agree

`int128_equiv.c` asks CBMC to prove that the native `__int128` arm and the emulated
arm (`FIX_FORCE_EMULATED_INT128`, the arm plain MSVC uses) give the same bits for
`fixMul`, `fixDiv` and `fixISqrt128High`, and for each 128-bit primitive those three use:
add and subtract, compares, shifts, the 64x64 and 128x128 products, and the divisions.
The bound is stated per property in `results.md`.

This is a bench-only check. It is not part of CTest or CI, and nothing in the library
build depends on it.

## Running it

On a Linux bench that has `cbmc` (version 6.6.0 recorded) and GNU `timeout` on PATH:

```sh
command -v cbmc && cbmc --version
make -C proofs/cbmc all            # every property, in the narrowing results.md names
make -C proofs/cbmc fixmul         # one property
make -C proofs/cbmc fixmul WIDTH_fixmul=12 SIGNS=0 TIMEOUT=600   # a wider try
```

Each target runs, for each sign combination it needs:

```
timeout $(TIMEOUT) cbmc -I include -D CBMC_WIDTH=<w> -D CBMC_WIDTH_B=<wb> -D CBMC_SIGNS=<s>
    --function harness_<name> --unwind <n> --unwinding-assertions --bounds-check
    --signed-overflow-check --undefined-shift-check --slice-formula --sat-solver cadical
    native.c emulated.c harness.c
```

and prints CBMC's `VERIFICATION SUCCESSFUL` or `VERIFICATION FAILED` line, the exit
status and the wall time. Full logs go to `$(LOGDIR)` (default
`$TMPDIR/fixed-cbmc-int128-logs`, outside the tree). CBMC 6.6.0 has no `--timeout`
option, so GNU `timeout` (default 1200 s) bounds each run; exit 124 is a timeout, and a
timeout is never read as a proof. The CBMC 6.6.0 build on the benches has no CaDiCaL
compiled in: it prints "The specified solver, 'cadical', is not available" and falls
back to its default, MiniSat 2. `SOLVER=--z3` selects Z3 instead.

## How the harness is built

The Makefile compiles `int128_equiv.c` three times, with `CBMC_NATIVE_ARM`,
`CBMC_EMULATED_ARM` or `CBMC_HARNESS_TU` defined first. Each arm includes
`fixed/fixed.h` once and exports wrappers under its own prefix (`cbmc_native_*`,
`cbmc_emulated_*`); the harness TU asserts that the two agree. No header is edited.

Two modelling choices, both stated in the source:

- The native arm undefines `__x86_64__`, so `fixInt128Div` and `fixUInt128DivRemBy64`
  take their plain C `/` spelling. CBMC does not model the inline `divq`.
- CBMC has no body for `__builtin_sqrt`. Left alone, it returns a different free value
  in each arm, so the repair loops in `fixISqrt128High` never bound. Both arms' seed is
  routed to one stub that returns a single shared seed within `CBMC_SEED_SLACK` (3) of
  the exact root. The claim is that the arms agree for every seed in that window, which
  includes the real double seed.

## Why some properties are narrowed

A full 64x64 multiply, or a 128-bit divide (which CBMC encodes through a multiply),
is the classic hard case for a SAT solver. At full width these runs do not finish in
20 minutes. Those properties are proved on narrowed inputs: `CBMC_WIDTH` free low bits
extended with constant zeros or ones (`CBMC_SIGNS` picks which, per input, and the
target runs every combination), or one input at full width and the other narrowed
(`CBMC_WIDTH_B`). `results.md` gives each property's exact domain. A narrowed result is
never called a full proof.
