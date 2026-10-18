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

	// Remainder |r| <= pi/4 in Q2.62 (fits 64 bits), rounded to Q32.32
	int64_t r62 = fixInt128ToI64( fixInt128Sub( x, fixInt128Mul( k, halfPi ) ) );
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
	int quadrant = (int)( fixInt128ToI64( k ) & 3 ); // two's complement: k mod 4 for negative k too
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

// ---------------------------------------------------------------------------------------------
// fixExp2Exact: 2^x within 2 raw units of correctly rounded over the whole Q48.16 domain.
// This widens fixExp2's binary exponentiation from a Q32 running product to a 128-bit Q126
// mantissa, so the accumulated product error stays far below the output ulp.
// ---------------------------------------------------------------------------------------------

// 2^(2^-k), k = 1..16, held to 126 fraction bits as two u64 words each: the value scaled by
// 2^126, rounded to nearest, split as hi = value >> 64, lo = value & (2^64 - 1). Computed
// with GNU bc -l at scale 80 then obase=16:
//
//   scale=80; obase=16
//   for(k=1;k<=16;k++){ v=e(l(2)*2^(-k))*2^126; scale=0; r=(v+0.5)/1; hi=r/2^64; lo=r-hi*2^64 }
//
// Each entry is in [2^126, 2^127), so hi < 2^63 and lo is any u64.
static const uint64_t fixExp2ExactTableHi[16] = {
	0x5A827999FCEF3242ull, // 2^(2^-1)
	0x4C1BF828C6DC54B7ull, // 2^(2^-2)
	0x45CAE0F1F545EB73ull, // 2^(2^-3)
	0x42D561B3E6243D8Aull, // 2^(2^-4)
	0x4166C34C5615D0EBull, // 2^(2^-5)
	0x40B268F9DE0183B9ull, // 2^(2^-6)
	0x4058F6A7ECCCD5B6ull, // 2^(2^-7)
	0x402C6BE96AF2FB58ull, // 2^(2^-8)
	0x4016321B687027A8ull, // 2^(2^-9)
	0x400B18178BA33B14ull, // 2^(2^-10)
	0x40058BCE410147E8ull, // 2^(2^-11)
	0x4002C5D7BFF71DAEull, // 2^(2^-12)
	0x400162E807EE7E5Bull, // 2^(2^-13)
	0x4000B1730DF6A524ull, // 2^(2^-14)
	0x400058B9497B8151ull, // 2^(2^-15)
	0x40002C5C955DD701ull, // 2^(2^-16)
};

static const uint64_t fixExp2ExactTableLo[16] = {
	0x2CBEC4D9BAA55F50ull,
	0xA356918C17217B7Bull,
	0x7DF23143AC529E48ull,
	0x62E4ADC610AA60D9ull,
	0x9F1523ADA3290600ull,
	0xBDF2B293DE8A6F7Aull,
	0x1299AB8CDB737E90ull,
	0x4A6AC4FB04772551ull,
	0x7FC674A533CBD9E7ull,
	0x1B486FF22688E804ull,
	0x4A2F2A71570C7978ull,
	0x8E38C59C72A4E5CAull,
	0x5B2B352C60245F10ull,
	0x7426170D231A39EDull,
	0xD5115F56654A480Aull,
	0x01862FD292B7A0FFull,
};

// 128x128 -> 256 widening multiply in the fixUInt128 vocabulary, so native and emulated arms
// agree bit-for-bit. a = ah*2^64 + al, b = bh*2^64 + bl; the three partial products are
// combined with carry detection through fixUInt128Add.
static fixUInt256 fixUInt128Mul128( fixUInt128 a, fixUInt128 b )
{
	uint64_t a0 = fixUInt128Lo( a ), a1 = fixUInt128Hi( a );
	uint64_t b0 = fixUInt128Lo( b ), b1 = fixUInt128Hi( b );

	fixUInt128 z0 = fixUInt128MulU64( a0, b0 );
	fixUInt128 z1a = fixUInt128MulU64( a0, b1 );
	fixUInt128 z1b = fixUInt128MulU64( a1, b0 );
	fixUInt128 z2 = fixUInt128MulU64( a1, b1 );
	fixUInt128 z1 = fixUInt128Add( z1a, z1b );

	uint64_t r0 = fixUInt128Lo( z0 );
	fixUInt128 acc1 = fixUInt128Add( z1, fixUInt128FromU64( fixUInt128Hi( z0 ) ) );
	uint64_t r1 = fixUInt128Lo( acc1 );
	uint64_t carry = fixUInt128Lt( acc1, z1 ) ? 1 : 0;

	fixUInt128 acc2 = fixUInt128Add( fixUInt128Add( fixUInt128FromU64( fixUInt128Hi( z1 ) ), fixUInt128FromU64( fixUInt128Lo( z2 ) ) ), fixUInt128FromU64( carry ) );
	fixUInt128 acc3 = fixUInt128Add( fixUInt128FromU64( fixUInt128Hi( z2 ) ), fixUInt128Shr( acc2, 64 ) );

	fixUInt256 r;
	r.lo = fixUInt128Make( r1, r0 );
	r.hi = fixUInt128Make( fixUInt128Lo( acc3 ), fixUInt128Lo( acc2 ) );
	return r;
}

// The 128-bit Q126 mantissa of 2^f for a Q16 fraction f, by binary exponentiation over the
// 16 fraction bits. Each multiply truncates the running product by at most 2^-126 relative,
// so after at most 16 multiplies the product is within ~2^-122 of exact -- far under the
// 2^-62 relative error a result near 2^63 raw needs.
static fixUInt128 fixExp2FractionMantissa( uint64_t f )
{
	fixUInt128 r = fixUInt128Shl( fixUInt128FromU64( 1 ), 126 ); // 1.0 in Q126
	for ( int k = 0; k < 16; ++k )
	{
		if ( f & ( 1ull << ( 15 - k ) ) )
		{
			fixUInt128 t = fixUInt128Make( fixExp2ExactTableHi[k], fixExp2ExactTableLo[k] );
			fixUInt256 prod = fixUInt128Mul128( r, t );
			r = fixUInt256Shr( prod, 126 ).lo;
		}
	}
	return r;
}

// 2^x within 2 raw units of correctly rounded over the whole Q48.16 domain, integer only.
// The result is the Q126 mantissa r = 2^f scaled by 2^n: raw = r * 2^n / 2^126 * 2^16 =
// r * 2^(n-110), with one round to nearest at that final shift. Checked against CORE-MATH
// at the pin by test/exp2_exact_test.c; the bound is 2 raw over the whole domain.
FIX_API fixed_t fixExp2Exact( fixed_t a )
{
	int64_t n = a >> FIX_FRACTION_BITS;

	if ( n >= 47 )
	{
		return INT64_MAX; // saturate: 2^47 is the top of the Q48.16 whole-unit domain
	}

	if ( n < -17 )
	{
		return 0; // underflow below the smallest representable value
	}

	uint64_t f = (uint64_t)( a - fixShiftLeft( n, FIX_FRACTION_BITS ) ); // Q16 fraction

	fixUInt128 r = fixExp2FractionMantissa( f );

	// r in [2^126, 2^127), so for n in [-17, 46] the shift 110 - n is in [64, 127] and the
	// round-to-nearest sum r + half stays below 2^128.
	int shift = 110 - (int)n;
	fixUInt128 half = fixUInt128Shl( fixUInt128FromU64( 1 ), shift - 1 );
	fixUInt128 sum = fixUInt128Add( r, half );
	return (fixed_t) fixUInt128Lo( fixUInt128Shr( sum, shift ) );
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
// fixPowExact: base^exponent within 2 raw units of correctly rounded, for every base > 0 and
// every exponent, integer only.
//
// DESIGN. The result is 2^z with z = y * log2 x, where x = X / 2^16 and y = Y / 2^16 for the
// raw inputs X > 0 and Y. fixPow loses its accuracy in two places: it rounds log2 x to 16
// fraction bits before the multiply, so its error grows with |y|, and it forms y * log2 x with
// fixMul, which wraps once the product leaves 64 bits. Here every stage is wide:
//
//   1. log2 x as a signed Q7.120 value L (|L| < 47 < 2^6, so |L| * 2^120 < 2^126). The
//      integer part is the leading-bit position of X minus 16. The fraction is the exact
//      binary digits of log2 m, m = X normalized into [1, 2), produced by repeated squaring as
//      fixLog2 does, but with m carried as a 128-bit Q126 mantissa squared at 256 bits for the
//      leading digits (the ones whose losses weigh most) and at 64 bits for the last 58. The
//      digit count F is adaptive: F = msb( |Y| ) + 57, in [57, 120], so that |y| * 2^-F is
//      below 2^-72 for every exponent.
//   2. z = Y * L / 2^16, formed exactly as a 192-bit product |Y| * |L| (units 2^-136), then
//      truncated to units of 2^-120. Saturation and underflow are decided from the integer
//      part of that wide value BEFORE any further work: z >= 47 saturates to INT64_MAX and
//      z < -17 returns 0 (this is where fixPow wraps today).
//   3. 2^f for the wide fraction f in [0, 1) (120 fraction bits), as a Q126 mantissa: the top
//      16 bits of f through fixExp2Exact's own helper fixExp2FractionMantissa, unchanged; the
//      next 16 bits through the same binary exponentiation over 2^(2^-k), k = 17..32; and the
//      remaining 88 bits g < 2^-32 through 2^g = 1 + t + t^2/2 + t^3/6, t = g * ln2.
//   4. raw = 2^f * 2^(n+16), one round to nearest at the final shift, ties to even (so an
//      exact half raw, as at base 2 rad and exponent -17 rad, is 0); a rounded value that
//      reaches 2^63 saturates.
//
// ERROR BUDGET, as an absolute error in z and then a relative error in 2^z:
//   log2 digits:      the truncated tail is below 2^-F; each of the first F - 58 squares
//                     (truncated, see fixPowSquare128, then cut back to Q126) loses under
//                     2^-124 + 2^-126 of a value >= 1, and a loss at digit k enters L weighted
//                     by 2^-(k+1), so those sum below 2^-123.1; the last 58 digits run on a Q62
//                     mantissa, losing under 2^-62 at the Q126-to-Q62 cut and at each digit
//                     after it, which sums below 2^-(F+2.4). With F = msb(|Y|)+57,
//                     |y| * ( 1.19 * 2^-F + 2^-123.1 ) < 2^-71.7 + 2^-76.1.
//   product:          truncation of |Y| * |L| to 2^-120: below 2^-120.
//   exp2 tables:      32 table entries rounded to 2^-127 and at most 32 truncating
//                     multiplies at 2^-126 each, plus the 2^g remainder (truncated Taylor
//                     tail t^4/24 < 2^-134, three truncating multiplies): below 2^-120 relative.
//   total:            ln2 * ( 2^-71.7 + 2^-76.1 + 2^-120 ) + 2^-120 < 2^-72.1 relative, so under
//                     2^-9.1 raw even for a result near 2^63 raw; with the final round to
//                     nearest (ties to even) the result is within 0.502 raw of exact, so within
//                     1 raw of the correctly rounded value. Bound claimed: 2 raw.
//   zero:             a true result below half a raw returns 0 unless it lies within that
//                     2^-72.1 relative error of half a raw; an exact half (base a power of two,
//                     z = -17 exactly, where every stage is exact) rounds to even, to 0.
//   measured:         maxulp 1 on every piece of test/pow_exact_test.c where the CORE-MATH
//                     cr_pow reference (at the pin) is exact, and 0 beyond the half spacing of
//                     the double reference above 2^53 raw. Off tree, against MPFR at 400 bits
//                     on 2^22 pairs spread over z in [-18, 48] (a quarter of them with results
//                     above 2^53 raw): maxulp 1, 29 pairs at 1 raw, the rest exact.
//
// Integer only, through fixUInt128 and fixUInt256, so the native and emulated 128-bit arms are
// bit-identical. fixPow, fixExp2, fixLog2 and fixExp2Exact are untouched.
// ---------------------------------------------------------------------------------------------

// 2^(2^-k), k = 17..32, in the same Q126 two-word form as fixExp2ExactTableHi/Lo above:
// round( 2^(2^-k) * 2^126 ), hi = value >> 64, lo = value & (2^64 - 1). Computed with
// Python's decimal module at 120 significant digits (the same script reproduces k = 1..16 of
// fixExp2ExactTableHi/Lo bit for bit):
//
//   from decimal import Decimal, getcontext; getcontext().prec = 120; ln2 = Decimal( 2 ).ln()
//   v = int( ( ( ln2 * Decimal( 2 ) ** -k ).exp() * Decimal( 2 ) ** 126 + Decimal( "0.5" ) )
//            .to_integral_value( rounding = "ROUND_FLOOR" ) )
static const uint64_t fixPowExp2TableHi[16] = {
	0x4000162E46D6F26Bull, // 2^(2^-17)
	0x40000B1722757B1Bull, // 2^(2^-18)
	0x4000058B90FD3E0Cull, // 2^(2^-19)
	0x400002C5C86F3F26ull, // 2^(2^-20)
	0x40000162E433C79Bull, // 2^(2^-21)
	0x400000B17218EDCFull, // 2^(2^-22)
	0x40000058B90C3968ull, // 2^(2^-23)
	0x4000002C5C860D54ull, // 2^(2^-24)
	0x400000162E4302D2ull, // 2^(2^-25)
	0x4000000B17218073ull, // 2^(2^-26)
	0x400000058B90BFFCull, // 2^(2^-27)
	0x40000002C5C85FEEull, // 2^(2^-28)
	0x4000000162E42FF3ull, // 2^(2^-29)
	0x40000000B17217F8ull, // 2^(2^-30)
	0x4000000058B90BFCull, // 2^(2^-31)
	0x400000002C5C85FEull, // 2^(2^-32)
};

static const uint64_t fixPowExp2TableLo[16] = {
	0x8BBB607A6DF533EBull,
	0x2935F22B3005EDF2ull,
	0x418380B60E6A745Bull,
	0x767E243A81A445D9ull,
	0x65FE728536F3058Aull,
	0xC0591A3DAEB1728Bull,
	0x63D84678F008A0A9ull,
	0x52E1B9B65BBF4700ull,
	0x31AF977E11B16CBCull,
	0x1AE7A5084F1C9CDFull,
	0x0DF7CE2A8AC867C2ull,
	0xA71CE6A96067D13Eull,
	0x7B96B34F073B7209ull,
	0xC7CD69A8C3686F94ull,
	0x266738D506E7FFF4ull,
	0x03D3BD6AB7691555ull,
};

// ln2 in Q128, round( ln2 * 2^128 ), by the same decimal script.
#define FIX_POW_LN2_Q128_HI 0xB17217F7D1CF79ABull
#define FIX_POW_LN2_Q128_LO 0xC9E3B39803F2F6AFull

// m^2 at 256 bits for a 128-bit m below 2^127, truncated by leaving out the low product
// m0 * m0 (< 2^128 in a 256-bit square, so the result is low by under 2^128, which is 2^-124
// of a Q252 value): two 64x64 products, since the cross term 2 * m0 * m1 stays below 2^128
// when m1 < 2^63. For a power-of-two X, m0 is zero at every step, so the square is exact.
static fixUInt256 fixPowSquare128( fixUInt128 m )
{
	uint64_t m0 = fixUInt128Lo( m ), m1 = fixUInt128Hi( m );
	fixUInt128 cross = fixUInt128Shl( fixUInt128MulU64( m0, m1 ), 1 );
	fixUInt128 high = fixUInt128MulU64( m1, m1 );

	// cross * 2^64 + high * 2^128
	fixUInt256 r;
	r.lo = fixUInt128Make( fixUInt128Lo( cross ), 0 );
	r.hi = fixUInt128Add( high, fixUInt128FromU64( fixUInt128Hi( cross ) ) );
	return r;
}

// |log2( X / 2^16 )| in Q7.120 for raw X > 0, with F fraction digits (the rest zero), and its
// sign. See the design above.
static fixUInt128 fixPowLog2Wide( uint64_t x, int digits, bool* negative )
{
	int msb = fixMostSignificantBit64( x );
	fixUInt128 m = fixUInt128Shl( fixUInt128FromU64( x ), 126 - msb ); // Q126, value in [1, 2)

	// A loss at digit k enters L weighted by 2^-(k+1), so only the first digits need the
	// 128-bit mantissa: after digits - 58 of them, the rest run on a 64-bit Q62 mantissa (one
	// 64x64 product per digit, as fixLog2 does), whose losses of 2^-62 at the cut and per
	// digit sum below 2^-(digits+2.4) in L.
	int wideDigits = digits > 58 ? digits - 58 : 0;
	const fixUInt128 two = fixUInt128Shl( fixUInt128FromU64( 1 ), 125 ); // 2.0 in Q252, high half
	fixUInt128 fraction = FIX_UINT128_ZERO;
	for ( int i = 0; i < wideDigits; ++i )
	{
		fixUInt256 sq = fixPowSquare128( m ); // Q252, value in [1, 4)
		fraction = fixUInt128Shl( fraction, 1 );
		if ( fixUInt128Ge( sq.hi, two ) )
		{
			fraction = fixUInt128Or( fraction, fixUInt128FromU64( 1 ) );
			// halve back into [1, 2) at Q126: sq >> 127
			m = fixUInt128Or( fixUInt128Shl( sq.hi, 1 ), fixUInt128Shr( sq.lo, 127 ) );
		}
		else
		{
			// sq >> 126
			m = fixUInt128Or( fixUInt128Shl( sq.hi, 2 ), fixUInt128Shr( sq.lo, 126 ) );
		}
	}

	uint64_t m62 = fixUInt128Lo( fixUInt128Shr( m, 64 ) ); // Q62, in [2^62, 2^63); exact at i = 0
	const fixUInt128 two124 = fixUInt128Shl( fixUInt128FromU64( 1 ), 125 ); // 2.0 in Q124
	for ( int i = wideDigits; i < digits; ++i )
	{
		fixUInt128 sq = fixUInt128MulU64( m62, m62 ); // Q124, value in [1, 4)
		fraction = fixUInt128Shl( fraction, 1 );
		if ( fixUInt128Ge( sq, two124 ) )
		{
			fraction = fixUInt128Or( fraction, fixUInt128FromU64( 1 ) );
			m62 = fixUInt128Lo( fixUInt128Shr( sq, 63 ) );
		}
		else
		{
			m62 = fixUInt128Lo( fixUInt128Shr( sq, 62 ) );
		}
	}
	fraction = fixUInt128Shl( fraction, 120 - digits ); // Q120, in [0, 2^120)

	int integerPart = msb - FIX_FRACTION_BITS;
	if ( integerPart >= 0 )
	{
		*negative = false;
		return fixUInt128Add( fixUInt128Shl( fixUInt128FromU64( (uint64_t)integerPart ), 120 ), fraction );
	}
	*negative = true;
	return fixUInt128Sub( fixUInt128Shl( fixUInt128FromU64( (uint64_t)-integerPart ), 120 ), fraction );
}

// The Q126 mantissa of 2^f for f in [0, 1) held in Q120.
static fixUInt128 fixPowExp2Mantissa( fixUInt128 f )
{
	// top 16 bits: fixExp2Exact's helper, unchanged
	fixUInt128 r = fixExp2FractionMantissa( fixUInt128Lo( fixUInt128Shr( f, 104 ) ) );

	// next 16 bits: the same binary exponentiation over 2^(2^-k), k = 17..32
	uint64_t mid = fixUInt128Lo( fixUInt128Shr( f, 88 ) ) & 0xFFFFull;
	for ( int k = 0; k < 16; ++k )
	{
		if ( mid & ( 1ull << ( 15 - k ) ) )
		{
			fixUInt128 t = fixUInt128Make( fixPowExp2TableHi[k], fixPowExp2TableLo[k] );
			r = fixUInt256Shr( fixUInt128Mul128( r, t ), 126 ).lo;
		}
	}

	// the last 88 bits: g < 2^-32, 2^g = 1 + t + t^2/2 + t^3/6 with t = g * ln2 < 2^-32.5.
	fixUInt128 g = fixUInt128And( f, fixUInt128Sub( fixUInt128Shl( fixUInt128FromU64( 1 ), 88 ), fixUInt128FromU64( 1 ) ) );
	if ( fixUInt128Eq( g, FIX_UINT128_ZERO ) )
	{
		return r;
	}
	fixUInt128 ln2 = fixUInt128Make( FIX_POW_LN2_Q128_HI, FIX_POW_LN2_Q128_LO );
	fixUInt128 t = fixUInt256Shr( fixUInt128Mul128( g, ln2 ), 120 ).lo;	// Q128, < 2^95.5
	fixUInt128 t2 = fixUInt256Shr( fixUInt128Mul128( t, t ), 128 ).lo;	// Q128, < 2^63
	fixUInt128 t3 = fixUInt256Shr( fixUInt128Mul128( t2, t ), 128 ).lo; // Q128, < 2^31
	fixUInt128 e = fixUInt128Add( t, fixUInt128Shr( t2, 1 ) );
	e = fixUInt128Add( e, fixUInt128FromU64( fixUInt128Lo( t3 ) / 6 ) ); // 2^g - 1 in Q128
	return fixUInt128Add( r, fixUInt256Shr( fixUInt128Mul128( r, e ), 128 ).lo );
}

// base^exponent within 2 raw units of correctly rounded for every base > 0 and every
// exponent, integer only; saturates to INT64_MAX at and above 2^47 rad, returns 0 where the
// correctly rounded result is 0, FIX_ONE for a zero exponent and 0 for base <= 0. The bound of
// 2 raw for every base > 0 is checked against CORE-MATH cr_pow at the pin by
// test/pow_exact_test.c.
FIX_API fixed_t fixPowExact( fixed_t base, fixed_t exponent )
{
	if ( base <= 0 )
	{
		return 0;
	}

	if ( exponent == 0 )
	{
		return FIX_ONE;
	}

	// |Y| as unsigned, defined at INT64_MIN
	uint64_t magY = exponent < 0 ? -(uint64_t)exponent : (uint64_t)exponent;
	int digits = fixMostSignificantBit64( magY ) + 57; // in [57, 120]

	bool logNegative;
	fixUInt128 magL = fixPowLog2Wide( (uint64_t)base, digits, &logNegative );
	bool negative = logNegative != ( exponent < 0 );

	// |z| * 2^136 = |Y| * |L|, exact in 192 bits
	fixUInt256 product = fixUInt256MulU128ByU64( magL, magY );

	// |z| >= 48 (|z| * 2^136 >= 48 * 2^136 = 12288 * 2^128): saturate or underflow at once.
	if ( fixUInt128Ge( product.hi, fixUInt128FromU64( 12288 ) ) )
	{
		return negative ? 0 : INT64_MAX;
	}

	fixUInt128 magZ = fixUInt256Shr( product, 16 ).lo; // Q120, < 48 * 2^120
	int64_t whole = (int64_t)fixUInt128Lo( fixUInt128Shr( magZ, 120 ) );
	fixUInt128 fracMask = fixUInt128Sub( fixUInt128Shl( fixUInt128FromU64( 1 ), 120 ), fixUInt128FromU64( 1 ) );
	fixUInt128 frac = fixUInt128And( magZ, fracMask );

	int64_t n;
	fixUInt128 f;
	if ( !negative )
	{
		n = whole;
		f = frac;
	}
	else if ( fixUInt128Eq( frac, FIX_UINT128_ZERO ) )
	{
		n = -whole;
		f = frac;
	}
	else
	{
		n = -whole - 1;
		f = fixUInt128Sub( fixUInt128Shl( fixUInt128FromU64( 1 ), 120 ), frac );
	}

	if ( n >= 47 )
	{
		return INT64_MAX; // saturate: 2^47 is the top of the Q48.16 whole-unit domain
	}

	if ( n < -17 )
	{
		return 0; // below half a raw unit
	}

	fixUInt128 r = fixPowExp2Mantissa( f ); // Q126, in [2^126, 2^127]

	// raw = r * 2^(n - 110), one round to nearest, ties to even; for n in [-17, 46] the shift
	// is in [64, 127].
	int shift = 110 - (int)n;
	fixUInt128 half = fixUInt128Shl( fixUInt128FromU64( 1 ), shift - 1 );
	fixUInt128 dropped = fixUInt128And( r, fixUInt128Sub( fixUInt128Shl( half, 1 ), fixUInt128FromU64( 1 ) ) );
	fixUInt128 rounded = fixUInt128Shr( r, shift );
	if ( fixUInt128Gt( dropped, half ) || ( fixUInt128Eq( dropped, half ) && ( fixUInt128Lo( rounded ) & 1 ) ) )
	{
		rounded = fixUInt128Add( rounded, fixUInt128FromU64( 1 ) );
	}
	if ( fixUInt128Ge( rounded, fixUInt128Shl( fixUInt128FromU64( 1 ), 63 ) ) )
	{
		return INT64_MAX;
	}
	return (fixed_t)fixUInt128Lo( rounded );
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
