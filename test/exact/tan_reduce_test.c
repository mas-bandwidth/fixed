/* SPDX-License-Identifier: MIT */
/* Fast unit test for tan's Payne-Hanek reduction (src/exact/tan_reduce.h). No MPFR, no clock.
 *
 * The four words and PIO2_127 are the digits of sollya/tan_kernel.out (the design's section
 * 3.2); W0 and W1 are also the first two words of 2/pi held by fixed-sincos-exact, the
 * FIX_TWO_OVER_PI_HI:LO of src/fixed_math.c. The points below were produced by a throwaway
 * MPFR program at 2048 bits (2048-bit 2/pi, nearest quarter, F = 128 bits from the leading
 * one, R = floor(F * PIO2_127 / 2^128)); the test compares parity, the sign of the fraction,
 * z and the top 64 bits of R.
 */
#include "../../src/exact/tan_reduce.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef struct tanReducePoint
{
	uint64_t m;
	int parity;
	int sign;
	int z;
	uint64_t top;
} tanReducePoint;

static const tanReducePoint points[] = {
	{ UINT64_C( 1 ), 0, 1, 16, UINT64_C( 0x7fffffffffffffff ) },
	{ UINT64_C( 2 ), 0, 1, 15, UINT64_C( 0x7fffffffffffffff ) },
	{ UINT64_C( 102944 ), 1, 1, 18, UINT64_C( 0x95777a5cf72cece6 ) },
	{ UINT64_C( 1750043 ), 1, -1, 21, UINT64_C( 0x9886fe9cb0222591 ) },
	{ UINT64_C( 557409 ), 1, 1, 1, UINT64_C( 0xa6c275aab1e869f0 ) },
	{ UINT64_C( 130146176612260 ), 1, 1, 47, UINT64_C( 0x89f1643ac4e27961 ) },
	{ UINT64_C( 195734331562903 ), 1, -1, 51, UINT64_C( 0x9f6cbd4110238714 ) },
	{ UINT64_C( 6173953965003744831 ), 1, -1, 64, UINT64_C( 0x679f5595e31503b6 ) },
};

static int checkConstant( const char* name, uint64_t got, uint64_t expected )
{
	if ( got == expected )
	{
		return 0;
	}
	printf( "FAIL %s = 0x%016" PRIx64 ", expected 0x%016" PRIx64 "\n", name, got, expected );
	return 1;
}

int main( void )
{
	int failures = 0;

	/* sollya/tan_kernel.out: floor(2^240 * 2/pi), high word first. */
	failures += checkConstant( "W0", FIX_TAN_W0, UINT64_C( 179192535600708 ) );
	failures += checkConstant( "W1", FIX_TAN_W1, UINT64_C( 1525027194748925236 ) );
	failures += checkConstant( "W2", FIX_TAN_W2, UINT64_C( 15979012694373645379 ) );
	failures += checkConstant( "W3", FIX_TAN_W3, UINT64_C( 10394869040465829563 ) );
	/* The first two words are fixed-sincos-exact's FIX_TWO_OVER_PI_HI and _LO. */
	failures += checkConstant( "W0 (sincos HI)", FIX_TAN_W0, UINT64_C( 0x0000A2F9836E4E44 ) );
	failures += checkConstant( "W1 (sincos LO)", FIX_TAN_W1, UINT64_C( 0x1529FC2757D1F534 ) );
	/* PIO2_127 = round(pi/2 * 2^127) = 267257146016241686964920093290467695825. */
	failures += checkConstant( "PIO2_127 hi", FIX_TAN_PIO2_127_HI, UINT64_C( 0xc90fdaa22168c234 ) );
	failures += checkConstant( "PIO2_127 lo", FIX_TAN_PIO2_127_LO, UINT64_C( 0xc4c6628b80dc1cd1 ) );

	for ( size_t i = 0; i < sizeof( points ) / sizeof( points[0] ); ++i )
	{
		const tanReducePoint* p = &points[i];
		int parity, sign, z;
		fixUInt128 r;
		fixTanReduce( p->m, &parity, &sign, &r, &z );

		uint64_t top = fixUInt128Hi( r );
		if ( parity != p->parity || sign != p->sign || z != p->z || top != p->top )
		{
			printf( "FAIL raw %" PRIu64 ": parity %d/%d sign %d/%d z %d/%d top 0x%016" PRIx64 "/0x%016" PRIx64 "\n", p->m,
					parity, p->parity, sign, p->sign, z, p->z, top, p->top );
			failures++;
		}
	}

	if ( failures != 0 )
	{
		printf( "%d tan reduce checks failed\n", failures );
		return 1;
	}
	printf( "tan reduce test passed\n" );
	return 0;
}
