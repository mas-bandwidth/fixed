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

int main(int argc, char** argv)
{
	struct { const char* name; void (*run)(void); } tests[] = {
		{ "dot_at_the_top", dotAtTheTop }, { "dot_differential", dotDifferential },
		{ "vec_add_at_the_top", vecAddAtTheTop }, { "vec_add_differential", vecAddDifferential }
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
