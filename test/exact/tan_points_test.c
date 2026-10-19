/* SPDX-License-Identifier: MIT */
/* Fast exact points test for fixTanExact, with no MPFR and no clock.
 *
 * The header is written by `fixed_sweep --fn tan --emit-points`: every libfixmath edge,
 * 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its correctly
 * rounded raw output. fixTanExact is correctly rounded, so every point must match bit for
 * bit: a single raw of difference fails and names the first failing input.
 */
#include "fixed/fixed_tan.h"

#include <stdint.h>
#include <stdio.h>

#include "data/tan_points.h"

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
	for ( size_t i = 0; i < TAN_POINTS_COUNT; ++i )
	{
		int64_t raw = tan_points_raw[i];
		int64_t expected = tan_points_expected[i];
		int64_t result = fixTanExact( raw );
		uint64_t e = diff( result, expected );
		if ( e > maxerr )
		{
			maxerr = e;
		}
		if ( e != 0 )
		{
			printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)raw,
					(long long)result, (long long)expected, (unsigned long long)e );
			printf( "exact points fn=tan inputs=%zu maxerr=%llu\n", (size_t)TAN_POINTS_COUNT,
					(unsigned long long)maxerr );
			printf( "tan points test failed: first mismatch at raw=%lld\n", (long long)raw );
			return 1;
		}
	}
	printf( "exact points fn=tan inputs=%zu maxerr=%llu\n", (size_t)TAN_POINTS_COUNT,
			(unsigned long long)maxerr );
	printf( "tan points test passed\n" );
	return 0;
}
