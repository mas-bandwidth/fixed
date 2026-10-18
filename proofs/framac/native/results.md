# Results: the native __int128 arm

Run 2026-10-18 on the card's bench. Frama-C could not be executed: the tools installed
under `/home/nova/.opam/default/bin` are not readable by the sandbox this card ran in.
`command -v frama-c` prints `/home/nova/sdk/bin/frama-c`, which is a symlink to
`/home/nova/.opam/default/bin/frama-c`, but every invocation (and `alt-ergo`, `why3`) ends
with `Permission denied (os error 13)` before the front end starts. The `make native`
logs contain exactly:

```
timeout: failed to execute process: Permission denied (os error 13)
```

So this file records what was checked here (the preprocessor branch, the UBSan
INT128_MIN / -1 case, the stub contract) and what was not: every `Proved goals:` row.
The next nightly on a bench where Frama-C is readable runs `make -C proofs/framac/native
nightly` and fills those rows. No row below is a claimed proof.

## The native arm is the one selected

`make -C proofs/framac/native branch`, run here with gcc:

```
native branch selection (-D__SIZEOF_INT128__=16, FIX_FORCE_EMULATED_INT128 unset):
#define FIX_HAS_INT128 1
#define __SIZEOF_INT128__ 16
#define FIX_INT128_EMULATED 0
```

`FIX_INT128_EMULATED 0` can only come from the `__SIZEOF_INT128__` branch at
include/fixed/fixed_int128.h:567. `FIX_FORCE_EMULATED_INT128` is not defined. In
addition, `native_acsl.c` carries `#if FIX_INT128_EMULATED #error ... #endif`, so a
Frama-C run that reaches the emulated branch fails instead of silently repeating the
main card's emulated rows. The Makefile also prints this `-dM` grep as the first line of
`make native`.

## The wrappers on the native arm

`native_acsl.c` proves, on the native types, the same set the main card proved on the
emulated pair:

- `proveFixMul` and `proveFixMul` with `-DFIX_SATURATE`
- `proveFixISqrt128High` (it contains no divide)
- the 13 int128 primitives: `proveInt128MulI64`, `proveInt128Add`,
  `proveInt128Shr`, `proveInt128ShiftLeft`, `proveUInt128Shl`, `proveUInt128Shr`,
  `proveUInt128MulU64`, `proveUInt128Add`, `proveUInt128Sub`, `proveInt128Div`,
  `proveInt128ToI64`, `proveInt128Compare`, `proveUInt128Compare`

`make -C proofs/framac/native native` builds each log and then prints one summary line
per wrapper. The target exists; its rows are empty here only because the prover could not
start. `make -C proofs/framac/native nightly` is the target the main card's `nightly`
calls (clean `out/`, then `native`, then the summary).

## INT128_MIN / -1: real undefined behaviour, not guarded

The native `fixInt128Div` has no guard for this case. include/fixed/fixed.h documents
the emulated arm's wrap ("Signed INT128_MIN / -1 wraps to INT128_MIN with a zero
remainder", fixed_int128.h:41) but the native arm is the compiler's `a / b`, which is
undefined. The x86-64 fast path does not catch it: with `a = INT128_MIN`, `b = -1`,
`|a| = 2^127` has high half `2^63`, which is not below `v = 1`, so control falls through
to `return a / b;` at include/fixed/fixed.h:166.

Program run here (`-fsanitize=undefined`, gcc 15.2.0), and re-confirmed on this bench's
gcc 13.3.0 at attempt 2 with the same message (line 166):

```c
#include <stdint.h>
#include <stdio.h>
#include "fixed/fixed.h"

int main( void )
{
	volatile fixInt128 a = (fixInt128)( ( (fixUInt128)1 ) << 127 ); // INT128_MIN
	volatile fixInt128 b = -1;
	volatile fixInt128 q = fixInt128Div( a, b );
	printf( "%016llx:%016llx\n", (unsigned long long)fixInt128Hi( q ), (unsigned long long)fixInt128Lo( q ) );
	return 0;
}
```

```
$ gcc -std=c11 -O0 -fsanitize=undefined -fno-sanitize-recover=all -I include int128_min_div.c
$ ./a.out
include/fixed/fixed.h:166:11: runtime error: division of 0x80000000000000000000000000000000 by -1 cannot be represented in type '__int128'
```

Attempt 2 re-run (gcc 13.3.0, the bench's compiler) with `-I repo/include` from the job
directory produced the identical report at the same line:

```
repo/include/fixed/fixed.h:166:11: runtime error: division of 0x80000000000000000000000000000000 by -1 cannot be represented in type '__int128'
```

This is a real undefined behaviour in the library's native path, not a prover limit. It is
left as an open goal on purpose: `proveInt128Div` requires only `b != 0`, so WP will
report the signed-overflow goal at include/fixed/fixed.h:166 rather than have the
contract exclude the case. The emulated arm does not have this goal (it wraps).

## The divq inline asm

`int128_native_stub.h` gives the asm the contract the code documents:

- `fixUInt128DivRemBy64` (include/fixed/fixed_int128.h:629): requires `d != 0` and
  `(a >> 64) < d` (the quotient fits in 64 bits, which is what keeps `divq` from
  faulting); assigns nothing to memory; ensures `\result == a / d` and
  `*remainder == a % d`.
- `fixInt128Divq` (the asm inside include/fixed/fixed.h:130): requires `b != 0`, a
  divisor whose magnitude fits in 64 bits, and `uhi < v` (the quotient fit); assigns
  nothing; ensures `\result == a / b`.

The `divq` target proves that these contracts compose as declarations. The asm body
itself is a trusted assumption: WP cannot read inline asm, so the contract is what is
proved against, and this is recorded here rather than claimed as a proof of the
instruction.

## Every unproved goal, with its line and reason

No Frama-C run completed on this bench, so there is no WP goal list from it. The open
items that are known before a run:

- `proveInt128Div`: the native signed divide at include/fixed/fixed.h:166 for
  `a == INT128_MIN && b == -1`. Reason: real undefined behaviour, shown under UBSan
  above. Deliberately not excluded from the contract.
- The two `divq` wrappers: not a goal WP can discharge; the asm body is behind the
  contracts in `int128_native_stub.h` (trusted assumption). Lines:
  include/fixed/fixed_int128.h:640 and include/fixed/fixed.h:130.
- If Frama-C 33 rejects `__int128` in its front end, every native log will end with that
  parse error at the first `__int128` typedef (include/fixed/fixed_int128.h:577) and the
  Makefile's summary prints `not run: front end rejects __int128`. The smallest
  equivalent then is a front-end model of the native operators, not a contract change;
  that is not attempted here because no Frama-C was available to read the error.

## Not done

- The Frama-C runs themselves (`Proved goals: N / M` per wrapper).
- Any decision about Frama-C 33's `__int128` support.
- The emulated/frame arms are the main card's and are not repeated here.
