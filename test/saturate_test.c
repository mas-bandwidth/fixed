// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// The FIX_SATURATE policy, at the top of the range. Built only with FIX_SATURATE.
//
// Each operation is checked two ways: at the edge, where it must saturate with the true
// sign of the exact result; and densely below the edge against the code it replaced, kept
// here as the oracle, which it must match bit for bit wherever that code did not overflow.
// No time, no I/O beyond the report: inputs come from a fixed-seed generator.
#include "fixed/fixed_vec.h"
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

int main(int argc, char** argv)
{
	struct { const char* name; void (*run)(void); } tests[] = {
		{ "dot_at_the_top", dotAtTheTop }, { "dot_differential", dotDifferential },
		{ "vec_add_at_the_top", vecAddAtTheTop }, { "vec_add_differential", vecAddDifferential },
		{ "cross_at_the_top", crossAtTheTop }, { "cross_differential", crossDifferential },
		{ "quat_at_the_top", quatAtTheTop }, { "quat_differential", quatDifferential }
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
