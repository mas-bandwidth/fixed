// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// The FIX_SATURATE policy, at the top of the range. Built only with FIX_SATURATE.
//
// Each operation is checked two ways: at the edge, where it must saturate with the true
// sign of the exact result; and densely below the edge against the code it replaced, kept
// here as the oracle, which it must match bit for bit wherever that code did not overflow.
// No time, no I/O beyond the report: inputs come from a fixed-seed generator.
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#if !defined( FIX_SATURATE )
	#error "saturate_test is built with FIX_SATURATE"
#endif

static int failures;
#define CHECK(c) do { if (!(c)) { if (failures < 30) printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static uint64_t state = 0x9E3779B97F4A7C15ull;
static uint64_t next(void)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

// A component of a random bit length from lowest to 63 (its top bit set) and a random sign, clamped into the
// domain [-INT64_MAX, INT64_MAX].
static fixed_t component( int lowest )
{
	int bits = lowest + (int)( next() % (uint64_t)( 64 - lowest ) );
	uint64_t magnitude = bits == 0 ? 0 : ( next() >> ( 64 - bits ) ) | ( UINT64_C( 1 ) << ( bits - 1 ) );
	if ( magnitude > (uint64_t)INT64_MAX )
	{
		magnitude = (uint64_t)INT64_MAX;
	}
	return ( next() & 1 ) ? -(fixed_t)magnitude : (fixed_t)magnitude;
}

static fixVec3 vector( int lowest )
{
	fixVec3 v;
	v.x = component( lowest );
	v.y = component( lowest );
	v.z = component( lowest );
	return v;
}

// THE ORACLE: fixFromDotRaw as it was, with its saturation test after the rounding add.
static fixed_t oldFromDotRaw( fixInt128 raw )
{
	fixInt128 r = fixInt128Shr( fixInt128Add( raw, fixInt128FromI64( FIX_HALF ) ), FIX_FRACTION_BITS );
	if ( fixInt128Gt( r, fixInt128FromI64( INT64_MAX ) ) )
	{
		return FIX_MAX;
	}
	if ( fixInt128Lt( r, fixInt128FromI64( -INT64_MAX ) ) )
	{
		return FIX_MIN;
	}
	return (fixed_t)fixInt128ToI64( r );
}

static fixed_t oldDot( fixVec3 a, fixVec3 b )
{
	return oldFromDotRaw( fixDotRaw( a, b ) );
}

static double doubleDot( fixVec3 a, fixVec3 b )
{
	return (double)a.x * (double)b.x + (double)a.y * (double)b.y + (double)a.z * (double)b.z;
}

static void dotAtTheTop(void)
{
	// Every component 2^47 - 1 in value: the squares sum to 3 * 2^126, past int128. This
	// came back as FIX_MIN, the most negative number, for the square of a vector.
	fixed_t big = fixShiftLeft( ( (fixed_t)1 << 47 ) - 1, FIX_FRACTION_BITS );
	fixVec3 v = { big, big, big };
	fixVec3 n = { -big, -big, -big };
	fixVec3 zero = { 0, 0, 0 };
	CHECK( fixDot( v, v ) == FIX_MAX );
	CHECK( fixDot( n, n ) == FIX_MAX );
	CHECK( fixDot( v, n ) == FIX_MIN );
	CHECK( fixLengthSquared( v ) == FIX_MAX );
	CHECK( fixDistanceSquared( zero, v ) == FIX_MAX );

	fixVec3 top = { FIX_MAX, FIX_MAX, FIX_MAX };
	fixVec3 bottom = { FIX_MIN, FIX_MIN, FIX_MIN };
	CHECK( fixDot( top, top ) == FIX_MAX );
	CHECK( fixDot( top, bottom ) == FIX_MIN );
	CHECK( fixDot( bottom, bottom ) == FIX_MAX );

	// One term overflows the first add, the third pulls the other way and cannot bring it
	// back: 2 * ( 2^63 - 1 )^2 - ( 2^63 - 1 )^2 is still far past the range.
	fixVec3 mixed = { FIX_MAX, FIX_MAX, FIX_MIN };
	CHECK( fixDot( mixed, top ) == FIX_MAX );
	CHECK( fixDot( mixed, bottom ) == FIX_MIN );

	// A sum that fits int128 but sits within FIX_HALF of its top: 2 * ( 2^63 - 1 )^2 +
	// 8 * ( 2^62 - 13 ) = 2^127 - 102. The rounding add wrapped it, and it read as FIX_MIN.
	fixVec3 a = { INT64_MAX, INT64_MAX, ( (fixed_t)1 << 62 ) - 13 };
	fixVec3 b = { INT64_MAX, INT64_MAX, 8 };
	CHECK( fixDot( a, b ) == FIX_MAX );
	fixVec3 c = { -INT64_MAX, -INT64_MAX, -( ( (fixed_t)1 << 62 ) - 13 ) };
	CHECK( fixDot( c, b ) == FIX_MIN );

	// The last values before the edge, and the first past it, on both sides.
	fixVec3 unit = { FIX_ONE, 0, 0 };
	fixVec3 edge = { FIX_MAX, 0, 0 };
	CHECK( fixDot( edge, unit ) == FIX_MAX );
	fixVec3 edgeNegative = { FIX_MIN, 0, 0 };
	CHECK( fixDot( edgeNegative, unit ) == FIX_MIN );
	fixVec3 nearEdge = { FIX_MAX - 1, 0, 0 };
	CHECK( fixDot( nearEdge, unit ) == FIX_MAX - 1 );
	fixVec3 nearEdgeNegative = { FIX_MIN + 1, 0, 0 };
	CHECK( fixDot( nearEdgeNegative, unit ) == FIX_MIN + 1 );
}

static void dotDifferential(void)
{
	// Dense over every magnitude, then again with every component 62 or 63 bits long,
	// with every product the same sign, where the sums leave int128. Below 2^126 the old code could not wrap, so
	// the new must give its bits exactly (saturated or not); at or past it the new must
	// saturate with the sign of the exact sum.
	uint64_t compared = 0;
	uint64_t saturated = 0;
	for ( int pass = 0; pass < 2; ++pass )
	{
		for ( int i = 0; i < ( 1 << 21 ); ++i )
		{
			fixVec3 a = vector( pass == 0 ? 0 : 62 );
			fixVec3 b = vector( pass == 0 ? 0 : 62 );
			if ( pass == 1 )
			{
				// Every product the same sign, so the sum runs off one end of int128.
				bool flip = ( next() & 1 ) != 0;
				b.x = ( ( a.x < 0 ) != flip ) ? -fixAbs( b.x ) : fixAbs( b.x );
				b.y = ( ( a.y < 0 ) != flip ) ? -fixAbs( b.y ) : fixAbs( b.y );
				b.z = ( ( a.z < 0 ) != flip ) ? -fixAbs( b.z ) : fixAbs( b.z );
			}
			double d = doubleDot( a, b );
			fixed_t dot = fixDot( a, b );
			if ( fabs( d ) < 0x1p126 )
			{
				CHECK( dot == oldDot( a, b ) );
				compared++;
			}
			else
			{
				CHECK( dot == ( d > 0.0 ? FIX_MAX : FIX_MIN ) );
				saturated++;
			}
			if ( doubleDot( a, a ) < 0x1p126 )
			{
				CHECK( fixLengthSquared( a ) == oldDot( a, a ) );
			}
			else
			{
				CHECK( fixLengthSquared( a ) == FIX_MAX );
			}
		}
	}
	CHECK( compared > ( 1u << 20 ) );
	CHECK( saturated > ( 1u << 18 ) );
	printf( "dot_differential: compared %llu, saturated %llu\n", (unsigned long long)compared, (unsigned long long)saturated );
}

// THE ORACLE for the vector adds: the plain int64 add, spelled through unsigned so the
// wrap the old code was undefined on is a defined value here.
static fixed_t oldAdd( fixed_t a, fixed_t b ) { return (fixed_t)( (uint64_t)a + (uint64_t)b ); }
static fixed_t oldSub( fixed_t a, fixed_t b ) { return (fixed_t)( (uint64_t)a - (uint64_t)b ); }

// The exact sum or difference, at 128 bits, inside [FIX_MIN, FIX_MAX].
static bool inRange( fixInt128 exact )
{
	return fixInt128Le( exact, fixInt128FromI64( FIX_MAX ) ) && fixInt128Ge( exact, fixInt128FromI64( FIX_MIN ) );
}

static void vecAddAtTheTop(void)
{
	CHECK( fixAddSaturate( FIX_MAX, 1 ) == FIX_MAX );
	CHECK( fixAddSaturate( FIX_MAX, FIX_MAX ) == FIX_MAX );
	CHECK( fixAddSaturate( FIX_MIN, -1 ) == FIX_MIN );
	CHECK( fixAddSaturate( FIX_MIN, FIX_MIN ) == FIX_MIN );
	CHECK( fixAddSaturate( FIX_MAX - 1, 1 ) == FIX_MAX );
	CHECK( fixAddSaturate( FIX_MIN + 1, -1 ) == FIX_MIN );
	CHECK( fixAddSaturate( FIX_MAX, FIX_MIN ) == 0 );
	CHECK( fixSubSaturate( FIX_MAX, -1 ) == FIX_MAX );
	CHECK( fixSubSaturate( FIX_MIN, 1 ) == FIX_MIN );
	CHECK( fixSubSaturate( FIX_MAX, FIX_MIN ) == FIX_MAX );
	CHECK( fixSubSaturate( FIX_MIN, FIX_MAX ) == FIX_MIN );
	CHECK( fixSubSaturate( FIX_MIN + 1, 1 ) == FIX_MIN );
	CHECK( fixSubSaturate( FIX_MAX, FIX_MAX ) == 0 );

	// -INT64_MAX - 1 fits int64 but is outside the domain: it saturates to FIX_MIN.
	CHECK( fixAddSaturate( FIX_MIN, -1 ) == -INT64_MAX );

	fixVec3 top = { FIX_MAX, FIX_MAX - 5, 1 };
	fixVec3 bottom = { FIX_MIN, FIX_MIN + 5, -1 };
	fixVec3 ten = { 10, 10, 10 };
	fixVec3 sum = fixVecAdd( top, ten );
	CHECK( sum.x == FIX_MAX && sum.y == FIX_MAX && sum.z == 11 );
	sum = fixVecAdd( bottom, fixVecNeg( ten ) );
	CHECK( sum.x == FIX_MIN && sum.y == FIX_MIN && sum.z == -11 );
	fixVec3 difference = fixVecSub( top, fixVecNeg( ten ) );
	CHECK( difference.x == FIX_MAX && difference.y == FIX_MAX && difference.z == 11 );
	difference = fixVecSub( bottom, ten );
	CHECK( difference.x == FIX_MIN && difference.y == FIX_MIN && difference.z == -11 );
	difference = fixVecSub( top, bottom );
	CHECK( difference.x == FIX_MAX && difference.y == FIX_MAX && difference.z == 2 );
}

static void vecAddDifferential(void)
{
	// Dense over every magnitude. An exact result inside the range is the old plain add's
	// bits; outside it, the edge with the sign of the exact result.
	uint64_t compared = 0;
	uint64_t saturated = 0;
	for ( int pass = 0; pass < 2; ++pass )
	{
		for ( int i = 0; i < ( 1 << 21 ); ++i )
		{
			fixVec3 a = vector( pass == 0 ? 0 : 62 );
			fixVec3 b = vector( pass == 0 ? 0 : 62 );
			fixVec3 sum = fixVecAdd( a, b );
			fixVec3 difference = fixVecSub( a, b );
			fixed_t as[3] = { a.x, a.y, a.z };
			fixed_t bs[3] = { b.x, b.y, b.z };
			fixed_t sums[3] = { sum.x, sum.y, sum.z };
			fixed_t differences[3] = { difference.x, difference.y, difference.z };
			for ( int k = 0; k < 3; ++k )
			{
				fixInt128 exactSum = fixInt128Add( fixInt128FromI64( as[k] ), fixInt128FromI64( bs[k] ) );
				fixInt128 exactDifference = fixInt128Sub( fixInt128FromI64( as[k] ), fixInt128FromI64( bs[k] ) );
				if ( inRange( exactSum ) )
				{
					CHECK( sums[k] == oldAdd( as[k], bs[k] ) );
					compared++;
				}
				else
				{
					CHECK( sums[k] == ( fixInt128IsNegative( exactSum ) ? FIX_MIN : FIX_MAX ) );
					saturated++;
				}
				if ( inRange( exactDifference ) )
				{
					CHECK( differences[k] == oldSub( as[k], bs[k] ) );
					compared++;
				}
				else
				{
					CHECK( differences[k] == ( fixInt128IsNegative( exactDifference ) ? FIX_MIN : FIX_MAX ) );
					saturated++;
				}
			}
		}
	}
	CHECK( compared > ( 1u << 22 ) );
	CHECK( saturated > ( 1u << 20 ) );
	printf( "vec_add_differential: compared %llu, saturated %llu\n", (unsigned long long)compared, (unsigned long long)saturated );
}

static fixed_t oldMul( fixed_t a, fixed_t b ) { return fixMul( a, b ); }

static void crossAtTheTop(void)
{
	// cross( ( 0, M, M ), ( 0, -M, M ) ).x = M * M - M * -M: FIX_MAX less FIX_MIN.
	fixVec3 a = { 0, FIX_MAX, FIX_MAX };
	fixVec3 b = { 0, FIX_MIN, FIX_MAX };
	fixVec3 c = fixCross( a, b );
	CHECK( c.x == FIX_MAX && c.y == 0 && c.z == 0 );
	c = fixCross( b, a );
	CHECK( c.x == FIX_MIN && c.y == 0 && c.z == 0 );
}

static void crossDifferential(void)
{
	// Each component is the difference of two fixMul products. Where that exact difference
	// is inside the range it is the old plain subtract's bits; outside, the edge.
	uint64_t compared = 0;
	uint64_t saturated = 0;
	for ( int pass = 0; pass < 2; ++pass )
	{
		for ( int i = 0; i < ( 1 << 21 ); ++i )
		{
			fixVec3 a = vector( pass == 0 ? 0 : 40 );
			fixVec3 b = vector( pass == 0 ? 0 : 40 );
			fixVec3 c = fixCross( a, b );
			fixed_t left[3] = { oldMul( a.y, b.z ), oldMul( a.z, b.x ), oldMul( a.x, b.y ) };
			fixed_t right[3] = { oldMul( a.z, b.y ), oldMul( a.x, b.z ), oldMul( a.y, b.x ) };
			fixed_t cs[3] = { c.x, c.y, c.z };
			for ( int k = 0; k < 3; ++k )
			{
				fixInt128 exact = fixInt128Sub( fixInt128FromI64( left[k] ), fixInt128FromI64( right[k] ) );
				if ( inRange( exact ) )
				{
					CHECK( cs[k] == oldSub( left[k], right[k] ) );
					compared++;
				}
				else
				{
					CHECK( cs[k] == ( fixInt128IsNegative( exact ) ? FIX_MIN : FIX_MAX ) );
					saturated++;
				}
			}
		}
	}
	CHECK( compared > ( 1u << 22 ) );
	CHECK( saturated > ( 1u << 16 ) );
	printf( "cross_differential: compared %llu, saturated %llu\n", (unsigned long long)compared, (unsigned long long)saturated );
}

static fixQuat quaternion( int lowest )
{
	fixQuat q;
	q.v = vector( lowest );
	q.s = component( lowest );
	return q;
}

// THE ORACLES for the quaternion reductions: the four-term sums as the default build makes
// them, through the seam's wrapping int128 add and subtract (defined, never undefined).
static fixed_t oldDotQuat( fixQuat a, fixQuat b )
{
	return oldFromDotRaw( fixInt128Add( fixDotRaw( a.v, b.v ), fixInt128MulI64( a.s, b.s ) ) );
}

static fixQuat oldMulQuat( fixQuat q1, fixQuat q2 )
{
	fixQuat q = {
		{
			oldFromDotRaw( fixInt128Add( fixInt128Add( fixInt128Sub( fixInt128MulI64( q1.v.y, q2.v.z ), fixInt128MulI64( q1.v.z, q2.v.y ) ),
				fixInt128MulI64( q1.s, q2.v.x ) ), fixInt128MulI64( q2.s, q1.v.x ) ) ),
			oldFromDotRaw( fixInt128Add( fixInt128Add( fixInt128Sub( fixInt128MulI64( q1.v.z, q2.v.x ), fixInt128MulI64( q1.v.x, q2.v.z ) ),
				fixInt128MulI64( q1.s, q2.v.y ) ), fixInt128MulI64( q2.s, q1.v.y ) ) ),
			oldFromDotRaw( fixInt128Add( fixInt128Add( fixInt128Sub( fixInt128MulI64( q1.v.x, q2.v.y ), fixInt128MulI64( q1.v.y, q2.v.x ) ),
				fixInt128MulI64( q1.s, q2.v.z ) ), fixInt128MulI64( q2.s, q1.v.z ) ) ),
		},
		oldFromDotRaw( fixInt128Sub( fixInt128MulI64( q1.s, q2.s ), fixDotRaw( q1.v, q2.v ) ) ),
	};
	return q;
}

static fixQuat oldInvMulQuat( fixQuat q1, fixQuat q2 )
{
	fixQuat q = {
		{
			oldFromDotRaw( fixInt128Sub( fixInt128Add( fixInt128Sub( fixInt128MulI64( q2.v.y, q1.v.z ), fixInt128MulI64( q2.v.z, q1.v.y ) ),
				fixInt128MulI64( q1.s, q2.v.x ) ), fixInt128MulI64( q2.s, q1.v.x ) ) ),
			oldFromDotRaw( fixInt128Sub( fixInt128Add( fixInt128Sub( fixInt128MulI64( q2.v.z, q1.v.x ), fixInt128MulI64( q2.v.x, q1.v.z ) ),
				fixInt128MulI64( q1.s, q2.v.y ) ), fixInt128MulI64( q2.s, q1.v.y ) ) ),
			oldFromDotRaw( fixInt128Sub( fixInt128Add( fixInt128Sub( fixInt128MulI64( q2.v.x, q1.v.y ), fixInt128MulI64( q2.v.y, q1.v.x ) ),
				fixInt128MulI64( q1.s, q2.v.z ) ), fixInt128MulI64( q2.s, q1.v.z ) ) ),
		},
		oldFromDotRaw( fixInt128Add( fixInt128MulI64( q1.s, q2.s ), fixDotRaw( q1.v, q2.v ) ) ),
	};
	return q;
}

static double p( fixed_t a, fixed_t b ) { return (double)a * (double)b; }

// The exact four-term sums, estimated in double. Only the classification uses them: below
// 2^126 the true sum fits int128 by a margin far past the estimate's error, so the default
// build's wrapping sum is exact there; at or past it the true sum is far past the 2^79
// that Q48.16 can hold, and the only right answer is the edge with its sign.
static void quatComponents( fixQuat a, fixQuat b, double dot[1], double mul[4], double inv[4] )
{
	dot[0] = p( a.v.x, b.v.x ) + p( a.v.y, b.v.y ) + p( a.v.z, b.v.z ) + p( a.s, b.s );
	mul[0] = p( a.v.y, b.v.z ) - p( a.v.z, b.v.y ) + p( a.s, b.v.x ) + p( b.s, a.v.x );
	mul[1] = p( a.v.z, b.v.x ) - p( a.v.x, b.v.z ) + p( a.s, b.v.y ) + p( b.s, a.v.y );
	mul[2] = p( a.v.x, b.v.y ) - p( a.v.y, b.v.x ) + p( a.s, b.v.z ) + p( b.s, a.v.z );
	mul[3] = p( a.s, b.s ) - ( p( a.v.x, b.v.x ) + p( a.v.y, b.v.y ) + p( a.v.z, b.v.z ) );
	inv[0] = p( b.v.y, a.v.z ) - p( b.v.z, a.v.y ) + p( a.s, b.v.x ) - p( b.s, a.v.x );
	inv[1] = p( b.v.z, a.v.x ) - p( b.v.x, a.v.z ) + p( a.s, b.v.y ) - p( b.s, a.v.y );
	inv[2] = p( b.v.x, a.v.y ) - p( b.v.y, a.v.x ) + p( a.s, b.v.z ) - p( b.s, a.v.z );
	inv[3] = p( a.s, b.s ) + ( p( a.v.x, b.v.x ) + p( a.v.y, b.v.y ) + p( a.v.z, b.v.z ) );
}

static void quatAtTheTop(void)
{
	// Four squares of FIX_MAX sum to just under 2^128. The default build wrapped that to a
	// small negative raw value: the dot of a quaternion with itself read as -1/65536.
	fixQuat top = { { FIX_MAX, FIX_MAX, FIX_MAX }, FIX_MAX };
	fixQuat bottom = { { FIX_MIN, FIX_MIN, FIX_MIN }, FIX_MIN };
	CHECK( fixDotQuat( top, top ) == FIX_MAX );
	CHECK( fixDotQuat( bottom, bottom ) == FIX_MAX );
	CHECK( fixDotQuat( top, bottom ) == FIX_MIN );
	CHECK( oldDotQuat( top, top ) != FIX_MAX );

	// Every component 2^47 - 1 in value: three of the four product terms of each component
	// carry the same sign and reach 3 * 2^126.
	fixed_t big = fixShiftLeft( ( (fixed_t)1 << 47 ) - 1, FIX_FRACTION_BITS );
	fixQuat a = { { big, big, big }, big };
	fixQuat b = { { big, -big, big }, big };
	// mul( a, b ).v.x = a.y b.z - a.z b.y + a.s b.x + b.s a.x = +4 big^2
	fixQuat m = fixMulQuat( a, b );
	CHECK( m.v.x == FIX_MAX );
	// mul( a, b ).s = a.s b.s - dot( a.v, b.v ) = big^2 - big^2 = 0: an exact cancellation.
	CHECK( m.s == 0 );
	fixQuat n = { { -big, -big, -big }, big };
	m = fixMulQuat( n, a );
	// s = big^2 - ( -3 big^2 ) = +4 big^2
	CHECK( m.s == FIX_MAX );
	m = fixInvMulQuat( a, a );
	// s = big^2 + 3 big^2
	CHECK( m.s == FIX_MAX );
	m = fixInvMulQuat( n, a );
	// v.x = a.y n.z - a.z n.y + n.s a.x - a.s n.x = 2 big^2: inside int128, past Q48.16
	CHECK( m.v.x == FIX_MAX );
	fixQuat c = { { -big, big, -big }, -big };
	m = fixInvMulQuat( a, c );
	// s = a.s c.s + dot( a.v, c.v ) = -big^2 - big^2
	CHECK( m.s == FIX_MIN );
}

static void quatCheck( fixed_t got, fixed_t old, double exact, uint64_t* compared, uint64_t* saturated )
{
	if ( fabs( exact ) < 0x1p126 )
	{
		CHECK( got == old );
		( *compared )++;
	}
	else
	{
		CHECK( got == ( exact > 0.0 ? FIX_MAX : FIX_MIN ) );
		( *saturated )++;
	}
}

static void quatDifferential(void)
{
	// Dense over every magnitude, then at the top (62 and 63 bit components), then at the
	// top with every component one sign, which drives the four-term sums off int128.
	uint64_t compared = 0;
	uint64_t saturated = 0;
	for ( int pass = 0; pass < 3; ++pass )
	{
		for ( int i = 0; i < ( 1 << 20 ); ++i )
		{
			fixQuat a = quaternion( pass == 0 ? 0 : 62 );
			fixQuat b = quaternion( pass == 0 ? 0 : 62 );
			if ( pass == 2 )
			{
				bool flip = ( next() & 1 ) != 0;
				a.v.x = fixAbs( a.v.x ); a.v.y = fixAbs( a.v.y ); a.v.z = fixAbs( a.v.z ); a.s = fixAbs( a.s );
				b.v.x = flip ? -fixAbs( b.v.x ) : fixAbs( b.v.x ); b.v.y = flip ? -fixAbs( b.v.y ) : fixAbs( b.v.y );
				b.v.z = flip ? -fixAbs( b.v.z ) : fixAbs( b.v.z ); b.s = flip ? -fixAbs( b.s ) : fixAbs( b.s );
			}
			double dot[1], mul[4], inv[4];
			quatComponents( a, b, dot, mul, inv );
			quatCheck( fixDotQuat( a, b ), oldDotQuat( a, b ), dot[0], &compared, &saturated );
			fixQuat m = fixMulQuat( a, b );
			fixQuat om = oldMulQuat( a, b );
			quatCheck( m.v.x, om.v.x, mul[0], &compared, &saturated );
			quatCheck( m.v.y, om.v.y, mul[1], &compared, &saturated );
			quatCheck( m.v.z, om.v.z, mul[2], &compared, &saturated );
			quatCheck( m.s, om.s, mul[3], &compared, &saturated );
			fixQuat v = fixInvMulQuat( a, b );
			fixQuat ov = oldInvMulQuat( a, b );
			quatCheck( v.v.x, ov.v.x, inv[0], &compared, &saturated );
			quatCheck( v.v.y, ov.v.y, inv[1], &compared, &saturated );
			quatCheck( v.v.z, ov.v.z, inv[2], &compared, &saturated );
			quatCheck( v.s, ov.s, inv[3], &compared, &saturated );
		}
	}
	CHECK( compared > ( 1u << 22 ) );
	CHECK( saturated > ( 1u << 20 ) );
	printf( "quat_differential: compared %llu, saturated %llu\n", (unsigned long long)compared, (unsigned long long)saturated );
}

// ---------------------------------------------------------------------------------------
// The fixed_t lanes: every remaining plain add and subtract that consumes a fixed_t.
// ---------------------------------------------------------------------------------------

// THE ORACLE for composite operations: the default build's formula spelled with these,
// which wrap (defined, through unsigned) exactly as the default build does, and record
// whether any step's exact result left the range. Where none did, the saturating build
// must give the default build's bits.
static bool wrapped;
static fixed_t wAdd( fixed_t a, fixed_t b )
{
	if ( !inRange( fixInt128Add( fixInt128FromI64( a ), fixInt128FromI64( b ) ) ) ) wrapped = true;
	return oldAdd( a, b );
}
static fixed_t wSub( fixed_t a, fixed_t b )
{
	if ( !inRange( fixInt128Sub( fixInt128FromI64( a ), fixInt128FromI64( b ) ) ) ) wrapped = true;
	return oldSub( a, b );
}

static fixed_t edgeOf( fixInt128 exact ) { return fixInt128IsNegative( exact ) ? FIX_MIN : FIX_MAX; }

static uint64_t laneCompared;
static uint64_t laneSaturated;

// One result that is a single exact sum of terms: the old bits inside, the edge outside.
static void checkExact( fixed_t got, fixed_t old, fixInt128 exact )
{
	if ( inRange( exact ) )
	{
		CHECK( got == old );
		laneCompared++;
	}
	else
	{
		CHECK( got == edgeOf( exact ) );
		laneSaturated++;
	}
}

static fixInt128 e2( fixed_t a, fixed_t b ) { return fixInt128Add( fixInt128FromI64( a ), fixInt128FromI64( b ) ); }
static fixInt128 e2s( fixed_t a, fixed_t b ) { return fixInt128Sub( fixInt128FromI64( a ), fixInt128FromI64( b ) ); }
static fixInt128 e3( fixed_t a, fixed_t b, fixed_t c ) { return fixInt128Add( e2( a, b ), fixInt128FromI64( c ) ); }
static void checkAdd( fixed_t got, fixed_t a, fixed_t b ) { checkExact( got, oldAdd( a, b ), e2( a, b ) ); }
static void checkSub( fixed_t got, fixed_t a, fixed_t b ) { checkExact( got, oldSub( a, b ), e2s( a, b ) ); }
static void checkSum3( fixed_t got, fixed_t a, fixed_t b, fixed_t c ) { checkExact( got, oldAdd( oldAdd( a, b ), c ), e3( a, b, c ) ); }

// A composite: the old bits when no step of the default build's formula left the range.
static void checkComposite( bool equal )
{
	if ( !wrapped )
	{
		CHECK( equal );
		laneCompared++;
	}
	else
	{
		laneSaturated++;
	}
}

static bool vecEqual( fixVec3 a, fixVec3 b ) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool quatEqual( fixQuat a, fixQuat b ) { return vecEqual( a.v, b.v ) && a.s == b.s; }

static fixVec3 oldCross( fixVec3 a, fixVec3 b )
{
	fixVec3 c = { wSub( fixMul( a.y, b.z ), fixMul( a.z, b.y ) ), wSub( fixMul( a.z, b.x ), fixMul( a.x, b.z ) ),
				  wSub( fixMul( a.x, b.y ), fixMul( a.y, b.x ) ) };
	return c;
}
static fixVec3 oldMulAdd( fixVec3 a, fixed_t s, fixVec3 b )
{
	fixVec3 c = { wAdd( a.x, fixMul( s, b.x ) ), wAdd( a.y, fixMul( s, b.y ) ), wAdd( a.z, fixMul( s, b.z ) ) };
	return c;
}
static fixVec3 oldMulSub( fixVec3 a, fixed_t s, fixVec3 b )
{
	fixVec3 c = { wSub( a.x, fixMul( s, b.x ) ), wSub( a.y, fixMul( s, b.y ) ), wSub( a.z, fixMul( s, b.z ) ) };
	return c;
}
static fixVec3 oldRotate( fixQuat q, fixVec3 v )
{
	return oldMulAdd( v, FIX( 2.0f ), oldCross( q.v, oldMulAdd( oldCross( q.v, v ), q.s, v ) ) );
}
static fixVec3 oldInvRotate( fixQuat q, fixVec3 v )
{
	return oldMulAdd( v, FIX( 2.0f ), oldCross( q.v, oldMulSub( oldCross( q.v, v ), q.s, v ) ) );
}
static fixVec3 oldVecSub( fixVec3 a, fixVec3 b )
{
	fixVec3 c = { wSub( a.x, b.x ), wSub( a.y, b.y ), wSub( a.z, b.z ) };
	return c;
}
static fixVec3 oldVecAdd( fixVec3 a, fixVec3 b )
{
	fixVec3 c = { wAdd( a.x, b.x ), wAdd( a.y, b.y ), wAdd( a.z, b.z ) };
	return c;
}

static fixQuat oldMakeQuatFromMatrix( const fixMatrix3* m )
{
	fixVec3 c1 = m->cx, c2 = m->cy, c3 = m->cz;
	fixQuat q;
	fixed_t trace = wAdd( wAdd( m->cx.x, m->cy.y ), m->cz.z );
	if ( trace >= FIX( 0.0f ) )
	{
		q.v.x = wSub( c2.z, c3.y ); q.v.y = wSub( c3.x, c1.z ); q.v.z = wSub( c1.y, c2.x ); q.s = wAdd( trace, FIX( 1.0f ) );
	}
	else if ( c1.x > c2.y && c1.x > c3.z )
	{
		q.v.x = wAdd( wSub( wSub( c1.x, c2.y ), c3.z ), FIX( 1.0f ) ); q.v.y = wAdd( c2.x, c1.y ); q.v.z = wAdd( c3.x, c1.z ); q.s = wSub( c2.z, c3.y );
	}
	else if ( c2.y > c3.z )
	{
		q.v.x = wAdd( c1.y, c2.x ); q.v.y = wAdd( wSub( wSub( c2.y, c3.z ), c1.x ), FIX( 1.0f ) ); q.v.z = wAdd( c3.y, c2.z ); q.s = wSub( c3.x, c1.z );
	}
	else
	{
		q.v.x = wAdd( c1.z, c3.x ); q.v.y = wAdd( c2.z, c3.y ); q.v.z = wAdd( wSub( wSub( c3.z, c1.x ), c2.y ), FIX( 1.0f ) ); q.s = wSub( c1.y, c2.x );
	}
	return fixNormalizeQuat( q );
}

static fixed_t oldSmooth( fixed_t current, fixed_t target, fixed_t* velocity, fixed_t smoothTime, fixed_t deltaTime )
{
	fixed_t omega = fixDiv( 2 * FIX_PI, smoothTime );
	fixed_t onePlus = wAdd( FIX_ONE, fixMul( omega, deltaTime ) );
	fixed_t denominator = fixMul( onePlus, onePlus );
	fixed_t spring = fixMul( fixMul( fixMul( omega, omega ), deltaTime ), wSub( current, target ) );
	*velocity = fixDiv( wSub( *velocity, spring ), denominator );
	return wAdd( current, fixMul( *velocity, deltaTime ) );
}

// An interpolation weight: most in [0, 1], some anywhere a 1 - t cannot leave the range.
static fixed_t weight( void )
{
	if ( ( next() & 3 ) != 0 )
	{
		return (fixed_t)( next() % (uint64_t)( FIX_ONE + 1 ) );
	}
	return component( 0 ) / 2;
}

static void lanesAtTheTop(void)
{
	fixVec3 top = { FIX_MAX, FIX_MAX, FIX_MAX };
	fixVec3 bottom = { FIX_MIN, FIX_MIN, FIX_MIN };
	fixVec3 one = { FIX_ONE, -FIX_ONE, FIX_ONE };

	fixVec3 r = fixMulAdd( top, FIX_ONE, one );
	CHECK( r.x == FIX_MAX && r.y == FIX_MAX - FIX_ONE && r.z == FIX_MAX );
	r = fixMulSub( bottom, FIX_ONE, one );
	CHECK( r.x == FIX_MIN && r.y == FIX_MIN + FIX_ONE && r.z == FIX_MIN );

	// Halfway between two points near the top: each half product is in range, their sum is not.
	fixed_t half = FIX( 0.5f );
	r = fixVecLerp( top, top, half );
	CHECK( vecEqual( r, top ) );
	r = fixBlend2( FIX_ONE, top, FIX_ONE, top );
	CHECK( vecEqual( r, top ) );
	r = fixBlend2( FIX_ONE, bottom, FIX_ONE, bottom );
	CHECK( vecEqual( r, bottom ) );
	// Half of FIX_MAX rounds up to 2^62, and two of those are 2^63: the default build
	// wrapped the midpoint of the top of the range to its bottom.
	CHECK( fixLerp( FIX_MAX, FIX_MAX, half ) == FIX_MAX );
	fixPos lp = fixLerpPosition( top, top, half );
	CHECK( vecEqual( lp, top ) );
	// Extrapolation with both products in range and their sum past the bottom.
	CHECK( fixLerp( INT64_C( 4600000000000000000 ), -INT64_C( 6000000000000000000 ), FIX( 1.5f ) ) == FIX_MIN );

	// Matrix times vector: the first two terms leave the range and the third brings the
	// total back. Exact at 128 bits that is 5; two chained saturating adds would say 0.
	fixMatrix3 m = { { FIX_MAX, 0, 0 }, { FIX_ONE * 5, 0, 0 }, { FIX_MIN, 0, 0 } };
	fixVec3 a = { FIX_ONE, FIX_ONE, FIX_ONE };
	r = fixMulMV( m, a );
	CHECK( r.x == FIX_ONE * 5 && r.y == 0 && r.z == 0 );
	fixMatrix3 big = { top, top, top };
	r = fixMulMV( big, a );
	CHECK( vecEqual( r, top ) );
	fixMatrix3 mm = fixAddMM( big, big );
	CHECK( vecEqual( mm.cx, top ) && vecEqual( mm.cz, top ) );
	fixMatrix3 negative = { bottom, bottom, bottom };
	mm = fixSubMM( negative, big );
	CHECK( vecEqual( mm.cy, bottom ) );

	fixAABB box = { { -( (fixed_t)1 << 40 ), -( (fixed_t)1 << 40 ), -( (fixed_t)1 << 40 ) }, { (fixed_t)1 << 40, (fixed_t)1 << 40, (fixed_t)1 << 40 } };
	CHECK( fixAABB_Area( box ) == FIX_MAX );
	fixAABBWide wideBox = { fixPosWideFromVec3( box.lowerBound ), fixPosWideFromVec3( box.upperBound ) };
	CHECK( fixAABBWide_Area( wideBox ) == FIX_MAX );

	fixVec3 zero = { 0, 0, 0 };
	CHECK( fixDistance( bottom, top ) == FIX_MAX );
	CHECK( fixDistanceSquared( bottom, top ) == FIX_MAX );
	CHECK( fixDistance( top, zero ) == FIX_MAX );

	fixVec3 d = fixSubPos( top, bottom );
	CHECK( vecEqual( d, top ) );
	d = fixSubPos( bottom, top );
	CHECK( vecEqual( d, bottom ) );
	fixPos p = fixOffsetPos( top, one );
	CHECK( p.x == FIX_MAX && p.y == FIX_MAX - FIX_ONE );

	// A quarter turn about z of ( M, M, 0 ) is ( -M, M, 0 ), past the range. The inner sum
	// t1 + s * v leaves the range and the default build wrapped it (signed overflow, and
	// UBSan says so). Saturated step by step the turn is held at ( 0, M ), the inverse turn
	// at ( M, 0 ).
	fixed_t c = FIX( 0.70710678f );
	fixQuat quarter = { { 0, 0, c }, c };
	fixVec3 v = { FIX_MAX, FIX_MAX, 0 };
	r = fixRotateVector( quarter, v );
	CHECK( r.x == 0 && r.y == FIX_MAX && r.z == 0 );
	wrapped = false;
	fixVec3 oldTurn = oldRotate( quarter, v );
	CHECK( wrapped && !vecEqual( oldTurn, r ) );
	r = fixInvRotateVector( quarter, v );
	CHECK( r.x == FIX_MAX && r.y == 0 && r.z == 0 );

	fixTransform t = { top, quarter };
	fixVec3 local = { FIX_ONE, FIX_ONE, 0 };
	fixPos world = fixTransformWorldPoint( t, local );
	CHECK( world.y == FIX_MAX );
	fixVec3 back = fixInvTransformWorldPoint( t, bottom );
	CHECK( back.z == FIX_MIN );
	fixTransform rel = fixToRelativeTransform( t, bottom );
	CHECK( vecEqual( rel.p, top ) );
	fixTransform composed = fixMulWorldTransforms( t, t );
	CHECK( composed.p.y == FIX_MAX );
	fixTransform inverse = fixInvMulWorldTransforms( t, FIX_LITERAL( fixTransform ){ bottom, quarter } );
	CHECK( inverse.p.z == FIX_MIN );

	// A quaternion far from unit: the squares sum past the range on the diagonal.
	fixQuat huge = { { (fixed_t)1 << 50, (fixed_t)1 << 50, (fixed_t)1 << 50 }, (fixed_t)1 << 50 };
	fixMatrix3 rot = fixMakeMatrixFromQuat( huge );
	CHECK( rot.cx.x == FIX_MIN && rot.cy.y == FIX_MIN && rot.cz.z == FIX_MIN );
	CHECK( rot.cx.y == FIX_MAX && rot.cy.z == FIX_MAX && rot.cx.z == 0 );

	fixMatrix3 trace = { { FIX_MAX, 0, 0 }, { 0, FIX_MAX, 0 }, { 0, 0, FIX_MAX } };
	fixQuat fromTrace = fixMakeQuatFromMatrix( &trace );
	CHECK( fromTrace.s == FIX_ONE && fromTrace.v.x == 0 );

	fixed_t topWhole = FIX_MAX & ~( FIX_ONE - 1 );
	CHECK( fixCeil( FIX_MAX ) == FIX_MAX );
	CHECK( fixCeil( topWhole + 1 ) == FIX_MAX );
	CHECK( fixCeil( topWhole ) == topWhole );
	CHECK( fixCeil( topWhole - 1 ) == topWhole );

	fixed_t velocity = FIX_MAX;
	fixed_t smoothed = fixSmoothCriticallyDamped( FIX_MAX, FIX_MIN, &velocity, FIX_ONE, FIX_ONE );
	CHECK( smoothed > 0 );
}

static void lanesDifferential(void)
{
	laneCompared = 0;
	laneSaturated = 0;
	for ( int pass = 0; pass < 2; ++pass )
	{
		int lowest = pass == 0 ? 0 : 62;
		for ( int i = 0; i < ( 1 << 18 ); ++i )
		{
			fixVec3 a = vector( lowest );
			fixVec3 b = vector( lowest );
			fixed_t s = component( lowest == 0 ? 0 : 40 );
			fixed_t t = component( lowest == 0 ? 0 : 40 );
			fixed_t alpha = weight();

			fixVec3 r = fixMulAdd( a, s, b );
			checkAdd( r.x, a.x, fixMul( s, b.x ) ); checkAdd( r.y, a.y, fixMul( s, b.y ) ); checkAdd( r.z, a.z, fixMul( s, b.z ) );
			r = fixMulSub( a, s, b );
			checkSub( r.x, a.x, fixMul( s, b.x ) ); checkSub( r.y, a.y, fixMul( s, b.y ) ); checkSub( r.z, a.z, fixMul( s, b.z ) );

			fixed_t unitAlpha = (fixed_t)( next() % (uint64_t)( FIX_ONE + 1 ) );
			r = fixVecLerp( a, b, unitAlpha );
			fixed_t beta = FIX_ONE - unitAlpha;
			checkAdd( r.x, fixMul( beta, a.x ), fixMul( unitAlpha, b.x ) );
			checkAdd( r.y, fixMul( beta, a.y ), fixMul( unitAlpha, b.y ) );
			checkAdd( r.z, fixMul( beta, a.z ), fixMul( unitAlpha, b.z ) );
			r = fixBlend2( s, a, t, b );
			checkAdd( r.x, fixMul( s, a.x ), fixMul( t, b.x ) );
			checkAdd( r.y, fixMul( s, a.y ), fixMul( t, b.y ) );
			checkAdd( r.z, fixMul( s, a.z ), fixMul( t, b.z ) );
			checkAdd( fixLerp( a.x, b.x, alpha ), fixMul( FIX_ONE - alpha, a.x ), fixMul( alpha, b.x ) );
			fixPos lp = fixLerpPosition( a, b, alpha );
			checkAdd( lp.y, fixMul( FIX_ONE - alpha, a.y ), fixMul( alpha, b.y ) );

			fixMatrix3 m = { vector( lowest ), vector( lowest ), vector( lowest ) };
			fixVec3 v = vector( lowest == 0 ? 0 : 40 );
			r = fixMulMV( m, v );
			checkSum3( r.x, fixMul( m.cx.x, v.x ), fixMul( m.cy.x, v.y ), fixMul( m.cz.x, v.z ) );
			checkSum3( r.y, fixMul( m.cx.y, v.x ), fixMul( m.cy.y, v.y ), fixMul( m.cz.y, v.z ) );
			checkSum3( r.z, fixMul( m.cx.z, v.x ), fixMul( m.cy.z, v.y ), fixMul( m.cz.z, v.z ) );
			fixMatrix3 n = { vector( lowest ), vector( lowest ), vector( lowest ) };
			fixMatrix3 sum = fixAddMM( m, n );
			fixMatrix3 difference = fixSubMM( m, n );
			checkAdd( sum.cx.x, m.cx.x, n.cx.x ); checkAdd( sum.cy.z, m.cy.z, n.cy.z ); checkAdd( sum.cz.y, m.cz.y, n.cz.y );
			checkSub( difference.cx.y, m.cx.y, n.cx.y ); checkSub( difference.cy.x, m.cy.x, n.cy.x ); checkSub( difference.cz.z, m.cz.z, n.cz.z );

			// Area: the three products summed exactly, then doubled by the saturating fixMul.
			fixAABB box = { fixVecMin( a, b ), fixVecMax( a, b ) };
			fixVec3 delta = fixVecSub( box.upperBound, box.lowerBound );
			fixed_t pxy = fixMul( delta.x, delta.y ), pyz = fixMul( delta.y, delta.z ), pzx = fixMul( delta.z, delta.x );
			fixInt128 exactArea = e3( pxy, pyz, pzx );
			fixed_t area = fixAABB_Area( box );
			if ( inRange( exactArea ) ) { CHECK( area == fixMul( FIX( 2.0f ), oldAdd( oldAdd( pxy, pyz ), pzx ) ) ); laneCompared++; }
			else { CHECK( area == edgeOf( exactArea ) ); laneSaturated++; }
			fixAABBWide wideBox = { fixPosWideFromVec3( box.lowerBound ), fixPosWideFromVec3( box.upperBound ) };
			CHECK( fixAABBWide_Area( wideBox ) == ( inRange( exactArea ) ? area : FIX_MAX ) || !inRange( e2s( box.upperBound.x, box.lowerBound.x ) ) ||
				   !inRange( e2s( box.upperBound.y, box.lowerBound.y ) ) || !inRange( e2s( box.upperBound.z, box.lowerBound.z ) ) );

			// Distances: a saturated difference is past 2^63 already, so its square and its
			// length are past the range too.
			wrapped = false;
			fixVec3 dv = oldVecSub( b, a );
			if ( !wrapped )
			{
				// The squares of an in-range difference can still leave int128: that is fixDot's
				// saturation, checked above, and here it must read the same.
				CHECK( fixDistanceSquared( a, b ) == ( doubleDot( dv, dv ) < 0x1p126 ? oldDot( dv, dv ) : FIX_MAX ) );
				CHECK( fixDistance( a, b ) == fixLength( dv ) );
				laneCompared++;
			}
			else
			{
				CHECK( fixDistanceSquared( a, b ) == FIX_MAX );
				CHECK( fixDistance( a, b ) == FIX_MAX );
				laneSaturated++;
			}
			fixVec3 sp = fixSubPos( a, b );
			checkSub( sp.x, a.x, b.x ); checkSub( sp.z, a.z, b.z );
			fixPos op = fixOffsetPos( a, b );
			checkAdd( op.y, a.y, b.y );

			// Rotations and the transforms built on them: a normalized quaternion, or any.
			fixQuat q = fixNormalizeQuat( quaternion( 0 ) );
			if ( pass == 1 && ( next() & 1 ) ) q = quaternion( 40 );
			wrapped = false;
			fixVec3 old = oldRotate( q, a );
			checkComposite( vecEqual( fixRotateVector( q, a ), old ) );
			wrapped = false;
			old = oldInvRotate( q, a );
			checkComposite( vecEqual( fixInvRotateVector( q, a ), old ) );

			fixTransform tr = { b, q };
			wrapped = false;
			old = oldVecAdd( b, oldRotate( q, a ) );
			checkComposite( vecEqual( fixTransformWorldPoint( tr, a ), old ) );
			wrapped = false;
			old = oldInvRotate( q, oldVecSub( a, b ) );
			checkComposite( vecEqual( fixInvTransformWorldPoint( tr, a ), old ) );
			fixTransform other = { a, fixNormalizeQuat( quaternion( 0 ) ) };
			wrapped = false;
			old = oldInvRotate( q, oldVecSub( other.p, tr.p ) );
			checkComposite( vecEqual( fixInvMulWorldTransforms( tr, other ).p, old ) );
			wrapped = false;
			old = oldVecAdd( tr.p, oldRotate( q, other.p ) );
			checkComposite( vecEqual( fixMulWorldTransforms( tr, other ).p, old ) );
			fixTransform relative = fixToRelativeTransform( tr, a );
			checkSub( relative.p.x, b.x, a.x ); checkSub( relative.p.y, b.y, a.y );

			// The rotation matrix: each entry one exact sum, doubled.
			fixQuat mq = quaternion( lowest == 0 ? 0 : 40 );
			fixed_t xx = fixMul( mq.v.x, mq.v.x ), yy = fixMul( mq.v.y, mq.v.y ), zz = fixMul( mq.v.z, mq.v.z );
			fixed_t xy = fixMul( mq.v.x, mq.v.y ), zw = fixMul( mq.v.z, mq.s ), yz = fixMul( mq.v.y, mq.v.z ), xw = fixMul( mq.v.x, mq.s );
			fixMatrix3 rot = fixMakeMatrixFromQuat( mq );
			fixInt128 one128 = fixInt128FromI64( FIX_ONE );
			fixInt128 exact = fixInt128Sub( one128, fixInt128Add( e2( yy, zz ), e2( yy, zz ) ) );
			checkExact( rot.cx.x, oldSub( FIX_ONE, fixMul( FIX( 2.0f ), oldAdd( yy, zz ) ) ), exact );
			exact = fixInt128Sub( one128, fixInt128Add( e2( xx, yy ), e2( xx, yy ) ) );
			checkExact( rot.cz.z, oldSub( FIX_ONE, fixMul( FIX( 2.0f ), oldAdd( xx, yy ) ) ), exact );
			exact = fixInt128Add( e2( xy, zw ), e2( xy, zw ) );
			checkExact( rot.cx.y, fixMul( FIX( 2.0f ), oldAdd( xy, zw ) ), exact );
			exact = fixInt128Add( e2s( yz, xw ), e2s( yz, xw ) );
			checkExact( rot.cz.y, fixMul( FIX( 2.0f ), oldSub( yz, xw ) ), exact );

			wrapped = false;
			fixQuat oq = oldMakeQuatFromMatrix( &m );
			checkComposite( quatEqual( fixMakeQuatFromMatrix( &m ), oq ) );

			fixed_t ceilInput = component( lowest );
			fixed_t topWhole = FIX_MAX & ~( FIX_ONE - 1 );
			if ( ceilInput <= topWhole ) { CHECK( fixCeil( ceilInput ) == ( (fixed_t)( (uint64_t)ceilInput + FIX_ONE - 1 ) & ~( FIX_ONE - 1 ) ) ); laneCompared++; }
			else { CHECK( fixCeil( ceilInput ) == FIX_MAX ); laneSaturated++; }

			fixed_t velocity = component( lowest ), oldVelocity = velocity;
			fixed_t smoothTime = fixAbs( component( 0 ) ) + 1, dt = fixAbs( component( 0 ) ) + 1;
			wrapped = false;
			fixed_t oldResult = oldSmooth( a.x, b.x, &oldVelocity, smoothTime, dt );
			fixed_t result = fixSmoothCriticallyDamped( a.x, b.x, &velocity, smoothTime, dt );
			checkComposite( result == oldResult && velocity == oldVelocity );
		}
	}
	CHECK( laneCompared > ( 1u << 22 ) );
	CHECK( laneSaturated > ( 1u << 19 ) );
	printf( "lanes_differential: compared %llu, saturated %llu\n", (unsigned long long)laneCompared, (unsigned long long)laneSaturated );
}

int main(int argc, char** argv)
{
	struct { const char* name; void (*run)(void); } tests[] = {
		{ "dot_at_the_top", dotAtTheTop }, { "dot_differential", dotDifferential },
		{ "vec_add_at_the_top", vecAddAtTheTop }, { "vec_add_differential", vecAddDifferential },
		{ "cross_at_the_top", crossAtTheTop }, { "cross_differential", crossDifferential },
		{ "quat_at_the_top", quatAtTheTop }, { "quat_differential", quatDifferential },
		{ "lanes_at_the_top", lanesAtTheTop }, { "lanes_differential", lanesDifferential }
	};
	for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i)
	{
		if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) continue;
		int before = failures;
		tests[i].run();
		printf("%s: %d failures\n", tests[i].name, failures - before);
	}
	return failures ? 1 : 0;
}
