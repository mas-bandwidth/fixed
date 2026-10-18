/* SPDX-License-Identifier: MIT */
#include "fixed/fixed_sinh_cosh.h"
#include <stdint.h>
#include <stdio.h>

// The named cases and the 64 spread raws are the integers only. The expected values come
// from a throwaway MPFR program at 256 bits (RN to nearest even of f(raw * 2^-16) * 2^16)
// written in the job directory and never committed; its source and output are in the pull
// request body.

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

static const struct { int64_t raw; int64_t sinh_want; int64_t cosh_want; } kNamed[] = {
	{ INT64_C( 0 ), INT64_C( 0 ), INT64_C( 65536 ) },
	{ INT64_C( 2344 ), INT64_C( 2344 ), INT64_C( 65578 ) },
	{ INT64_C( 2345 ), INT64_C( 2346 ), INT64_C( 65578 ) },
	{ INT64_C( 4096 ), INT64_C( 4099 ), INT64_C( 65664 ) },
	{ INT64_C( 4097 ), INT64_C( 4100 ), INT64_C( 65664 ) },
	{ INT64_C( 441619 ), INT64_C( 27667735 ), INT64_C( 27667812 ) },
	{ INT64_C( 2180452 ), INT64_C( 9223302511604728222 ), INT64_C( 9223302511604728222 ) },
	{ INT64_C( 2180453 ), INT64_MAX, INT64_MAX },
	{ INT64_C( -2180452 ), INT64_C( -9223302511604728222 ), INT64_C( 9223302511604728222 ) },
	{ INT64_C( -2180453 ), -INT64_MAX, INT64_MAX },
	{ INT64_MIN, -INT64_MAX, INT64_MAX },
};

static const struct { int64_t raw; int64_t sinh_want; int64_t cosh_want; } kRandom[64] = {
	{ INT64_C( 17034 ), INT64_C( 17226 ), INT64_C( 67762 ) },
	{ INT64_C( 51104 ), INT64_C( 56443 ), INT64_C( 86491 ) },
	{ INT64_C( 85173 ), INT64_C( 111258 ), INT64_C( 129125 ) },
	{ INT64_C( 119243 ), INT64_C( 196827 ), INT64_C( 207451 ) },
	{ INT64_C( 153313 ), INT64_C( 336800 ), INT64_C( 343117 ) },
	{ INT64_C( 187382 ), INT64_C( 569857 ), INT64_C( 573613 ) },
	{ INT64_C( 221452 ), INT64_C( 960429 ), INT64_C( 962662 ) },
	{ INT64_C( 255521 ), INT64_C( 1616442 ), INT64_C( 1617770 ) },
	{ INT64_C( 289591 ), INT64_C( 2719260 ), INT64_C( 2720050 ) },
	{ INT64_C( 323660 ), INT64_C( 4573622 ), INT64_C( 4574092 ) },
	{ INT64_C( 357730 ), INT64_C( 7692191 ), INT64_C( 7692470 ) },
	{ INT64_C( 391799 ), INT64_C( 12936710 ), INT64_C( 12936876 ) },
	{ INT64_C( 425869 ), INT64_C( 21757100 ), INT64_C( 21757198 ) },
	{ INT64_C( 459939 ), INT64_C( 36591228 ), INT64_C( 36591286 ) },
	{ INT64_C( 494008 ), INT64_C( 61538361 ), INT64_C( 61538396 ) },
	{ INT64_C( 528078 ), INT64_C( 103495472 ), INT64_C( 103495493 ) },
	{ INT64_C( 562147 ), INT64_C( 174056439 ), INT64_C( 174056452 ) },
	{ INT64_C( 596217 ), INT64_C( 292728798 ), INT64_C( 292728805 ) },
	{ INT64_C( 630286 ), INT64_C( 492304914 ), INT64_C( 492304919 ) },
	{ INT64_C( 664356 ), INT64_C( 827960307 ), INT64_C( 827960310 ) },
	{ INT64_C( 698426 ), INT64_C( 1392466842 ), INT64_C( 1392466844 ) },
	{ INT64_C( 732495 ), INT64_C( 2341820376 ), INT64_C( 2341820377 ) },
	{ INT64_C( 766565 ), INT64_C( 3938482548 ), INT64_C( 3938482549 ) },
	{ INT64_C( 800634 ), INT64_C( 6623654081 ), INT64_C( 6623654082 ) },
	{ INT64_C( 834704 ), INT64_C( 11139686999 ), INT64_C( 11139686999 ) },
	{ INT64_C( 868773 ), INT64_C( 18734482723 ), INT64_C( 18734482723 ) },
	{ INT64_C( 902843 ), INT64_C( 31507725351 ), INT64_C( 31507725351 ) },
	{ INT64_C( 936912 ), INT64_C( 52989005550 ), INT64_C( 52989005550 ) },
	{ INT64_C( 970982 ), INT64_C( 89117114052 ), INT64_C( 89117114052 ) },
	{ INT64_C( 1005052 ), INT64_C( 149877506373 ), INT64_C( 149877506373 ) },
	{ INT64_C( 1039121 ), INT64_C( 252060722520 ), INT64_C( 252060722520 ) },
	{ INT64_C( 1073191 ), INT64_C( 423916695998 ), INT64_C( 423916695998 ) },
	{ INT64_C( 1107260 ), INT64_C( 712933856905 ), INT64_C( 712933856905 ) },
	{ INT64_C( 1141330 ), INT64_C( 1199014912210 ), INT64_C( 1199014912210 ) },
	{ INT64_C( 1175399 ), INT64_C( 2016477137885 ), INT64_C( 2016477137885 ) },
	{ INT64_C( 1209469 ), INT64_C( 3391319033369 ), INT64_C( 3391319033369 ) },
	{ INT64_C( 1243539 ), INT64_C( 5703533439587 ), INT64_C( 5703533439587 ) },
	{ INT64_C( 1277608 ), INT64_C( 9592078187664 ), INT64_C( 9592078187664 ) },
	{ INT64_C( 1311678 ), INT64_C( 16131994117974 ), INT64_C( 16131994117974 ) },
	{ INT64_C( 1345747 ), INT64_C( 27130435990525 ), INT64_C( 27130435990525 ) },
	{ INT64_C( 1379817 ), INT64_C( 45628071962555 ), INT64_C( 45628071962555 ) },
	{ INT64_C( 1413886 ), INT64_C( 76736296622619 ), INT64_C( 76736296622619 ) },
	{ INT64_C( 1447956 ), INT64_C( 129055399834330 ), INT64_C( 129055399834330 ) },
	{ INT64_C( 1482025 ), INT64_C( 217042557716769 ), INT64_C( 217042557716769 ) },
	{ INT64_C( 1516095 ), INT64_C( 365023011274001 ), INT64_C( 365023011274001 ) },
	{ INT64_C( 1550165 ), INT64_C( 613897109217696 ), INT64_C( 613897109217696 ) },
	{ INT64_C( 1584234 ), INT64_C( 1032438773817937 ), INT64_C( 1032438773817937 ) },
	{ INT64_C( 1618304 ), INT64_C( 1736359514648052 ), INT64_C( 1736359514648052 ) },
	{ INT64_C( 1652373 ), INT64_C( 2920171574834103 ), INT64_C( 2920171574834103 ) },
	{ INT64_C( 1686443 ), INT64_C( 4911155825364343 ), INT64_C( 4911155825364343 ) },
	{ INT64_C( 1720512 ), INT64_C( 8259474791841582 ), INT64_C( 8259474791841582 ) },
	{ INT64_C( 1754582 ), INT64_C( 13890816583510910 ), INT64_C( 13890816583510910 ) },
	{ INT64_C( 1788652 ), INT64_C( 23361628943687270 ), INT64_C( 23361628943687270 ) },
	{ INT64_C( 1822721 ), INT64_C( 39289078216618593 ), INT64_C( 39289078216618593 ) },
	{ INT64_C( 1856791 ), INT64_C( 66076523386371087 ), INT64_C( 66076523386371087 ) },
	{ INT64_C( 1890860 ), INT64_C( 111126056400740415 ), INT64_C( 111126056400740415 ) },
	{ INT64_C( 1924930 ), INT64_C( 186892230561235987 ), INT64_C( 186892230561235987 ) },
	{ INT64_C( 1958999 ), INT64_C( 314311278648352931 ), INT64_C( 314311278648352931 ) },
	{ INT64_C( 1993069 ), INT64_C( 528609921558896200 ), INT64_C( 528609921558896200 ) },
	{ INT64_C( 2027138 ), INT64_C( 889004641083477960 ), INT64_C( 889004641083477960 ) },
	{ INT64_C( 2061208 ), INT64_C( 1495131436611253590 ), INT64_C( 1495131436611253590 ) },
	{ INT64_C( 2095278 ), INT64_C( 2514517820760537735 ), INT64_C( 2514517820760537735 ) },
	{ INT64_C( 2129347 ), INT64_C( 4228861248292266784 ), INT64_C( 4228861248292266784 ) },
	{ INT64_C( 2163417 ), INT64_C( 7112115169256097441 ), INT64_C( 7112115169256097441 ) },
};

static void check_odd_even( int64_t raw )
{
	if ( raw == INT64_MIN )
	{
		return;
	}
	CHECK( fixSinhExact( -raw ) == -fixSinhExact( raw ) );
	CHECK( fixCoshExact( -raw ) == fixCoshExact( raw ) );
}

int main( void )
{
	for ( size_t i = 0; i < sizeof( kNamed ) / sizeof( kNamed[0] ); ++i )
	{
		CHECK( fixSinhExact( kNamed[i].raw ) == kNamed[i].sinh_want );
		CHECK( fixCoshExact( kNamed[i].raw ) == kNamed[i].cosh_want );
		check_odd_even( kNamed[i].raw );
	}

	for ( size_t i = 0; i < sizeof( kRandom ) / sizeof( kRandom[0] ); ++i )
	{
		CHECK( fixSinhExact( kRandom[i].raw ) == kRandom[i].sinh_want );
		CHECK( fixCoshExact( kRandom[i].raw ) == kRandom[i].cosh_want );
		check_odd_even( kRandom[i].raw );
	}

	if ( g_fail == 0 )
	{
		printf( "sinh_cosh exact test passed\n" );
		return 0;
	}
	return 1;
}
