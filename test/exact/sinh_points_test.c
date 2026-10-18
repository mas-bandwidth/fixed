/* SPDX-License-Identifier: MIT */
/* Fast points test for fixSinhExact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn sinh --emit-points`: every libfixmath edge,
 * 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its correctly
 * rounded raw output. fixSinhExact is correctly rounded on every input, so every point must
 * match bit for bit.
 */
#include "fixed/fixed_sinh_cosh.h"

#include <stdint.h>
#include <stdio.h>

#include "data/sinh_points.h"

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
	for ( size_t i = 0; i < SINH_POINTS_COUNT; ++i )
	{
		int64_t raw = sinh_points_raw[i];
		int64_t expected = sinh_points_expected[i];
		int64_t result = fixSinhExact( raw );
		uint64_t e = diff( result, expected );
		if ( e > maxerr )
		{
			maxerr = e;
		}
		if ( e != 0 )
		{
			printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)raw,
					(long long)result, (long long)expected, (unsigned long long)e );
			return 1;
		}
	}
	printf( "exact points fn=sinh inputs=%zu maxerr=%llu\n", (size_t)SINH_POINTS_COUNT,
			(unsigned long long)maxerr );
	printf( "sinh points test passed\n" );
	return 0;
}
