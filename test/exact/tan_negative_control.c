/* SPDX-License-Identifier: MIT */
/* Negative control for fixTanExact: the same src/exact/tan.c compiled with a 112-bit
 * 2/pi (W2 and W3 zeroed, the sincos reduction) must disagree with the shipped function.
 *
 * docs/design/transcendentals.md section 5 states it:
 *
 *   Negative control (`test/exact/tan_negative_control.c`, WILL_FAIL; likewise not yet in
 *   fixed-tan-exact's NEW list): W2 and W3 zeroed (112 bits, the sincos reduction): it must
 *   fail at 130146176612260.
 *
 * The comparison covers that raw and every named point of section 3.1. CTest marks this
 * WILL_FAIL, so exiting 1 when any raw differs is the pass: a control that agreed
 * everywhere would mean the reducers cannot tell 240 bits of 2/pi from 112.
 */
#include "fixed/fixed_tan.h"

#include <stdint.h>
#include <stdio.h>

static fixed_t fixTanExactReal( fixed_t x )
{
	return fixTanExact( x );
}

/* 112 bits of 2/pi: W0:W1 only, the sincos reduction. tan_reduce.h guards each word. */
#define FIX_TAN_W2 UINT64_C( 0 )
#define FIX_TAN_W3 UINT64_C( 0 )

/* Compile the real tan.c beside the real one, under another name. */
#define fixTanExact fixTanExactW112
FIX_API fixed_t fixTanExactW112( fixed_t x );
#include "../../src/exact/tan.c"
#undef fixTanExact

/* Section 3.1, in order: the pole convergent (6173953965003744831), the below-32-rad
 * worst raw (1750043), the first saturating raw (195734331562903), the largest unsaturated
 * near-edge raw (130146176612260), and the three near-edge unsaturated raws
 * (2544546310317739, 5870999813598064, 8285399947303543). Design section 5: "W2 and W3
 * zeroed (112 bits, the sincos reduction): it must fail at 130146176612260." */
static const int64_t points[] = {
	INT64_C( 6173953965003744831 ),
	INT64_C( 1750043 ),
	INT64_C( 195734331562903 ),
	INT64_C( 130146176612260 ),
	INT64_C( 2544546310317739 ),
	INT64_C( 5870999813598064 ),
	INT64_C( 8285399947303543 ),
};

int main( void )
{
	int differences = 0;

	for ( size_t i = 0; i < sizeof( points ) / sizeof( points[0] ); ++i )
	{
		int64_t raw = points[i];
		fixed_t real = fixTanExactReal( raw );
		fixed_t control = fixTanExactW112( raw );

		if ( real != control )
		{
			printf( "FAIL raw=%lld real=%lld control=%lld\n", (long long)raw, (long long)real,
					(long long)control );
			differences++;
		}
	}

	if ( differences != 0 )
	{
		printf( "%d tan negative control points differ\n", differences );
		return 1;
	}
	printf( "tan negative control: the 112-bit reduction agreed everywhere\n" );
	return 0;
}
