/* SPDX-License-Identifier: MIT */
/* Fast points test for fixAsinExact and fixAcosExact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn asin_acos --emit-points`: every libfixmath
 * edge, 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its
 * correctly rounded raw output. Both functions promise the correctly rounded raw value
 * on every input in [-1, 1], so every point must match bit for bit.
 */
#include "fixed/fixed_asin_acos.h"

#include <stdint.h>
#include <stdio.h>

#include "data/asin_acos_points.h"

/* Keep in step with sweep/fns/asin_acos.c: acos points are stored shifted by
 * ACOS_SHIFT, and the clamped inputs are carried as sentinels, so one header holds the
 * points of both functions. */
#define ACOS_SHIFT ( 1 << 20 )
#define ASIN_CLAMP_BASE ( 1 << 21 )
#define ACOS_CLAMP_BASE ( ( 1 << 21 ) + 16 )

static const int64_t clamp_raw[4] = { INT64_MIN, -65537, 65537, INT64_MAX };

static int is_acos_point( int64_t a )
{
	return ( a >= ACOS_SHIFT && a < ACOS_SHIFT + 65537 ) || ( a >= ACOS_CLAMP_BASE && a < ACOS_CLAMP_BASE + 4 );
}

static int64_t decode_raw( int64_t a )
{
	if ( a >= ACOS_SHIFT && a < ACOS_SHIFT + 65537 )
	{
		return a - ACOS_SHIFT;
	}
	if ( a >= ASIN_CLAMP_BASE && a < ASIN_CLAMP_BASE + 4 )
	{
		return clamp_raw[a - ASIN_CLAMP_BASE];
	}
	if ( a >= ACOS_CLAMP_BASE && a < ACOS_CLAMP_BASE + 4 )
	{
		return clamp_raw[a - ACOS_CLAMP_BASE];
	}
	return a;
}

static uint64_t diff( int64_t a, int64_t b )
{
	if ( a >= b )
	{
		return (uint64_t)a - (uint64_t)b;
	}
	return (uint64_t)b - (uint64_t)a;
}

static int check( const char* what, int64_t in, int64_t got, int64_t expected, uint64_t* maxerr, int64_t* worstin,
				   int64_t* worstgot, int64_t* worste, uint64_t* worstdiff )
{
	uint64_t e = diff( got, expected );
	if ( e > *maxerr )
	{
		*maxerr = e;
	}
	if ( e != 0 )
	{
		if ( e > *worstdiff )
		{
			*worstdiff = e;
			*worstin = in;
			*worstgot = got;
			*worste = expected;
		}
		printf( "FAIL %s raw=%lld result=%lld expected=%lld error=%llu\n", what, (long long)in, (long long)got,
				(long long)expected, (unsigned long long)e );
		return 1;
	}
	return 0;
}

int main( void )
{
	uint64_t maxerr = 0;
	uint64_t worstdiff = 0;
	int64_t worstin = 0;
	int64_t worstgot = 0;
	int64_t worste = 0;
	int fails = 0;

	for ( size_t i = 0; i < ASIN_ACOS_POINTS_COUNT; ++i )
	{
		int64_t raw = asin_acos_points_raw[i];
		int64_t expected = asin_acos_points_expected[i];
		int64_t in = decode_raw( raw );
		int64_t got = is_acos_point( raw ) ? fixAcosExact( in ) : fixAsinExact( in );
		fails += check( is_acos_point( raw ) ? "acos" : "asin", in, got, expected, &maxerr, &worstin, &worstgot, &worste,
						&worstdiff );
	}

	struct
	{
		int64_t in;
		int64_t asin;
		int64_t acos;
	} edges[] = {
		{ INT64_MIN, -102944, 205887 }, { -65537, -102944, 205887 }, { -65536, -102944, 205887 },
		{ 0, 0, 102944 },               { 65536, 102944, 0 },       { 65537, 102944, 0 },
		{ INT64_MAX, 102944, 0 },
	};
	for ( size_t i = 0; i < sizeof( edges ) / sizeof( edges[0] ); ++i )
	{
		fails += check( "asin edge", edges[i].in, fixAsinExact( edges[i].in ), edges[i].asin, &maxerr, &worstin,
						&worstgot, &worste, &worstdiff );
		fails += check( "acos edge", edges[i].in, fixAcosExact( edges[i].in ), edges[i].acos, &maxerr, &worstin,
						&worstgot, &worste, &worstdiff );
	}

	printf( "exact points fn=asin_acos inputs=%zu maxerr=%llu\n", (size_t)ASIN_ACOS_POINTS_COUNT,
			(unsigned long long)maxerr );
	if ( fails )
	{
		printf( "asin_acos exact test failed: worst raw=%lld asin/acos result=%lld expected=%lld\n", (long long)worstin,
				(long long)worstgot, (long long)worste );
		return 1;
	}
	printf( "asin_acos exact test passed\n" );
	return 0;
}
