// Every scalar function survives INT64_MIN and the other boundary raws.
//
// Fixed point has no error channel, no NaN and no infinity. INT64_MIN is reserved
// (fixIsValidFixed is false for it), but it is reachable -- fixLog2 returns it for
// a <= 0, and FIX_MIN - 1 produces it. This suite calls every scalar entry point
// over the six boundary raws in every argument position and requires that an
// out-of-domain raw is neither a trap nor undefined behavior.
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "fixed/fixed_math.h"

static long long calls;

#define CHECK( expr )                                                                                                         \
	do                                                                                                                        \
	{                                                                                                                         \
		if ( !( expr ) )                                                                                                      \
		{                                                                                                                     \
			fprintf( stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr );                                        \
			exit( 1 );                                                                                                        \
		}                                                                                                                     \
	} while ( 0 )

// The six boundary raws: reserved INT64_MIN, the saturation values, and the units.
static const fixed_t raws[6] = { INT64_MIN, FIX_MIN, -1, 0, 1, FIX_MAX };

static fixed_t canon( fixed_t v )
{
	return v == INT64_MIN ? FIX_MIN : v;
}

// The functions that saturate or that cannot reach the reserved raw must never
// return it. fixMul, fixLerp and the smoothing functions deliberately wrap on
// in-range overflow through unsigned arithmetic, and that wrap can land on the
// reserved bit pattern; fixMin/fixMax/fixClamp pass an argument through; fixFloor
// and fixCeil hit the exact integral edge pinned by the frozen exhaustive hash.
static void noMin( fixed_t v, const char* name )
{
	if ( v == INT64_MIN )
	{
		fprintf( stderr, "%s returned INT64_MIN\n", name );
		exit( 1 );
	}
}

static void testScalars( void )
{
	for ( int i = 0; i < 6; ++i )
	{
		for ( int j = 0; j < 6; ++j )
		{
			calls++;
			(void)fixMul( raws[i], raws[j] );

			calls++;
			noMin( fixDiv( raws[i], raws[j] ), "fixDiv" );

			calls++;
			(void)fixMin( raws[i], raws[j] );

			calls++;
			(void)fixMax( raws[i], raws[j] );

			calls++;
			noMin( fixAtan2( raws[i], raws[j] ), "fixAtan2" );
			CHECK( fixAtan2( raws[i], raws[j] ) == fixAtan2( canon( raws[i] ), canon( raws[j] ) ) );

			calls++;
			noMin( fixPow( raws[i], raws[j] ), "fixPow" );

			for ( int k = 0; k < 6; ++k )
			{
				calls++;
				noMin( fixMulDiv( raws[i], raws[j], raws[k] ), "fixMulDiv" );

				calls++;
				(void)fixClamp( raws[i], raws[j], raws[k] );

				calls++;
				(void)fixLerp( raws[i], raws[j], raws[k] );
			}

			// fixNormalizeComponent30: length is never 0.
			if ( raws[i] != 0 )
			{
				(void)fixNormalizeComponent30( raws[j], (uint64_t)raws[i] );
				calls++;
			}
		}

		calls++;
		noMin( fixSqrt( raws[i] ), "fixSqrt" );

		calls++;
		noMin( fixAbs( raws[i] ), "fixAbs" );

		calls++;
		(void)fixFromInt( raws[i] );

		calls++;
		(void)fixTruncToInt( raws[i] );

		calls++;
		(void)fixFloorToInt( raws[i] );

		calls++;
		(void)fixRoundToInt( raws[i] );

		// floor(FIX_MIN) and ceil(FIX_MAX) are the exact integral edges, and their raw
		// bit pattern is INT64_MIN. The exhaustive frozen hash pins those two results,
		// so they are the one place the reserved pattern is a value rather than a trap.
		calls++;
		(void)fixFloor( raws[i] );

		calls++;
		(void)fixCeil( raws[i] );

		calls++;
		(void)fixToFloat( raws[i] );

		calls++;
		(void)fixToDouble( raws[i] );

		calls++;
		(void)fix30FromFix( raws[i] );

		calls++;
		noMin( fixUnwindAngle( raws[i] ), "fixUnwindAngle" );

		calls++;
		noMin( fixComputeCosSin( raws[i] ).cosine, "fixComputeCosSin.cosine" );
		noMin( fixComputeCosSin( raws[i] ).sine, "fixComputeCosSin.sine" );

		calls++;
		fixCosSin precise = fixComputeCosSinPrecise( raws[i] );
		noMin( precise.cosine, "fixComputeCosSinPrecise.cosine" );
		noMin( precise.sine, "fixComputeCosSinPrecise.sine" );

		calls++;
		noMin( fixSin( raws[i] ), "fixSin" );

		calls++;
		noMin( fixCos( raws[i] ), "fixCos" );

		calls++;
		noMin( fixExp2( raws[i] ), "fixExp2" );

		calls++;
		if ( raws[i] <= 0 )
		{
			CHECK( fixLog2( raws[i] ) == INT64_MIN );
		}
		else
		{
			CHECK( fixLog2( raws[i] ) != INT64_MIN );
		}
	}

	// The int32 boundary raws for the Q2.30 unpack.
	static const int32_t raws30[5] = { INT32_MIN, INT32_MAX, 0, -1, 1 };
	for ( int i = 0; i < 5; ++i )
	{
		fixed30_t v = { raws30[i] };
		calls++;
		noMin( fixFromFix30( v ), "fixFromFix30" );
	}

	CHECK( fixAbs( INT64_MIN ) == FIX_MAX );
}

static void testSmoothing( void )
{
	// FIX_ONE is here with the boundary raws because a positive smoothTime is
	// what drives the dynamics; the raws still cover the zero and negative exits.
	static const fixed_t values[7] = { INT64_MIN, FIX_MIN, -1, 0, 1, FIX_MAX, FIX_ONE };

	for ( int a = 0; a < 7; ++a )
	{
		for ( int b = 0; b < 7; ++b )
		{
			for ( int c = 0; c < 7; ++c )
			{
				for ( int d = 0; d < 7; ++d )
				{
					for ( int e = 0; e < 7; ++e )
					{
						fixed_t velocity = values[c];
						calls++;
						(void)fixSmoothCriticallyDamped( values[a], values[b], &velocity, values[d], values[e] );
					}
				}
			}
		}
	}

	for ( int a = 0; a < 7; ++a )
	{
		for ( int b = 0; b < 7; ++b )
		{
			for ( int c = 0; c < 7; ++c )
			{
				for ( int d = 0; d < 7; ++d )
				{
					for ( int e = 0; e < 7; ++e )
					{
						for ( int f = 0; f < 7; ++f )
						{
							fixed_t velocity = values[c];
							calls++;
							(void)fixSmoothCriticallyDampedUpDown( values[a], values[b], &velocity, values[d], values[e],
																   values[f] );
						}
					}
				}
			}
		}
	}
}

int main( void )
{
	testScalars();
	testSmoothing();
	printf( "boundary test passed: %lld calls\n", calls );
	return 0;
}
