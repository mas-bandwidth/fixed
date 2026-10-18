/* SPDX-License-Identifier: MIT */
#include "fixed/fixed_log.h"
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>

// The named cases and the 32 seeded raws per function are the integers only. The
// expected values for the named cases (raw 1, 65536, 541866, 692513, 967694, 655360,
// INT64_MAX, the two fixLog2 witnesses) come from a throwaway MPFR program at 256 bits
// (RN to nearest even of f(raw * 2^-16) * 2^16) written in the job directory and never
// committed; its source and output are in the pull request body. The 32 raws per
// function use the same oracle.

static int g_fail = 0;
#define CHECK( cond )                                                                          \
	do                                                                                         \
	{                                                                                          \
		if ( !( cond ) )                                                                       \
		{                                                                                      \
			printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond );                           \
			++g_fail;                                                                          \
		}                                                                                      \
	} while ( 0 )

static const struct { int64_t raw; int64_t want; } kLnRandom[] = {
	{ INT64_C( 6 ), INT64_C( -609393 ) }, { INT64_C( 20 ), INT64_C( -530489 ) }, { INT64_C( 102 ), INT64_C( -423715 ) }, { INT64_C( 392 ), INT64_C( -335485 ) },
	{ INT64_C( 1258 ), INT64_C( -259069 ) }, { INT64_C( 5196 ), INT64_C( -166115 ) }, { INT64_C( 26542 ), INT64_C( -59236 ) }, { INT64_C( 105232 ), INT64_C( 31036 ) },
	{ INT64_C( 429682 ), INT64_C( 123237 ) }, { INT64_C( 1409492 ), INT64_C( 201090 ) }, { INT64_C( 4486454 ), INT64_C( 276969 ) }, { INT64_C( 20146328 ), INT64_C( 375402 ) },
	{ INT64_C( 94526458 ), INT64_C( 476711 ) }, { INT64_C( 504450908 ), INT64_C( 586457 ) }, { INT64_C( 1249919678 ), INT64_C( 645922 ) }, { INT64_C( 7632533024 ), INT64_C( 764499 ) },
	{ INT64_C( 21531339138 ), INT64_C( 832466 ) }, { INT64_C( 95559687396 ), INT64_C( 930130 ) }, { INT64_C( 375746465862 ), INT64_C( 1019860 ) }, { INT64_C( 1205689058216 ), INT64_C( 1096268 ) },
	{ INT64_C( 4509532813066 ), INT64_C( 1182719 ) }, { INT64_C( 17708981217900 ), INT64_C( 1272364 ) }, { INT64_C( 70490848222670 ), INT64_C( 1362896 ) }, { INT64_C( 281602389627184 ), INT64_C( 1453665 ) },
	{ INT64_C( 1126032628630674 ), INT64_C( 1544495 ) }, { INT64_C( 4503737658030068 ), INT64_C( 1635341 ) }, { INT64_C( 18014541849013078 ), INT64_C( 1726192 ) }, { INT64_C( 72057742686330552 ), INT64_C( 1817044 ) },
	{ INT64_C( 288230530108985882 ), INT64_C( 1907896 ) }, { INT64_C( 1152921663872992636 ), INT64_C( 1998748 ) }, { INT64_C( 4611686183002405086 ), INT64_C( 2089600 ) }, { INT64_C( 2025344046841679278 ), INT64_C( 2035674 ) },
};

static const struct { int64_t raw; int64_t want; } kLog2Random[] = {
	{ INT64_C( 6 ), INT64_C( -879168 ) }, { INT64_C( 20 ), INT64_C( -765334 ) }, { INT64_C( 102 ), INT64_C( -611292 ) }, { INT64_C( 392 ), INT64_C( -484002 ) },
	{ INT64_C( 1258 ), INT64_C( -373757 ) }, { INT64_C( 5196 ), INT64_C( -239653 ) }, { INT64_C( 26542 ), INT64_C( -85460 ) }, { INT64_C( 105232 ), INT64_C( 44775 ) },
	{ INT64_C( 429682 ), INT64_C( 177793 ) }, { INT64_C( 1409492 ), INT64_C( 290111 ) }, { INT64_C( 4486454 ), INT64_C( 399582 ) }, { INT64_C( 20146328 ), INT64_C( 541590 ) },
	{ INT64_C( 94526458 ), INT64_C( 687749 ) }, { INT64_C( 504450908 ), INT64_C( 846079 ) }, { INT64_C( 1249919678 ), INT64_C( 931869 ) }, { INT64_C( 7632533024 ), INT64_C( 1102939 ) },
	{ INT64_C( 21531339138 ), INT64_C( 1200994 ) }, { INT64_C( 95559687396 ), INT64_C( 1341894 ) }, { INT64_C( 375746465862 ), INT64_C( 1471347 ) }, { INT64_C( 1205689058216 ), INT64_C( 1581580 ) },
	{ INT64_C( 4509532813066 ), INT64_C( 1706303 ) }, { INT64_C( 17708981217900 ), INT64_C( 1835634 ) }, { INT64_C( 70490848222670 ), INT64_C( 1966244 ) }, { INT64_C( 281602389627184 ), INT64_C( 2097195 ) },
	{ INT64_C( 1126032628630674 ), INT64_C( 2228235 ) }, { INT64_C( 4503737658030068 ), INT64_C( 2359299 ) }, { INT64_C( 18014541849013078 ), INT64_C( 2490369 ) }, { INT64_C( 72057742686330552 ), INT64_C( 2621440 ) },
	{ INT64_C( 288230530108985882 ), INT64_C( 2752512 ) }, { INT64_C( 1152921663872992636 ), INT64_C( 2883584 ) }, { INT64_C( 4611686183002405086 ), INT64_C( 3014656 ) }, { INT64_C( 2025344046841679278 ), INT64_C( 2936856 ) },
};

static const struct { int64_t raw; int64_t want; } kLog10Random[] = {
	{ INT64_C( 6 ), INT64_C( -264656 ) }, { INT64_C( 20 ), INT64_C( -230389 ) }, { INT64_C( 102 ), INT64_C( -184017 ) }, { INT64_C( 392 ), INT64_C( -145699 ) },
	{ INT64_C( 1258 ), INT64_C( -112512 ) }, { INT64_C( 5196 ), INT64_C( -72143 ) }, { INT64_C( 26542 ), INT64_C( -25726 ) }, { INT64_C( 105232 ), INT64_C( 13479 ) },
	{ INT64_C( 429682 ), INT64_C( 53521 ) }, { INT64_C( 1409492 ), INT64_C( 87332 ) }, { INT64_C( 4486454 ), INT64_C( 120286 ) }, { INT64_C( 20146328 ), INT64_C( 163035 ) },
	{ INT64_C( 94526458 ), INT64_C( 207033 ) }, { INT64_C( 504450908 ), INT64_C( 254695 ) }, { INT64_C( 1249919678 ), INT64_C( 280520 ) }, { INT64_C( 7632533024 ), INT64_C( 332018 ) },
	{ INT64_C( 21531339138 ), INT64_C( 361535 ) }, { INT64_C( 95559687396 ), INT64_C( 403950 ) }, { INT64_C( 375746465862 ), INT64_C( 442919 ) }, { INT64_C( 1205689058216 ), INT64_C( 476103 ) },
	{ INT64_C( 4509532813066 ), INT64_C( 513648 ) }, { INT64_C( 17708981217900 ), INT64_C( 552581 ) }, { INT64_C( 70490848222670 ), INT64_C( 591898 ) }, { INT64_C( 281602389627184 ), INT64_C( 631319 ) },
	{ INT64_C( 1126032628630674 ), INT64_C( 670766 ) }, { INT64_C( 4503737658030068 ), INT64_C( 710220 ) }, { INT64_C( 18014541849013078 ), INT64_C( 749676 ) }, { INT64_C( 72057742686330552 ), INT64_C( 789132 ) },
	{ INT64_C( 288230530108985882 ), INT64_C( 828589 ) }, { INT64_C( 1152921663872992636 ), INT64_C( 868045 ) }, { INT64_C( 4611686183002405086 ), INT64_C( 907502 ) }, { INT64_C( 2025344046841679278 ), INT64_C( 884082 ) },
};

static void test_ln( void )
{
	CHECK( fixLnExact( -1 ) == INT64_MIN );
	CHECK( fixLnExact( 0 ) == INT64_MIN );
	CHECK( fixLnExact( INT64_MIN ) == INT64_MIN );
	CHECK( fixLnExact( 1 ) == INT64_C( -726817 ) );
	CHECK( fixLnExact( 65536 ) == 0 );
	CHECK( fixLnExact( 541866 ) == INT64_C( 138440 ) );
	CHECK( fixLnExact( INT64_MAX ) == INT64_C( 2135026 ) );

	for ( size_t i = 0; i < sizeof( kLnRandom ) / sizeof( kLnRandom[0] ); ++i )
	{
		CHECK( fixLnExact( kLnRandom[i].raw ) == kLnRandom[i].want );
	}
}

static void test_log2( void )
{
	CHECK( fixLog2Exact( -1 ) == INT64_MIN );
	CHECK( fixLog2Exact( 0 ) == INT64_MIN );
	CHECK( fixLog2Exact( INT64_MIN ) == INT64_MIN );
	CHECK( fixLog2Exact( 65536 ) == 0 );
	CHECK( fixLog2Exact( 692513 ) == INT64_C( 222920 ) );

	// 2^k raws: log2 is exactly k - 16.
	{
		const int ks[] = { 16, 17, 18, 24, 31, 40, 47 };
		for ( size_t i = 0; i < sizeof( ks ) / sizeof( ks[0] ); ++i )
		{
			CHECK( fixLog2Exact( (int64_t)1 << ks[i] ) == ( (int64_t)ks[i] - 16 ) * 65536 );
		}
	}

	// The two fixLog2 witnesses of design section 2.2, and fixLog2 unchanged there.
	CHECK( fixLog2Exact( INT64_C( 208707216768631 ) ) == INT64_C( 2068872 ) );
	CHECK( fixLog2Exact( INT64_C( 4611710406440186476 ) ) == INT64_C( 3014657 ) );
	CHECK( fixLog2( INT64_C( 208707216768631 ) ) == INT64_C( 2068871 ) );
	CHECK( fixLog2( INT64_C( 4611710406440186476 ) ) == INT64_C( 3014656 ) );

	CHECK( fixLog2Exact( INT64_MAX ) == INT64_C( 3080192 ) );

	for ( size_t i = 0; i < sizeof( kLog2Random ) / sizeof( kLog2Random[0] ); ++i )
	{
		CHECK( fixLog2Exact( kLog2Random[i].raw ) == kLog2Random[i].want );
	}
}

static void test_log10( void )
{
	CHECK( fixLog10Exact( -1 ) == INT64_MIN );
	CHECK( fixLog10Exact( 0 ) == INT64_MIN );
	CHECK( fixLog10Exact( INT64_MIN ) == INT64_MIN );
	CHECK( fixLog10Exact( 655360 ) == 65536 );
	CHECK( fixLog10Exact( 967694 ) == INT64_C( 76628 ) );
	CHECK( fixLog10Exact( INT64_MAX ) == INT64_C( 927230 ) );

	for ( size_t i = 0; i < sizeof( kLog10Random ) / sizeof( kLog10Random[0] ); ++i )
	{
		CHECK( fixLog10Exact( kLog10Random[i].raw ) == kLog10Random[i].want );
	}
}

int main( void )
{
	test_ln();
	test_log2();
	test_log10();

	if ( g_fail == 0 )
	{
		printf( "log exact test passed\n" );
		return 0;
	}
	return 1;
}
