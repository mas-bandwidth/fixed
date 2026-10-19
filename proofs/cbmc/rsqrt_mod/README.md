# CBMC: fixRsqrtExact, fixFmod and fixMod meet their integer specifications

This is a bench-only check, as `proofs/cbmc/README.md` is. It is not part of CTest or
CI, and nothing in the library build depends on it.

`rsqrt_spec.c` asserts the integer specification the source states for
`fixRsqrtExact`: for positive `raw`, `q` is the unique non-negative integer with

    (2q-1)^2 * raw <= 2^50 < (2q+1)^2 * raw

and `q == 0` whenever `raw > 2^50`; for `x <= 0` the result is `INT64_MAX`. The
inequality is written independently in unsigned `__int128` arithmetic. At a tie
(`raw = 2^50`) it selects `q = 1`, which is the source's stated tie direction (round
up); it is not ties-to-even.

`mod_spec.c` asserts, in signed `__int128` arithmetic, that `fixFmod(a, b)` is
`a - trunc(a/b)*b` and `fixMod(a, b)` is the floored remainder, that `b == 0` gives
`0`, that `INT64_MIN` with `-1` gives `0`, and that the results have the sign of `a`
(fmod) or `b` (mod), or are zero. Both files run with `--signed-overflow-check` and
`--div-by-zero-check`, so the calls inside the library are checked for undefined
behaviour too.

## Arms

Each property is built and proved twice, as `proofs/cbmc/int128_equiv.c` builds its
arms:

- `native`: `-D CBMC_NATIVE_ARM`, with `__x86_64__` undefined before `fixed/fixed.h`
  so the library's division takes its plain C `/` spelling (CBMC does not model the
  inline `divq`);
- `emulated`: `-D CBMC_EMULATED_ARM`, which defines `FIX_FORCE_EMULATED_INT128`.

System headers are included before `__x86_64__` is undefined, as in
`int128_equiv.c`, so glibc picks its 64-bit stubs.

## The sqrt seed

`fixRsqrtExact` calls `fixISqrt128High`, whose seed is `__builtin_sqrt`. CBMC has no
body for that builtin, so the harness routes it to `cbmc_rsqrt_seed`. A seed is any
integer a correctly rounded IEEE sqrt may return for the library's double argument
`x`, i.e. any `r` with

    (2r - 1)^2 <= 4x < (2r + 3)^2

(exact integers). The harness draws `r` nondeterministically and assumes that
condition, so the proof covers every seed the hardware can produce. It also asserts
that the stub receives exactly the library's own double expression. The allowed set
is never empty: `r = floor(sqrt(x))` satisfies it for every integer `x >= 1`.
`rsqrt_edges` pins the seed to the exact integer root for its constant inputs, so it
needs no assumption.

## Running it

On a Linux bench with `cbmc`, GNU `timeout` and `cadical` on PATH:

```sh
command -v cbmc && cbmc --version
make -C proofs/cbmc/rsqrt_mod all          # every property, at the widths results.md names
make -C proofs/cbmc/rsqrt_mod rsqrt        # one property, both arms
make -C proofs/cbmc/rsqrt_mod rsqrt WIDTH_rsqrt=6 SIGNS_rsqrt=0 TIMEOUT=600
```

Each run is

```sh
timeout $(TIMEOUT) cbmc -I include -D CBMC_NATIVE_ARM|CBMC_EMULATED_ARM \
    -D CBMC_WIDTH=<w> -D CBMC_SIGNS=<s> --function harness_<name> --unwind <n> \
    --unwinding-assertions --bounds-check --signed-overflow-check --div-by-zero-check \
    --undefined-shift-check --slice-formula --external-sat-solver cadical <spec.c>
```

Logs go to `$(LOGDIR)` (default `$TMPDIR/fixed-cbmc-rsqrt-mod-logs`, outside the
tree). A timeout (exit 124) is never read as a proof. `make all` prints CBMC's
`VERIFICATION SUCCESSFUL`/`VERIFICATION FAILED` line and the exit status and wall
time of every run.

## Why some properties are narrowed

Full-width 64-bit division and multiplication are the classic hard cases for a SAT
solver; the parent directory's README narrows the same operations for the same
reason. `results.md` gives each property's exact domain, and a narrowed result is
never called a full proof. The narrowed inputs are the sign extensions of a
`CBMC_WIDTH`-bit value (0 = zero extension, 1 = ones extension), exactly as
`proofs/cbmc/int128_equiv.c` does. `rsqrt_high` narrows the top of the positive
range instead, `raw = INT64_MAX - u` with `u` the free low bits; `rsqrt_edges` and
`mod_edges` are fixed constants and include the tie, the largest positive raw, the
`INT64_MIN / -1` pair and the zero divisors.

## Red at BASE

`proofs/cbmc/rsqrt_mod` does not exist at BASE `4130305` (it is untracked in the
worktree; the first `git status --short` shows only `?? proofs/cbmc/rsqrt_mod/`).
