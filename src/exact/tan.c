// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// tan, correctly rounded for every Q48.16 raw in [INT64_MIN, INT64_MAX].
//
// METHOD. tan is odd, so the magnitude is computed and the signs are applied at the end
// (the input's sign, the sign of the reduced fraction r, and the odd-quarter cotangent).
// fixTanReduce (src/exact/tan_reduce.h) is the Payne-Hanek reduction: it gives the
// quarter parity k & 1, the sign of r, the 128-bit mantissa R and the exponent z with
// |r| = R * 2^-(127+z), R in [2^126.65, 2^127.65), so |r| <= pi/4.
//
//   * Table path, |r| >= 1/256. r to Q0.128, j = round( r * 128 ) in 1..101, s = r - j/128,
//     t = tan(s) from fixTanKernel and TAN_j from the table in src/exact/tan_kernel.h.
//     tan r = (TAN_j + t) / (1 - TAN_j * t), cot r = the reciprocal, one fixUInt256DivMod
//     with the numerator shifted up 128. Odd quarters use cot.
//   * Pole-and-zero path, |r| < 1/256. tan r = r * (1 + w), w = r^2 * q(r^2) in Q0.127;
//     odd quarters give y = 2^16 / ( r * (1 + w) ) via D = ( R * ( 2^127 + w ) ) >> 127
//     and 2^255 / D, kept in fixUInt256 until the exponent shift. |r| < 2^-48 saturates
//     without dividing, and a computed |Y| >= 2^63 - 3/2 saturates.
//
// One final round to nearest. The budgets (docs/design/transcendentals.md, 3.4) are
// eps <= 2^-91.6 raw on the table path (|y| <= 2^24) and eps <= 2^-61 raw on the pole
// path, against the exhaustive margin 2^-22.24 raw below 32 rad and every BaCSeL k <= 60.
// The 2/pi carries 240 bits, which is 215 needed at the saturation edge plus slack.
//
// Integer only, through the fixInt128/fixUInt128/fixUInt256 vocabulary, so the native
// and emulated 128-bit arms, gcc and clang, produce identical bits. No gcc builtin
// outside the library's wrappers and no variable-length array: plain MSVC builds it.
#include "fixed/fixed_tan.h"
#include "fixed/fixed_int128.h"
#include "fixed/fixed_int256.h"

#include "tan_kernel.h"
#include "tan_reduce.h"

#include <stdint.h>

// w = r^2 * q(r^2) in Q0.127, the kernel's own c term, from the committed coefficients.
// rq is |r| in Q0.128 (|r| < 1/256), so rq^2 is Q0.128 and every product truncates by
// less than one unit of Q0.127; the design budgets the Horner for w at 2^-126 absolute
// on 1 + w, which is what the pole path divides by.
static fixUInt128 fixTanExactPoleW( fixUInt128 rq )
{
	fixUInt128 s2 = fixTanKernelMulHi128( rq, rq );
	fixUInt128 w = fixTanKernelCoefficient( 13 );
	for ( int k = 11; k >= 3; k -= 2 )
	{
		w = fixUInt128Add( fixTanKernelCoefficient( k ), fixTanKernelMulHi128( w, s2 ) );
	}
	return fixTanKernelMulHi128( s2, w );
}

// round( value / 2^112 ) for a value below 2^121: the final Q0.128 -> Q48.16 rounding for
// the small (even-quarter) results. A computed tie cannot occur where the margin holds.
static uint64_t fixTanExactRoundMagnitude256( fixUInt256 value )
{
	fixUInt256 half = fixUInt256FromU128( fixUInt128Shl( fixUInt128FromU64( 1 ), 111 ) );
	return fixUInt128Lo( fixUInt256Shr( fixUInt256Add( value, half ), 112 ).lo );
}

// round( quotient * 2^(z-112) ) for the pole path's 2^255 / D, kept at 256 bits; returns
// false when the magnitude reaches INT64_MAX or beyond, which saturates.
static bool fixTanExactPoleMagnitude( fixUInt256 quotient, int z, uint64_t* magnitudeOut )
{
	int shift = 112 - z;
	fixUInt128 half = fixUInt128Shl( fixUInt128FromU64( 1 ), shift - 1 );
	fixUInt256 rounded = fixUInt256Shr( fixUInt256Add( quotient, fixUInt256FromU128( half ) ), shift );
	if ( !fixUInt128Eq( rounded.hi, FIX_UINT128_ZERO ) || fixUInt128Lo( rounded.lo ) >= (uint64_t)INT64_MAX )
	{
		return false;
	}
	*magnitudeOut = fixUInt128Lo( rounded.lo );
	return true;
}

FIX_API fixed_t fixTanExact( fixed_t x )
{
	if ( x == 0 )
	{
		return 0;
	}

	uint64_t m = x < 0 ? (uint64_t)0 - (uint64_t)x : (uint64_t)x;
	int inputSign = x < 0 ? -1 : 1;

	int parity;
	int fractionSign;
	fixUInt128 R;
	int z;
	fixTanReduce( m, &parity, &fractionSign, &R, &z );

	int sign = inputSign * fractionSign * ( parity != 0 ? -1 : 1 );

	// |r| in Q0.128: |r| = R * 2^-(127+z), so |r| * 2^128 = R >> (z-1). z >= 1 for m >= 1.
	fixUInt128 r128 = z > 1 ? fixUInt128Shr( R, z - 1 ) : R;

	// The same switch the design states: |r| >= 1/256 is the table path, below it the
	// pole-and-zero path. 1/256 in Q0.128 is 2^120.
	fixUInt128 tableThreshold = fixUInt128Shl( fixUInt128FromU64( 1 ), 120 );

	if ( fixUInt128Ge( r128, tableThreshold ) )
	{
		// j = round( |r| * 128 ), in 1..101. The Q0.128 truncation is 2^-249 in r * 128.
		uint64_t j = (uint64_t)fixUInt128Lo( fixUInt128Shr(
			fixUInt128Add( r128, fixUInt128Shl( fixUInt128FromU64( 1 ), 120 ) ), 121 ) );

		// s = |r| - j/128 in signed Q0.128; form the magnitude first so neither operand
		// has to be a signed 128-bit value above 2^127.
		fixUInt128 jShift = fixUInt128Shl( fixUInt128FromU64( j ), 121 );
		bool sNegative = fixUInt128Lt( r128, jShift );
		fixUInt128 sMagnitude = sNegative ? fixUInt128Sub( jShift, r128 ) : fixUInt128Sub( r128, jShift );
		fixInt128 s = fixInt128FromUnsigned( sMagnitude );
		if ( sNegative )
		{
			s = fixInt128Neg( s );
		}

		fixInt128 t = fixTanKernel( s );
		bool tNegative = fixInt128IsNegative( t );
		fixUInt128 tMagnitude = fixInt128ToUnsigned( t );
		if ( tNegative )
		{
			tMagnitude = fixUInt128Neg( tMagnitude );
		}

		// A = TAN_j and b = t in Q0.127. A * b in Q0.127 is mulhi( A, |b| ) << 1, below
		// 2^119 for every cell; N = A + b and D = 1 - A*b both stay below 2^128.
		fixUInt128 A = fixTanKernelTable( (int)j );
		fixUInt128 b = fixUInt128Shr( tMagnitude, 1 );
		fixUInt128 product = fixUInt128Shl( fixTanKernelMulHi128( A, b ), 1 );
		fixUInt128 oneHalf = fixUInt128Shl( fixUInt128FromU64( 1 ), 127 );

		fixUInt128 numerator128;
		fixUInt128 denominator128;
		if ( tNegative )
		{
			numerator128 = fixUInt128Sub( A, b );
			denominator128 = fixUInt128Add( oneHalf, product );
		}
		else
		{
			numerator128 = fixUInt128Add( A, b );
			denominator128 = fixUInt128Sub( oneHalf, product );
		}

		// tan r = N/D, cot r = D/N. Shift the numerator up 128 for the single division.
		fixUInt128 top = parity != 0 ? denominator128 : numerator128;
		fixUInt128 bottom = parity != 0 ? numerator128 : denominator128;

		fixUInt256 dividend;
		dividend.hi = top;
		dividend.lo = FIX_UINT128_ZERO;
		fixUInt256 quotient;
		fixUInt256 remainder;
		fixUInt256DivMod( dividend, fixUInt256FromU128( bottom ), &quotient, &remainder );

		// quotient is tan r * 2^128; one final round to nearest in raw units.
		uint64_t magnitude = fixTanExactRoundMagnitude256( quotient );
		if ( magnitude == 0 )
		{
			return 0;
		}
		return sign > 0 ? (fixed_t)magnitude : -(fixed_t)magnitude;
	}

	// Pole-and-zero path, |r| < 1/256.
	if ( parity == 0 )
	{
		fixInt128 t = fixTanKernel( fixInt128FromUnsigned( r128 ) );
		uint64_t magnitude = fixTanExactRoundMagnitude256( fixUInt256FromU128( fixInt128ToUnsigned( t ) ) );
		if ( magnitude == 0 )
		{
			return 0;
		}
		return sign > 0 ? (fixed_t)magnitude : -(fixed_t)magnitude;
	}

	// Odd quarter: y = 2^16 / ( r * (1 + w) ). |r| < 2^-48 saturates without dividing,
	// because then |y| > 2^64. |r| < 2^-48 is R < 2^(79+z); z >= 49 always satisfies it.
	bool saturate = z >= 49;
	if ( !saturate )
	{
		fixUInt128 limit = fixUInt128Shl( fixUInt128FromU64( 1 ), 79 + z );
		saturate = fixUInt128Lt( R, limit );
	}
	if ( saturate )
	{
		return sign > 0 ? INT64_MAX : -INT64_MAX;
	}

	fixUInt128 w = fixTanExactPoleW( r128 );
	fixUInt128 X = fixUInt128Add( fixUInt128Shl( fixUInt128FromU64( 1 ), 127 ), w );
	fixUInt128 D = fixUInt128Shl( fixTanKernelMulHi128( R, X ), 1 );

	// 2^255 / D, kept in 256 bits: the quotient is near 2^128, not an overflow.
	fixUInt256 dividend;
	dividend.hi = fixUInt128Shl( fixUInt128FromU64( 1 ), 127 );
	dividend.lo = FIX_UINT128_ZERO;
	fixUInt256 quotient;
	fixUInt256 remainder;
	fixUInt256DivMod( dividend, fixUInt256FromU128( D ), &quotient, &remainder );

	uint64_t magnitude;
	if ( !fixTanExactPoleMagnitude( quotient, z, &magnitude ) )
	{
		return sign > 0 ? INT64_MAX : -INT64_MAX;
	}
	if ( magnitude == 0 )
	{
		return 0;
	}
	return sign > 0 ? (fixed_t)magnitude : -(fixed_t)magnitude;
}
