# Native __int128 Frama-C results

Frama-C 33.0 parses the native arm when given -machdep gcc_x86_64 and
-cpp-extra-args="-I../../../include -D__SIZEOF_INT128__=16". The default x86_64
machdep rejects the first native typedef at include/fixed/fixed_int128.h:577:

    __int128 is a GCC extension, use a GCC-based machdep to enable it.

With gcc_x86_64, Frama-C -print starts with typedef __int128 fixInt128 and typedef
unsigned __int128 fixUInt128. The preprocessing branch check prints
FIX_HAS_INT128=1, __SIZEOF_INT128__=16, FIX_INT128_EMULATED=0. The source also
has a preprocessor error if the emulated branch is reached. No native-path
result below relies on the default machdep.

The native target uses one WP worker, Alt-Ergo 2.4.3 and Z3 4.13.3, per-goal
timeout 60 seconds except the square-root wrapper's 10 seconds, a 600-second
process cap, and an 8 GiB address-space cap. The complete make native target
returned 0 and printed these exact rows; 0 / 0 means Frama-C generated no
runtime-error or ensures goal, not that it proved the arithmetic behavior.

| wrapper | arm | proved / total |
| --- | --- | --- |
| proveFixMul | native | 3 / 3 |
| proveFixISqrt128High | native | 8 / 11 |
| proveInt128MulI64 | native | 3 / 3 |
| proveInt128Add | native | 0 / 0, no goal generated |
| proveInt128Shr | native | 2 / 2 |
| proveInt128ShiftLeft | native | 2 / 2 |
| proveUInt128Shl | native | 2 / 2 |
| proveUInt128Shr | native | 2 / 2 |
| proveUInt128MulU64 | native | 0 / 0, no goal generated |
| proveUInt128Add | native | 0 / 0, no goal generated |
| proveUInt128Sub | native | 0 / 0, no goal generated |
| proveInt128Div | native | 4 / 5 |
| proveInt128ToI64 | native | 0 / 0, no goal generated |
| proveInt128Compare | native | 3 / 3 |
| proveUInt128Compare | native | 7 / 7 |
| proveUInt128DivRemBy64 | native | 6 / 6 |
| proveInt128Divq | native | 5 / 5 |
| proveFixMul | native-saturate | 3 / 4 |

Every unproved generated goal:

- typed_proveFixISqrt128High_assert_rte_is_nan_or_infinite and its _2 variant:
  the floating seed conversions at include/fixed/fixed.h:300 and :316.
  Alt-Ergo timed out at 10 seconds on each. A focused Z3-only retry failed
  both; no proof or counterexample was established.
- typed_proveFixISqrt128High_call___builtin_sqrt_builtin_sqrt_requires:
  the assumed finite, nonnegative argument contract in native_acsl.c:69 at
  the call in include/fixed/fixed.h:300. Alt-Ergo timed out at 10 seconds.
  Z3-only also failed this call goal and the analogous second call at :316.
  These are prover limits; neither makes the square-root result proved.
- typed_proveFixMul_ensures: the saturation range postcondition in
  native_acsl.c:92. Alt-Ergo and a separate Z3-only run each timed out at
  60 seconds. The contract is retained; the result is not proved.
- typed_proveInt128Div_assert_rte_signed_overflow_2: the generic native
  division at include/fixed/fixed.h:166. The code does not guard
  INT128_MIN / -1. This is a real undefined behavior, confirmed below,
  rather than a prover limit.

The INT128_MIN / -1 confirmation uses the following C program, compiled
with gcc -std=c11 -O0 -fsanitize=undefined -fno-sanitize-recover=all and the
repository include directory:

    #include <stdint.h>
    #include <stdio.h>
    #include "fixed/fixed.h"
    int main(void) {
        volatile fixInt128 a = (fixInt128)(((fixUInt128)1) << 127);
        volatile fixInt128 b = -1;
        volatile fixInt128 q = fixInt128Div(a, b);
        printf("%016llx:%016llx\n",
               (unsigned long long)fixInt128Hi(q),
               (unsigned long long)fixInt128Lo(q));
        return 0;
    }

The process exits 1 and prints:

    repo/include/fixed/fixed.h:166:11: runtime error: division of 0x80000000000000000000000000000000 by -1 cannot be represented in type '__int128'

The two divq rows prove wrapper composition against declarations; they do not
verify the inline assembly. The hardware divq instruction has no memory
side effect. The enclosing fixUInt128DivRemBy64 helper does write its
remainder through a pointer, so its sound stub contract requires a valid
pointer and assigns *remainder (int128_native_stub.h:36-43). Its quotient
and remainder postconditions are the documented division semantics.
The signed fixInt128Divq declaration has assigns nothing and the
quotient-fit preconditions that exclude the INT128_MIN / -1 case.
The real instruction bodies at include/fixed/fixed_int128.h:640 and
include/fixed/fixed.h:130 remain trusted assumptions, not proved goals.

The WP runs exclude assigns and terminates goals. Frama-C also warns that
unaligned-pointer and invalid-function-pointer RTE guards are unsupported.
The five unsigned arithmetic rows with 0 / 0 establish no property.
No fixDiv wrapper was run; that belongs to the separate fixDiv card.
The nightly target cleans generated logs and invokes this native target;
the coordinator must connect it to the main nightly target after landing.
