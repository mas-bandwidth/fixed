/* SPDX-License-Identifier: MIT */
/* Fast points test for fixTanExact, with no MPFR and no clock.
 *
 * Expected values come from a throwaway MPFR program at 256+ bits: y = tan(raw * 2^-16) * 2^16,
 * round to nearest even, with the design's saturation at 2^63 - 3/2 to +/-INT64_MAX (never
 * INT64_MIN). The named points are docs/design/transcendentals.md section 3.1 and section 5:
 * the pole convergents, the three near-edge unsaturated raws, raw 1750043, the margin raw
 * 557409, INT64_MIN, both sides of pi/4 (the j = 101 cell) and of 1/256 (the path switch),
 * plus a spread of 64 raws inside |raw| < 2^21 and 32 above it.
 */
#include "fixed/fixed_tan.h"

#include <stdint.h>
#include <stdio.h>

typedef struct tanExactPoint
{
	int64_t raw;
	int64_t expected;
} tanExactPoint;

static const tanExactPoint points[] = {
	/* named cases: 0, +/-1, the convergents, 1750043, the margin, INT64_MIN,
	 * pi/4 and 1/256 from both sides, and the saturation signs. */
	{ INT64_C( 0 ), INT64_C( 0 ) },
	{ INT64_C( 1 ), INT64_C( 1 ) },
	{ INT64_C( -1 ), INT64_C( -1 ) },
	{ INT64_C( 195734331562903 ), INT64_C( 9223372036854775807 ) },
	{ INT64_C( 130146176612260 ), INT64_C( -8558550789933375681 ) },
	{ INT64_C( 6173953965003744831 ), INT64_C( 9223372036854775807 ) },
	{ INT64_C( 2544546310317739 ), INT64_C( 9114241824554695325 ) },
	{ INT64_C( 5870999813598064 ), INT64_C( -8755794028678553344 ) },
	{ INT64_C( 8285399947303543 ), INT64_C( 8900715308678236010 ) },
	{ INT64_C( 1750043 ), INT64_C( 115337932560 ) },
	{ INT64_C( 557409 ), INT64_C( -85957 ) },
	{ INT64_MIN, INT64_C( -4261 ) },
	{ INT64_C( 51454 ), INT64_C( 65500 ) },
	{ INT64_C( 51455 ), INT64_C( 65502 ) },
	{ INT64_C( 51471 ), INT64_C( 65534 ) },
	{ INT64_C( 51472 ), INT64_C( 65536 ) },
	{ INT64_C( -51454 ), INT64_C( -65500 ) },
	{ INT64_C( -51455 ), INT64_C( -65502 ) },
	{ INT64_C( -51471 ), INT64_C( -65534 ) },
	{ INT64_C( -51472 ), INT64_C( -65536 ) },
	{ INT64_C( 255 ), INT64_C( 255 ) },
	{ INT64_C( 256 ), INT64_C( 256 ) },
	{ INT64_C( 257 ), INT64_C( 257 ) },
	{ INT64_C( -255 ), INT64_C( -255 ) },
	{ INT64_C( -256 ), INT64_C( -256 ) },
	{ INT64_C( -257 ), INT64_C( -257 ) },
	{ INT64_C( -195734331562903 ), INT64_C( -9223372036854775807 ) },
	{ INT64_C( -6173953965003744831 ), INT64_C( -9223372036854775807 ) },
	{ INT64_C( -130146176612260 ), INT64_C( 8558550789933375681 ) },
	{ INT64_C( 9223372036854775807 ), INT64_C( 4260 ) },
	/* 64 raws spread over |raw| < 2^21. */
	{ INT64_C( 1 ), INT64_C( 1 ) },
	{ INT64_C( -1539747 ), INT64_C( -972750 ) },
	{ INT64_C( 982342 ), INT64_C( -57319 ) },
	{ INT64_C( -424937 ), INT64_C( -13342 ) },
	{ INT64_C( 1964683 ), INT64_C( -487790 ) },
	{ INT64_C( -1407278 ), INT64_C( 37331 ) },
	{ INT64_C( 849873 ), INT64_C( 27837 ) },
	{ INT64_C( -292468 ), INT64_C( -257001 ) },
	{ INT64_C( 1832214 ), INT64_C( -21498 ) },
	{ INT64_C( -1274809 ), INT64_C( -45075 ) },
	{ INT64_C( 717404 ), INT64_C( 1340290 ) },
	{ INT64_C( -159999 ), INT64_C( 55223 ) },
	{ INT64_C( 1699745 ), INT64_C( 67927 ) },
	{ INT64_C( -1142340 ), INT64_C( 427931 ) },
	{ INT64_C( 584935 ), INT64_C( -35750 ) },
	{ INT64_C( -27530 ), INT64_C( -29272 ) },
	{ INT64_C( 1567276 ), INT64_C( -177994 ) },
	{ INT64_C( -1009871 ), INT64_C( 20169 ) },
	{ INT64_C( 452466 ), INT64_C( 46876 ) },
	{ INT64_C( -1992212 ), INT64_C( 106030 ) },
	{ INT64_C( 1434807 ), INT64_C( -6425 ) },
	{ INT64_C( -877402 ), INT64_C( -70479 ) },
	{ INT64_C( 319997 ), INT64_C( -380922 ) },
	{ INT64_C( -1859743 ), INT64_C( -6780 ) },
	{ INT64_C( 1302338 ), INT64_C( 107313 ) },
	{ INT64_C( -744933 ), INT64_C( 168367 ) },
	{ INT64_C( 187528 ), INT64_C( -18855 ) },
	{ INT64_C( -1727274 ), INT64_C( -180980 ) },
	{ INT64_C( 1169869 ), INT64_C( -101791 ) },
	{ INT64_C( -612464 ), INT64_C( 5209 ) },
	{ INT64_C( 55059 ), INT64_C( 73134 ) },
	{ INT64_C( -1594805 ), INT64_C( 67202 ) },
	{ INT64_C( 1037400 ), INT64_C( 8002 ) },
	{ INT64_C( -479995 ), INT64_C( -111893 ) },
	{ INT64_C( 2019741 ), INT64_C( -44559 ) },
	{ INT64_C( -1462336 ), INT64_C( -21887 ) },
	{ INT64_C( 904931 ), INT64_C( 191948 ) },
	{ INT64_C( -347526 ), INT64_C( 97787 ) },
	{ INT64_C( 1887272 ), INT64_C( 37798 ) },
	{ INT64_C( -1329867 ), INT64_C( -508429 ) },
	{ INT64_C( 772462 ), INT64_C( -64772 ) },
	{ INT64_C( -215057 ), INT64_C( -9230 ) },
	{ INT64_C( 1754803 ), INT64_C( -900724 ) },
	{ INT64_C( -1197398 ), INT64_C( 42817 ) },
	{ INT64_C( 639993 ), INT64_C( 23237 ) },
	{ INT64_C( -82588 ), INT64_C( -204166 ) },
	{ INT64_C( 1622334 ), INT64_C( -26016 ) },
	{ INT64_C( -1064929 ), INT64_C( -39424 ) },
	{ INT64_C( 507524 ), INT64_C( 594576 ) },
	{ INT64_C( -2047270 ), INT64_C( 11727 ) },
	{ INT64_C( 1489865 ), INT64_C( 60128 ) },
	{ INT64_C( -932460 ), INT64_C( 717842 ) },
	{ INT64_C( 375055 ), INT64_C( -41115 ) },
	{ INT64_C( -1914801 ), INT64_C( -90342 ) },
	{ INT64_C( 1357396 ), INT64_C( -218128 ) },
	{ INT64_C( -799991 ), INT64_C( 24629 ) },
	{ INT64_C( 242586 ), INT64_C( 41086 ) },
	{ INT64_C( -1782332 ), INT64_C( 122075 ) },
	{ INT64_C( 1224927 ), INT64_C( -10486 ) },
	{ INT64_C( -667522 ), INT64_C( -62389 ) },
	{ INT64_C( 110117 ), INT64_C( -596351 ) },
	{ INT64_C( -1649863 ), INT64_C( -2765 ) },
	{ INT64_C( 1092458 ), INT64_C( 93933 ) },
	{ INT64_C( -535053 ), INT64_C( 204394 ) },
	/* 32 raws above 2^21, spread to INT64_MAX. */
	{ INT64_C( 2097153 ), INT64_C( 43321 ) },
	{ INT64_C( -297528130223151303 ), INT64_C( 9373 ) },
	{ INT64_C( 595056260444205453 ), INT64_C( -77397 ) },
	{ INT64_C( -892584390665259603 ), INT64_C( -2783615 ) },
	{ INT64_C( 1190112520886313753 ), INT64_C( 70393 ) },
	{ INT64_C( -1487640651107367903 ), INT64_C( -6243 ) },
	{ INT64_C( 1785168781328422053 ), INT64_C( -47901 ) },
	{ INT64_C( -2082696911549476203 ), INT64_C( 302300 ) },
	{ INT64_C( 2380225041770530353 ), INT64_C( 116436 ) },
	{ INT64_C( -2677753171991584503 ), INT64_C( -22594 ) },
	{ INT64_C( 2975281302212638653 ), INT64_C( -27257 ) },
	{ INT64_C( -3272809432433692803 ), INT64_C( 135492 ) },
	{ INT64_C( 3570337562654746953 ), INT64_C( 231719 ) },
	{ INT64_C( -3867865692875801103 ), INT64_C( -41917 ) },
	{ INT64_C( 4165393823096855253 ), INT64_C( -10382 ) },
	{ INT64_C( -4462921953317909403 ), INT64_C( 79804 ) },
	{ INT64_C( 4760450083538963553 ), INT64_C( 1697011 ) },
	{ INT64_C( -5057978213760017703 ), INT64_C( -68301 ) },
	{ INT64_C( 5355506343981071853 ), INT64_C( 5248 ) },
	{ INT64_C( -5653034474202126003 ), INT64_C( 49432 ) },
	{ INT64_C( 5950562604423180153 ), INT64_C( -325930 ) },
	{ INT64_C( -6248090734644234303 ), INT64_C( -112441 ) },
	{ INT64_C( 6545618864865288453 ), INT64_C( 21496 ) },
	{ INT64_C( -6843146995086342603 ), INT64_C( 28422 ) },
	{ INT64_C( 7140675125307396753 ), INT64_C( -140865 ) },
	{ INT64_C( -7438203255528450903 ), INT64_C( -219066 ) },
	{ INT64_C( 7735731385749505053 ), INT64_C( 40539 ) },
	{ INT64_C( -8033259515970559203 ), INT64_C( 11396 ) },
	{ INT64_C( 8330787646191613353 ), INT64_C( -82300 ) },
	{ INT64_C( -8628315776412667503 ), INT64_C( -1220159 ) },
	{ INT64_C( 8925843906633721653 ), INT64_C( 66274 ) },
	{ INT64_C( -9223372036854775803 ), INT64_C( -4256 ) },
};

int main( void )
{
	int failures = 0;

	for ( size_t i = 0; i < sizeof( points ) / sizeof( points[0] ); ++i )
	{
		int64_t raw = points[i].raw;
		int64_t expected = points[i].expected;
		int64_t got = fixTanExact( raw );

		if ( got != expected )
		{
			printf( "FAIL raw=%lld result=%lld expected=%lld\n", (long long)raw, (long long)got, (long long)expected );
			failures++;
		}
		if ( got == INT64_MIN )
		{
			printf( "FAIL raw=%lld returned INT64_MIN, which saturation must never do\n", (long long)raw );
			failures++;
		}
		if ( raw != INT64_MIN )
		{
			int64_t negated = fixTanExact( -(int64_t)raw );
			if ( negated != -got )
			{
				printf( "FAIL oddness raw=%lld: f(-x)=%lld f(x)=%lld\n", (long long)raw, (long long)negated, (long long)got );
				failures++;
			}
		}
	}

	if ( failures != 0 )
	{
		printf( "%d tan exact checks failed\n", failures );
		return 1;
	}
	printf( "tan exact test passed\n" );
	return 0;
}
