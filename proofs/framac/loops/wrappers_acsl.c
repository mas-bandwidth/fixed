// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Frama-C WP proof harness for the loops of fixISqrt128High and the emulated
// fixInt128Div path. It carries the loop annotations the library headers do not:
// loop invariant, loop assigns and loop variant for each loop of
// include/fixed/fixed.h:299-351 and include/fixed/fixed_int128.h:395-436.
//
// No library header is edited. The harness copies the two functions (and the two
// helpers the divide chain calls) into this file, unchanged except for the annotations,
// so the loops WP analyses are the same statements the library executes. This file is
// read by frama-c only: no build target, no CTest entry and no CI job compiles it.
//
// The contracts and invariants state only what the headers already document: the seed
// range is what FIX_SQRT_SEED's IEEE contract gives, the 64-bit repair keeps r at most
// UINT32_MAX (fixed.h says its square then fits uint64_t), the 104-bit repair keeps r
// at most 2^52 (the square root of 2^104, the branch bound), and the long-division
// loop's i stays in [-1, 127] so every lane shift is inside [0, 63].
//
// Built only on the emulated two-lane path: the variant of the shift-subtract loops is
// the unsigned 128-bit value fixed_int128.h:395 defines.

#include <stdint.h>

// The seed of fixISqrt128High. Frama-C has no contract for the gcc builtin, so this is
// the one assumption the proof takes, exactly as in proofs/framac/kernels_acsl.c: the
// IEEE square root of a finite non-negative double is finite, non-negative, and maps
// the two seed ranges the kernel feeds it (below 2^104 and below 2^64) to at most their
// square roots, 2^52 and 2^32. The seed VALUE is irrelevant to correctness; only its
// range matters.
/*@ requires \is_finite( x ) && x >= 0.0;
    assigns \result \from x;
    ensures \is_finite( \result ) && \result >= 0.0;
    ensures x <= 20282409603651670423947251286016.0 ==> \result <= 4503599627370496.0;
    ensures x <= 18446744073709551616.0 ==> \result <= 4294967296.0;
*/
double __builtin_sqrt( double x );

#include "fixed/fixed.h"

#if !FIX_INT128_EMULATED
#error "the loop harness is written for the emulated two-lane path"
#endif

// ---- fixISqrt128High (include/fixed/fixed.h:299) ---------------------------------

/*@ assigns \nothing;
  @ ensures hi == 0 && lo == 0 ==> \result == 0;
*/
uint64_t proveFixISqrt128High( uint64_t hi, uint64_t lo )
{
	// Up to 104 input bits the double seed has at most a few raw units of
	// error and fits uint64_t. Repair against the full integer input, exactly
	// as in the 64-bit arm. Lifted vector/quaternion norms land in this range.
	if ( hi != 0 && hi < ( UINT64_C( 1 ) << 40 ) )
	{
		fixUInt128 n = fixUInt128Make( hi, lo );
		uint64_t r = (uint64_t)FIX_SQRT_SEED( (double)hi * 18446744073709551616.0 + (double)lo );
		/*@ loop invariant r <= 4503599627370496;
		    loop assigns r;
		    loop variant r;
		*/
		while ( fixUInt128Gt( fixUInt128MulU64( r, r ), n ) ) { --r; }
		/*@ loop invariant r <= 4503599627370496;
		    loop assigns r;
		    loop variant 4503599627370496 - r;
		*/
		while ( fixUInt128Le( fixUInt128MulU64( r + 1, r + 1 ), n ) ) { ++r; }
		return r;
	}

	if ( hi == 0 )
	{
		// Common case: 64 bit input. Seed with the hardware double sqrt (an exact,
		// correctly rounded IEEE operation) and repair to the exact integer floor.
		// The result is exact on every platform, so determinism is preserved.
		if ( lo == 0 )
		{
			return 0;
		}

		uint64_t r = (uint64_t)FIX_SQRT_SEED( (double)lo );
		if ( r > 0xFFFFFFFFu )
		{
			r = 0xFFFFFFFFu;
		}
		// r <= UINT32_MAX, so its square fits uint64_t. After the downward
		// repair, lo-r*r is nonnegative. The next square is <= lo exactly
		// when that remainder exceeds 2*r; this avoids forming (r+1)^2,
		// which would overflow at r == UINT32_MAX. At that endpoint the
		// remainder is at most 2*r, so the upward repair cannot overflow r.
		/*@ loop invariant r <= 0xFFFFFFFF;
		    loop assigns r;
		    loop variant r;
		*/
		while ( r * r > lo )
		{
			r -= 1;
		}
		/*@ loop invariant r <= 0xFFFFFFFF && r * r <= lo;
		    loop assigns r;
		    loop variant 0xFFFFFFFF - r;
		*/
		while ( lo - r * r > 2 * r )
		{
			r += 1;
		}
		return r;
	}

	// Rare 128 bit case: restoring shift-subtract square root
	fixUInt128 n = fixUInt128Make( hi, lo );
	fixUInt128 x = n;
	fixUInt128 c = FIX_UINT128_ZERO;
	fixUInt128 d = fixUInt128Shl( fixUInt128FromU64( 1 ), 126 );
	/*@ loop invariant fixUInt128Val( d ) >= 0;
	    loop assigns d;
	    loop variant fixUInt128Val( d );
	*/
	while ( fixUInt128Gt( d, n ) )
	{
		d = fixUInt128Shr( d, 2 );
	}

	/*@ loop invariant fixUInt128Val( d ) >= 0;
	    loop assigns x, c, d;
	    loop variant fixUInt128Val( d );
	*/
	while ( !fixUInt128Eq( d, FIX_UINT128_ZERO ) )
	{
		fixUInt128 cd = fixUInt128Add( c, d );
		if ( fixUInt128Ge( x, cd ) )
		{
			x = fixUInt128Sub( x, cd );
			c = fixUInt128Add( fixUInt128Shr( c, 1 ), d );
		}
		else
		{
			c = fixUInt128Shr( c, 1 );
		}
		d = fixUInt128Shr( d, 2 );
	}

	return fixUInt128Lo( c );
}

// ---- the emulated 128-bit divide chain (include/fixed/fixed_int128.h:395) --------
//
// fixInt128Div on the emulated path is fixEmuInt128Div, which is fixEmuInt128DivMod,
// which is the shift-subtract loop of fixEmuUInt128DivMod. The copies below keep the
// bodies and annotate that loop. The only invariant it needs is the shift bound: the
// loop runs i from 127 down to -1, so the lane shifts are 0..63. The loop assigns
// leaves dividend, divisor and the out-pointers untouched, which is what lets the RTE
// goals of the writes after the loop stay provable.

FIX_ALWAYS_INLINE void proveEmuUInt128DivMod( fixEmuUInt128 dividend, fixEmuUInt128 divisor,
											  fixEmuUInt128* quotientOut, fixEmuUInt128* remainderOut )
{
	fixEmuUInt128 quotient = fixEmuUInt128Make( 0, 0 );
	fixEmuUInt128 remainder = fixEmuUInt128Make( 0, 0 );

	if ( divisor.hi == 0 && divisor.lo == 0 )
	{
		if ( quotientOut != NULL ) *quotientOut = quotient;
		if ( remainderOut != NULL ) *remainderOut = remainder;
		return;
	}

	if ( dividend.hi == 0 && divisor.hi == 0 )
	{
		quotient = fixEmuUInt128FromU64( dividend.lo / divisor.lo );
		remainder = fixEmuUInt128FromU64( dividend.lo % divisor.lo );
		if ( quotientOut != NULL ) *quotientOut = quotient;
		if ( remainderOut != NULL ) *remainderOut = remainder;
		return;
	}

	/*@ loop invariant -1 <= i <= 127;
	    loop assigns i, quotient, remainder;
	    loop variant i + 1;
	*/
	for ( int i = 127; i >= 0; i-- )
	{
		// The reference spells this ( dividend >> i ).lo & 1; selecting the lane directly
		// is the same bit for every i in [0, 127] and skips a 128-bit shift per iteration.
		uint64_t bit = ( i < 64 ) ? ( ( dividend.lo >> i ) & 1 ) : ( ( dividend.hi >> ( i - 64 ) ) & 1 );

		remainder = fixEmuUInt128Shl( remainder, 1 );
		remainder.lo |= bit;

		if ( fixEmuUInt128Ge( remainder, divisor ) )
		{
			remainder = fixEmuUInt128Sub( remainder, divisor );
			quotient = fixEmuUInt128Or( quotient, fixEmuUInt128Shl( fixEmuUInt128FromU64( 1 ), i ) );
		}
	}

	if ( quotientOut != NULL ) *quotientOut = quotient;
	if ( remainderOut != NULL ) *remainderOut = remainder;
}

FIX_ALWAYS_INLINE fixEmuUInt128 proveEmuUInt128Div( fixEmuUInt128 a, fixEmuUInt128 b )
{
	fixEmuUInt128 quotient;
	proveEmuUInt128DivMod( a, b, &quotient, NULL );
	return quotient;
}

FIX_ALWAYS_INLINE void proveEmuInt128DivMod( fixEmuInt128 dividend, fixEmuInt128 divisor,
											 fixEmuInt128* quotientOut, fixEmuInt128* remainderOut )
{
	const bool dividendNegative = fixEmuInt128IsNegative( dividend );
	const bool divisorNegative = fixEmuInt128IsNegative( divisor );

	fixEmuUInt128 dividendMagnitude = fixEmuToUnsigned( dividend );
	fixEmuUInt128 divisorMagnitude = fixEmuToUnsigned( divisor );
	if ( dividendNegative ) dividendMagnitude = fixEmuUInt128Neg( dividendMagnitude );
	if ( divisorNegative ) divisorMagnitude = fixEmuUInt128Neg( divisorMagnitude );

	fixEmuUInt128 quotient;
	fixEmuUInt128 remainder;
	proveEmuUInt128DivMod( dividendMagnitude, divisorMagnitude, &quotient, &remainder );

	if ( quotientOut != NULL )
	{
		*quotientOut = fixEmuToSigned( ( dividendNegative != divisorNegative ) ? fixEmuUInt128Neg( quotient ) : quotient );
	}
	if ( remainderOut != NULL )
	{
		*remainderOut = fixEmuToSigned( dividendNegative ? fixEmuUInt128Neg( remainder ) : remainder );
	}
}

FIX_ALWAYS_INLINE fixEmuInt128 proveEmuInt128Div( fixEmuInt128 a, fixEmuInt128 b )
{
	fixEmuInt128 quotient;
	proveEmuInt128DivMod( a, b, &quotient, NULL );
	return quotient;
}

// Division by zero is undefined on both arms (fixed_int128.h: "Every divide in this
// library guards its divisor first").
/*@ requires b.hi != 0 || b.lo != 0;
  @ assigns \nothing;
*/
fixInt128 proveInt128Div( fixInt128 a, fixInt128 b )
{
	return proveEmuInt128Div( a, b );
}
