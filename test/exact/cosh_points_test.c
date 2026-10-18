/* SPDX-License-Identifier: MIT */
/* Fast points test for fixCoshExact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn cosh --emit-points`: every libfixmath
 * edge, 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its
 * correctly rounded raw output. fixCoshExact is correctly rounded, so every point must
 * match bit for bit. The named cases of docs/design/transcendentals.md section 5 and
 * evenness on every point are checked beside them.
 */
#include "fixed/fixed_sinh_cosh.h"

#include <stdint.h>
#include <stdio.h>

#include "data/cosh_points.h"

static uint64_t g_maxerr;
static int g_fails;
static int64_t g_fail_raw;
static int64_t g_fail_result;
static int64_t g_fail_expected;

static uint64_t diff( int64_t a, int64_t b )
{
	if ( a >= b )
	{
		return (uint64_t)a - (uint64_t)b;
	}
	return (uint64_t)b - (uint64_t)a;
}

static void check( int64_t raw, int64_t result, int64_t expected )
{
	uint64_t e = diff( result, expected );
	if ( e > g_maxerr )
	{
		g_maxerr = e;
	}
	if ( e != 0 )
	{
		if ( g_fails == 0 )
		{
			g_fail_raw = raw;
			g_fail_result = result;
			g_fail_expected = expected;
		}
		g_fails++;
	}
}

static const struct { int64_t raw; int64_t expected; } kNamed[] = {
	{ INT64_C( 0 ), INT64_C( 65536 ) },
	{ INT64_C( 2344 ), INT64_C( 65578 ) },
	{ INT64_C( 2345 ), INT64_C( 65578 ) },
	{ INT64_C( 4096 ), INT64_C( 65664 ) },
	{ INT64_C( 4097 ), INT64_C( 65664 ) },
	{ INT64_C( 441619 ), INT64_C( 27667812 ) },
	{ INT64_C( 2180452 ), INT64_C( 9223302511604728222 ) },
	{ INT64_C( 2180453 ), INT64_MAX },
	{ INT64_C( -2180452 ), INT64_C( 9223302511604728222 ) },
	{ INT64_C( -2180453 ), INT64_MAX },
	{ INT64_MIN, INT64_MAX },
};

int main( void )
{
	for ( size_t i = 0; i < COSH_POINTS_COUNT; ++i )
	{
		int64_t raw = cosh_points_raw[i];
		check( raw, fixCoshExact( raw ), cosh_points_expected[i] );
	}
	for ( size_t i = 0; i < sizeof( kNamed ) / sizeof( kNamed[0] ); ++i )
	{
		check( kNamed[i].raw, fixCoshExact( kNamed[i].raw ), kNamed[i].expected );
	}
	for ( size_t i = 0; i < COSH_POINTS_COUNT; ++i )
	{
		int64_t raw = cosh_points_raw[i];
		if ( raw == INT64_MIN )
		{
			continue;
		}
		check( raw, fixCoshExact( raw ), fixCoshExact( -raw ) );
	}
	printf( "exact points fn=cosh inputs=%zu maxerr=%llu\n", (size_t)COSH_POINTS_COUNT,
			(unsigned long long)g_maxerr );
	if ( g_fails )
	{
		printf( "FAIL raw=%lld result=%lld expected=%lld\n", (long long)g_fail_raw,
				(long long)g_fail_result, (long long)g_fail_expected );
		return 1;
	}
	printf( "cosh points test passed\n" );
	return 0;
}
