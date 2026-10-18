// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Fast test for src/exact/tan_kernel.h: the 102-entry table, zero, oddness, and 16 points
// whose correctly rounded values come from a Python decimal Taylor series at 80 digits.
// No MPFR, no clock.
//
// Expected values:
//   python3 -c "from decimal import*;getcontext().prec=80;import math;S=lambda x:sum((-1)**k*x**(2*k+1)/Decimal(math.factorial(2*k+1)) for k in range(50));C=lambda x:sum((-1)**k*x**(2*k)/Decimal(math.factorial(2*k)) for k in range(50));V=[Decimal(2)**-65,Decimal(2)**-40,Decimal(2)**-20,Decimal(2)**-9,Decimal(2)**-8,Decimal(2)**-8+Decimal(2)**-100,Decimal(3)*Decimal(2)**-32,Decimal(5)*Decimal(2)**-16];print([int((S(v)/C(v)*Decimal(2)**128).to_integral_value(rounding=ROUND_HALF_EVEN)) for v in V])"
#include "../../src/exact/tan_kernel.h"

#include <stdint.h>
#include <stdio.h>

static int failures = 0;

static void print128( const char* label, fixInt128 v )
{
	printf( "  %s hi=0x%016llx lo=0x%016llx\n", label, (unsigned long long)fixInt128Hi( v ),
			(unsigned long long)fixInt128Lo( v ) );
}

static void check_table( int j, uint64_t hi, uint64_t lo )
{
	fixUInt128 got = fixTanKernelTable( j );
	if ( fixUInt128Hi( got ) != hi || fixUInt128Lo( got ) != lo )
	{
		printf( "FAIL TAN_%d hi=0x%016llx lo=0x%016llx, expected hi=0x%016llx lo=0x%016llx\n", j,
				(unsigned long long)fixUInt128Hi( got ), (unsigned long long)fixUInt128Lo( got ),
				(unsigned long long)hi, (unsigned long long)lo );
		failures++;
	}
}

// One point: s and round( tan(s) * 2^128 ), both as sign-magnitude hi:lo.
static void check_point( const char* name, uint64_t s_hi, uint64_t s_lo, uint64_t e_hi, uint64_t e_lo )
{
	fixInt128 magnitude = fixInt128Make( s_hi, s_lo );
	fixInt128 expected_magnitude = fixInt128Make( e_hi, e_lo );
	fixInt128 positive = fixTanKernel( magnitude );

	// The kernel is odd exactly.
	fixInt128 negative = fixTanKernel( fixInt128Neg( magnitude ) );
	if ( !fixInt128Eq( negative, fixInt128Neg( positive ) ) )
	{
		printf( "FAIL %s: kernel is not odd\n", name );
		print128( "f(+s)", positive );
		print128( "f(-s)", negative );
		failures++;
	}

	for ( int sign = 0; sign < 2; sign++ )
	{
		fixInt128 s = sign == 0 ? magnitude : fixInt128Neg( magnitude );
		fixInt128 expected = sign == 0 ? expected_magnitude : fixInt128Neg( expected_magnitude );
		fixInt128 got = sign == 0 ? positive : negative;

		fixInt128 error = fixInt128Sub( got, expected );
		if ( fixInt128IsNegative( error ) )
		{
			error = fixInt128Neg( error );
		}
		if ( fixInt128Gt( error, fixInt128FromI64( 8 ) ) )
		{
			printf( "FAIL %s (%s): error %llu units of 2^-128\n", name, sign == 0 ? "+" : "-",
					(unsigned long long)fixInt128Lo( error ) );
			print128( "got", got );
			print128( "expected", expected );
			failures++;
		}
	}
}

int main( void )
{
	check_table( 0, UINT64_C( 0x0000000000000000 ), UINT64_C( 0x0000000000000000 ) );
	check_table( 1, UINT64_C( 0x0100015557777aeb ), UINT64_C( 0xb45431e4465196fa ) );
	check_table( 64, UINT64_C( 0x45ed3d6fcd1d290c ), UINT64_C( 0x5e59201e20911332 ) );
	check_table( 101, UINT64_C( 0x80f107bf03725a03 ), UINT64_C( 0x4fb7eb4048941cf6 ) );

	fixInt128 zero = fixTanKernel( FIX_INT128_ZERO );
	if ( !fixInt128Eq( zero, FIX_INT128_ZERO ) )
	{
		printf( "FAIL fixTanKernel(0) is not 0\n" );
		print128( "got", zero );
		failures++;
	}

	// s = 2^-65, 2^-40, 2^-20, 2^-9, 2^-8, 2^-8 + 2^-100, 3*2^-32, 5*2^-16, in Q0.128,
	// with round( tan(s) * 2^128 ) beside each; both signs are checked above.
	static const uint64_t points[8][4] = {
		{ UINT64_C( 0x0000000000000000 ), UINT64_C( 0x8000000000000000 ), UINT64_C( 0x0000000000000000 ), UINT64_C( 0x8000000000000000 ) }, // 2^-65
		{ UINT64_C( 0x0000000001000000 ), UINT64_C( 0x0000000000000000 ), UINT64_C( 0x0000000001000000 ), UINT64_C( 0x0000000000000055 ) }, // 2^-40
		{ UINT64_C( 0x0000100000000000 ), UINT64_C( 0x0000000000000000 ), UINT64_C( 0x0000100000000005 ), UINT64_C( 0x5555555557777777 ) }, // 2^-20
		{ UINT64_C( 0x0080000000000000 ), UINT64_C( 0x0000000000000000 ), UINT64_C( 0x0080000aaaabbbbb ), UINT64_C( 0xd75d78a3fc4f7874 ) }, // 2^-9
		{ UINT64_C( 0x0100000000000000 ), UINT64_C( 0x0000000000000000 ), UINT64_C( 0x0100005555777785 ), UINT64_C( 0x485a1e87bb6da2ba ) }, // 2^-8
		{ UINT64_C( 0x0100000000000000 ), UINT64_C( 0x0000000010000000 ), UINT64_C( 0x0100005555777785 ), UINT64_C( 0x485a1e87cb6db2ba ) }, // 2^-8 + 2^-100
		{ UINT64_C( 0x0000000300000000 ), UINT64_C( 0x0000000000000000 ), UINT64_C( 0x0000000300000000 ), UINT64_C( 0x0000000900000000 ) }, // 3*2^-32
		{ UINT64_C( 0x0005000000000000 ), UINT64_C( 0x0000000000000000 ), UINT64_C( 0x000500000029aaaa ), UINT64_C( 0xac4b555565cd9a6a ) }, // 5*2^-16
	};
	static const char* names[8] = { "2^-65", "2^-40", "2^-20", "2^-9", "2^-8", "2^-8+2^-100", "3*2^-32", "5*2^-16" };
	for ( int i = 0; i < 8; i++ )
	{
		check_point( names[i], points[i][0], points[i][1], points[i][2], points[i][3] );
	}

	if ( failures != 0 )
	{
		printf( "tan kernel test failed: %d failure(s)\n", failures );
		return 1;
	}
	printf( "tan kernel test passed\n" );
	return 0;
}
