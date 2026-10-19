/* SPDX-License-Identifier: MIT */
/* Fast exact points test for fixRsqrtExact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn rsqrt --emit-points`: every libfixmath edge,
 * 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its correctly
 * rounded raw output. fixRsqrtExact is exact by construction, so every point must match
 * bit for bit.
 */
#include "fixed/fixed_rsqrt.h"

#include <stdint.h>
#include <stdio.h>

#include "data/rsqrt_points.h"

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
	for ( size_t i = 0; i < RSQRT_POINTS_COUNT; ++i )
	{
		int64_t raw = rsqrt_points_raw[i];
		int64_t expected = rsqrt_points_expected[i];
		int64_t result = fixRsqrtExact( raw );
		uint64_t e = diff( result, expected );
		if ( e > maxerr )
		{
			maxerr = e;
		}
		if ( e != 0 )
		{
			if ( fails == 0 )
			{
				printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)raw,
						(long long)result, (long long)expected, (unsigned long long)e );
			}
			fails++;
		}
	}
	printf( "exact points fn=rsqrt inputs=%zu maxerr=%llu\n", (size_t)RSQRT_POINTS_COUNT,
			(unsigned long long)maxerr );
	if ( fails )
	{
		printf( "rsqrt points test failed: %d points differ\n", fails );
		return 1;
	}
	printf( "rsqrt points test passed\n" );
	return 0;
}
