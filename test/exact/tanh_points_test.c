/* SPDX-License-Identifier: MIT */
/* Fast points test for fixTanhExact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn tanh --emit-points`: every libfixmath edge,
 * 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its correctly
 * rounded raw output. fixTanhExact is correctly rounded over every input, so every point
 * must match bit for bit.
 */
#include "fixed/fixed_tanh.h"

#include <stdint.h>
#include <stdio.h>

#include "data/tanh_points.h"

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
	for ( size_t i = 0; i < TANH_POINTS_COUNT; ++i )
	{
		int64_t raw = tanh_points_raw[i];
		int64_t expected = tanh_points_expected[i];
		uint64_t e = diff( fixTanhExact( raw ), expected );
		if ( e > maxerr )
		{
			maxerr = e;
		}
		if ( e != 0 )
		{
			printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)raw,
					(long long)fixTanhExact( raw ), (long long)expected, (unsigned long long)e );
			fails++;
		}
	}
	printf( "exact points fn=tanh inputs=%zu maxerr=%llu\n", (size_t)TANH_POINTS_COUNT, (unsigned long long)maxerr );
	if ( fails )
	{
		printf( "tanh points test failed: %d points differ\n", fails );
		return 1;
	}
	printf( "tanh points test passed\n" );
	return 0;
}
