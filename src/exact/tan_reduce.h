// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// TAN'S PAYNE-HANEK REDUCTION: 240 bits of 2/pi, integer only.
//
// Internal to src/exact/tan.c (include guard, static inline only, not installed). The
// method and the first two words are shared with fixed-sincos-exact; the extra words are
// tan's own because cot r near a pole needs 215 bits of 2/pi while sine and cosine need
// only the 112 in FIX_TWO_OVER_PI_HI:LO. The design (docs/design/transcendentals.md 3.1)
// needs 215 bits for relative precision 2^-120 at the saturation edge and carries 240,
// which also covers the 232 bits a floating-point tan would need at the closest input.
#ifndef FIX_TAN_REDUCE_H
#define FIX_TAN_REDUCE_H

#include "fixed/fixed_int256.h"

// floor(2^240 * 2/pi) as four 64-bit words, high first (sollya/tan_kernel.out):
//   W0 = 179192535600708          = FIX_TWO_OVER_PI_HI (0x0000A2F9836E4E44)
//   W1 = 1525027194748925236      = FIX_TWO_OVER_PI_LO (0x1529FC2757D1F534)
//   W2 = 15979012694373645379
//   W3 = 10394869040465829563
// The first two are the sincos reduction's constants, cited from src/fixed_math.c. W2 and
// W3 are the words tan adds; fixed-tan-negative-control overrides them with zero (the
// 112-bit sincos reduction) to prove this test catches the pole path.
#ifndef FIX_TAN_W0
#define FIX_TAN_W0 UINT64_C( 179192535600708 )
#endif
#ifndef FIX_TAN_W1
#define FIX_TAN_W1 UINT64_C( 1525027194748925236 )
#endif
#ifndef FIX_TAN_W2
#define FIX_TAN_W2 UINT64_C( 15979012694373645379 )
#endif
#ifndef FIX_TAN_W3
#define FIX_TAN_W3 UINT64_C( 10394869040465829563 )
#endif

// PIO2_127 = round(pi/2 * 2^127) = 267257146016241686964920093290467695825 (tan_kernel.out).
// The rounding of pi/2 leaves the product below 2^255.65, so R fits 128 bits while shifting
// by 127 would not.
#ifndef FIX_TAN_PIO2_127_HI
#define FIX_TAN_PIO2_127_HI UINT64_C( 0xc90fdaa22168c234 )
#endif
#ifndef FIX_TAN_PIO2_127_LO
#define FIX_TAN_PIO2_127_LO UINT64_C( 0xc4c6628b80dc1cd1 )
#endif

// ( a * b ) >> 128 from two fixUInt256MulU128ByU64 calls (design section 0): the same
// six-line product as the other exact headers, named for this file so it cannot collide.
static inline fixUInt128 fixTanReduceMulHi128( fixUInt128 a, fixUInt128 b )
{
	fixUInt256 low = fixUInt256MulU128ByU64( a, fixUInt128Lo( b ) );
	fixUInt256 high = fixUInt256MulU128ByU64( a, fixUInt128Hi( b ) );
	fixUInt256 sum = fixUInt256Add( low, fixUInt256Shl( high, 64 ) );
	return fixUInt256Shr( sum, 128 ).lo;
}

// Reduce m = |raw|, the magnitude of a Q48.16 angle, to a quarter and a signed fraction.
//
// q = m * 2^-16 * 2/pi; k = round(q) to the nearest integer (the nearest quarter turn),
// f = q - k in [-1/2, 1/2). The product t = m * (W0:W1:W2:W3) is summed exactly into five
// 64-bit words (no truncation), so q = t * 2^-256 and the fraction's top bit selects the
// rounding direction. The 2/pi truncation moves f by below 2^-193 at |m| < 2^64.
//
// Contract, for m >= 1:
//   *parityOut = k & 1;                    odd k means tan(x) = -cot(r)
//   *signOut   = f < 0 ? -1 : +1;          the sign of the reduced angle f * pi/2
//   *zOut      = leading zeros of |f|;     z <= 65 by tools/tan_reduction_bound.c
//   |f| = F * 2^-(128+z) with F in [2^127, 2^128) the 128 bits from the leading one
//   *rOut = R = ( F * PIO2_127 ) >> 128, so |r| = R * 2^(-127-z), r = f * pi/2
// R is in [2^126.65, 2^127.65): a 128-bit mantissa and an exponent rather than a fixed
// point, which is what keeps the relative error below 2^-125.7 all the way to the poles.
// The caller applies the input sign (tan(-x) = -tan(x)) and the odd-quarter cotangent.
//
// m == 0 returns parity 0, sign +1, R 0 and z 256; tan's caller handles zero exactly.
static inline void fixTanReduce( uint64_t m, int* parityOut, int* signOut, fixUInt128* rOut, int* zOut )
{
	if ( m == 0 )
	{
		*parityOut = 0;
		*signOut = 1;
		*rOut = FIX_UINT128_ZERO;
		*zOut = 256;
		return;
	}

	fixUInt128 p0 = fixUInt128MulU64( m, FIX_TAN_W3 );
	fixUInt128 p1 = fixUInt128MulU64( m, FIX_TAN_W2 );
	fixUInt128 p2 = fixUInt128MulU64( m, FIX_TAN_W1 );
	fixUInt128 p3 = fixUInt128MulU64( m, FIX_TAN_W0 );

	// Sum the four 64x64 -> 128 partial products into limbs 0..4, little endian.
	uint64_t limb0 = fixUInt128Lo( p0 );
	fixUInt128 sum = fixUInt128Add( fixUInt128FromU64( fixUInt128Hi( p0 ) ), fixUInt128FromU64( fixUInt128Lo( p1 ) ) );
	uint64_t limb1 = fixUInt128Lo( sum );
	fixUInt128 carry = fixUInt128FromU64( fixUInt128Hi( sum ) );

	sum = fixUInt128Add( fixUInt128Add( fixUInt128FromU64( fixUInt128Hi( p1 ) ), fixUInt128FromU64( fixUInt128Lo( p2 ) ) ),
						 carry );
	uint64_t limb2 = fixUInt128Lo( sum );
	carry = fixUInt128FromU64( fixUInt128Hi( sum ) );

	sum = fixUInt128Add( fixUInt128Add( fixUInt128FromU64( fixUInt128Hi( p2 ) ), fixUInt128FromU64( fixUInt128Lo( p3 ) ) ),
						 carry );
	uint64_t limb3 = fixUInt128Lo( sum );
	carry = fixUInt128FromU64( fixUInt128Hi( sum ) );

	uint64_t whole = fixUInt128Hi( p3 ) + fixUInt128Lo( carry );

	// t >> 256 is the quarter count; its parity and the fraction's top bit give k's parity.
	int roundUp = ( limb3 >> 63 ) != 0;
	*parityOut = (int)( ( whole + (uint64_t)roundUp ) & 1 );

	// The low 256 bits as the fraction; round to the nearest quarter by its top bit.
	fixUInt256 fraction;
	fraction.hi = fixUInt128Make( limb3, limb2 );
	fraction.lo = fixUInt128Make( limb1, limb0 );

	fixUInt256 magnitude;
	if ( roundUp )
	{
		magnitude = fixUInt256Neg( fraction );
		*signOut = -1;
	}
	else
	{
		magnitude = fraction;
		*signOut = 1;
	}

	uint64_t limbs[4];
	fixUInt256ToLimbs( magnitude, limbs );
	int z;
	if ( limbs[3] != 0 )
	{
		z = fixCountLeadingZeros64( limbs[3] );
	}
	else if ( limbs[2] != 0 )
	{
		z = 64 + fixCountLeadingZeros64( limbs[2] );
	}
	else if ( limbs[1] != 0 )
	{
		z = 128 + fixCountLeadingZeros64( limbs[1] );
	}
	else
	{
		z = 192 + fixCountLeadingZeros64( limbs[0] );
	}
	*zOut = z;

	// F = the 128 bits from the leading one. z <= 65 for m >= 1; the wider branch keeps the
	// shift defined if a caller ever violates that.
	fixUInt128 F;
	if ( z <= 128 )
	{
		F = fixUInt256Shr( magnitude, 128 - z ).lo;
	}
	else if ( z - 128 >= 128 )
	{
		F = FIX_UINT128_ZERO;
	}
	else
	{
		F = fixUInt256Shl( magnitude, z - 128 ).lo;
	}

	*rOut = fixTanReduceMulHi128( F, fixUInt128Make( FIX_TAN_PIO2_127_HI, FIX_TAN_PIO2_127_LO ) );
}

#endif
