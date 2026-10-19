/* SPDX-License-Identifier: MIT */
/* Fast points test for fixExpExact, with no MPFR.
 *
 * The expected integers are RN( exp( raw * 2^-16 ) * 2^16 ) at 256-bit MPFR precision
 * (round to nearest-even), from a throwaway program in the job directory; its source and
 * output are in the pull request body. The named cases are the design's section 5 set
 * (-772244 -> 0, -772243 -> 1, 2135026, 2135027 -> INT64_MAX, INT64_MIN -> 0,
 * INT64_MAX -> INT64_MAX, 0 -> 65536, and the exhaustive margin raw -129660), plus 64
 * raws spread across [-772243, 2135026], including every k * 65536 for k = -11..32.
 */
#include "fixed/fixed_exp.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int g_fail = 0;

static void check_point( int64_t raw, int64_t want )
{
	int64_t got = fixExpExact( (fixed_t)raw );
	if ( got != want )
	{
		printf( "FAIL raw=%lld got=%lld want=%lld\n", (long long)raw, (long long)got, (long long)want );
		++g_fail;
	}
}

static const struct { int64_t raw; int64_t want; } kNamed[] = {
	{ INT64_MIN, INT64_C( 0 ) },
	{ INT64_C( -772244 ), INT64_C( 0 ) },
	{ INT64_C( -772243 ), INT64_C( 1 ) },
	{ INT64_C( -129660 ), INT64_C( 9062 ) },
	{ INT64_C( 0 ), INT64_C( 65536 ) },
	{ INT64_C( 2135026 ), INT64_C( 9223315688087014081 ) },
	{ INT64_C( 2135027 ), INT64_MAX },
	{ INT64_MAX, INT64_MAX },
};

static const struct { int64_t raw; int64_t want; } kSpread[64] = {
	{ INT64_C( -720896 ), INT64_C( 1 ) },
	{ INT64_C( -655360 ), INT64_C( 3 ) },
	{ INT64_C( -589824 ), INT64_C( 8 ) },
	{ INT64_C( -524288 ), INT64_C( 22 ) },
	{ INT64_C( -458752 ), INT64_C( 60 ) },
	{ INT64_C( -393216 ), INT64_C( 162 ) },
	{ INT64_C( -327680 ), INT64_C( 442 ) },
	{ INT64_C( -262144 ), INT64_C( 1200 ) },
	{ INT64_C( -196608 ), INT64_C( 3263 ) },
	{ INT64_C( -131072 ), INT64_C( 8869 ) },
	{ INT64_C( -65536 ), INT64_C( 24109 ) },
	{ INT64_C( 0 ), INT64_C( 65536 ) },
	{ INT64_C( 65536 ), INT64_C( 178145 ) },
	{ INT64_C( 131072 ), INT64_C( 484249 ) },
	{ INT64_C( 196608 ), INT64_C( 1316326 ) },
	{ INT64_C( 262144 ), INT64_C( 3578144 ) },
	{ INT64_C( 327680 ), INT64_C( 9726405 ) },
	{ INT64_C( 393216 ), INT64_C( 26439109 ) },
	{ INT64_C( 458752 ), INT64_C( 71868951 ) },
	{ INT64_C( 524288 ), INT64_C( 195360063 ) },
	{ INT64_C( 589824 ), INT64_C( 531043708 ) },
	{ INT64_C( 655360 ), INT64_C( 1443526462 ) },
	{ INT64_C( 720896 ), INT64_C( 3923911751 ) },
	{ INT64_C( 786432 ), INT64_C( 10666298010 ) },
	{ INT64_C( 851968 ), INT64_C( 28994004059 ) },
	{ INT64_C( 917504 ), INT64_C( 78813874367 ) },
	{ INT64_C( 983040 ), INT64_C( 214238322522 ) },
	{ INT64_C( 1048576 ), INT64_C( 582360139072 ) },
	{ INT64_C( 1114112 ), INT64_C( 1583018983658 ) },
	{ INT64_C( 1179648 ), INT64_C( 4303091737384 ) },
	{ INT64_C( 1245184 ), INT64_C( 11697016075923 ) },
	{ INT64_C( 1310720 ), INT64_C( 31795786246376 ) },
	{ INT64_C( 1376256 ), INT64_C( 86429907975092 ) },
	{ INT64_C( 1441792 ), INT64_C( 234940848284080 ) },
	{ INT64_C( 1507328 ), INT64_C( 638635438653368 ) },
	{ INT64_C( 1572864 ), INT64_C( 1735991107901422 ) },
	{ INT64_C( 1638400 ), INT64_C( 4718913082974921 ) },
	{ INT64_C( 1703936 ), INT64_C( 12827335683528377 ) },
	{ INT64_C( 1769472 ), INT64_C( 34868313496079474 ) },
	{ INT64_C( 1835008 ), INT64_C( 94781902965406117 ) },
	{ INT64_C( 1900544 ), INT64_C( 257643924497631941 ) },
	{ INT64_C( 1966080 ), INT64_C( 700348798174787151 ) },
	{ INT64_C( 2031616 ), INT64_C( 1903745411661655260 ) },
	{ INT64_C( 2097152 ), INT64_C( 5174916558532162038 ) },
	{ INT64_C( -772243 ), INT64_C( 1 ) },
	{ INT64_C( -700000 ), INT64_C( 2 ) },
	{ INT64_C( -600000 ), INT64_C( 7 ) },
	{ INT64_C( -400000 ), INT64_C( 146 ) },
	{ INT64_C( -300000 ), INT64_C( 674 ) },
	{ INT64_C( -200000 ), INT64_C( 3098 ) },
	{ INT64_C( -100000 ), INT64_C( 14249 ) },
	{ INT64_C( -50000 ), INT64_C( 30559 ) },
	{ INT64_C( -25000 ), INT64_C( 44752 ) },
	{ INT64_C( 12345 ), INT64_C( 79120 ) },
	{ INT64_C( 50000 ), INT64_C( 140547 ) },
	{ INT64_C( 100000 ), INT64_C( 301412 ) },
	{ INT64_C( 200000 ), INT64_C( 1386250 ) },
	{ INT64_C( 500000 ), INT64_C( 134860224 ) },
	{ INT64_C( 1000000 ), INT64_C( 277515868272 ) },
	{ INT64_C( 1500000 ), INT64_C( 571073182407451 ) },
	{ INT64_C( 1800000 ), INT64_C( 55556404754405034 ) },
	{ INT64_C( 2000000 ), INT64_C( 1175156511575312201 ) },
	{ INT64_C( 2100000 ), INT64_C( 5404761078472608332 ) },
	{ INT64_C( 2135026 ), INT64_C( 9223315688087014081 ) },
};

int main( void )
{
	for ( size_t i = 0; i < sizeof( kNamed ) / sizeof( kNamed[0] ); ++i )
	{
		check_point( kNamed[i].raw, kNamed[i].want );
	}

	for ( size_t i = 0; i < sizeof( kSpread ) / sizeof( kSpread[0] ); ++i )
	{
		check_point( kSpread[i].raw, kSpread[i].want );
	}

	if ( g_fail != 0 )
	{
		printf( "exp exact test failed: %d\n", g_fail );
		return 1;
	}
	printf( "exp exact test passed\n" );
	return 0;
}
