// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// THE WIDE AND TIME ARITHMETIC, HELD AGAINST AN EXACT 256-BIT REFERENCE.
//
// fixWideMul rounds a 256-bit product; fixWideDiv divides a 144-bit numerator; the adds,
// subtracts and negations have exact results one bit wider than their operands; and every
// one of them then applies an overflow rule -- wrap in the default build, saturate under
// FIX_SATURATE, saturate always for the divisions and the narrowings. Each of those steps
// is a place to be off by one bit or one edge, and the frozen hash in wide_time_test.c sees
// only the inputs it was given. This suite computes every result a second way.
//
// THE REFERENCE IS WRITTEN ON 64-BIT LIMBS, NOT ON THE LIBRARY'S SEAM. fixUInt256 and the
// fixInt128 vocabulary are the implementation; an oracle spelled in the same vocabulary
// shares its transformation, and a shared transformation cannot detect a wrong one. The
// limb arithmetic below is schoolbook multiplication in 32-bit pieces and shift-subtract
// division, slow and obvious, compiled on every arm including plain MSVC. The exact result
// is formed at 256 bits with its true sign, and ONLY THEN is the policy applied, in the
// test's own words: the low bits for a wrap, the named edge for a saturation.
//
// Built four ways: the default policy and FIX_SATURATE, each on the native and the emulated
// 128-bit arm. Each policy carries its own frozen hash so the arms are held to the same
// bits, and a planted wrap in the saturating arm (FIX_WIDE_NEGATIVE_CONTROL, the
// wide_arith_negative_control build) goes red.

#include "fixed/fixed.h"
#include "fixed/fixed_time.h"
#include "fixed/fixed_wide.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int fails = 0;
#define CHECK( cond )                                                                                                          \
	do                                                                                                                         \
	{                                                                                                                          \
		if ( !( cond ) )                                                                                                       \
		{                                                                                                                      \
			if ( fails < 30 )                                                                                                  \
			{                                                                                                                  \
				printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond );                                                       \
			}                                                                                                                  \
			fails++;                                                                                                           \
		}                                                                                                                      \
	} while ( 0 )

// Frozen: captured on arm64 clang, one per policy, must match on every platform forever.
#if defined( FIX_SATURATE )
	#define EXPECTED_WIDE_ARITH_HASH 0x2ab9b41a7a8a53fbULL
#else
	#define EXPECTED_WIDE_ARITH_HASH 0x33d6070a4d2c5a63ULL
#endif

// ---- the reference: two's complement 256-bit integers on four little-endian limbs ----

typedef struct ref256
{
	uint64_t limb[4];
} ref256;

static ref256 refZero( void )
{
	ref256 r = { { 0, 0, 0, 0 } };
	return r;
}

static ref256 refFromU64( uint64_t v )
{
	ref256 r = { { v, 0, 0, 0 } };
	return r;
}

static ref256 refFromI64( int64_t v )
{
	uint64_t extension = v < 0 ? UINT64_MAX : 0;
	ref256 r = { { (uint64_t)v, extension, extension, extension } };
	return r;
}

static ref256 refFromLanes( uint64_t hi, uint64_t lo )
{
	uint64_t extension = ( hi >> 63 ) != 0 ? UINT64_MAX : 0;
	ref256 r = { { lo, hi, extension, extension } };
	return r;
}

static bool refIsNegative( ref256 a )
{
	return ( a.limb[3] >> 63 ) != 0;
}

static ref256 refAdd( ref256 a, ref256 b )
{
	ref256 r;
	uint64_t carry = 0;
	for ( int i = 0; i < 4; i++ )
	{
		uint64_t sum = a.limb[i] + b.limb[i];
		uint64_t carryOut = sum < a.limb[i] ? 1 : 0;
		r.limb[i] = sum + carry;
		carryOut += r.limb[i] < sum ? 1 : 0;
		carry = carryOut;
	}
	return r;
}

static ref256 refNot( ref256 a )
{
	ref256 r;
	for ( int i = 0; i < 4; i++ )
	{
		r.limb[i] = ~a.limb[i];
	}
	return r;
}

static ref256 refNeg( ref256 a )
{
	return refAdd( refNot( a ), refFromU64( 1 ) );
}

static ref256 refSub( ref256 a, ref256 b )
{
	return refAdd( a, refNeg( b ) );
}

static bool refEq( ref256 a, ref256 b )
{
	return a.limb[0] == b.limb[0] && a.limb[1] == b.limb[1] && a.limb[2] == b.limb[2] && a.limb[3] == b.limb[3];
}

static bool refLtUnsigned( ref256 a, ref256 b )
{
	for ( int i = 3; i >= 0; i-- )
	{
		if ( a.limb[i] != b.limb[i] )
		{
			return a.limb[i] < b.limb[i];
		}
	}
	return false;
}

static bool refLtSigned( ref256 a, ref256 b )
{
	if ( refIsNegative( a ) != refIsNegative( b ) )
	{
		return refIsNegative( a );
	}
	return refLtUnsigned( a, b );
}

static ref256 refShl1( ref256 a )
{
	ref256 r;
	r.limb[0] = a.limb[0] << 1;
	for ( int i = 1; i < 4; i++ )
	{
		r.limb[i] = ( a.limb[i] << 1 ) | ( a.limb[i - 1] >> 63 );
	}
	return r;
}

// Logical shift by 1..63 places.
static ref256 refShrLogical( ref256 a, int shift )
{
	ref256 r;
	for ( int i = 0; i < 4; i++ )
	{
		r.limb[i] = a.limb[i] >> shift;
		if ( i < 3 )
		{
			r.limb[i] |= a.limb[i + 1] << ( 64 - shift );
		}
	}
	return r;
}

// Arithmetic shift by 1..63 places: floor division by 2^shift.
static ref256 refShrArithmetic( ref256 a, int shift )
{
	ref256 r = refShrLogical( a, shift );
	if ( refIsNegative( a ) )
	{
		r.limb[3] |= UINT64_MAX << ( 64 - shift );
	}
	return r;
}

static ref256 refAbs( ref256 a )
{
	return refIsNegative( a ) ? refNeg( a ) : a;
}

// 64x64 -> 128 in 32-bit pieces. Written out here rather than borrowed from the seam.
static void refMulU64( uint64_t a, uint64_t b, uint64_t* hi, uint64_t* lo )
{
	uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32;
	uint64_t b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
	uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
	uint64_t middle = ( p00 >> 32 ) + ( p01 & 0xFFFFFFFFu ) + ( p10 & 0xFFFFFFFFu );
	*lo = ( p00 & 0xFFFFFFFFu ) | ( middle << 32 );
	*hi = p11 + ( p01 >> 32 ) + ( p10 >> 32 ) + ( middle >> 32 );
}

// Unsigned schoolbook product of two 128-bit magnitudes (limbs 2 and 3 zero), exact at 256.
static ref256 refMulMagnitude( ref256 a, ref256 b )
{
	ref256 r = refZero();
	for ( int i = 0; i < 2; i++ )
	{
		for ( int j = 0; j < 2; j++ )
		{
			uint64_t hi, lo;
			refMulU64( a.limb[i], b.limb[j], &hi, &lo );
			ref256 term = refZero();
			term.limb[i + j] = lo;
			term.limb[i + j + 1] = hi;
			r = refAdd( r, term );
		}
	}
	return r;
}

// Signed product: magnitudes multiplied, sign applied.
static ref256 refMul( ref256 a, ref256 b )
{
	ref256 product = refMulMagnitude( refAbs( a ), refAbs( b ) );
	return refIsNegative( a ) != refIsNegative( b ) ? refNeg( product ) : product;
}

// Unsigned shift-subtract division, one bit per step. b is nonzero.
static ref256 refDivMagnitude( ref256 a, ref256 b )
{
	ref256 quotient = refZero();
	ref256 remainder = refZero();
	for ( int i = 255; i >= 0; i-- )
	{
		remainder = refShl1( remainder );
		remainder.limb[0] |= ( a.limb[i / 64] >> ( i % 64 ) ) & 1;
		if ( !refLtUnsigned( remainder, b ) )
		{
			remainder = refSub( remainder, b );
			quotient.limb[i / 64] |= UINT64_C( 1 ) << ( i % 64 );
		}
	}
	return quotient;
}

// ---- the policy, in the test's words ----

static const ref256 wideMax = { { UINT64_MAX, UINT64_C( 0x7FFFFFFFFFFFFFFF ), 0, 0 } };
static const ref256 wideMin = { { 1, UINT64_C( 0x8000000000000000 ), UINT64_MAX, UINT64_MAX } };
static const ref256 wideReserved = { { 0, UINT64_C( 0x8000000000000000 ), UINT64_MAX, UINT64_MAX } };

static fixedWide_t wideFromRef( ref256 a )
{
	return fixInt128Make( a.limb[1], a.limb[0] );
}

static ref256 refFromWide( fixedWide_t a )
{
	return refFromLanes( fixInt128Hi( a ), fixInt128Lo( a ) );
}

// An exact result held at the wide range: the edge with its sign outside, and the reserved
// minimum lifted to FIX_WIDE_MIN.
static fixedWide_t saturateWide( ref256 exact )
{
	if ( refLtSigned( wideMax, exact ) )
	{
		return wideFromRef( wideMax );
	}
	if ( refLtSigned( exact, wideMin ) )
	{
		return wideFromRef( wideMin );
	}
	return wideFromRef( exact );
}

// An exact result under the default build's rule for add, subtract, negate and multiply:
// the low 128 bits, as the implementation's unsigned arithmetic leaves them.
static fixedWide_t wrapWide( ref256 exact )
{
	return wideFromRef( exact );
}

static fixedWide_t policyWide( ref256 exact )
{
#if defined( FIX_SATURATE )
	return saturateWide( exact );
#else
	return wrapWide( exact );
#endif
}

static int64_t saturate64( ref256 exact )
{
	if ( refLtSigned( refFromI64( INT64_MAX ), exact ) )
	{
		return INT64_MAX;
	}
	if ( refLtSigned( exact, refFromI64( -INT64_MAX ) ) )
	{
		return -INT64_MAX;
	}
	return (int64_t)exact.limb[0];
}

static int64_t policy64( ref256 exact )
{
#if defined( FIX_SATURATE )
	return saturate64( exact );
#else
	return (int64_t)exact.limb[0];
#endif
}

// (a + half) >> 16, the rounding every multiply here applies, floor on the exact product.
static ref256 refRoundProduct( ref256 product )
{
	return refShrArithmetic( refAdd( product, refFromU64( (uint64_t)FIX_HALF ) ), FIX_FRACTION_BITS );
}

// Truncating signed quotient of ( a << 16 ) by b, b nonzero: magnitudes divide, sign applies.
static ref256 refQuotient( ref256 a, ref256 b )
{
	ref256 numerator = refAbs( a );
	for ( int i = 0; i < FIX_FRACTION_BITS; i++ )
	{
		numerator = refShl1( numerator );
	}
	ref256 quotient = refDivMagnitude( numerator, refAbs( b ) );
	return refIsNegative( a ) != refIsNegative( b ) ? refNeg( quotient ) : quotient;
}

// ---- the sweep ----

static uint64_t fnv = 1469598103934665603ULL;
static void hashWide( fixedWide_t v )
{
	fnv = ( fnv ^ fixInt128Lo( v ) ) * 1099511628211ULL;
	fnv = ( fnv ^ fixInt128Hi( v ) ) * 1099511628211ULL;
}
static void hash64( int64_t v )
{
	fnv = ( fnv ^ (uint64_t)v ) * 1099511628211ULL;
}

static uint64_t state = 0x9E3779B97F4A7C15ULL;
static uint64_t next( void )
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

// A wide value of a random bit length from 0 to 127, its top bit set, and a random sign.
static fixedWide_t randomWide( void )
{
	int bits = (int)( next() % 128 );
	uint64_t lo = next(), hi = next();
	if ( bits == 0 )
	{
		return FIX_INT128_ZERO;
	}
	if ( bits <= 64 )
	{
		hi = 0;
		lo = bits == 64 ? lo | ( UINT64_C( 1 ) << 63 ) : ( lo >> ( 64 - bits ) ) | ( UINT64_C( 1 ) << ( bits - 1 ) );
	}
	else
	{
		hi = ( hi >> ( 128 - bits ) ) | ( UINT64_C( 1 ) << ( bits - 65 ) );
	}
	fixedWide_t magnitude = fixInt128Make( hi, lo );
	return ( next() & 1 ) ? fixInt128Neg( magnitude ) : magnitude;
}

// A fixed_t of a random bit length from 0 to 63 and a random sign, INT64_MIN excluded.
static fixed_t randomFixed( void )
{
	int bits = (int)( next() % 64 );
	uint64_t magnitude = bits == 0 ? 0 : ( next() >> ( 64 - bits ) ) | ( UINT64_C( 1 ) << ( bits - 1 ) );
	return ( next() & 1 ) ? -(fixed_t)magnitude : (fixed_t)magnitude;
}

static uint64_t cases;

static void checkWidePair( fixedWide_t a, fixedWide_t b )
{
	ref256 ra = refFromWide( a ), rb = refFromWide( b );

	fixedWide_t sum = fixWideAdd( a, b );
	CHECK( fixWideEq( sum, policyWide( refAdd( ra, rb ) ) ) );
	hashWide( sum );

	fixedWide_t difference = fixWideSub( a, b );
	CHECK( fixWideEq( difference, policyWide( refSub( ra, rb ) ) ) );
	hashWide( difference );

	fixedWide_t negated = fixWideNeg( a );
	CHECK( fixWideEq( negated, policyWide( refNeg( ra ) ) ) );
	hashWide( negated );

	// The boundary narrowing: the exact difference at 129 bits, held at fixed_t's range, in
	// every build.
	fixed_t narrowed = fixWideSubToFixed( a, b );
	CHECK( narrowed == saturate64( refSub( ra, rb ) ) );
	hash64( narrowed );
	CHECK( fixWideToFixed( a ) == saturate64( ra ) );

	fixedWide_t product = fixWideMul( a, b );
	CHECK( fixWideEq( product, policyWide( refRoundProduct( refMul( ra, rb ) ) ) ) );
	hashWide( product );

	fixedWide_t quotient = fixWideDiv( a, b );
	if ( fixInt128Eq( b, FIX_INT128_ZERO ) )
	{
		fixedWide_t edge = fixInt128IsNegative( a ) ? FIX_WIDE_MIN : FIX_WIDE_MAX;
		CHECK( fixWideEq( quotient, fixInt128Eq( a, FIX_INT128_ZERO ) ? FIX_INT128_ZERO : edge ) );
	}
	else
	{
		CHECK( fixWideEq( quotient, saturateWide( refQuotient( ra, rb ) ) ) );
	}
	hashWide( quotient );

	// Every result of a saturating operation is a valid coordinate.
#if defined( FIX_SATURATE )
	CHECK( fixIsValidWideCoord( sum ) && fixIsValidWideCoord( difference ) && fixIsValidWideCoord( negated ) );
	CHECK( fixIsValidWideCoord( product ) );
#endif
	CHECK( fixIsValidWideCoord( quotient ) );
	CHECK( fixIsValidFixed( narrowed ) );
	cases++;
}

static void checkWideFixed( fixedWide_t a, fixed_t s )
{
	ref256 ra = refFromWide( a ), rs = refFromI64( s );

	fixedWide_t offset = fixWideOffset( a, s );
	CHECK( fixWideEq( offset, policyWide( refAdd( ra, rs ) ) ) );
	hashWide( offset );

	fixedWide_t product = fixWideMulFixed( a, s );
	CHECK( fixWideEq( product, policyWide( refRoundProduct( refMul( ra, rs ) ) ) ) );
	CHECK( fixWideEq( product, fixWideMul( a, fixWideFromFixed( s ) ) ) );
	hashWide( product );

	fixedWide_t quotient = fixWideDivFixed( a, s );
	CHECK( fixWideEq( quotient, fixWideDiv( a, fixWideFromFixed( s ) ) ) );
	if ( s != 0 )
	{
		CHECK( fixWideEq( quotient, saturateWide( refQuotient( ra, rs ) ) ) );
	}
	hashWide( quotient );
	cases++;
}

static void checkTime( fixTime t, fixTime u, fixed_t s )
{
	ref256 rt = refFromI64( t ), ru = refFromI64( u ), rs = refFromI64( s );

	fixTime sum = fixTimeAdd( t, u );
	CHECK( sum == policy64( refAdd( rt, ru ) ) );
	hash64( sum );

	fixTime difference = fixTimeSub( t, u );
	CHECK( difference == policy64( refSub( rt, ru ) ) );
	hash64( difference );

	fixTime negated = fixTimeNeg( t );
	CHECK( negated == policy64( refNeg( rt ) ) );
	hash64( negated );

	fixTime scaled = fixTimeMulFixed( t, s );
	CHECK( scaled == policy64( refRoundProduct( refMul( rt, rs ) ) ) );
	hash64( scaled );

	fixTime divided = fixTimeDivFixed( t, s );
	if ( s == 0 )
	{
		CHECK( divided == ( t > 0 ? FIX_TIME_MAX : ( t < 0 ? FIX_TIME_MIN : 0 ) ) );
	}
	else
	{
		CHECK( divided == saturate64( refQuotient( rt, rs ) ) );
	}
	hash64( divided );

	fixed_t ratio = fixTimeDiv( t, u );
	if ( u == 0 )
	{
		CHECK( ratio == ( t > 0 ? FIX_MAX : ( t < 0 ? FIX_MIN : 0 ) ) );
	}
	else
	{
		CHECK( ratio == saturate64( refQuotient( rt, ru ) ) );
	}
	hash64( ratio );

	// The conversion in: seconds << 16 exactly, held at the time range in every build.
	ref256 widened = rs;
	for ( int i = 0; i < FIX_TIME_FRACTION_BITS - FIX_FRACTION_BITS; i++ )
	{
		widened = refShl1( widened );
	}
	fixTime converted = fixTimeFromFixed( s );
	CHECK( converted == saturate64( widened ) );
	hash64( converted );

#if defined( FIX_SATURATE )
	CHECK( sum != INT64_MIN && difference != INT64_MIN && negated != INT64_MIN && scaled != INT64_MIN );
#endif
	CHECK( divided != INT64_MIN && ratio != INT64_MIN && converted != INT64_MIN );
	cases++;
}

int main( void )
{
	// The reference's own edges, before it is trusted with anything: the wide bounds as
	// limbs round-trip through the seam's lanes, and the multiply reproduces a product that
	// can be checked by hand.
	CHECK( fixWideEq( wideFromRef( wideMax ), FIX_WIDE_MAX ) );
	CHECK( fixWideEq( wideFromRef( wideMin ), FIX_WIDE_MIN ) );
	CHECK( !fixIsValidWideCoord( wideFromRef( wideReserved ) ) );
	CHECK( refEq( refMul( refFromI64( -3 ), refFromI64( 7 ) ), refFromI64( -21 ) ) );
	CHECK( refEq( refQuotient( refFromI64( FIX( 7.0 ) ), refFromI64( FIX( 2.0 ) ) ), refFromI64( FIX( 3.5 ) ) ) );
	CHECK( refEq( refQuotient( refFromI64( FIX( -7.0 ) ), refFromI64( FIX( 2.0 ) ) ), refFromI64( FIX( -3.5 ) ) ) );
	CHECK( refEq( refRoundProduct( refMul( refFromI64( FIX( 1.5 ) ), refFromI64( FIX( 2.5 ) ) ) ), refFromI64( FIX( 3.75 ) ) ) );

	// ---- the edge set, all pairs ----
	fixedWide_t wideEdges[] = {
		FIX_INT128_ZERO,
		fixWideFromFixed( 1 ),
		fixWideFromFixed( -1 ),
		fixWideFromFixed( FIX_ONE ),
		fixWideFromFixed( -FIX_ONE ),
		fixWideFromFixed( FIX_HALF ),
		fixWideFromFixed( FIX_MAX ),
		fixWideFromFixed( FIX_MIN ),
		fixWideFromFixed( INT64_MIN ),
		fixInt128Make( 1, 0 ),
		fixInt128Neg( fixInt128Make( 1, 0 ) ),
		fixInt128Make( UINT64_C( 1 ) << 47, 0 ),
		fixInt128Neg( fixInt128Make( UINT64_C( 1 ) << 47, 0 ) ),
		fixInt128Make( UINT64_C( 1 ) << 62, 0 ),
		fixInt128Neg( fixInt128Make( UINT64_C( 1 ) << 62, 0 ) ),
		FIX_WIDE_MAX,
		FIX_WIDE_MIN,
		wideFromRef( wideReserved ),
	};
	fixed_t fixedEdges[] = { 0, 1, -1, FIX_ONE, -FIX_ONE, FIX_HALF, FIX( 2.0 ), FIX( -2.0 ), (fixed_t)1 << 47, -( (fixed_t)1 << 47 ),
							 ( (fixed_t)1 << 47 ) - 1, FIX_MAX, FIX_MIN, INT64_MIN };
	int wideCount = (int)( sizeof( wideEdges ) / sizeof( wideEdges[0] ) );
	int fixedCount = (int)( sizeof( fixedEdges ) / sizeof( fixedEdges[0] ) );
	for ( int i = 0; i < wideCount; i++ )
	{
		for ( int j = 0; j < wideCount; j++ )
		{
			checkWidePair( wideEdges[i], wideEdges[j] );
		}
		for ( int j = 0; j < fixedCount; j++ )
		{
			checkWideFixed( wideEdges[i], fixedEdges[j] );
		}
	}
	for ( int i = 0; i < fixedCount; i++ )
	{
		for ( int j = 0; j < fixedCount; j++ )
		{
			checkTime( fixedEdges[i], fixedEdges[j], fixedEdges[( i + j ) % fixedCount] );
		}
	}

	// ---- hand values the edge set should have produced, stated so a reader can see them ----
	// fixMul's rounding at this width, and the wrap or the edge at the top.
	CHECK( fixWideEq( fixWideMul( fixWideFromFixed( FIX( 1.5 ) ), fixWideFromFixed( FIX( 2.5 ) ) ), fixWideFromFixed( FIX( 3.75 ) ) ) );
	CHECK( fixWideEq( fixWideMul( fixInt128Make( 1, 0 ), fixWideFromFixed( FIX( 2.0 ) ) ), fixInt128Make( 2, 0 ) ) );
	CHECK( fixWideEq( fixWideMulFixed( fixInt128Make( 1, 0 ), FIX( -0.5 ) ), fixInt128Neg( fixInt128Make( 0, UINT64_C( 1 ) << 63 ) ) ) );
	CHECK( fixWideEq( fixWideDiv( fixWideFromFixed( FIX( 7.0 ) ), fixWideFromFixed( FIX( 2.0 ) ) ), fixWideFromFixed( FIX( 3.5 ) ) ) );
	CHECK( fixWideEq( fixWideDiv( fixWideFromFixed( FIX( -7.0 ) ), fixWideFromFixed( FIX( 2.0 ) ) ), fixWideFromFixed( FIX( -3.5 ) ) ) );
	// truncation toward zero, as fixDiv: -1/65536 over 2 is 0, not -1
	CHECK( fixWideEq( fixWideDiv( fixWideFromFixed( -1 ), fixWideFromFixed( FIX( 2.0 ) ) ), FIX_INT128_ZERO ) );
	CHECK( fixWideEq( fixWideDivFixed( fixInt128Make( 1, 0 ), FIX( 2.0 ) ), fixInt128Make( 0, UINT64_C( 1 ) << 63 ) ) );
	// division by zero, by sign, and 0 / 0
	CHECK( fixWideEq( fixWideDiv( fixWideFromFixed( FIX_ONE ), FIX_INT128_ZERO ), FIX_WIDE_MAX ) );
	CHECK( fixWideEq( fixWideDiv( fixWideFromFixed( -FIX_ONE ), FIX_INT128_ZERO ), FIX_WIDE_MIN ) );
	CHECK( fixWideEq( fixWideDiv( FIX_INT128_ZERO, FIX_INT128_ZERO ), FIX_INT128_ZERO ) );
	// a quotient past the range saturates in every build: 2^126 / 2^-16 is 2^142
	CHECK( fixWideEq( fixWideDiv( fixInt128Make( UINT64_C( 1 ) << 62, 0 ), fixWideFromFixed( 1 ) ), FIX_WIDE_MAX ) );
	CHECK( fixWideEq( fixWideDiv( fixInt128Make( UINT64_C( 1 ) << 62, 0 ), fixWideFromFixed( -1 ) ), FIX_WIDE_MIN ) );
	CHECK( fixTimeDivFixed( FIX_TIME_ONE * 7, FIX( 2.0 ) ) == FIX_TIME_ONE * 7 / 2 );
	CHECK( fixTimeDiv( FIX_TIME_ONE * 7, FIX_TIME_ONE * 2 ) == FIX( 3.5 ) );
	CHECK( fixTimeDiv( -FIX_TIME_ONE * 7, FIX_TIME_ONE * 2 ) == FIX( -3.5 ) );
	CHECK( fixTimeDiv( FIX_TIME_ONE, 0 ) == FIX_MAX );
	CHECK( fixTimeDivFixed( -FIX_TIME_ONE, 0 ) == FIX_TIME_MIN );
	CHECK( fixTimeDivFixed( FIX_TIME_MAX, 1 ) == FIX_TIME_MAX );
	CHECK( fixTimeDivFixed( FIX_TIME_MIN, 1 ) == FIX_TIME_MIN );
	CHECK( fixTimeDiv( FIX_TIME_MAX, 1 ) == FIX_MAX );

	// ---- seeded random pairs ----
	for ( int i = 0; i < ( 1 << 15 ); i++ )
	{
		fixedWide_t a = randomWide(), b = randomWide();
		fixed_t s = randomFixed(), u = randomFixed();
		checkWidePair( a, b );
		checkWideFixed( a, s );
		checkTime( s, u, randomFixed() );
	}

	printf( "wide arith hash = 0x%016" PRIx64 " over %" PRIu64 " cases (%s)\n", fnv, cases,
#if defined( FIX_SATURATE )
		"saturating"
#else
		"wrapping"
#endif
	);
	CHECK( fnv == EXPECTED_WIDE_ARITH_HASH );

	if ( fails == 0 )
	{
		printf( "wide arith tests passed\n" );
	}
	return fails == 0 ? 0 : 1;
}
