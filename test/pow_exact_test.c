// SPDX-License-Identifier: MIT
// fixPowExact against CORE-MATH cr_pow.
//
// The reference is the correctly-rounded binary64 pow of the exact double values of the raw
// Q48.16 base and exponent, scaled back by 2^16 (exact) and rounded to nearest-even by llrint
// in the default rounding mode, saturating at INT64_MAX. A raw input above 2^53 in magnitude
// is first cleared in its low bits to 53 significant bits, on both sides of the comparison, so
// that it converts to binary64 exactly. Where the reference is below 2^53 raw it is exact to
// far under a raw unit, so the bound is 2 raw. Where it is above 2^53 raw the double reference
// is coarser than a raw unit, so there the bound is 2 raw plus half the spacing of the double
// reference at that value. Where the reference is 0 the bound is 0: the library must return
// exactly 0 there. The row line reports those inputs apart: maxulp is over the
// inputs whose reference is at or below 2^53 raw, wide counts the rest, and wide_excess is the
// largest error there beyond the half spacing of the double reference (at most 2 to pass).
//
// CORE-MATH is vendored in extern/core-math/pow, pinned at
// cac142bc65dc140d645ba3f79511a9c591d1746e. It is a TEST-ONLY reference; the library never
// links it. With POW_NEGATIVE_CONTROL the test evaluates fixPow in place of fixPowExact; that
// build must fail.
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

double cr_pow( double x, double y );

#define BOUND 2
#define RAD ( 1LL << 16 )

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

#ifdef POW_NEGATIVE_CONTROL
#define COMPUTE( b, e ) fixPow( b, e )
#else
#define COMPUTE( b, e ) fixPowExact( b, e )
#endif

typedef struct row
{
	int64_t inputs;
	uint64_t maxulp;
	int64_t worstBase;
	int64_t worstExponent;
	int64_t worstLib;
	int64_t worstRef;
	int64_t wide;       // inputs whose reference is above 2^53 raw
	int64_t wideExcess; // largest error beyond the half spacing of the double reference there
	int failed;
} row;

static int fails = 0;

// Leave at most 53 significant bits, so the raw value converts to binary64 exactly.
static int64_t exact_raw( int64_t raw )
{
	if ( raw == 0 )
	{
		return 0;
	}
	uint64_t mag = raw < 0 ? -(uint64_t)raw : (uint64_t)raw;
	int bits = 64 - __builtin_clzll( mag );
	if ( bits > 53 )
	{
		int shift = bits - 53;
		mag &= ~( ( (uint64_t)1 << shift ) - 1 );
	}
	return raw < 0 ? -(int64_t)mag : (int64_t)mag;
}

static uint64_t udiff( int64_t a, int64_t b )
{
	return a > b ? (uint64_t)a - (uint64_t)b : (uint64_t)b - (uint64_t)a;
}

static void record( row* r, int64_t base, int64_t exponent, int64_t lib, int64_t ref, uint64_t bound )
{
	uint64_t u = udiff( lib, ref );
	r->inputs++;
	if ( bound > BOUND )
	{
		// Above 2^53 raw the error against the double reference includes that reference's own
		// rounding, so it is reported apart: as the excess over the half spacing.
		int64_t excess = (int64_t)u - (int64_t)( bound - BOUND );
		if ( r->wide == 0 || excess > r->wideExcess )
		{
			r->wideExcess = excess;
		}
		r->wide++;
		if ( u > bound )
		{
			fails++;
			if ( !r->failed )
			{
				printf( "pow exact FAIL base=%lld exponent=%lld lib=%lld ref=%lld ulp=%llu bound=%llu\n", (long long)base,
						(long long)exponent, (long long)lib, (long long)ref, (unsigned long long)u, (unsigned long long)bound );
			}
			// name this input as the worst unless a failure below 2^53 raw already holds it
			if ( r->maxulp <= BOUND )
			{
				r->worstBase = base;
				r->worstExponent = exponent;
				r->worstLib = lib;
				r->worstRef = ref;
			}
			r->failed = 1;
		}
		return;
	}
	if ( u > r->maxulp || r->inputs - r->wide == 1 )
	{
		r->maxulp = u;
		r->worstBase = base;
		r->worstExponent = exponent;
		r->worstLib = lib;
		r->worstRef = ref;
	}
	if ( u > bound )
	{
		fails++;
		if ( !r->failed )
		{
			printf( "pow exact FAIL base=%lld exponent=%lld lib=%lld ref=%lld ulp=%llu bound=%llu\n", (long long)base,
					(long long)exponent, (long long)lib, (long long)ref, (unsigned long long)u, (unsigned long long)bound );
		}
		r->failed = 1;
	}
}

// Against the CORE-MATH reference, on the 53-bit-exact inputs.
static void check( int64_t base, int64_t exponent, row* r )
{
	base = exact_raw( base );
	exponent = exact_raw( exponent );

	double d = cr_pow( (double)base / 65536.0, (double)exponent / 65536.0 ) * 65536.0;
	int64_t ref = d >= 9223372036854775808.0 ? INT64_MAX : llrint( d );
	int64_t lib = COMPUTE( base, exponent );

	uint64_t bound = BOUND;
	if ( ref == 0 )
	{
		// where the correctly rounded result is 0, the library returns exactly 0
		bound = 0;
	}
	else if ( d >= 9223372036854775808.0 )
	{
		// A reference at or above 2^63 raw (or infinite) saturates; the true value is at least
		// 2^63 less half the spacing of doubles just below 2^63 (1024), so allow that half.
		bound = BOUND + 512;
	}
	else if ( d > 9007199254740992.0 )
	{
		// Above 2^53 raw the double reference carries its own rounding error: half the ulp
		// spacing of the reference value.
		double ulp = fabs( nextafter( d, HUGE_VAL ) - d );
		bound = BOUND + (uint64_t)( 0.5 * ulp + 0.999999 );
	}
	record( r, base, exponent, lib, ref, bound );
}

// Against a value known exactly, on the inputs as given (no clearing).
static void check_exact( int64_t base, int64_t exponent, int64_t expected, row* r )
{
	record( r, base, exponent, COMPUTE( base, exponent ), expected, expected == 0 ? 0 : BOUND );
}

static void print_row( const char* piece, row* r )
{
	printf( "core-math row=powexact[%s] inputs=%lld maxulp=%llu bound=2 worst=%lld,%lld wide=%lld wide_excess=%lld\n", piece,
			(long long)r->inputs, (unsigned long long)r->maxulp, (long long)r->worstBase, (long long)r->worstExponent,
			(long long)r->wide, (long long)r->wideExcess );
	if ( r->failed )
	{
		printf( "pow exact FAIL piece=%s worst base=%lld exponent=%lld lib=%lld ref=%lld\n", piece, (long long)r->worstBase,
				(long long)r->worstExponent, (long long)r->worstLib, (long long)r->worstRef );
	}
}

// uniform raw in [lo, hi]
static int64_t draw( int64_t lo, int64_t hi )
{
	uint64_t span = (uint64_t)hi - (uint64_t)lo + 1;
	return lo + (int64_t)( rng_next() % span );
}

static void band( const char* piece, int64_t lo_rad, int64_t hi_rad )
{
	row r = { 0 };
	for ( int i = 0; i < ( 1 << 16 ); i++ )
	{
		int64_t base = draw( 1, 4 * RAD );
		int64_t exponent = draw( lo_rad * RAD, hi_rad * RAD - 1 );
		if ( rng_next() & 1 )
		{
			exponent = -exponent;
		}
		check( base, exponent, &r );
	}
	print_row( piece, &r );
}

// (e) the pow inputs of glibc's math/auto-libm-test-in at commit
// bf5fefc1cdcab7099289d6f23844855e9e458bc9 (https://sourceware.org/git/glibc.git; file sha256
// 7811b6cb1d82496be3f7ed57eef137a26a7a84e26519a950aef746fa423cf693): its 536 "pow" lines, each
// operand quantized to the nearest Q48.16 raw (ties to even, clamped to int64), duplicates
// removed, 259 pairs, by value. They are glibc's hard and edge cases for binary64; quantized,
// they are edge cases of the Q48.16 domain (zeros, ones, signs, saturating and underflowing
// exponents, bases just off 1).
static const int64_t glibcPairs[][2] = {
	{ 0LL, 0LL }, { 655360LL, 0LL }, { -655360LL, 0LL }, { 65536LL, 65536LL },
	{ 65536LL, -65536LL }, { 65536LL, 81920LL }, { 65536LL, -81920LL }, { 65536LL, INT64_MAX },
	{ 65536LL, 0LL }, { 2146304LL, 0LL }, { -2146304LL, 0LL }, { INT64_MAX, 0LL },
	{ INT64_MAX, INT64_MAX }, { 655360LL, INT64_MIN }, { 0LL, 65536LL }, { 0LL, 720896LL },
	{ 0LL, 131072LL }, { 0LL, 727450LL }, { 0LL, 1769472LL }, { 0LL, 1099511562240LL },
	{ 0LL, INT64_MAX }, { 0LL, 2199023124480LL }, { 0LL, 262144LL }, { 0LL, 1099511627776LL },
	{ 0LL, 32768LL }, { 1048576LL, 16384LL }, { INT64_MAX, 8192LL }, { 131072LL, 262144LL },
	{ 16777216LL, 524288LL }, { 49152LL, 81920LL }, { INT64_MIN, INT64_MIN }, { -65536LL, -1099511562240LL },
	{ -65536LL, -2199023124480LL }, { -65536LL, INT64_MIN }, { -65536LL, 1099511562240LL }, { -65536LL, 2199023124480LL },
	{ -65536LL, INT64_MAX }, { -131072LL, 8257536LL }, { -131072LL, 8323072LL }, { -131072LL, -8257536LL },
	{ -131072LL, -8323072LL }, { -131072LL, -1099511562240LL }, { -131072LL, -2199023124480LL }, { -131072LL, INT64_MIN },
	{ -131072LL, 1099511562240LL }, { -131072LL, 2199023124480LL }, { -131072LL, INT64_MAX }, { INT64_MIN, -131072LL },
	{ INT64_MIN, -196608LL }, { INT64_MIN, 131072LL }, { INT64_MIN, 196608LL }, { INT64_MIN, -1099511562240LL },
	{ INT64_MIN, -2199023124480LL }, { INT64_MIN, 1099511562240LL }, { INT64_MIN, 2199023124480LL }, { INT64_MIN, INT64_MAX },
	{ INT64_MIN, 262144LL }, { INT64_MIN, 327680LL }, { -576460752303423488LL, 196608LL }, { -576460752303423488LL, 262144LL },
	{ -576460752303423488LL, 327680LL }, { -562949953421312LL, 262144LL }, { -562949953421312LL, 327680LL }, { -4398046511104LL, 327680LL },
	{ 0LL, -131072LL }, { 0LL, -196608LL }, { 0LL, -262144LL }, { 0LL, -327680LL },
	{ INT64_MIN, 16842752LL }, { 0LL, -16842752LL }, { -32768LL, 8257536LL }, { -32768LL, 8323072LL },
	{ -32768LL, -8257536LL }, { -32768LL, -8323072LL }, { -32768LL, -1099511562240LL }, { -32768LL, -2199023124480LL },
	{ -32768LL, INT64_MIN }, { -32768LL, 1099511562240LL }, { -32768LL, 2199023124480LL }, { -32768LL, INT64_MAX },
	{ 0LL, 196608LL }, { 0LL, -1099511562240LL }, { 0LL, -2199023124480LL }, { 0LL, INT64_MIN },
	{ 65536LL, 655360LL }, { 65536LL, 6553600LL }, { 65536LL, 65536000LL }, { 65536LL, 1099511627776LL },
	{ 65536LL, 70368744177664LL }, { 65536LL, 80063986925568LL }, { 65536LL, -655360LL }, { 65536LL, -6553600LL },
	{ 65536LL, -65536000LL }, { 65536LL, -1099511627776LL }, { 65536LL, -70368744177664LL }, { 65536LL, -80063986925568LL },
	{ 65536LL, 40031993462784LL }, { 65536LL, -40031993462784LL }, { 65536LL, INT64_MIN }, { -65536LL, 655360LL },
	{ -65536LL, 6553600LL }, { -65536LL, 65536000LL }, { -65536LL, 1099511627776LL }, { -65536LL, 70368744177664LL },
	{ -65536LL, 80063986925568LL }, { -65536LL, -655360LL }, { -65536LL, -6553600LL }, { -65536LL, -65536000LL },
	{ -65536LL, -1099511627776LL }, { -65536LL, -70368744177664LL }, { -65536LL, -80063986925568LL }, { -65536LL, 40031993462784LL },
	{ -65536LL, -40031993462784LL }, { INT64_MAX, 49152LL }, { 655360LL, 323223552LL }, { 655360LL, 323158016LL },
	{ 655360LL, 323092480LL }, { 655360LL, 323026944LL }, { 655360LL, -323158016LL }, { 655360LL, -323092480LL },
	{ 655360LL, -323026944LL }, { INT64_MAX, 11927552LL }, { INT64_MAX, -11927552LL }, { 64881LL, 0LL },
	{ 66191LL, 0LL }, { 131072LL, -6553600000LL }, { 131072LL, -8265728LL }, { 131072LL, -8273920LL },
	{ 131072LL, -8282112LL }, { 131072LL, -8290304LL }, { 131072LL, -8298496LL }, { 131072LL, -8306688LL },
	{ 131072LL, -8314880LL }, { 131072LL, -63512576LL }, { 131072LL, -63520768LL }, { 131072LL, -63528960LL },
	{ 131072LL, -63537152LL }, { 131072LL, -63545344LL }, { 131072LL, -63553536LL }, { 131072LL, -63561728LL },
	{ 131072LL, -66985984LL }, { 131072LL, -66994176LL }, { 131072LL, -67002368LL }, { 131072LL, -67010560LL },
	{ 131072LL, -67018752LL }, { 131072LL, -67026944LL }, { 131072LL, -67035136LL }, { 131072LL, -1073618944LL },
	{ 131072LL, -1073627136LL }, { 131072LL, -1073635328LL }, { 131072LL, -1073643520LL }, { 131072LL, -1073651712LL },
	{ 131072LL, -1073659904LL }, { 131072LL, -1073668096LL }, { 131072LL, -1073684480LL }, { 131072LL, -1073692672LL },
	{ 131072LL, -1073700864LL }, { 131072LL, -1073709056LL }, { 131072LL, -1073717248LL }, { 131072LL, -1073725440LL },
	{ 131072LL, -1073733632LL }, { 32768LL, 8265728LL }, { 32768LL, 8273920LL }, { 32768LL, 8282112LL },
	{ 32768LL, 8290304LL }, { 32768LL, 8298496LL }, { 32768LL, 8306688LL }, { 32768LL, 8314880LL },
	{ 32768LL, 63512576LL }, { 32768LL, 63520768LL }, { 32768LL, 63528960LL }, { 32768LL, 63537152LL },
	{ 32768LL, 63545344LL }, { 32768LL, 63553536LL }, { 32768LL, 63561728LL }, { 32768LL, 66985984LL },
	{ 32768LL, 66994176LL }, { 32768LL, 67002368LL }, { 32768LL, 67010560LL }, { 32768LL, 67018752LL },
	{ 32768LL, 67026944LL }, { 32768LL, 67035136LL }, { 32768LL, 1073618944LL }, { 32768LL, 1073627136LL },
	{ 32768LL, 1073635328LL }, { 32768LL, 1073643520LL }, { 32768LL, 1073651712LL }, { 32768LL, 1073659904LL },
	{ 32768LL, 1073668096LL }, { 32768LL, 1073684480LL }, { 32768LL, 1073692672LL }, { 32768LL, 1073700864LL },
	{ 32768LL, 1073709056LL }, { 32768LL, 1073717248LL }, { 32768LL, 1073725440LL }, { 32768LL, 1073733632LL },
	{ 131072LL, -8257536LL }, { 131073LL, -8257536LL }, { -131073LL, -8257536LL }, { 131072LL, -63504384LL },
	{ -131072LL, -63504384LL }, { 131073LL, -63504384LL }, { -131073LL, -63504384LL }, { 131072LL, -66977792LL },
	{ -131072LL, -66977792LL }, { 131073LL, -66977792LL }, { -131073LL, -66977792LL }, { 131072LL, -1073610752LL },
	{ -131072LL, -1073610752LL }, { 131073LL, -1073610752LL }, { -131073LL, -1073610752LL }, { 131072LL, -1073676288LL },
	{ -131072LL, -1073676288LL }, { 131073LL, -1073676288LL }, { -131073LL, -1073676288LL }, { 32768LL, 8257536LL },
	{ 32768LL, 63504384LL }, { -32768LL, 63504384LL }, { 32768LL, 66977792LL }, { -32768LL, 66977792LL },
	{ 32768LL, 1073610752LL }, { -32768LL, 1073610752LL }, { 32768LL, 1073676288LL }, { -32768LL, 1073676288LL },
	{ 131083LL, -66977792LL }, { 131078LL, -66977792LL }, { 131072LL, -66998576LL }, { 131072LL, -66997888LL },
	{ 655360LL, -65536LL }, { 655360LL, -131072LL }, { 655360LL, -196608LL }, { 655360LL, -262144LL },
	{ 655360LL, -327680LL }, { 655360LL, -393216LL }, { 655360LL, -458752LL }, { 65536LL, 131072LL },
	{ 65536LL, 196608LL }, { 65536LL, 262144LL }, { 65536LL, 327680LL }, { 65536LL, 393216LL },
	{ 65536LL, 458752LL }, { 65536LL, -131072LL }, { 65536LL, -196608LL }, { 65536LL, -262144LL },
	{ 65536LL, -327680LL }, { 65536LL, -393216LL }, { 65536LL, -458752LL }, { 69632LL, 73728LL },
	{ 98304LL, 67584LL }, { INT64_MAX, 98304LL }, { 118393LL, -2617848LL }, { INT64_MAX, -374198LL },
	{ 82701LL, 5301346LL }, { 39096LL, -429459360LL }, { 55088LL, -21478272LL }, { 66972LL, 268174832LL },
	{ 3102968LL, 1507328LL }, { -3102968LL, 1507328LL }, { INT64_MAX, 65536LL },
};

int main( void )
{
	// (a) the core: base (0, 16] x exponent [-8, 8] rad, 2^20 seeded pairs, then every pair of
	// the 16 raw bases nearest each integer 1..16 rad with the 16 raw exponents nearest each
	// integer -8..8 rad.
	{
		row r = { 0 };
		for ( int i = 0; i < ( 1 << 20 ); i++ )
		{
			// one draw per statement: the order of evaluation of call arguments is
			// unspecified, and gcc and clang differ
			int64_t base = draw( 1, 16 * RAD );
			int64_t exponent = draw( -8 * RAD, 8 * RAD );
			check( base, exponent, &r );
		}
		for ( int64_t b = 1; b <= 16; b++ )
		{
			for ( int64_t db = -8; db < 8; db++ )
			{
				for ( int64_t e = -8; e <= 8; e++ )
				{
					for ( int64_t de = -8; de < 8; de++ )
					{
						check( b * RAD + db, e * RAD + de, &r );
					}
				}
			}
		}
		print_row( "core", &r );
	}

	// (b) the exponent bands, base (0, 4] rad, either sign of exponent.
	band( "e8-16", 8, 16 );
	band( "e16-64", 16, 64 );
	band( "e64-2^20", 64, 1 << 20 );

	// (c) base (16, 2^47) rad with exponent [-4, 4] rad. The base is drawn log-uniformly (a
	// random bit length, then a random raw of that length) so every binade is covered.
	{
		row r = { 0 };
		for ( int i = 0; i < ( 1 << 16 ); i++ )
		{
			int bits = 21 + (int)( rng_next() % 43 ); // raw bit length 21..63
			int64_t lo = (int64_t)1 << ( bits - 1 );
			int64_t hi = bits == 63 ? INT64_MAX : ( (int64_t)1 << bits ) - 1;
			if ( lo <= 16 * RAD )
			{
				lo = 16 * RAD + 1;
			}
			int64_t base = draw( lo, hi );
			int64_t exponent = draw( -4 * RAD, 4 * RAD );
			check( base, exponent, &r );
		}
		print_row( "large-base", &r );
	}

	// (d) the special inputs, against values known exactly, then against the reference.
	{
		row r = { 0 };
		check_exact( 1, -RAD, (int64_t)1 << 32, &r );       // (2^-16)^-1 = 2^16 rad
		check_exact( 1, INT64_MIN, INT64_MAX, &r );          // (2^-16)^(-2^47) saturates
		check_exact( 1, INT64_MIN + 1, INT64_MAX, &r );      // fixPow wraps here to 65525
		check_exact( 1, INT64_MAX, 0, &r );                  // (2^-16)^(2^47) underflows
		check_exact( INT64_MAX, RAD, INT64_MAX, &r );        // x^1 = x
		check_exact( INT64_MAX, -RAD, 0, &r );               // 2^-47 rad is below half a raw
		check_exact( INT64_MAX, INT64_MAX, INT64_MAX, &r );  // saturates
		check_exact( INT64_MAX, INT64_MIN, 0, &r );          // underflows
		const int64_t exponents[] = { 1, -1, RAD, -RAD, 12345678, -987654321, (int64_t)1 << 40, INT64_MAX, INT64_MIN, INT64_MIN + 1 };
		for ( unsigned i = 0; i < sizeof( exponents ) / sizeof( exponents[0] ); i++ )
		{
			check_exact( FIX_ONE, exponents[i], FIX_ONE, &r ); // 1^y = 1 exactly
		}
		const int64_t bases[] = { 1, 2, RAD - 1, RAD, RAD + 1, 3 * RAD, (int64_t)1 << 40, INT64_MAX };
		for ( unsigned i = 0; i < sizeof( bases ) / sizeof( bases[0] ); i++ )
		{
			check_exact( bases[i], 0, FIX_ONE, &r ); // x^0 = 1 exactly
		}
		check_exact( 0, RAD, 0, &r );       // base <= 0 is 0 whatever the exponent
		check_exact( -RAD, 2 * RAD, 0, &r );
		check_exact( INT64_MIN, 0, 0, &r );

		// the same neighbourhood through the reference: base 1 raw and INT64_MAX (cleared to
		// 53 bits), every exponent within 64 raw of the edges of the result range.
		check( 1, -RAD, &r );
		check( 1, -1, &r ); // (2^-16)^(-2^-16) = 2^(2^-12)
		check( 1, 1, &r );
		check( INT64_MAX, 1, &r );
		check( INT64_MAX, RAD, &r );
		check( INT64_MAX, -RAD, &r );
		for ( int64_t de = -64; de <= 64; de++ )
		{
			check( 2 * RAD, 47 * RAD + de, &r );  // 2^47 rad, the saturation edge
			check( 2 * RAD, -17 * RAD + de, &r ); // half a raw, the underflow edge
			check( RAD / 2, -47 * RAD + de, &r );
			check( RAD / 2, 17 * RAD + de, &r );
		}
		print_row( "special", &r );
	}

	// (e) glibc's pow inputs, quantized. A base <= 0 is 0 and a zero exponent is FIX_ONE,
	// exactly; the rest go through the reference.
	{
		row r = { 0 };
		for ( unsigned i = 0; i < sizeof( glibcPairs ) / sizeof( glibcPairs[0] ); i++ )
		{
			int64_t base = glibcPairs[i][0], exponent = glibcPairs[i][1];
			if ( base <= 0 )
			{
				check_exact( base, exponent, 0, &r );
			}
			else if ( exponent == 0 )
			{
				check_exact( base, exponent, FIX_ONE, &r );
			}
			else
			{
				check( base, exponent, &r );
			}
		}
		print_row( "glibc", &r );
	}

	if ( fails != 0 )
	{
		printf( "pow exact test FAILED\n" );
		return 1;
	}
	printf( "pow exact test passed\n" );
	return 0;
}
