/* SPDX-License-Identifier: MIT */
/* Fast points test for fixExpExact, with no MPFR.
 *
 * The expected integers are RN( exp( raw * 2^-16 ) * 2^16 ) computed at 256-bit
 * MPFR precision (round to nearest-even), verified exhaustively with 0 mismatches
 * across the entire non-trivial Q48.16 domain [-772243, 2135026] (2,907,270 inputs).
 * Named cases cover the design's section 5 set (-772244 -> 0, -772243 -> 1,
 * 2135026, 2135027 -> INT64_MAX, INT64_MIN -> 0, INT64_MAX -> INT64_MAX, 0 -> 65536,
 * and the exhaustive margin raw -129660) plus 64 raws spread across [-772243, 2135026].
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
	{ INT64_C( -1000000 ), INT64_C( 0 ) },
	{ INT64_C( -772245 ), INT64_C( 0 ) },
	{ INT64_C( -772244 ), INT64_C( 0 ) },
	{ INT64_C( -772243 ), INT64_C( 1 ) },
	{ INT64_C( -772242 ), INT64_C( 1 ) },
	{ INT64_C( -129660 ), INT64_C( 9062 ) },
	{ INT64_C( -65536 ), INT64_C( 24109 ) },
	{ INT64_C( -45426 ), INT64_C( 32768 ) },
	{ INT64_C( -1 ), INT64_C( 65535 ) },
	{ INT64_C( 0 ), INT64_C( 65536 ) },
	{ INT64_C( 1 ), INT64_C( 65537 ) },
	{ INT64_C( 45426 ), INT64_C( 131072 ) },
	{ INT64_C( 65536 ), INT64_C( 178145 ) },
	{ INT64_C( 2135025 ), INT64_C( 9223174952532202517 ) },
	{ INT64_C( 2135026 ), INT64_C( 9223315688087014081 ) },
	{ INT64_C( 2135027 ), INT64_MAX },
	{ INT64_C( 2135028 ), INT64_MAX },
	{ INT64_C( 4194304 ), INT64_MAX },
	{ INT64_MAX, INT64_MAX },
};

static const struct { int64_t raw; int64_t want; } kSpread[64] = {
	{ INT64_C( -772243 ), INT64_C( 1 ) },
	{ INT64_C( -726096 ), INT64_C( 1 ) },
	{ INT64_C( -679949 ), INT64_C( 2 ) },
	{ INT64_C( -633802 ), INT64_C( 4 ) },
	{ INT64_C( -587655 ), INT64_C( 8 ) },
	{ INT64_C( -541508 ), INT64_C( 17 ) },
	{ INT64_C( -495361 ), INT64_C( 34 ) },
	{ INT64_C( -449214 ), INT64_C( 69 ) },
	{ INT64_C( -403066 ), INT64_C( 140 ) },
	{ INT64_C( -356919 ), INT64_C( 283 ) },
	{ INT64_C( -310772 ), INT64_C( 572 ) },
	{ INT64_C( -264625 ), INT64_C( 1156 ) },
	{ INT64_C( -218478 ), INT64_C( 2337 ) },
	{ INT64_C( -172331 ), INT64_C( 4726 ) },
	{ INT64_C( -126184 ), INT64_C( 9556 ) },
	{ INT64_C( -80037 ), INT64_C( 19324 ) },
	{ INT64_C( -33889 ), INT64_C( 39075 ) },
	{ INT64_C( 12258 ), INT64_C( 79015 ) },
	{ INT64_C( 58405 ), INT64_C( 159779 ) },
	{ INT64_C( 104552 ), INT64_C( 323092 ) },
	{ INT64_C( 150699 ), INT64_C( 653331 ) },
	{ INT64_C( 196846 ), INT64_C( 1321115 ) },
	{ INT64_C( 242993 ), INT64_C( 2671455 ) },
	{ INT64_C( 289140 ), INT64_C( 5402007 ) },
	{ INT64_C( 335288 ), INT64_C( 10923683 ) },
	{ INT64_C( 381435 ), INT64_C( 22089017 ) },
	{ INT64_C( 427582 ), INT64_C( 44666683 ) },
	{ INT64_C( 473729 ), INT64_C( 90321472 ) },
	{ INT64_C( 519876 ), INT64_C( 182641014 ) },
	{ INT64_C( 566023 ), INT64_C( 369322369 ) },
	{ INT64_C( 612170 ), INT64_C( 746814802 ) },
	{ INT64_C( 658317 ), INT64_C( 1510150465 ) },
	{ INT64_C( 704465 ), INT64_C( 3053754719 ) },
	{ INT64_C( 750612 ), INT64_C( 6175063882 ) },
	{ INT64_C( 796759 ), INT64_C( 12486731077 ) },
	{ INT64_C( 842906 ), INT64_C( 25249690685 ) },
	{ INT64_C( 889053 ), INT64_C( 51057949093 ) },
	{ INT64_C( 935200 ), INT64_C( 103245390137 ) },
	{ INT64_C( 981347 ), INT64_C( 208774750533 ) },
	{ INT64_C( 1027494 ), INT64_C( 422167967036 ) },
	{ INT64_C( 1073642 ), INT64_C( 853688060779 ) },
	{ INT64_C( 1119789 ), INT64_C( 1726261208227 ) },
	{ INT64_C( 1165936 ), INT64_C( 3490710361242 ) },
	{ INT64_C( 1212083 ), INT64_C( 7058641396799 ) },
	{ INT64_C( 1258230 ), INT64_C( 14273432399839 ) },
	{ INT64_C( 1304377 ), INT64_C( 28862618317054 ) },
	{ INT64_C( 1350524 ), INT64_C( 58363728693975 ) },
	{ INT64_C( 1396671 ), INT64_C( 118018566078987 ) },
	{ INT64_C( 1442819 ), INT64_C( 238651552649030 ) },
	{ INT64_C( 1488966 ), INT64_C( 482582498864063 ) },
	{ INT64_C( 1535113 ), INT64_C( 975840574363973 ) },
	{ INT64_C( 1581260 ), INT64_C( 1973268464597281 ) },
	{ INT64_C( 1627407 ), INT64_C( 3990189110462002 ) },
	{ INT64_C( 1673554 ), INT64_C( 8068648246754881 ) },
	{ INT64_C( 1719701 ), INT64_C( 16315789233940014 ) },
	{ INT64_C( 1765848 ), INT64_C( 32992512523199592 ) },
	{ INT64_C( 1811996 ), INT64_C( 66715895651404880 ) },
	{ INT64_C( 1858143 ), INT64_C( 134907664668570537 ) },
	{ INT64_C( 1904290 ), INT64_C( 272799724992438505 ) },
	{ INT64_C( 1950437 ), INT64_C( 551634261394843091 ) },
	{ INT64_C( 1996584 ), INT64_C( 1115471646289484742 ) },
	{ INT64_C( 2042731 ), INT64_C( 2255619494208967534 ) },
	{ INT64_C( 2088878 ), INT64_C( 4561137272811629026 ) },
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

	for ( int i = 0; i + 1 < 64; ++i )
	{
		if ( fixExpExact( kSpread[i].raw ) > fixExpExact( kSpread[i + 1].raw ) )
		{
			printf( "FAIL monotonicity at index %d\n", i );
			++g_fail;
		}
	}

	if ( g_fail != 0 )
	{
		printf( "exp exact test failed: %d\n", g_fail );
		return 1;
	}
	printf( "exp exact test passed\n" );
	return 0;
}
