// SPDX-FileCopyrightText: 2025-2026 Erin Catto -- derived from Box3D (https://github.com/erincatto/box3d)
// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC -- fixed-point conversion
// SPDX-License-Identifier: MIT
#include "fixed/fixed_math.h"
// For FIX_PI, which the smoothing dynamics need. The vector header defines it and
// includes this one, so the dependency only exists in this translation unit.
#include "fixed/fixed_vec.h"

// Index of the highest set bit of a NON-ZERO 64-bit value. gcc and clang have
// __builtin_clzll; plain MSVC has _BitScanReverse64. Both compile to one instruction, and
// the callers below all guard against zero, where the answer is undefined for either.
#if defined( _MSC_VER ) && !defined( __clang__ )
	#include <intrin.h>
static inline int fixMostSignificantBit64( uint64_t v )
{
	unsigned long index;
	_BitScanReverse64( &index, v );
	return (int)index;
}
#else
static inline int fixMostSignificantBit64( uint64_t v )
{
	return 63 - __builtin_clzll( v );
}
#endif

static inline int64_t fixQ32Mul( int64_t a, int64_t b )
{
	return fixInt128ToI64( fixInt128Shr( fixInt128MulI64( a, b ), 32 ) );
}

static inline int64_t fixQ32Div( int64_t a, int64_t b )
{
	return fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( fixInt128FromI64( a ), 32 ), fixInt128FromI64( b ) ) );
}


static inline int64_t fixQ32Sqrt( int64_t a )
{
	if ( a <= 0 )
	{
		return 0;
	}
	uint64_t hi = (uint64_t)a >> 32;
	uint64_t lo = (uint64_t)a << 32;
	return (int64_t)fixISqrt128High( hi, lo );
}

// Round a Q32.32 value to Q48.16
static inline fixed_t fixQ32ToFix( int64_t a )
{
	return ( a + ( (int64_t)1 << 15 ) ) >> 16;
}

#define FIX_Q32_PI 13493037705LL	   // round(pi * 2^32)
#define FIX_Q32_HALF_PI 6746518852LL // round(pi/2 * 2^32)

#define FIX_Q62_HALF_PI 7244019458077122842LL // round(pi/2 * 2^62)

// Cosine and sine of quadrant * pi/2 + r62 * 2^-62, for |r62 * 2^-62| <= pi/4 (a hair past is
// fine): degree 11/12 Taylor polynomials in Q32.32, then the quadrant's signs and swap. Shared by
// fixComputeCosSinPrecise and fixComputeCosSinExact, which differ only in the reduction.
static fixCosSin fixCosSinOfQuadrant( int64_t r62, int quadrant )
{
	int64_t r = ( r62 + ( (int64_t)1 << 29 ) ) >> 30;
	int64_t r2 = fixQ32Mul( r, r );

	// Taylor coefficients 1/n! in Q32.32. Truncation error below 1e-11 for |r| <= pi/4.
	int64_t s = 108;                         // 1/11!
	s = 11836 - fixQ32Mul( r2, s );          // 1/9!
	s = 852176 - fixQ32Mul( r2, s );         // 1/7!
	s = 35791394 - fixQ32Mul( r2, s );       // 1/5!
	s = 715827883 - fixQ32Mul( r2, s );      // 1/3!
	s = ( (int64_t)1 << 32 ) - fixQ32Mul( r2, s );
	s = fixQ32Mul( r, s );

	int64_t c = 9;                           // 1/12!
	c = 1184 - fixQ32Mul( r2, c );           // 1/10!
	c = 106522 - fixQ32Mul( r2, c );         // 1/8!
	c = 5965232 - fixQ32Mul( r2, c );        // 1/6!
	c = 178956971 - fixQ32Mul( r2, c );      // 1/4!
	c = 2147483648LL - fixQ32Mul( r2, c );   // 1/2!
	c = ( (int64_t)1 << 32 ) - fixQ32Mul( r2, c );

	fixed_t sr = fixQ32ToFix( s ), cr = fixQ32ToFix( c );
	fixCosSin out;
	switch ( quadrant )
	{
		case 0:
			out.cosine = cr;
			out.sine = sr;
			break;
		case 1:
			out.cosine = -sr;
			out.sine = cr;
			break;
		case 2:
			out.cosine = -cr;
			out.sine = -sr;
			break;
		default:
			out.cosine = sr;
			out.sine = -cr;
			break;
	}
	return out;
}

fixCosSin fixComputeCosSinPrecise( fixed_t radians )
{
	// The angle in Q2.62 at 128 bits is exact: radians * 2^46. The reduction below is not quite:
	// FIX_Q62_HALF_PI is pi/2 rounded to 62 fraction bits, so r is off by k times its rounding
	// (0.38 Q2.62 units), up to 0.49 ulp of Q48.16 at |radians| near 2^47 (fixed_math.h).
	fixInt128 x = fixInt128ShiftLeft( fixInt128FromI64( radians ), 46 );
	fixInt128 halfPi = fixInt128FromI64( FIX_Q62_HALF_PI );

	// Nearest quadrant k = round( x / (pi/2) ), ties away from zero; |k| < 2^47
	fixInt128 bias = fixInt128FromI64( FIX_Q62_HALF_PI / 2 );
	fixInt128 numerator = fixInt128IsNegative( x ) ? fixInt128Sub( x, bias ) : fixInt128Add( x, bias );
	fixInt128 k = fixInt128Div( numerator, halfPi );

	// Remainder |r| <= pi/4 in Q2.62 (fits 64 bits)
	int64_t r62 = fixInt128ToI64( fixInt128Sub( x, fixInt128Mul( k, halfPi ) ) );
	int quadrant = (int)( fixInt128ToI64( k ) & 3 ); // two's complement: k mod 4 for negative k too
	return fixCosSinOfQuadrant( r62, quadrant );
}

// 2^113 / pi, truncated: the 112 leading bits of 2/pi (0.A2F9836E4E441529FC2757D1F534DDC0...
// in hex) as two words. Checked with Python's decimal at 120 digits (pi by Machin) and bc -l:
//   floor( 2^113 / pi ) = 3305518844145349671841498641069364 = 0xA2F9836E4E44_1529FC2757D1F534
#define FIX_TWO_OVER_PI_HI UINT64_C( 0x0000A2F9836E4E44 )
#define FIX_TWO_OVER_PI_LO UINT64_C( 0x1529FC2757D1F534 )

fixCosSin fixComputeCosSinExact( fixed_t radians )
{
	// THE REDUCTION, integer only, for every int64 input including INT64_MIN.
	//
	// With m = |radians| (raw, as uint64, so 2^63 is fine) the angle is m * 2^-16 rad, and its
	// count of quarter turns is q = m * 2^-16 * 2/pi = m * C * 2^-128 with C = 2^113 / pi.
	// C is held truncated to 112 bits, C = HI * 2^64 + LO + c with 0 <= c < 1, so
	//   m * C = ( m * HI ) * 2^64 + m * LO + m * c
	// t = m * HI + floor( m * LO / 2^64 ) is that sum in units of 2^-64 quarter turns, short of the
	// truth by m * c / 2^64 + frac( m * LO / 2^64 ) < 1/2 + 1 < 2 units. It fits 128 bits
	// (m * HI < 2^63 * 2^48 = 2^111). Its high word is the whole quarter-turn count, its low
	// word F the fraction in units of 2^-64. Rounding to the nearest quarter, k = high + F / 2^63
	// and the signed fraction is f = F as int64 in [-2^63, 2^63), so q = k + f * 2^-64 exactly up
	// to the < 2 units above.
	//
	// So the reduced angle r = f * 2^-64 * pi/2 is in error by < 2 * 2^-64 * pi/2 < 2^-62 rad at
	// EVERY input, with no growth in the angle (the error does not
	// scale with k, unlike a reduction by a rounded pi/2). A true fraction within 2 units of a
	// half (r = +-pi/4) can land in the neighbouring quadrant, which is the same angle written
	// the other way: both quadrants hold it to the same error.
	//
	// r in Q2.62 is ( f * round( pi/2 * 2^62 ) ) >> 64, |r| <= pi/4, off by a further 1.2 * 2^-62
	// rad at most from the rounding of pi/2 (0.38 units, times |f| / 2^64 <= 1/2) and the floor of
	// the shift (1 unit). In all under 2^-61 rad, 2^-45 of one Q48.16 unit. The polynomials of
	// fixCosSinOfQuadrant then carry it to Q48.16.
	//
	// The bound is 2 raw Q48.16 units of the correctly rounded cosine and sine at every angle
	// (measured maxulp 1), checked against CORE-MATH at the pin in test/sincos_exact_test.c, on
	// the native and the emulated 128-bit arms.
	uint64_t m = radians < 0 ? -(uint64_t)radians : (uint64_t)radians;
	fixUInt128 t = fixUInt128Add( fixUInt128MulU64( m, FIX_TWO_OVER_PI_HI ),
								  fixUInt128Shr( fixUInt128MulU64( m, FIX_TWO_OVER_PI_LO ), 64 ) );
	uint64_t whole = fixUInt128Hi( t );
	uint64_t fraction = fixUInt128Lo( t );
	int quadrant = (int)( ( whole + ( fraction >> 63 ) ) & 3 );

	// Two's complement: the fraction as int64 is F - 2^64 when F >= 2^63, the remainder from the
	// next quarter up. Through the unsigned subtraction so the conversion is the plain one.
	int64_t f = fraction >= ( UINT64_C( 1 ) << 63 ) ? -(int64_t)( ~fraction ) - 1 : (int64_t)fraction;
	int64_t r62 = fixInt128ToI64( fixInt128Shr( fixInt128MulI64( f, FIX_Q62_HALF_PI ), 64 ) );

	fixCosSin out = fixCosSinOfQuadrant( r62, quadrant );
	if ( radians < 0 )
	{
		out.sine = -out.sine; // sin( -x ) = -sin( x ), cos( -x ) = cos( x )
	}
	return out;
}

fixed_t fixAtan2( fixed_t y, fixed_t x )
{
	// Added check for (0,0) to match atan2f and avoid a zero divide
	if ( x == 0 && y == 0 )
	{
		return 0;
	}

	fixed_t ax = fixAbs( x );
	fixed_t ay = fixAbs( y );
	fixed_t mx = fixMax( ay, ax );
	fixed_t mn = fixMin( ay, ax );

	// a = mn / mx in [0, 1], evaluated in Q32.32. mn >= 0 so the raw shift
	// would be fine, but the helper is the convention for signed shifts.
	int64_t a = fixQ32Div( mn, mx );

	// Minimax polynomial approximation to atan(a) on [0,1]
	int64_t s = fixQ32Mul( a, a );
	int64_t c = fixQ32Mul( s, a );
	int64_t q = fixQ32Mul( s, s );
	int64_t r = fixQ32Mul( 106688212LL, q ) + 802360794LL;	  // 0.024840285, 0.18681418
	int64_t t = fixQ32Mul( -404147609LL, q ) - 1426490580LL; // -0.094097948, -0.33213072
	r = fixQ32Mul( r, s ) + t;
	r = fixQ32Mul( r, c ) + a;

	// Map to full circle
	if ( ay > ax )
	{
		r = FIX_Q32_HALF_PI - r;
	}

	if ( x < 0 )
	{
		r = FIX_Q32_PI - r;
	}

	if ( y < 0 )
	{
		r = -r;
	}

	return fixQ32ToFix( r );
}

// ---------------------------------------------------------------------------------------------
// fixAtan2Exact and the wide atan2 under the exact quaternion angles
// ---------------------------------------------------------------------------------------------
//
// DESIGN. Everything is integer, through the 128-bit seam and the 256-bit layer, so the native and
// the emulated arms run the same arithmetic. fixAtan2 and the four quaternion angles built on it
// are frozen and untouched; this is new code beside them.
//
// 1. The ratio. With the magnitudes ax = |x| and ay = |y| (INT64_MIN taken as -INT64_MAX, so its
//    magnitude is INT64_MAX), mx = max( ax, ay ) and mn = min( ax, ay ), the ratio a = mn / mx in
//    [0, 1] is held in Q2.62 at 128 bits: a62 = floor( mn * 2^62 / mx ), one 128/64 divide, never
//    rounded to Q32 as fixAtan2 does. Its error is under 2^-62.
//    The quaternion angles take their lengths from exact 128-bit sums of squares (fixDotRaw), never
//    from the floored fixLength: fixDoubleAtan2ExactOfSquares takes the squared magnitudes, forms
//    r = floor( mn2 * 2^124 / mx2 ) at 256 bits (r is the squared ratio in Q4.124, exact to 2^-124)
//    and a62 = floor( sqrt( r ) ) with the exact integer square root fixISqrt128High. The ratio is
//    then within 2^-62 + sqrt( 2^-124 ) = 2^-61 of the true mn / mx, about 2^-45 of one raw unit, so
//    no floored length enters (the floor costs far under 1/8 raw).
//
// 2. No argument reduction: one polynomial on the whole of [0, 1]. atan( a ) = a * h( a ) with h of
//    degree 20 in a (all powers), the Chebyshev interpolant of atan( a ) / a at 21 nodes on [0, 1],
//    computed at 90 decimal digits. The singularities of atan( a ) / a are at +-i, outside the
//    Bernstein ellipse of [0, 1] with rho = 4.6, so degree 20 gives |a h( a ) - atan( a )| < 2^-52
//    on [0, 1] (measured 2^-52.1 on a 1001-point grid at 90 digits). The coefficients are stored
//    rounded to Q1.62 (each off by under 2^-63, under 21 * 2^-63 < 2^-58 in all) and evaluated by
//    Horner in a in signed Q7.120 at 128 bits, every product the exact 192-bit product of the
//    128-bit magnitude and a62 shifted down 62 (truncated: 2^-120 per step, 22 steps). Every
//    partial sum of the Horner chain is below 4 in magnitude, so |b| < 2^122.
//    P( a62 ) = a * h( a ) is then taken to Q.64 radians, truncated. Error of the wide value
//    against atan( a ) for the computed a: under 2^-52 + 2^-58 + 2^-112 + 2^-64 < 2^-51 rad.
//
// 3. The octant mapping, in the wide format (Q.64 radians, unsigned magnitude plus a sign):
//    p = P( a62 ); if ay > ax, p = pi/2 - p; if x < 0, p = pi - p; the sign of y is the sign of
//    the result. pi/2 is round( pi/2 * 2^64 ) (off by 0.47 * 2^-64) and pi is exactly twice it,
//    so pi - ( pi/2 - p ) = pi/2 + p bit for bit and the two halves of the x = 0 axis agree.
//    fixAtan2Exact( 0, 0 ) is 0. y = 0 with x < 0 is +pi, as atan2( +0, x < 0 ) is.
//
// 4. One final rounding, round half away from zero on the magnitude: ( |w| + 2^47 ) >> 48 for the
//    angle in Q48.16, ( |w| + 2^46 ) >> 47 for twice the angle. ERROR BUDGET for fixAtan2Exact:
//    the wide value is within 2^-61 (ratio, atan' <= 1) + 2^-51 (polynomial) + 2^-64 (pi/2) of
//    atan2( y, x ) for the policy-adjusted inputs, under 2^-35 raw units; after one rounding the
//    result is within 1/2 + 2^-35 raw of the true value, so it is the correctly rounded value or
//    its neighbour on the side the true value leans: within 1 raw of correctly rounded at every
//    input (bound 1, measured in test/quat_angles_exact_test.c). The doubled angles carry twice
//    the wide error, under 2^-34 raw, and are within 1 raw of correctly rounded 2 * atan2 too
//    (bound 2 in the test).
//
// MONOTONICITY (why the final rounding cannot reverse). Within one octant the wide value is a
// monotone function of each input: a62 = floor( mn * 2^62 / mx ) is non-decreasing in mn and
// non-increasing in mx; the computed P is strictly increasing in a62 because one step of a62 moves
// the polynomial a * h( a ) by at least 2^-62 * min P' = 2^-63 (P' >= 0.5 on [0, 1], its minimum at
// a = 1 where atan' = 1/2, checked on a 100001-point grid), while the evaluation noise of the
// Horner chain is under 2^-112, far less than a step; the Q.64 truncation
// and the octant maps (p, pi/2 - p, pi - p, -p, each exact, slope +-1) preserve order. Round half
// away from zero on a signed value is a non-decreasing function. So no reversal inside an octant.
// At an octant seam (ax = ay, or an axis) the true angle is a multiple of pi/4; the wide values on
// either side are within 2^-51 rad of the seam value or ordered past it, and every multiple of pi/4
// lies at least 0.06 raw from a rounding boundary (pi/4 * 2^16 = 51471.854, 3 pi/4 * 2^16 =
// 154415.562, pi/2 * 2^16 = 102943.708, pi * 2^16 = 205887.416, 0 exactly), so the values within
// 2^-35 raw of a seam all round to the seam's integer and the order holds across it. The only
// place the result goes down where the true atan2 goes up is the branch cut at y = 0, x < 0 (pi to
// -pi), where the true atan2 jumps the same way. The test counts reversals on every row and column
// of the raws in [-1024, 1024)^2 and requires 0.

// h( a ) = atan( a ) / a = sum c[k] a^k on [0, 1], degree 20, Q1.62 (value * 2^62, rounded).
// Generated by Chebyshev interpolation at 90 digits (see DESIGN above).
static const int64_t fixAtanExactH[21] = {
	INT64_C( 4611686018427383805 ),	  INT64_C( 3618649 ),			   INT64_C( -1537228673342627013 ),
	INT64_C( 31375458015 ),			  INT64_C( 922336222561954965 ),   INT64_C( 18852904719459 ),
	INT64_C( -659054929339319031 ),	  INT64_C( 2212055260543836 ),	   INT64_C( 497569987467941582 ),
	INT64_C( 75205626934639447 ),	  INT64_C( -712283520511835208 ),  INT64_C( 887133533414868761 ),
	INT64_C( -1739542595242347461 ),  INT64_C( 3840198338570828549 ),  INT64_C( -5699403533313715536 ),
	INT64_C( 5619654850244462402 ),	  INT64_C( -3804764586762214013 ), INT64_C( 1774454945956014247 ),
	INT64_C( -550190038520872054 ),	  INT64_C( 102811041878999994 ),   INT64_C( -8803898929941731 ),
};

// round( pi/2 * 2^64 ) = 0x1_921FB54442D1846A (pi/2 * 2^64 = ...846A - 0.463), checked at 90
// digits. pi in the same format is exactly twice it.
#define FIX_W64_HALF_PI_HI UINT64_C( 0x1 )
#define FIX_W64_HALF_PI_LO UINT64_C( 0x921FB54442D1846A )

// b * a62 * 2^-62 for signed Q7.120 b: the exact 192-bit product of the magnitude, shifted down 62
// and truncated toward zero. With m = H * 2^64 + L, ( m * a62 ) >> 62 = H * a62 * 4 + ( L * a62 ) >> 62
// exactly (the low part's dropped bits are all below 2^62). |b| < 2^126 and a62 <= 2^62, so
// H * a62 < 2^124 and the result fits 128 bits.
static inline fixInt128 fixAtanExactMulQ62( fixInt128 b, uint64_t a62 )
{
	bool negative = fixInt128IsNegative( b );
	fixUInt128 m = fixInt128ToUnsigned( negative ? fixInt128Neg( b ) : b );
	fixUInt128 high = fixUInt128Shl( fixUInt128MulU64( fixUInt128Hi( m ), a62 ), 2 );
	fixUInt128 low = fixUInt128Shr( fixUInt128MulU64( fixUInt128Lo( m ), a62 ), 62 );
	fixInt128 r = fixInt128FromUnsigned( fixUInt128Add( high, low ) );
	return negative ? fixInt128Neg( r ) : r;
}

// atan( a62 * 2^-62 ) for a62 in [0, 2^62], in Q.64 radians (at most pi/4 * 2^64 < 2^64).
static uint64_t fixAtanExactUnit( uint64_t a62 )
{
	fixInt128 b = fixInt128ShiftLeft( fixInt128FromI64( fixAtanExactH[20] ), 58 );
	for ( int k = 19; k >= 0; --k )
	{
		b = fixInt128Add( fixAtanExactMulQ62( b, a62 ), fixInt128ShiftLeft( fixInt128FromI64( fixAtanExactH[k] ), 58 ) );
	}
	b = fixAtanExactMulQ62( b, a62 ); // a * h( a ) in Q.120, >= 0
	if ( fixInt128IsNegative( b ) )
	{
		return 0;
	}
	return fixUInt128Lo( fixUInt128Shr( fixInt128ToUnsigned( b ), 56 ) );
}

// THE WIDE ENTRY POINT: the angle before rounding, as a Q.64 magnitude in radians, for the ratio
// a62 = min/max of the magnitudes in Q2.62, steep when |y| > |x|, and the signs of x and y (the
// result's sign is yNegative). At most pi * 2^64 < 2^66.
static fixUInt128 fixAtan2ExactWide( uint64_t a62, bool steep, bool xNegative )
{
	fixUInt128 halfPi = fixUInt128Make( FIX_W64_HALF_PI_HI, FIX_W64_HALF_PI_LO );
	fixUInt128 w = fixUInt128FromU64( fixAtanExactUnit( a62 ) );
	if ( steep )
	{
		w = fixUInt128Sub( halfPi, w );
	}
	if ( xNegative )
	{
		w = fixUInt128Sub( fixUInt128Shl( halfPi, 1 ), w );
	}
	return w;
}

// One rounding of a wide magnitude to Q48.16 after dropping `shift` bits (48 for the angle, 47 for
// twice it), half away from zero, then the sign.
static fixed_t fixAtan2ExactRound( fixUInt128 w, bool negative, int shift )
{
	fixUInt128 r = fixUInt128Shr( fixUInt128Add( w, fixUInt128Shl( fixUInt128FromU64( 1 ), shift - 1 ) ), shift );
	int64_t v = (int64_t)fixUInt128Lo( r );
	return negative ? -v : v;
}

static inline uint64_t fixAtan2ExactMagnitude( fixed_t v )
{
	// INT64_MIN policy: taken as -INT64_MAX, so its magnitude is INT64_MAX.
	if ( v == INT64_MIN )
	{
		return (uint64_t)INT64_MAX;
	}
	return v < 0 ? -(uint64_t)v : (uint64_t)v;
}

fixed_t fixAtan2Exact( fixed_t y, fixed_t x )
{
	// Within 1 raw of the correctly rounded atan2 at every pair and monotone (DESIGN above);
	// checked against CORE-MATH cr_atan2 at the pin in test/quat_angles_exact_test.c.
	if ( x == 0 && y == 0 )
	{
		return 0;
	}
	uint64_t ax = fixAtan2ExactMagnitude( x );
	uint64_t ay = fixAtan2ExactMagnitude( y );
	bool steep = ay > ax;
	uint64_t mx = steep ? ay : ax;
	uint64_t mn = steep ? ax : ay;

	// mn <= mx, so the high word of mn * 2^62 (mn >> 2) is below mx: the quotient fits 64 bits.
	uint64_t remainder;
	uint64_t a62 = fixUInt128DivRemBy64( fixUInt128Shl( fixUInt128FromU64( mn ), 62 ), mx, &remainder );
	(void)remainder;
	return fixAtan2ExactRound( fixAtan2ExactWide( a62, steep, x < 0 ), y < 0, 48 );
}

fixed_t fixDoubleAtan2ExactOfSquares( fixUInt128 ySquared, bool yNegative, fixUInt128 xSquared, bool xNegative )
{
	// 2 * atan2( +-sqrt( ySquared ), +-sqrt( xSquared ) ), rounded once: within 1 raw of the
	// correctly rounded value (DESIGN above), bound 2 in test/quat_angles_exact_test.c.
	bool yZero = fixUInt128Eq( ySquared, FIX_UINT128_ZERO );
	bool xZero = fixUInt128Eq( xSquared, FIX_UINT128_ZERO );
	if ( yZero && xZero )
	{
		return 0;
	}
	if ( yZero )
	{
		yNegative = false; // atan2( +-0, x ): the sign of a zero length is +
	}
	if ( xZero )
	{
		xNegative = false;
	}
	bool steep = fixUInt128Gt( ySquared, xSquared );
	fixUInt128 mx2 = steep ? ySquared : xSquared;
	fixUInt128 mn2 = steep ? xSquared : ySquared;

	// r = floor( mn2 * 2^124 / mx2 ) <= 2^124, the squared ratio in Q4.124; a62 = floor( sqrt( r ) ).
	fixUInt256 quotient, remainder;
	fixUInt256DivMod( fixUInt256Shl( fixUInt256FromU128( mn2 ), 124 ), fixUInt256FromU128( mx2 ), &quotient, &remainder );
	uint64_t a62 = fixISqrt128High( fixUInt128Hi( quotient.lo ), fixUInt128Lo( quotient.lo ) );
	return fixAtan2ExactRound( fixAtan2ExactWide( a62, steep, xNegative ), yNegative, 47 );
}

// Approximate cosine and sine for determinism, evaluated in pure integer math.
// https://en.wikipedia.org/wiki/Bh%C4%81skara_I%27s_sine_approximation_formula
fixCosSin fixComputeCosSin( fixed_t radians )
{
	const int64_t pi = FIX_Q32_PI;
	const int64_t halfPi = FIX_Q32_HALF_PI;
	const int64_t pi2 = 42389628127LL;		 // pi^2 in Q32.32
	const int64_t fivePi2 = 211948140636LL; // 5*pi^2 in Q32.32

	// The unwound angle is in [-pi, pi] to within an ulp; promote to Q32.32.
	// Below 32 rad, keep the single-word fixUnwindAngle path bit for bit: it is frozen by
	// test/transcendental_test.c. At and above 32 rad, its one-word 2*pi is off by 0.17 raw
	// per turn, which grows without bound; reduce there with a two-word 2*pi in Q2.62 instead.
	// The error bound is 107 ulps of Q48.16 at every angle, checked against CORE-MATH at the
	// pin in test/sincos_core_math_test.c.
	int64_t x;
	if ( radians > -( (fixed_t)32 << 16 ) && radians < ( (fixed_t)32 << 16 ) )
	{
		x = fixShiftLeft( fixUnwindAngle( radians ), 16 );
	}
	else
	{
		// xw is the angle in Q2.62, exact (radians * 2^46). twoPi is 4 * round(pi/2 * 2^62),
		// whose per-turn error is corrected below.
		fixInt128 xw = fixInt128ShiftLeft( fixInt128FromI64( radians ), 46 );
		fixInt128 twoPi = fixInt128ShiftLeft( fixInt128FromI64( FIX_Q62_HALF_PI ), 2 );
		fixInt128 bias = fixInt128Shr( twoPi, 1 );
		fixInt128 numerator = fixInt128IsNegative( xw ) ? fixInt128Sub( xw, bias ) : fixInt128Add( xw, bias );
		fixInt128 n = fixInt128Div( numerator, twoPi );
		fixInt128 r = fixInt128Sub( xw, fixInt128Mul( n, twoPi ) );

		// corr = round( n * ( 2*pi*2^62 - twoPi ) ), the twoPi rounding error times the turn
		// count. 6602671383 = round( ( 2*pi*2^62 - 4*7244019458077122842 ) * 2^32 ):
		//   $ bc -l
		//   scale=60
		//   pi=4*a(1)
		//   2*pi*2^62
		//   => 28976077832308491369.537304227940439549844109316229232808384851519475345105354752
		//   (2*pi*2^62 - 4*7244019458077122842)*2^32
		//   => 6602671383.006717302445411411453476951183171857962514305812334318190592
		fixInt128 corr = fixInt128Shr(
			fixInt128Add( fixInt128Mul( n, fixInt128FromI64( INT64_C( 6602671383 ) ) ), fixInt128FromI64( INT64_C( 1 ) << 31 ) ),
			32 );
		r = fixInt128Sub( r, corr );

		// Q2.62 to Q32.32, rounded to nearest.
		x = fixInt128ToI64( fixInt128Shr( fixInt128Add( r, fixInt128FromI64( INT64_C( 1 ) << 29 ) ), 30 ) );
	}
	x = x < -pi ? -pi : ( x > pi ? pi : x );

	// cosine needs angle in [-pi/2, pi/2]
	int64_t c;
	if ( x < -halfPi )
	{
		int64_t y = x + pi;
		int64_t y2 = fixQ32Mul( y, y );
		c = -fixQ32Div( pi2 - 4 * y2, pi2 + y2 );
	}
	else if ( x > halfPi )
	{
		int64_t y = x - pi;
		int64_t y2 = fixQ32Mul( y, y );
		c = -fixQ32Div( pi2 - 4 * y2, pi2 + y2 );
	}
	else
	{
		int64_t y2 = fixQ32Mul( x, x );
		c = fixQ32Div( pi2 - 4 * y2, pi2 + y2 );
	}

	// sine needs angle in [0, pi]
	int64_t s;
	if ( x < 0 )
	{
		int64_t y = x + pi;
		int64_t u = fixQ32Mul( y, pi - y );
		s = -fixQ32Div( 16 * u, fivePi2 - 4 * u );
	}
	else
	{
		int64_t u = fixQ32Mul( x, pi - x );
		s = fixQ32Div( 16 * u, fivePi2 - 4 * u );
	}

	// Normalize so the pair lies on the unit circle
	int64_t mag = fixQ32Sqrt( fixQ32Mul( s, s ) + fixQ32Mul( c, c ) );
	if ( mag > 0 )
	{
		c = fixQ32Div( c, mag );
		s = fixQ32Div( s, mag );
	}

	fixCosSin cs = { fixQ32ToFix( c ), fixQ32ToFix( s ) };
	return cs;
}

// ---------------------------------------------------------------------------------------------
// The exp2 / log2 / pow ladder. Pure integer arithmetic throughout, so the results are
// bit-identical on every platform.
// ---------------------------------------------------------------------------------------------

// 2^(2^-k) in Q32, k = 1..16: the binary-exponentiation table for the fractional part of exp2.
static const uint64_t fixExp2Table[16] = {
	0x000000016A09E668ull, // 2^(2^-1)
	0x00000001306FE0A3ull, // 2^(2^-2)
	0x00000001172B83C8ull, // 2^(2^-3)
	0x000000010B5586D0ull, // 2^(2^-4)
	0x00000001059B0D31ull, // 2^(2^-5)
	0x0000000102C9A3E7ull, // 2^(2^-6)
	0x000000010163DAA0ull, // 2^(2^-7)
	0x0000000100B1AFA6ull, // 2^(2^-8)
	0x000000010058C86Eull, // 2^(2^-9)
	0x00000001002C605Eull, // 2^(2^-10)
	0x0000000100162F39ull, // 2^(2^-11)
	0x00000001000B175Full, // 2^(2^-12)
	0x0000000100058BA0ull, // 2^(2^-13)
	0x000000010002C5CCull, // 2^(2^-14)
	0x00000001000162E5ull, // 2^(2^-15)
	0x000000010000B172ull, // 2^(2^-16)
};

fixed_t fixLog2( fixed_t a )
{
	if ( a <= 0 )
	{
		return INT64_MIN;
	}

	// integer part: position of the leading bit relative to the fraction point
	int msb = fixMostSignificantBit64( (uint64_t)a );
	int64_t integerPart = (int64_t)( msb - FIX_FRACTION_BITS );

	// mantissa m in [1, 2), carried in Q2.62 (fits u64: raw in [2^62, 2^63)): the exact binary
	// digits of log2( m ) fall out of repeated squaring -- square m; if the square reaches
	// [2, 4), the digit is 1 and the square halves back into [1, 2).
	uint64_t m;
	if ( msb <= 62 )
	{
		m = (uint64_t)a << ( 62 - msb );
	}
	else
	{
		m = (uint64_t)a >> ( msb - 62 );
	}

	const fixUInt128 half4 = fixUInt128Shl( fixUInt128FromU64( 1 ), 125 );
	uint64_t fraction32 = 0;
	for ( int i = 0; i < 32; ++i )
	{
		fixUInt128 sq = fixUInt128MulU64( m, m ); // Q4.124, value in [1, 4)
		fraction32 <<= 1;
		if ( fixUInt128Ge( sq, half4 ) )
		{
			fraction32 |= 1;
			m = fixUInt128Lo( fixUInt128Shr( sq, 63 ) ); // halve back into [1, 2) at Q2.62
		}
		else
		{
			m = fixUInt128Lo( fixUInt128Shr( sq, 62 ) ); // renormalize to Q2.62
		}
	}

	// assemble: integer part in Q48.16 plus the 32 exact fraction bits rounded to 16.
	// The integer part is negative for a < 1, so the shift routes through fixShiftLeft.
	int64_t fraction16 = (int64_t)( ( fraction32 + ( 1ull << 15 ) ) >> 16 );
	return fixShiftLeft( integerPart, FIX_FRACTION_BITS ) + fraction16;
}

fixed_t fixExp2( fixed_t a )
{
	// split into integer floor and fractional part in [0, 1)
	int64_t n = a >> FIX_FRACTION_BITS;

	// The saturation tests come BEFORE the fractional split. box3d computed the split
	// first, and n << FIX_FRACTION_BITS is signed overflow once |a| leaves the saturating
	// range; the split is unused on both early-return paths, so the order is free. Same
	// answer for every input, no undefined behavior.
	if ( n >= 47 )
	{
		return INT64_MAX; // saturate: 2^47 is the top of the Q48.16 whole-unit domain
	}

	if ( n < -17 )
	{
		return 0; // underflow below the smallest representable value
	}

	// n is negative for a < 1, and shifting a negative value left is undefined even when
	// it does not overflow, so the split routes through fixShiftLeft. Same bits.
	uint64_t f = (uint64_t)( a - fixShiftLeft( n, FIX_FRACTION_BITS ) ); // Q16 fraction, [0, 2^16)

	// 2^f as a product over the set bits of f, in Q32: r in [ 2^32, 2^33 )
	uint64_t r = 1ull << 32;
	for ( int k = 0; k < 16; ++k )
	{
		// bit for 2^-( k + 1 ) is fraction bit ( 15 - k )
		if ( f & ( 1ull << ( 15 - k ) ) )
		{
			r = fixUInt128Lo( fixUInt128Shr( fixUInt128MulU64( r, fixExp2Table[k] ), 32 ) );
		}
	}

	// scale by 2^n: result raw = r * 2^n / 2^16 (r is Q32, output is Q16)
	int shift = 16 - (int)n;
	if ( shift <= 0 )
	{
		return (fixed_t)( r << ( -shift ) );
	}
	if ( shift >= 64 )
	{
		return 0;
	}
	// round to nearest on the dropped bits, the pinned ( raw + half ) >> drop form
	return (fixed_t)( ( r + ( 1ull << ( shift - 1 ) ) ) >> shift );
}

fixed_t fixPow( fixed_t base, fixed_t exponent )
{
	if ( base <= 0 )
	{
		return 0;
	}

	if ( exponent == 0 )
	{
		return FIX_ONE;
	}

	return fixExp2( fixMul( exponent, fixLog2( base ) ) );
}

// ---------------------------------------------------------------------------------------------
// Q2.30 normalization
// ---------------------------------------------------------------------------------------------

int32_t fixNormalizeComponent30( int64_t raw, uint64_t length )
{
	FIX_ASSERT( length != 0 );

	// Unsigned negation rather than -raw: the magnitude is identical for every input and
	// stays defined at INT64_MIN, where negating the signed value is not.
	uint64_t magnitude = raw < 0 ? -(uint64_t)raw : (uint64_t)raw;
	fixUInt128 numerator = fixUInt128Add( fixUInt128Shl( fixUInt128FromU64( magnitude ), FIX30_FRACTION_BITS ),
										  fixUInt128FromU64( length >> 1 ) );

	// Through fixInt128Div rather than the native 128-bit divide. Both operands are
	// non-negative and well inside the signed 128-bit range, so the truncated quotient is
	// the same; the library's own divide is the one that works on ClangCL, which does not
	// link the compiler-rt 128-bit division builtins.
	uint64_t q = (uint64_t)fixInt128ToI64( fixInt128Div( fixInt128FromUnsigned( numerator ), fixInt128FromU64( length ) ) );
	if ( q > (uint64_t)FIX30_ONE )
	{
		q = (uint64_t)FIX30_ONE; // the rounded divide can overshoot one by an ulp
	}

	return raw < 0 ? -(int32_t)q : (int32_t)q;
}

// ---------------------------------------------------------------------------------------------
// Critically damped smoothing (deterministic control dynamics on fixed point)
// ---------------------------------------------------------------------------------------------

fixed_t fixSmoothCriticallyDamped( fixed_t current, fixed_t target, fixed_t* velocity, fixed_t smoothTime,
								   fixed_t deltaTime )
{
	if ( smoothTime <= 0 )
	{
		return target;
	}

	if ( deltaTime <= 0 )
	{
		return current;
	}

	fixed_t omega = fixDiv( 2 * FIX_PI, smoothTime );
	fixed_t onePlus = (fixed_t)( (uint64_t)FIX_ONE + (uint64_t)fixMul( omega, deltaTime ) ); // INT64_MIN policy: unsigned add, so extremes wrap instead of trapping.
	fixed_t denominator = fixMul( onePlus, onePlus );

	fixed_t difference = (fixed_t)( (uint64_t)current - (uint64_t)target ); // INT64_MIN policy: unsigned subtract keeps INT64_MIN defined.
	fixed_t spring = fixMul( fixMul( fixMul( omega, omega ), deltaTime ), difference );

	*velocity = fixDiv( (fixed_t)( (uint64_t)*velocity - (uint64_t)spring ), denominator ); // INT64_MIN policy: unsigned subtract keeps INT64_MIN defined.
	return (fixed_t)( (uint64_t)current + (uint64_t)fixMul( *velocity, deltaTime ) ); // INT64_MIN policy: unsigned add, so extremes wrap instead of trapping.
}

fixed_t fixSmoothCriticallyDampedUpDown( fixed_t current, fixed_t target, fixed_t* velocity, fixed_t smoothTimeUp,
										 fixed_t smoothTimeDown, fixed_t deltaTime )
{
	fixed_t smoothTime = fixAbs( target ) >= fixAbs( current ) ? smoothTimeUp : smoothTimeDown;
	return fixSmoothCriticallyDamped( current, target, velocity, smoothTime, deltaTime );
}
