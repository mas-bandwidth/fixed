/* SPDX-License-Identifier: MIT */
/* Fast points test for fixCbrtExact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn cbrt --emit-points`: every libfixmath edge
 * (including INT64_MIN and INT64_MAX), 4096 seeded draws and the 256 worst inputs of the
 * MPFR sweep, each with its correctly rounded raw output. fixCbrtExact is correctly
 * rounded over every input, so every point must match bit for bit.
 *
 * The perfect-cube cases below are hand values with no reference required: for integer k,
 * cbrt( k^3 * 2^16 ) == k * 2^16 exactly. They cover the saturation and edge inputs the
 * card names without a sweep.
 */
#include "fixed/fixed_cbrt.h"

#include <stdint.h>
#include <stdio.h>

#include "data/cbrt_points.h"

static uint64_t diff( int64_t a, int64_t b )
{
	if ( a >= b )
	{
		return (uint64_t)a - (uint64_t)b;
	}
	return (uint64_t)b - (uint64_t)a;
}

int main( void )
{
	uint64_t maxerr = 0;
	int fails = 0;
	int64_t worst_raw = 0;
	int64_t worst_result = 0;
	int64_t worst_expected = 0;

	for ( size_t i = 0; i < CBRT_POINTS_COUNT; ++i )
	{
		int64_t raw = cbrt_points_raw[i];
		int64_t expected = cbrt_points_expected[i];
		int64_t result = fixCbrtExact( raw );
		uint64_t e = diff( result, expected );
		if ( e > maxerr )
		{
			maxerr = e;
			worst_raw = raw;
			worst_result = result;
			worst_expected = expected;
		}
		if ( e != 0 )
		{
			printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)raw, (long long)result,
					(long long)expected, (unsigned long long)e );
			fails++;
		}
	}

	static const struct
	{
		int64_t raw;
		int64_t expected;
	} exact[] = {
		{ 0, 0 },
		{ 65536, 65536 },
		{ -65536, -65536 },
		{ 8 * 65536, 2 * 65536 },
		{ -8 * 65536, -2 * 65536 },
		{ 27 * 65536, 3 * 65536 },
		{ -27 * 65536, -3 * 65536 },
		{ 1000LL * 1000 * 1000 * 65536, 1000LL * 65536 },
		{ -1000LL * 1000 * 1000 * 65536, -1000LL * 65536 },
		{ INT64_MIN, cbrt_points_expected[8] },
		{ INT64_MAX, cbrt_points_expected[10] },
	};
	const size_t nexact = sizeof( exact ) / sizeof( exact[0] );

	for ( size_t i = 0; i < nexact; ++i )
	{
		int64_t result = fixCbrtExact( exact[i].raw );
		uint64_t e = diff( result, exact[i].expected );
		if ( e > maxerr )
		{
			maxerr = e;
			worst_raw = exact[i].raw;
			worst_result = result;
			worst_expected = exact[i].expected;
		}
		if ( e != 0 )
		{
			printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)exact[i].raw,
					(long long)result, (long long)exact[i].expected, (unsigned long long)e );
			fails++;
		}
	}

	printf( "exact points fn=cbrt inputs=%zu maxerr=%llu\n", (size_t)( CBRT_POINTS_COUNT + nexact ),
			(unsigned long long)maxerr );
	if ( fails )
	{
		printf( "cbrt exact test failed: %d points differ; worst raw=%lld result=%lld expected=%lld\n", fails,
				(long long)worst_raw, (long long)worst_result, (long long)worst_expected );
		return 1;
	}
	printf( "cbrt exact test passed\n" );
	return 0;
}
