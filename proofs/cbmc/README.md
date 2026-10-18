# CBMC integer-width equivalence checks

Run on a Linux bench with gcc/clang, CMake, MPFR/GMP development headers, GNU
`timeout`, Cadical, and CBMC on `PATH`:

```sh
command -v cbmc
cbmc --version
make -C proofs/cbmc all
```

CBMC version used for the recorded run: 6.6.0. The Makefile compiles
`int128_equiv.c` as three separate translation units: one includes the normal
native `fixed.h`, one defines `FIX_FORCE_EMULATED_INT128` before including the
same library header, and one contains the nondeterministic harnesses. Separate
translation units avoid defeating `#pragma once` or changing a library header.
The CBMC frontend links those arms into each selected harness. `-U__x86_64__`
keeps the native 128/64 reference on the compiler's defined C division operator
instead of the platform-specific inline `divq` assembly; it does not change the
integer operation being checked.

Each property uses Cadical and a 1200-second GNU `timeout` wrapper. CBMC 6.6.0
rejects a `--timeout` option, so the wrapper enforces the requested wall limit.
The run is bench-only and is outside CTest and CI. Full per-property output and
commands are saved to the allocation's evidence directory; `results.md` records
the verification status, width, elapsed time, and scope for each property.
