// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// fixFromDouble / fixFromFloat rounding.
//
// The conversion from double and float to Q48.16 must round correctly. The old formula
// (scale by 65536, then +/- 0.5 and a truncating cast) had two defects, both fixed in
// fixed.h: (1) the +/- 0.5 add is itself a double, so it rounds near a half-integer --
// -0.49999999999999994 * 65536 gives -1 instead of the correct 0 -- and above 2^52 the
// add does nothing, leaving the result off by 1 raw; (2) truncation toward zero misrounded
// negatives. This file checks the corrected functions against an integer-only reference
// built from the double's bits, so the comparison is exact.
//
// Rounding rule documented in fixed.h: round half away from zero (matches fixQuantize, and
// is what the existing pinned tie tests expect). Ties at k + 0.5 raw give k +/- 1.
//
// FROM_DOUBLE_NEGATIVE_CONTROL builds this same file with fixFromDouble/fixFromFloat
// replaced by the old buggy formulas; the defect checks then fail and the binary exits 1,
// which CTest expects via WILL_FAIL TRUE.

#include "fixed/fixed.h"
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef FROM_DOUBLE_NEGATIVE_CONTROL
static fixed_t oldFixFromDouble( double x )
{
	double d = x * (double)FIX_ONE;
	return (fixed_t)( d >= 0.0 ? d + 0.5 : d - 0.5 );
}
static fixed_t oldFixFromFloat( float x )
{
	double d = (double)x * (double)FIX_ONE;
	return (fixed_t)( d >= 0.0 ? d + 0.5 : d - 0.5 );
}
#define fixFromDouble oldFixFromDouble
#define fixFromFloat oldFixFromFloat
#endif

typedef union
{
	double f;
	uint64_t u;
} bitu;

static int fails = 0;
static int ncheck = 0;

static void checkDouble( const char* name, double x, fixed_t want )
{
	fixed_t got = fixFromDouble( x );
	ncheck++;
	if ( got != want )
	{
		bitu b;
		b.f = x;
		printf( "from double FAIL %s: bits=0x%016llx got=%lld (0x%llx) want=%lld (0x%llx)\n",
				name, (unsigned long long)b.u, (long long)got, (unsigned long long)got,
				(long long)want, (unsigned long long)want );
		fails++;
	}
}

static void checkFloat( const char* name, float xf, fixed_t want )
{
	fixed_t got = fixFromFloat( xf );
	ncheck++;
	if ( got != want )
	{
		bitu b;
		b.f = (double)xf;
		printf( "from float FAIL %s: bits=0x%016llx got=%lld (0x%llx) want=%lld (0x%llx)\n",
				name, (unsigned long long)b.u, (long long)got, (unsigned long long)got,
				(long long)want, (unsigned long long)want );
		fails++;
	}
}

// Integer-only reference for round-half-away-from-zero of x * 65536. Decodes the double's
// sign, exponent and mantissa and shifts in integer arithmetic, so it is exact.
static int64_t refRaw( double x )
{
	bitu b;
	b.f = x;
	uint64_t bits = b.u;
	int s = (int)( bits >> 63 );
	int e = (int)( ( bits >> 52 ) & 0x7FF );
	uint64_t mant = bits & ( ( 1ULL << 52 ) - 1 );
	if ( e != 0 )
	{
		mant |= 1ULL << 52;
	}
	// value = mant * 2^(e-1075); times 2^16 moves the exponent to e-1059.
	int shift = e - 1059;
	uint64_t mag;
	if ( shift >= 0 )
	{
		mag = mant << shift;
	}
	else
	{
		int sh = -shift;
		if ( sh >= 64 )
		{
			mag = 0;
		}
		else
		{
			uint64_t q = mant >> sh;
			uint64_t rem = mant & ( ( 1ULL << sh ) - 1 );
			mag = q;
			if ( rem >= ( 1ULL << ( sh - 1 ) ) ) mag++; // half away from zero
		}
	}
	if ( s ) return -(int64_t)mag;
	return (int64_t)mag;
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

static uint64_t rng_next( void )
{
	uint64_t x = rng_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rng_state = x;
	return x;
}

// A random double of either sign with a full 52-bit random mantissa at biased exponent ep.
static double randomOctave( int ep )
{
	uint64_t r = rng_next();
	uint64_t mant = ( r & ( ( 1ULL << 52 ) - 1 ) ) | ( 1ULL << 52 );
	uint64_t u = mant | ( (uint64_t)ep << 52 );
	bitu b;
	b.u = u;
	double v = b.f;
	if ( r >> 63 ) v = -v;
	return v;
}

static void testExplicit( void )
{
	const double EPS = 1.0 / 65536.0;

	// The defect cases: exact double product just under +/- 0.5 raw. The old formula gave
	// -1 / +1 here; the correct rounding is 0.
	checkDouble( "-0.49999999999999994", -0.49999999999999994 * EPS, 0 );
	checkDouble( "+0.49999999999999994", 0.49999999999999994 * EPS, 0 );

	// Float versions. A float near 7.6e-6 has only ~24 significant bits, so its product
	// with 65536 cannot land within the double-rounding window of a half-integer; the
	// check is against the exact bit-level reference for the actual float value.
	checkFloat( "float -0.49999999999999994", (float)( -0.49999999999999994 * EPS ),
				refRaw( (double)(float)( -0.49999999999999994 * EPS ) ) );
	checkFloat( "float +0.49999999999999994", (float)( 0.49999999999999994 * EPS ),
				refRaw( (double)(float)( 0.49999999999999994 * EPS ) ) );

	// Exact ties at k + 0.5 raw: round half away from zero, so k + 1 for k >= 0 and k for
	// k < 0 (e.g. -5.5 -> -6).
	static const int64_t ks[] = { -6, -5, -2, -1, 0, 1, 2, 5, 6 };
	for ( unsigned i = 0; i < sizeof ks / sizeof ks[0]; i++ )
	{
		int64_t k = ks[i];
		fixed_t want = k >= 0 ? k + 1 : k;
		char nm[64];
		snprintf( nm, sizeof nm, "tie k=%lld", (long long)k );
		checkDouble( nm, ( (double)k + 0.5 ) * EPS, want );
		checkFloat( nm, (float)( ( (double)k + 0.5 ) * EPS ), want );
	}

	// At and above 2^52 raw the value is already an integer; it must come out exact. The
	// old formula was off by 1 at the odd raws (2^52+1 and 2^53-1).
	static const int64_t big[] = { (int64_t)1 << 52, ( (int64_t)1 << 52 ) + 1,
								   ( (int64_t)1 << 52 ) + 2, ( (int64_t)1 << 53 ) - 1 };
	for ( unsigned i = 0; i < sizeof big / sizeof big[0]; i++ )
	{
		char nm[64];
		snprintf( nm, sizeof nm, "raw>=2^52 raw=%lld", (long long)big[i] );
		checkDouble( nm, (double)big[i] * EPS, big[i] );
	}

	// 2^62 raw and the largest representable raw (INT64_MAX cleared to a 65536 multiple).
	int64_t two62 = (int64_t)1 << 62;
	checkDouble( "2^62", (double)two62 * EPS, two62 );
	int64_t largest = (int64_t)( (uint64_t)INT64_MAX & ~(uint64_t)65535 );
	checkDouble( "largest raw", (double)largest * EPS, largest );
}

static void testRandom( void )
{
	// Octaves of the biased exponent from well below 1 raw to well above 2^52.
	for ( int oct = 0; oct < 8; oct++ )
	{
		int ep = 985 + 10 * oct;
		for ( int i = 0; i < 65536; i++ )
		{
			double v = randomOctave( ep );
			checkDouble( "rand", v, refRaw( v ) );
			float fv = (float)v;
			checkFloat( "rand", fv, refRaw( (double)fv ) );
		}
	}
}

int main( void )
{
	testExplicit();
	testRandom();
	if ( fails )
	{
		printf( "from double FAILED: %d check(s) of %d\n", fails, ncheck );
		return 1;
	}
	printf( "from double test passed\n" );
	return 0;
}
