/* SPDX-License-Identifier: MIT */
/* Fast points test for fixExpExact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn exp --emit-points`: every libfixmath
 * edge, 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its
 * correctly rounded raw output. fixExpExact is correctly rounded over every input,
 * so every point must match bit for bit.
 */
#include "fixed/fixed_exp.h"

#include <stdint.h>
#include <stdio.h>

#include "data/exp_points.h"

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
	for ( size_t i = 0; i < EXP_POINTS_COUNT; ++i )
	{
		int64_t raw = exp_points_raw[i];
		int64_t expected = exp_points_expected[i];
		int64_t result = fixExpExact( raw );
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
	printf( "exact points fn=exp inputs=%zu maxerr=%llu\n", (size_t)EXP_POINTS_COUNT,
			(unsigned long long)maxerr );
	printf( "exp points test passed\n" );
	return 0;
}
