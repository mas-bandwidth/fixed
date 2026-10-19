/* SPDX-License-Identifier: MIT */
/* Fast points test for fixLnExact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn ln --emit-points`: every libfixmath edge,
 * 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its correctly
 * rounded raw output. fixLnExact is correctly rounded, so every point must match bit for bit.
 */
#include "fixed/fixed_log.h"

#include <stdint.h>
#include <stdio.h>

#include "data/ln_points.h"

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
	for ( size_t i = 0; i < LN_POINTS_COUNT; ++i )
	{
		int64_t raw = ln_points_raw[i];
		int64_t expected = ln_points_expected[i];
		int64_t result = fixLnExact( raw );
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
	printf( "exact points fn=ln inputs=%zu maxerr=%llu\n", (size_t)LN_POINTS_COUNT,
			(unsigned long long)maxerr );
	if ( fails )
	{
		printf( "ln points test failed: %d points not exact\n", fails );
		return 1;
	}
	printf( "ln points test passed\n" );
	return 0;
}
