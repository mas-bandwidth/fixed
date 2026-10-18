// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Frama-C WP wrappers for the integer kernels. Read by frama-c only: no build target,
// no CTest test and no CI job compiles this file. See README.md.
//
// Each wrapper calls one library kernel and carries an ACSL contract that states only
// what the library already documents: the input ranges it names as preconditions,
// assigns \nothing (every kernel is a pure function of its arguments), and the
// saturation results where they are simple. The library headers are not edited; every
// annotation lives in this file. The Makefile runs
//
//   frama-c -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60
//
// on one wrapper at a time, with -inline-calls so that the RTE goals of every inlined
// primitive (the int128 seam and the emulated lane arithmetic) are generated and proved
// in the calling context, rather than for unconstrained arguments.
//
// The two arms: the Makefile passes -DFIX_FORCE_EMULATED_INT128 for the emulated
// (two-lane) path, and nothing for the native __int128 path.

#include <stdint.h>

// The seed of fixISqrt128High is __builtin_sqrt (FIX_SQRT_SEED in fixed.h). Frama-C has
// no contract for the gcc builtin, so this is the one assumption the proof takes: the
// IEEE square root of a finite non-negative double is finite, non-negative, and
// monotone, so it maps the two seed ranges the kernel feeds it (below 2^104 and below
// 2^64) to at most their square roots, 2^52 and 2^32 (both exact in binary64). The seed
// VALUE is irrelevant to correctness (the repair loops fix any seed); only its range
// matters, for the double-to-uint64 conversion that RTE checks.
/*@ requires \is_finite( x ) && x >= 0.0;
    assigns \result \from x;
    ensures \is_finite( \result ) && \result >= 0.0;
    ensures x <= 0x1p104 ==> \result <= 0x1p52;
    ensures x <= 0x1p64 ==> \result <= 0x1p32;
*/
double __builtin_sqrt( double x );

#include "fixed/fixed.h"

// ---- the kernels -----------------------------------------------------------------

// fixMul: round half up, arithmetic shift. Unchecked by default (wraps in two's
// complement through the unsigned round trip, fixed.h); with FIX_SATURATE the result
// saturates to [FIX_MIN, FIX_MAX] = [-INT64_MAX, INT64_MAX].
#if defined( FIX_SATURATE )
/*@ assigns \nothing;
  @ ensures -9223372036854775807 <= \result <= 9223372036854775807;
*/
#else
/*@ assigns \nothing; */
#endif
fixed_t proveFixMul( fixed_t a, fixed_t b )
{
	return fixMul( a, b );
}

// fixDiv: truncation toward zero; division by zero saturates with the sign of the
// numerator and 0/0 == 0; a quotient past the range saturates (fixed.h).
/*@ assigns \nothing;
  @ ensures b == 0 && a > 0 ==> \result == 9223372036854775807;
  @ ensures b == 0 && a < 0 ==> \result == -9223372036854775807;
  @ ensures b == 0 && a == 0 ==> \result == 0;
  @ ensures -9223372036854775807 <= \result <= 9223372036854775807;
*/
fixed_t proveFixDiv( fixed_t a, fixed_t b )
{
	return fixDiv( a, b );
}

// fixISqrt128High: exact integer square root of any unsigned 128-bit value hi:lo.
/*@ assigns \nothing;
  @ ensures hi == 0 && lo == 0 ==> \result == 0;
*/
uint64_t proveFixISqrt128High( uint64_t hi, uint64_t lo )
{
	return fixISqrt128High( hi, lo );
}

// ---- the int128 primitives those three use ---------------------------------------
//
// Proved on their own as well, so a goal inside a primitive is reported against the
// primitive and not only against the kernel that inlines it. fixed_int256.h is not
// used by fixMul, fixDiv or fixISqrt128High (its consumers are src/fixed_math.c and the
// fixDivShifted256 family in fixed_vec.h), so no int256 primitive is in scope.

// The widening multiply cannot overflow: the largest product of two int64 is 2^126.
/*@ assigns \nothing; */
fixInt128 proveInt128MulI64( int64_t a, int64_t b )
{
	return fixInt128MulI64( a, b );
}

// Addition wraps through the unsigned type on both arms (fixed_int128.h).
/*@ assigns \nothing; */
fixInt128 proveInt128Add( fixInt128 a, fixInt128 b )
{
	return fixInt128Add( a, b );
}

// Shift counts: native __int128 shifts are undefined outside [0, 127], and the header
// says the library never produces such a count; the emulated arm is total.
/*@ requires 0 <= shift <= 127;
  @ assigns \nothing;
*/
fixInt128 proveInt128Shr( fixInt128 a, int shift )
{
	return fixInt128Shr( a, shift );
}

/*@ requires 0 <= shift <= 127;
  @ assigns \nothing;
*/
fixInt128 proveInt128ShiftLeft( fixInt128 a, int shift )
{
	return fixInt128ShiftLeft( a, shift );
}

/*@ requires 0 <= shift <= 127;
  @ assigns \nothing;
*/
fixUInt128 proveUInt128Shl( fixUInt128 a, int shift )
{
	return fixUInt128Shl( a, shift );
}

/*@ requires 0 <= shift <= 127;
  @ assigns \nothing;
*/
fixUInt128 proveUInt128Shr( fixUInt128 a, int shift )
{
	return fixUInt128Shr( a, shift );
}

/*@ assigns \nothing; */
fixUInt128 proveUInt128MulU64( uint64_t a, uint64_t b )
{
	return fixUInt128MulU64( a, b );
}

/*@ assigns \nothing; */
fixUInt128 proveUInt128Add( fixUInt128 a, fixUInt128 b )
{
	return fixUInt128Add( a, b );
}

/*@ assigns \nothing; */
fixUInt128 proveUInt128Sub( fixUInt128 a, fixUInt128 b )
{
	return fixUInt128Sub( a, b );
}

// Division by zero is undefined on both arms (fixed_int128.h: "Every divide in this
// library guards its divisor first"). The quotient fixDiv asks for is a 64-bit value
// shifted up by 16 over a non-zero 64-bit divisor. On the native arm, INT128_MIN / -1 is
// also undefined and this contract does not exclude it, so that RTE goal is expected to
// stay unproved for proveInt128Div native; fixDiv never forms that dividend.
#if FIX_INT128_EMULATED
/*@ requires b.hi != 0 || b.lo != 0;
  @ assigns \nothing;
*/
#else
/*@ requires b != 0;
  @ assigns \nothing;
*/
#endif
fixInt128 proveInt128Div( fixInt128 a, fixInt128 b )
{
	return fixInt128Div( a, b );
}

/*@ assigns \nothing; */
int64_t proveInt128ToI64( fixInt128 a )
{
	return fixInt128ToI64( a );
}

/*@ assigns \nothing; */
int proveInt128Compare( fixInt128 a, fixInt128 b )
{
	return ( fixInt128Gt( a, b ) ? 1 : 0 ) + ( fixInt128Lt( a, b ) ? 2 : 0 );
}

/*@ assigns \nothing; */
int proveUInt128Compare( fixUInt128 a, fixUInt128 b )
{
	return ( fixUInt128Gt( a, b ) ? 1 : 0 ) + ( fixUInt128Le( a, b ) ? 2 : 0 ) + ( fixUInt128Ge( a, b ) ? 4 : 0 ) +
		   ( fixUInt128Eq( a, b ) ? 8 : 0 );
}
