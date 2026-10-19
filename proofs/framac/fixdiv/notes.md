# fixDiv WP notes

The goal-generation investigation comes first; the helper-contract notes are at the end.

## Goal-generation investigation (attempt 4)

The main Frama-C card is present in the carried tree. The prior pass spent more than
15 minutes in VC generation for `proveFixDiv` with `-inline-calls @inline`.
Attempt 3 could not run Frama-C 33 because its bench denied the opam toolchain.
This attempt used Frama-C 33.0, Alt-Ergo 2.4.3, Z3 4.13.3 and Why3 1.8.2 on
amd-2, with load guard <=16, CPU 2, nice 19, 8 GiB address-space cap and
wall-clock caps. Logs remain in the allocated gate job, not in this tree.

## Ordered attempts

| Step | Experiment | Elapsed / bound | Result |
|---|---|---|---|
| 1, smaller total contract | Carried wrapper, no inline: `make fixdiv`, four documented ensures and no precondition | about 2 minutes, 600 s cap | Goal generation ended; `Proved goals: 1 / 5`. This only reasons about an opaque call to fixDiv and is not an actual-body proof. |
| 2, split stages | Added zero, fast and wide branch wrappers, each calling the production fixDiv; no full-function assumption was added | under 300 s | Together with step 3, scheduled 77 goals and ended `73 / 77`; four goals remained open. |
| 3, `-wp-split` | Split the staged wrappers' conditional goals | under 300 s | The four open names were `typed_proveFixDivFast_ensures_part04`, `typed_proveFixDivFast_assert_rte_signed_overflow`, `typed_proveFixDivWide_ensures_part07`, `typed_proveFixDivWide_ensures_part10`; Z3 hit the 100000-step bound on each. |
| 4, `-wp-fct`/`-wp-prop` | Isolated `proveFixDiv` runtime assertions with `-wp-fct proveFixDiv -wp-prop=@assert`; selectively inlined fixDiv and fixShiftLeft | under 240 s | `Proved goals: 2 / 3`; the fixed.h:235 signed-overflow goal timed out. |
| 5, `-wp-timeout`/`-wp-steps` | Alt-Ergo/Z3 30 s per goal on the total wrapper; Z3 10 s and 100000 steps on staged/split goals | all jobs bounded as above | Limits prevented runaway proving; they did not close every goal. |
| 6, per-function wall cap | `LIMIT=timeout 600` in Makefile, plus independent guarded outer cap | selective-inline run ended inside the cap | `nightly` now invokes `fixdiv` because this target produces a summary inside its cap. |

The crucial change is **selectively inlining fixDiv** instead of every function tagged
`@inline`. With `-inline-calls fixDiv`, actual fixDiv branches enter the wrapper VC,
but the 128-iteration emulated divider at fixed_int128.h:417 stays out.
Goal generation ended and the emulated total wrapper printed
`Proved goals: 5 / 7` within about two minutes. This is a bounded partial
proof, not a complete proof of fixDiv. The helper calls have no assigns
specifications, and their own obligations remain separate.

The two Eva alarms at fixed.h:235 are division by zero and signed overflow.
The source returns before line 235 when b == 0; the fast-path strict range
`-2^47 < a < 2^47` makes its shifted numerator strictly between
`-2^63` and `2^63`, excluding the only signed quotient-overflow case
`INT64_MIN / -1`. Attempt 3 also ran a GCC UBSan edge sweep of 114 pairs on
native and emulated arms with no report. That finite sweep does not prove all
inputs; the open WP overflow goal remains a prover/abstraction limit, not a
confirmed real undefined behavior.

See results.md for the exact open total-function goals. No SDK header, main
Frama-C proof, CI, CTest or global toolchain was changed.

## Focused wide-path follow-up

With `-wp-prop=@ensures` and `-wp-fct proveFixDiv`, selectively inlining
`fixInt128Gt/Lt/ToI64/FromI64` still ended `3 / 4`; the result-range goal
timed out. Expanding their emulated counterparts also ended `3 / 4`.
Expanding `fixEmuInt128Make` as well also ended `3 / 4`, with the same
`typed_proveFixDiv_ensures_4` timeout. Each experiment finished inside its
240 s cap. The emulated divider remained opaque, and no contract about its
return value was assumed. These bounded expansions did not close the goal.
`-wp-prop=-@assigns,-@terminates` deliberately filters those categories
in the total-function target; neither category is claimed proved.

The carried attempt-3 UBSan output (GCC 13.3.0,
`-fsanitize=undefined -fno-sanitize-recover=all`, 114 edge pairs on each
arm) was:

```text
===== emulated (-DFIX_FORCE_EMULATED_INT128) =====
fixDiv edge sweep done: 114 cases, sink=140737488355329
rc=0
===== native =====
fixDiv edge sweep done: 114 cases, sink=140737488355329
rc=0
```

This run was not repeated in attempt 4. Its absence of a sanitizer report
supports the source guard analysis at the sampled boundaries; it is not a
substitute for closing the two WP goals.

## Final Makefile gate

With the finished own-directory Makefile and wrapper, the guarded exact
`make -C proofs/framac/fixdiv fixdiv` exited 0 in 128.78 real seconds.
It printed `[wp] Proved goals: 5 / 7`; the two open names in results.md
timed out. The goal-generation hang is removed for this selected body path.
The target is in `nightly` because it ended within 600 seconds; `nightly`
running it does not imply all goals are proved. This card remains HOLD on
the full-proof objective.

## Helper contracts (card fixed-framac-fixdiv-helper-contracts)

All runs on amd-2 (Frama-C 33.0, Alt-Ergo 2.6.4, Z3), nice 19, in a rowan slot, each under
`timeout`. The order of work and what each run showed:

| Run | What | Result |
|---|---|---|
| red | `make fixdiv` at base d77cbcc | `Proved goals: 5 / 7`, 127.49 s |
| pass 1 | 28 of the 29 helper bodies (all but fixEmuUInt128DivMod, whose loop has no invariant), `-wp-timeout 30` | `233 / 240`, 419 s; 7 timeouts in 5 helpers |
| fixdiv 1 | `make fixdiv` with the contracts | `10 / 10`, 3.11 s |
| pass 2 | the 5 helpers with timeouts, `-wp-timeout 120` | `71 / 78`; the same 7 goals time out |
| lemmas | 7 lemmas (lsr by 63; sign rules for truncating `/` and `%`; bounds) | signed DivMod proves with them (40 / 41), but `fix_div_neg_num`, `fix_div_neg_den` and `fix_lsr63` do not prove on Alt-Ergo or Z3 at 60 s, so the lemmas were dropped rather than kept as axioms |

Three tries at the same seven goals made no progress, so the helpers behind them are ASSUMED
(results.md has each reason). The tracer did show that the helper contracts are what fixDiv
needed: with them, `ensures_4` and the signed-overflow check prove in about a second.

The ASSUMED contracts are held by this boundary program (not committed; run from a scratch
directory against this tree's `include/`):

```c
// Boundary check for the ASSUMED contracts in proofs/framac/fixdiv/helpers_acsl.h, against
// native unsigned/signed __int128: fixEmuUInt128DivMod, fixEmuInt128DivMod and the divides
// on it (every ordered pair of boundary values, divisor non-zero, INT128_MIN / -1 skipped
// where the contract excludes it), fixEmuUInt128Shl / fixInt128ShiftLeft (every boundary
// value x shift counts -1, 0, 1, 16, 63, 64, 65, 127, 128), fixShiftLeft (int64 boundaries x
// counts 0, 1, 16, 47, 62, 63) and fixEmuInt128IsNegative. Build: gcc -O1 -fsanitize=undefined -fno-sanitize-recover=all
//   -DFIX_FORCE_EMULATED_INT128 -I include helpers_check.c
#include <stdio.h>
#include "fixed/fixed.h"

#ifndef CHECK_PLANT
#define CHECK_PLANT 0 // -DCHECK_PLANT=1 plants an off-by-one expectation; the run must fail
#endif

typedef unsigned __int128 u128;
typedef __int128 s128;

static fixEmuUInt128 toEmuU( u128 v ) { return fixEmuUInt128Make( (uint64_t)( v >> 64 ), (uint64_t)v ); }
static u128 fromEmuU( fixEmuUInt128 v ) { return ( (u128)v.hi << 64 ) | v.lo; }

int main( void )
{
	const u128 one = 1;
	const u128 vals[] = {
		0, 1, 2, 3, 65536, (u128)INT64_MAX, (u128)INT64_MAX + 1, (u128)UINT64_MAX, ( one << 64 ),
		( one << 64 ) + 1, ( one << 79 ), ( one << 127 ) - 1, ( one << 127 ), ( one << 127 ) + 1,
		~(u128)0 - 1, ~(u128)0, (u128)(s128)INT64_MIN, (u128)(s128)-1, (u128)(s128)-2,
		(u128)( (s128)INT64_MIN * 65536 ), (u128)( (s128)INT64_MAX * 65536 ),
	};
	const int n = (int)( sizeof( vals ) / sizeof( vals[0] ) );
	long cases = 0, bad = 0;
	for ( int i = 0; i < n; i++ )
	{
		for ( int j = 0; j < n; j++ )
		{
			u128 a = vals[i], b = vals[j];
			if ( b == 0 )
				continue;
			fixEmuUInt128 q, r;
			fixEmuUInt128DivMod( toEmuU( a ), toEmuU( b ), &q, &r );
			cases++;
			if ( fromEmuU( q ) != a / b + CHECK_PLANT || fromEmuU( r ) != a % b )
				bad++;
			s128 sa = (s128)a, sb = (s128)b;
			if ( sa == (s128)( one << 127 ) && sb == -1 )
				continue; // excluded by the contract: INT128_MIN / -1
			fixEmuInt128 sq = fixEmuInt128Div( fixEmuToSigned( toEmuU( a ) ), fixEmuToSigned( toEmuU( b ) ) );
			fixInt128 tq = fixInt128Div( fixEmuToSigned( toEmuU( a ) ), fixEmuToSigned( toEmuU( b ) ) );
			cases++;
			if ( (s128)fromEmuU( fixEmuToUnsigned( sq ) ) != sa / sb || (s128)fromEmuU( fixEmuToUnsigned( tq ) ) != sa / sb )
				bad++;
		}
	}
	const int shifts[] = { -1, 0, 1, 16, 63, 64, 65, 127, 128 };
	for ( int i = 0; i < n; i++ )
	{
		u128 a = vals[i];
		s128 sa = (s128)a;
		cases++;
		if ( ( fixEmuInt128IsNegative( fixEmuToSigned( toEmuU( a ) ) ) != 0 ) != ( sa < 0 ) + CHECK_PLANT )
			bad++;
		for ( int k = 0; k < (int)( sizeof( shifts ) / sizeof( shifts[0] ) ); k++ )
		{
			int sh = shifts[k];
			u128 want = ( sh >= 0 && sh < 128 ) ? ( a << sh ) : 0; // (a * 2^sh) mod 2^128
			cases++;
			if ( fromEmuU( fixEmuUInt128Shl( toEmuU( a ), sh ) ) != want + CHECK_PLANT ||
				 fromEmuU( fixUInt128Shl( toEmuU( a ), sh ) ) != want ||
				 fromEmuU( fixEmuToUnsigned( fixInt128ShiftLeft( fixEmuToSigned( toEmuU( a ) ), sh ) ) ) != want )
				bad++;
		}
		for ( int j = 0; j < n; j++ )
		{
			u128 b = vals[j];
			s128 sb = (s128)b;
			if ( sb == 0 || ( sa == (s128)( one << 127 ) && sb == -1 ) )
				continue;
			fixEmuInt128 q, r;
			fixEmuInt128DivMod( fixEmuToSigned( toEmuU( a ) ), fixEmuToSigned( toEmuU( b ) ), &q, &r );
			cases++;
			if ( (s128)fromEmuU( fixEmuToUnsigned( q ) ) != (s128)( (u128)( sa / sb ) + CHECK_PLANT ) ||
				 (s128)fromEmuU( fixEmuToUnsigned( r ) ) != sa % sb )
				bad++;
		}
	}
	const int64_t svals[] = { 0, 1, -1, 2, -2, INT64_MAX, INT64_MIN, INT64_MAX - 1, INT64_MIN + 1,
							  ( (int64_t)1 << 47 ) - 1, -( ( (int64_t)1 << 47 ) - 1 ), (int64_t)1 << 47, -( (int64_t)1 << 47 ) };
	const int sshifts[] = { 0, 1, 16, 47, 62, 63 };
	for ( int i = 0; i < (int)( sizeof( svals ) / sizeof( svals[0] ) ); i++ )
	{
		for ( int k = 0; k < (int)( sizeof( sshifts ) / sizeof( sshifts[0] ) ); k++ )
		{
			int64_t a = svals[i];
			int sh = sshifts[k];
			int64_t got = fixShiftLeft( a, sh );
			uint64_t low = (uint64_t)a << sh; // low 64 bits of a * 2^sh
			int64_t want = low < ( (uint64_t)1 << 63 ) ? (int64_t)low : -(int64_t)( ~low ) - 1;
			s128 exact = (s128)a * ( (s128)1 << sh );
			cases++;
			if ( got != (int64_t)( (uint64_t)want + CHECK_PLANT ) || ( exact >= INT64_MIN && exact <= INT64_MAX && got != (int64_t)exact ) )
				bad++;
		}
	}
	printf( "helpers_check: %ld cases, %ld mismatches\n", cases, bad );
	return bad != 0;
}
```

Output (GCC 15.2.0, `-O1 -g -fsanitize=undefined -fno-sanitize-recover=all -DFIX_FORCE_EMULATED_INT128`):

```text
== gcc -O1 -fsanitize=undefined -fno-sanitize-recover=all -DFIX_FORCE_EMULATED_INT128 -DCHECK_PLANT=0
gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
helpers_check: 1544 cases, 0 mismatches
rc=0
== gcc -O1 -fsanitize=undefined -fno-sanitize-recover=all -DFIX_FORCE_EMULATED_INT128 -DCHECK_PLANT=1
gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
helpers_check: 1544 cases, 1126 mismatches
rc=1
```

The landed goal-generation run completed at 5/7 goals; the later helper-contract
run reached 10/10 conditionally on six explicitly assumed helper summaries.
The full-proof target now counts assigns and terminates and records per-goal
verdicts separately; its new count and planted-bug verdict await the Linux run.
