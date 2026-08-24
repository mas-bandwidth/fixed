// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Bit-equality between this library and the box3d/fixed3d code it was ported from.
//
// Every function below was moved here out of fixed3d, where the game already depends on
// its exact numbers. "Ported" has to mean the SAME BITS, not "close enough" -- a one-ulp
// drift in fixPow is a control input that diverges between a client and the server. So
// each function is checked against a reference copy of the fixed3d implementation
// transcribed verbatim into this file, over a structured domain sweep plus deterministic
// pseudo-random sampling.
//
// NEGATIVE CONTROL, INLINE AND ALWAYS ON. Every sweep is run twice: once against the
// reference, and once against the reference perturbed by one quantum. The first run must
// find zero mismatches and the second must find some. A comparison that only ever runs
// its passing arm cannot tell you it is comparing anything -- if the sweep were reading
// the wrong variable, or the domain were empty, or the loop bound were zero, the clean
// arm would pass exactly as it does now. The perturbed arm is what proves the sweep has
// its eyes open. Functions with an out-parameter get a second control that perturbs the
// out-parameter alone, because a sweep can perfectly well check a return value and be
// blind to the state written beside it.
//
// The structured sweeps also feed a frozen FNV hash, in the manner of the other suites
// here, so CI's three-OS matrix enforces cross-platform bit-identity on this code too.
// The hash covers ONLY the structured sweeps, whose size is fixed, so it does not move
// when the random sample count is scaled up.
//
// Scale the random sweeps with -DPORT_SWEEP_SCALE=N for an exhaustive-scale proof run.
// The committed default is sized to keep the suite well under a second.

#include "fixed/fixed.h"
#include "fixed/fixed_math.h"
#include "fixed/fixed_vec.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef PORT_SWEEP_SCALE
	#define PORT_SWEEP_SCALE 1
#endif

// Negative control in the other direction, built as a separate WILL_FAIL target in the
// manner of exhaustive_test.c. The inline controls above perturb the REFERENCE and prove
// the sweeps can see a difference; this one perturbs the LIBRARY, which is where a real
// regression would come from. The macro does not expand recursively, so the call inside
// resolves to the real function.
#ifdef PORT_NEGATIVE_CONTROL
	#define fixLog2( a ) ( fixLog2( a ) + 1 )
#endif

static int fails = 0;
static int printCounts = 0;

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix( int64_t v )
{
	const uint8_t* b = (const uint8_t*)&v;
	for ( int i = 0; i < 8; i++ ) { fnv ^= b[i]; fnv *= 0x00000100000001B3ULL; }
}

// FROZEN 2026-08-24. Every OS and compiler in CI must reproduce this exactly. If a
// legitimate change alters it, re-capture deliberately and say why in the pull request;
// never loosen it to make a mismatch go away.
#ifndef EXPECTED_PORT_HASH
	#define EXPECTED_PORT_HASH 0x6f1643ee0915c7abULL
#endif

// Deterministic sampling. splitmix64: pure integer, identical on every platform.
static uint64_t rngState;
static uint64_t nextRandom( void )
{
	rngState += 0x9E3779B97F4A7C15ULL;
	uint64_t z = rngState;
	z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
	z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
	return z ^ ( z >> 31 );
}

// A random value inside the library's stated domain [FIX_MIN, FIX_MAX]. INT64_MIN is
// excluded on purpose: FIX_MIN is -INT64_MAX, and fixAbs( INT64_MIN ) has no value.
static fixed_t randomFixed( void )
{
	uint64_t r = nextRandom();
	fixed_t v = (fixed_t)r;
	return v == INT64_MIN ? 0 : v;
}

// One quantum applied to a reference value. The add is unsigned because a saturating
// result sits exactly at INT64_MAX, where +1 as a signed value is overflow -- the
// negative control must not itself be undefined behavior. The wrapped value still
// differs from the implementation's, which is all the control needs.
static fixed_t bump( fixed_t v, int perturb )
{
	return (fixed_t)( (uint64_t)v + (uint64_t)(int64_t)perturb );
}

static int32_t bump32( int32_t v, int perturb )
{
	return (int32_t)( (uint32_t)v + (uint32_t)perturb );
}

// ================================================================================
// The reference implementations, transcribed from fixed3d.
//
// These are the box3d bodies with the b3 spelling mapped to this library's (b3FixMul ->
// fixMul, B3_FIXED_ONE -> FIX_ONE, and so on). The arithmetic is untouched. Where the
// original relies on signed-overflow undefined behavior, the transcription uses the
// equivalent unsigned form and says so: the transcribed reference has to be runnable
// under the UBSan job, and the two agree on every input a defined execution can observe.
// ================================================================================

// REF_NOINLINE IS LOAD-BEARING, NOT DECORATION.
//
// Most of these references are transcriptions of code that is header-inline in this
// library, so the compiler can see both sides of the comparison, prove them equivalent,
// and delete the sweep. Measured: 200 million fixLerp comparisons folded to nothing and
// ran in 0.00s, reporting zero mismatches without executing a single one. The negative
// control did NOT catch it, because a folded comparison folds identically on the
// perturbed arm -- it "detects" the difference by constant folding rather than by
// running the code. An oracle the compiler can collapse into the thing it is checking
// is not an oracle. Keeping the references out of line forces both sides to execute.
#if defined( _MSC_VER ) && !defined( __clang__ )
	#define REF_NOINLINE __declspec( noinline )
#else
	#define REF_NOINLINE __attribute__( ( noinline ) )
#endif

static REF_NOINLINE int64_t refQ32Mul( int64_t a, int64_t b )
{
	return fixInt128ToI64( fixInt128Shr( fixInt128MulI64( a, b ), 32 ) );
}

static REF_NOINLINE fixed_t refQ32ToFix( int64_t a )
{
	return ( a + ( (int64_t)1 << 15 ) ) >> 16;
}

#define REF_Q32_PI 13493037705LL
#define REF_Q32_HALF_PI 6746518852LL

static REF_NOINLINE fixed_t refAtan2( fixed_t y, fixed_t x )
{
	if ( x == 0 && y == 0 )
	{
		return 0;
	}

	fixed_t ax = fixAbs( x );
	fixed_t ay = fixAbs( y );
	fixed_t mx = fixMax( ay, ax );
	fixed_t mn = fixMin( ay, ax );

	int64_t a = fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( fixInt128FromI64( mn ), 32 ), fixInt128FromI64( mx ) ) );

	int64_t s = refQ32Mul( a, a );
	int64_t c = refQ32Mul( s, a );
	int64_t q = refQ32Mul( s, s );
	int64_t r = refQ32Mul( 106688212LL, q ) + 802360794LL;
	int64_t t = refQ32Mul( -404147609LL, q ) - 1426490580LL;
	r = refQ32Mul( r, s ) + t;
	r = refQ32Mul( r, c ) + a;

	if ( ay > ax )
	{
		r = REF_Q32_HALF_PI - r;
	}

	if ( x < 0 )
	{
		r = REF_Q32_PI - r;
	}

	if ( y < 0 )
	{
		r = -r;
	}

	return refQ32ToFix( r );
}

static REF_NOINLINE fixed_t refLerp( fixed_t a, fixed_t b, fixed_t alpha )
{
	return fixMul( ( FIX( 1.0f ) - alpha ) , a ) + fixMul( alpha , b );
}

static const uint64_t refExp2Table[16] = {
	0x000000016A09E668ull, 0x00000001306FE0A3ull, 0x00000001172B83C8ull, 0x000000010B5586D0ull,
	0x00000001059B0D31ull, 0x0000000102C9A3E7ull, 0x000000010163DAA0ull, 0x0000000100B1AFA6ull,
	0x000000010058C86Eull, 0x00000001002C605Eull, 0x0000000100162F39ull, 0x00000001000B175Full,
	0x0000000100058BA0ull, 0x000000010002C5CCull, 0x00000001000162E5ull, 0x000000010000B172ull,
};

static REF_NOINLINE fixed_t refLog2( fixed_t a )
{
	if ( a <= 0 )
	{
		return INT64_MIN;
	}

	int msb = 63 - __builtin_clzll( (uint64_t)a );
	int64_t integerPart = (int64_t)( msb - FIX_FRACTION_BITS );

	uint64_t m;
	if ( msb <= 62 )
	{
		m = (uint64_t)a << ( 62 - msb );
	}
	else
	{
		m = (uint64_t)a >> ( msb - 62 );
	}

	uint64_t fraction32 = 0;
	for ( int i = 0; i < 32; ++i )
	{
		fixUInt128 sq = fixUInt128MulU64( m, m );
		fraction32 <<= 1;
		if ( fixUInt128Ge( sq, fixUInt128Shl( fixUInt128FromU64( 1 ), 125 ) ) )
		{
			fraction32 |= 1;
			m = fixUInt128Lo( fixUInt128Shr( sq, 63 ) );
		}
		else
		{
			m = fixUInt128Lo( fixUInt128Shr( sq, 62 ) );
		}
	}

	int64_t fraction16 = (int64_t)( ( fraction32 + ( 1ull << 15 ) ) >> 16 );
	// box3d: ( integerPart << FIX_FRACTION_BITS ) + fraction16. integerPart is negative
	// for a < 1, where shifting a signed value left is undefined; the unsigned shift is
	// the same bits.
	return (fixed_t)( (uint64_t)integerPart << FIX_FRACTION_BITS ) + fraction16;
}

static REF_NOINLINE fixed_t refExp2( fixed_t a )
{
	int64_t n = a >> FIX_FRACTION_BITS;

	// box3d computed the fractional split here, before the range tests. n <<
	// FIX_FRACTION_BITS overflows for |a| outside the saturating range, and the split is
	// dead on both early-return paths, so the tests are hoisted above it. Identical
	// results, and this reference can run under UBSan.
	if ( n >= 47 )
	{
		return INT64_MAX;
	}

	if ( n < -17 )
	{
		return 0;
	}

	// n is negative for a < 1, where the left shift is undefined; same bits unsigned.
	uint64_t f = (uint64_t)( a - (int64_t)( (uint64_t)n << FIX_FRACTION_BITS ) );

	uint64_t r = 1ull << 32;
	for ( int k = 0; k < 16; ++k )
	{
		if ( f & ( 1ull << ( 15 - k ) ) )
		{
			r = fixUInt128Lo( fixUInt128Shr( fixUInt128MulU64( r, refExp2Table[k] ), 32 ) );
		}
	}

	int shift = 16 - (int)n;
	if ( shift <= 0 )
	{
		return (fixed_t)( r << ( -shift ) );
	}
	if ( shift >= 64 )
	{
		return 0;
	}
	return (fixed_t)( ( r + ( 1ull << ( shift - 1 ) ) ) >> shift );
}

static REF_NOINLINE fixed_t refPow( fixed_t base, fixed_t exponent )
{
	if ( base <= 0 )
	{
		return 0;
	}

	if ( exponent == 0 )
	{
		return FIX_ONE;
	}

	return refExp2( fixMul( exponent, refLog2( base ) ) );
}

static REF_NOINLINE int32_t refFix30FromFix( fixed_t a )
{
	// box3d shifted first and tested after, which overflows for |a| >= 2^49. The shifted
	// value is discarded in exactly those cases; the unsigned shift keeps the same bits
	// for every input the test can reach.
	int64_t raw = (int64_t)( (uint64_t)a << ( 30 - 16 ) );
	if ( a >= ( (fixed_t)2 << FIX_FRACTION_BITS ) )
	{
		raw = INT32_MAX;
	}
	else if ( a < -( (fixed_t)2 << FIX_FRACTION_BITS ) )
	{
		raw = INT32_MIN;
	}
	return (int32_t)raw;
}

static REF_NOINLINE fixed_t refFixFromFix30( int32_t raw )
{
	return ( (fixed_t)raw + ( (fixed_t)1 << ( ( 30 - 16 ) - 1 ) ) ) >> ( 30 - 16 );
}

static REF_NOINLINE double refFix30ToDouble( int32_t raw )
{
	return (double)raw / (double)( (int32_t)1 << 30 );
}

static REF_NOINLINE int32_t refFix30FromDouble( double x )
{
	double d = x * (double)( (int32_t)1 << 30 );
	d = ( d >= 0.0 ? d + 0.5 : d - 0.5 );
	if ( d >= (double)INT32_MAX )
	{
		d = (double)INT32_MAX;
	}
	else if ( d <= (double)INT32_MIN )
	{
		d = (double)INT32_MIN;
	}
	return (int32_t)d;
}

static REF_NOINLINE int32_t refNormalizeComponent30( int64_t raw, uint64_t length )
{
	// box3d: -raw, undefined at INT64_MIN; the unsigned negation is the same magnitude.
	// The divide goes through fixInt128Div for the same reason the library's does --
	// ClangCL has no 128-bit division builtin -- and agrees with / on these operands.
	uint64_t magnitude = raw < 0 ? -(uint64_t)raw : (uint64_t)raw;
	fixUInt128 numerator =
		fixUInt128Add( fixUInt128Shl( fixUInt128FromU64( magnitude ), 30 ), fixUInt128FromU64( length >> 1 ) );
	uint64_t q = (uint64_t)fixInt128ToI64( fixInt128Div( fixInt128FromUnsigned( numerator ), fixInt128FromU64( length ) ) );
	if ( q > (uint64_t)( (int32_t)1 << 30 ) )
	{
		q = (uint64_t)( (int32_t)1 << 30 );
	}
	return raw < 0 ? -(int32_t)q : (int32_t)q;
}

static REF_NOINLINE fixed_t refSmooth( fixed_t current, fixed_t target, fixed_t* velocity, fixed_t smoothTime, fixed_t deltaTime )
{
	if ( smoothTime <= 0 )
	{
		return target;
	}

	if ( deltaTime <= 0 )
	{
		return current;
	}

	fixed_t omega = fixDiv( 2 * FIX_PI, smoothTime );
	fixed_t onePlus = FIX_ONE + fixMul( omega, deltaTime );
	fixed_t denominator = fixMul( onePlus, onePlus );

	fixed_t spring = fixMul( fixMul( fixMul( omega, omega ), deltaTime ), current - target );

	*velocity = fixDiv( *velocity - spring, denominator );

	return current + fixMul( *velocity, deltaTime );
}

static REF_NOINLINE fixed_t refSmoothUpDown( fixed_t current, fixed_t target, fixed_t* velocity, fixed_t smoothTimeUp,
								fixed_t smoothTimeDown, fixed_t deltaTime )
{
	fixed_t smoothTime = fixAbs( target ) >= fixAbs( current ) ? smoothTimeUp : smoothTimeDown;
	return refSmooth( current, target, velocity, smoothTime, deltaTime );
}

// ================================================================================
// Structured domains
// ================================================================================

// Values that matter for a Q48.16 scalar: zero, both signs, exact halves and integers,
// every power of two and its immediate neighbours, and the saturation edges.
static fixed_t domain[4096];
static int domainCount = 0;

static void addDomain( fixed_t v )
{
	if ( domainCount < (int)( sizeof( domain ) / sizeof( domain[0] ) ) )
	{
		domain[domainCount++] = v;
	}
}

static void buildDomain( void )
{
	addDomain( 0 );
	addDomain( FIX_MAX );
	addDomain( FIX_MIN );
	addDomain( FIX_MAX - 1 );
	addDomain( FIX_MIN + 1 );

	// every power of two, both signs, and the values either side of each
	for ( int k = 0; k < 63; k++ )
	{
		fixed_t p = (fixed_t)1 << k;
		for ( int d = -2; d <= 2; d++ )
		{
			addDomain( p + d );
			addDomain( -( p + d ) );
		}
	}

	// whole and half units around the origin
	for ( int i = -64; i <= 64; i++ )
	{
		addDomain( (fixed_t)i * FIX_ONE );
		addDomain( (fixed_t)i * FIX_ONE + FIX_HALF );
		addDomain( (fixed_t)i * FIX_ONE - FIX_HALF );
		addDomain( (fixed_t)i * FIX_ONE + 1 );
		addDomain( (fixed_t)i * FIX_ONE - 1 );
	}

	// sub-unit resolution: the fraction bits one at a time
	for ( int k = 0; k < 16; k++ )
	{
		addDomain( (fixed_t)1 << k );
		addDomain( -( (fixed_t)1 << k ) );
		addDomain( FIX_ONE + ( (fixed_t)1 << k ) );
		addDomain( FIX_ONE - ( (fixed_t)1 << k ) );
	}

	// the exp2 / log2 saturation edges, named explicitly
	addDomain( FIX( 47.0f ) );
	addDomain( FIX( 47.0f ) + 1 );
	addDomain( FIX( 47.0f ) - 1 );
	addDomain( FIX( 46.0f ) + 65535 );
	addDomain( FIX( -17.0f ) );
	addDomain( FIX( -17.0f ) - 1 );
	addDomain( FIX( -18.0f ) );
	addDomain( FIX( 2.0f ) );
	addDomain( FIX( -2.0f ) );
	addDomain( FIX( 2.0f ) + 1 );
	addDomain( -FIX( 2.0f ) - 1 );
}

// ================================================================================
// The sweeps. Each returns the number of mismatches found. perturb == 0 compares
// against the reference; a non-zero perturb shifts the reference by one quantum and the
// sweep must then find mismatches.
// ================================================================================

#define SAMPLES( n ) ( (uint64_t)( n ) * (uint64_t)PORT_SWEEP_SCALE )

static uint64_t sweepLog2( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int i = 0; i < domainCount; i++ )
	{
		fixed_t got = fixLog2( domain[i] );
		fixed_t want = bump( refLog2( domain[i] ), perturb );
		if ( got != want ) bad++;
		if ( !perturb ) mix( got );
	}

	// a dense low run, where the mantissa loop does its most interesting work
	for ( fixed_t a = -8; a <= 200000; a++ )
	{
		fixed_t got = fixLog2( a );
		fixed_t want = bump( refLog2( a ), perturb );
		if ( got != want ) bad++;
	}

	rngState = 0x1234567800000001ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixed_t a = randomFixed();
		fixed_t got = fixLog2( a );
		fixed_t want = bump( refLog2( a ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepExp2( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int i = 0; i < domainCount; i++ )
	{
		fixed_t got = fixExp2( domain[i] );
		fixed_t want = bump( refExp2( domain[i] ), perturb );
		if ( got != want ) bad++;
		if ( !perturb ) mix( got );
	}

	// every representable fraction across the whole saturating exponent range
	for ( fixed_t a = FIX( -19.0f ); a <= FIX( 48.0f ); a += 37 )
	{
		fixed_t got = fixExp2( a );
		fixed_t want = bump( refExp2( a ), perturb );
		if ( got != want ) bad++;
	}

	rngState = 0x2234567800000002ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixed_t a = randomFixed();
		fixed_t got = fixExp2( a );
		fixed_t want = bump( refExp2( a ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepPow( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// structured grid: the domain crossed against a range of exponents, which covers
	// base <= 0, a zero exponent, and the saturating tails of the ladder
	for ( int i = 0; i < domainCount; i += 3 )
	{
		for ( int e = -40; e <= 40; e += 5 )
		{
			fixed_t exponent = (fixed_t)e * FIX_ONE / 8;
			fixed_t got = fixPow( domain[i], exponent );
			fixed_t want = bump( refPow( domain[i], exponent ), perturb );
			if ( got != want ) bad++;
			if ( !perturb ) mix( got );
		}
	}

	rngState = 0x3234567800000003ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixed_t base = randomFixed();
		fixed_t exponent = randomFixed();
		// half the samples in the range a game actually uses, half anywhere at all
		if ( ( i & 1 ) == 0 )
		{
			base = ( base & 0xFFFFF ) + 1;
			exponent = ( exponent % ( 8 * FIX_ONE ) );
		}
		fixed_t got = fixPow( base, exponent );
		fixed_t want = bump( refPow( base, exponent ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepAtan2( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// the axes and the quadrant boundaries, then the full cross product of the domain
	// against itself on a stride -- every sign combination, both magnitudes ordered
	// either way, and (0,0)
	for ( int i = 0; i < domainCount; i += 7 )
	{
		for ( int j = 0; j < domainCount; j += 61 )
		{
			fixed_t got = fixAtan2( domain[i], domain[j] );
			fixed_t want = bump( refAtan2( domain[i], domain[j] ), perturb );
			if ( got != want ) bad++;
			if ( !perturb ) mix( got );
		}
	}

	// a dense angular grid including y == x, y == -x and both axes
	for ( int i = -720; i <= 720; i++ )
	{
		fixed_t y = (fixed_t)i * FIX_ONE / 8;
		for ( int j = -4; j <= 4; j++ )
		{
			fixed_t x = (fixed_t)j * FIX_ONE;
			fixed_t got = fixAtan2( y, x );
			fixed_t want = bump( refAtan2( y, x ), perturb );
			if ( got != want ) bad++;
		}
	}

	rngState = 0x4234567800000004ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixed_t y = randomFixed();
		fixed_t x = randomFixed();
		fixed_t got = fixAtan2( y, x );
		fixed_t want = bump( refAtan2( y, x ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

// fixLerp is a plain integer add of two saturating products, so extreme operands
// overflow the add -- the library documents addition as the plain integer operation.
// The sweep stays inside the range where the add is defined.
static fixed_t lerpOperand( void )
{
	return (fixed_t)( (int64_t)nextRandom() >> 20 );
}

static uint64_t sweepLerp( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int i = 0; i < domainCount; i += 5 )
	{
		for ( int t = -8; t <= 24; t++ )
		{
			fixed_t alpha = (fixed_t)t * FIX_ONE / 8;
			fixed_t a = domain[i] >> 20;
			fixed_t b = domain[( i + 37 ) % domainCount] >> 20;
			fixed_t got = fixLerp( a, b, alpha );
			fixed_t want = bump( refLerp( a, b, alpha ), perturb );
			if ( got != want ) bad++;
			if ( !perturb ) mix( got );
		}
	}

	rngState = 0x5234567800000005ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixed_t a = lerpOperand();
		fixed_t b = lerpOperand();
		fixed_t alpha = (fixed_t)( (int64_t)nextRandom() >> 44 );
		fixed_t got = fixLerp( a, b, alpha );
		fixed_t want = bump( refLerp( a, b, alpha ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepFix30FromFix( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int i = 0; i < domainCount; i++ )
	{
		fixed30_t got = fix30FromFix( domain[i] );
		int32_t want = bump32( refFix30FromFix( domain[i] ), perturb );
		if ( got.raw != want ) bad++;
		if ( !perturb ) mix( got.raw );
	}

	// dense across the whole [-2, 2) domain and past both saturation edges
	for ( fixed_t a = -FIX( 3.0f ); a <= FIX( 3.0f ); a += 3 )
	{
		fixed30_t got = fix30FromFix( a );
		int32_t want = bump32( refFix30FromFix( a ), perturb );
		if ( got.raw != want ) bad++;
	}

	rngState = 0x6234567800000006ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		// half inside the domain where the shift is exact, half anywhere at all
		fixed_t a = ( i & 1 ) ? randomFixed() : (fixed_t)( (int64_t)nextRandom() >> 44 );
		fixed30_t got = fix30FromFix( a );
		int32_t want = bump32( refFix30FromFix( a ), perturb );
		if ( got.raw != want ) bad++;
	}

	return bad;
}

static uint64_t sweepFixFromFix30( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	static const int32_t edges[] = { 0, 1, -1, INT32_MAX, INT32_MIN, INT32_MAX - 1, INT32_MIN + 1,
									 ( (int32_t)1 << 30 ), -( (int32_t)1 << 30 ), ( (int32_t)1 << 14 ),
									 ( (int32_t)1 << 13 ), -( (int32_t)1 << 13 ), -( (int32_t)1 << 14 ) };
	for ( unsigned i = 0; i < sizeof( edges ) / sizeof( edges[0] ); i++ )
	{
		fixed30_t v = { edges[i] };
		fixed_t got = fixFromFix30( v );
		fixed_t want = bump( refFixFromFix30( edges[i] ), perturb );
		if ( got != want ) bad++;
		if ( !perturb ) mix( got );
	}

	// dense around zero, where the rounding rule decides the answer
	for ( int32_t raw = -100000; raw <= 100000; raw++ )
	{
		fixed30_t v = { raw };
		fixed_t got = fixFromFix30( v );
		fixed_t want = bump( refFixFromFix30( raw ), perturb );
		if ( got != want ) bad++;
	}

	rngState = 0x7234567800000007ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int32_t raw = (int32_t)nextRandom();
		fixed30_t v = { raw };
		fixed_t got = fixFromFix30( v );
		fixed_t want = bump( refFixFromFix30( raw ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepFix30ToDouble( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int32_t raw = -100000; raw <= 100000; raw += 7 )
	{
		fixed30_t v = { raw };
		double got = fix30ToDouble( v );
		double want = refFix30ToDouble( raw );
		// one quantum on a double result is one ulp of the Q2.30 grid
		if ( perturb ) want += 1.0 / (double)( (int32_t)1 << 30 );
		if ( memcmp( &got, &want, sizeof( double ) ) != 0 ) bad++;
	}

	rngState = 0x8234567800000008ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int32_t raw = (int32_t)nextRandom();
		fixed30_t v = { raw };
		double got = fix30ToDouble( v );
		double want = refFix30ToDouble( raw );
		if ( perturb ) want += 1.0 / (double)( (int32_t)1 << 30 );
		if ( memcmp( &got, &want, sizeof( double ) ) != 0 ) bad++;
		if ( !perturb && i < 4096 )
		{
			int64_t bits;
			memcpy( &bits, &got, sizeof( bits ) );
			mix( bits );
		}
	}

	return bad;
}

static uint64_t sweepFix30FromDouble( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// A finite input, like the library's other double boundary helpers require.
	static const double edges[] = { 0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 1.9999999, -1.9999999,
									3.0, -3.0, 1e9, -1e9, 1.0 / 1073741824.0, -1.0 / 1073741824.0,
									0.4999999999, -0.4999999999 };
	for ( unsigned i = 0; i < sizeof( edges ) / sizeof( edges[0] ); i++ )
	{
		fixed30_t got = fix30FromDouble( edges[i] );
		int32_t want = bump32( refFix30FromDouble( edges[i] ), perturb );
		if ( got.raw != want ) bad++;
		if ( !perturb ) mix( got.raw );
	}

	for ( int i = -300000; i <= 300000; i += 11 )
	{
		double x = (double)i / 100000.0;
		fixed30_t got = fix30FromDouble( x );
		int32_t want = bump32( refFix30FromDouble( x ), perturb );
		if ( got.raw != want ) bad++;
	}

	rngState = 0x9234567800000009ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		// deterministic finite doubles spanning well past both saturation edges
		double x = (double)( (int64_t)nextRandom() >> 24 ) / 1073741824.0;
		fixed30_t got = fix30FromDouble( x );
		int32_t want = bump32( refFix30FromDouble( x ), perturb );
		if ( got.raw != want ) bad++;
	}

	return bad;
}

static uint64_t sweepNormalize30( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// length is a Q2.30 magnitude and must be non-zero: a zero length has no direction.
	static const int64_t raws[] = { 0, 1, -1, ( (int64_t)1 << 30 ), -( (int64_t)1 << 30 ),
									( (int64_t)1 << 31 ), -( (int64_t)1 << 31 ), 12345, -12345 };
	static const uint64_t lengths[] = { 1, 2, 3, ( 1ull << 15 ), ( 1ull << 30 ), ( 1ull << 31 ),
										( 1ull << 30 ) + 1, ( 1ull << 30 ) - 1, 0x7FFFFFFFull };
	for ( unsigned i = 0; i < sizeof( raws ) / sizeof( raws[0] ); i++ )
	{
		for ( unsigned j = 0; j < sizeof( lengths ) / sizeof( lengths[0] ); j++ )
		{
			int32_t got = fixNormalizeComponent30( raws[i], lengths[j] );
			int32_t want = bump32( refNormalizeComponent30( raws[i], lengths[j] ), perturb );
			if ( got != want ) bad++;
			if ( !perturb ) mix( got );
		}
	}

	// the clamp path: raw at and just past the length, where the rounded divide overshoots
	for ( int64_t raw = -70000; raw <= 70000; raw += 3 )
	{
		uint64_t length = 65536;
		int32_t got = fixNormalizeComponent30( raw, length );
		int32_t want = bump32( refNormalizeComponent30( raw, length ), perturb );
		if ( got != want ) bad++;
	}

	rngState = 0xA23456780000000AULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int64_t raw = (int64_t)(int32_t)nextRandom();
		uint64_t length = ( nextRandom() >> 33 ) + 1; // non-zero, inside the Q2.30 magnitude range
		int32_t got = fixNormalizeComponent30( raw, length );
		int32_t want = bump32( refNormalizeComponent30( raw, length ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

// The smoothing pair carries state through a velocity pointer. perturb == 1 shifts the
// returned value, perturb == 2 shifts the velocity the reference writes. BOTH must be
// caught, or the sweep is only half looking.
// SMOOTHING DOMAIN. The box3d formula is not total over the whole Q48.16 range: with a
// smoothTime of a few raw units, omega saturates, the spring term saturates to
// INT64_MAX, and `*velocity - spring` then overflows for a velocity of the opposite
// sign. That is inherited behavior, not something introduced here, and it is well
// outside any smoothing time a caller would use (a raw smoothTime of 1 is 1/65536 of a
// second). The sweeps stay inside the region where every intermediate is defined:
// smoothTime at or above FIX_ONE/64, magnitudes at or below 2^29. Non-positive
// smoothTime and deltaTime are still swept, because those return before any arithmetic.
static uint64_t sweepSmooth( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	static const fixed_t times[] = { 0, -1, -FIX_ONE, FIX_ONE / 64, FIX_ONE / 8, FIX_ONE, FIX( 4.0f ) };
	static const fixed_t deltas[] = { 0, -1, 1, FIX_ONE / 240, FIX_ONE / 60, FIX_ONE / 30, FIX_ONE };

	for ( int i = 0; i < domainCount; i += 11 )
	{
		for ( unsigned s = 0; s < sizeof( times ) / sizeof( times[0] ); s++ )
		{
			for ( unsigned d = 0; d < sizeof( deltas ) / sizeof( deltas[0] ); d++ )
			{
				fixed_t current = domain[i] >> 34;
				fixed_t target = domain[( i + 53 ) % domainCount] >> 34;
				fixed_t v0 = domain[( i + 101 ) % domainCount] >> 34;

				fixed_t gotVelocity = v0;
				fixed_t got = fixSmoothCriticallyDamped( current, target, &gotVelocity, times[s], deltas[d] );

				fixed_t wantVelocity = v0;
				fixed_t want = refSmooth( current, target, &wantVelocity, times[s], deltas[d] );
				if ( perturb == 1 ) want = bump( want, 1 );
				if ( perturb == 2 ) wantVelocity = bump( wantVelocity, 1 );

				if ( got != want || gotVelocity != wantVelocity ) bad++;
				if ( !perturb ) { mix( got ); mix( gotVelocity ); }
			}
		}
	}

	// an integrated run: state carried forward, which is how the game uses it
	{
		fixed_t gotVelocity = 0, wantVelocity = 0;
		fixed_t gotCurrent = FIX( 10.0f ), wantCurrent = FIX( 10.0f );
		for ( int step = 0; step < 4000; step++ )
		{
			fixed_t target = ( step % 400 < 200 ) ? FIX( -5.0f ) : FIX( 7.5f );
			gotCurrent = fixSmoothCriticallyDamped( gotCurrent, target, &gotVelocity, FIX_ONE / 4, FIX_ONE / 60 );
			wantCurrent = refSmooth( wantCurrent, target, &wantVelocity, FIX_ONE / 4, FIX_ONE / 60 );
			if ( perturb == 1 ) wantCurrent = bump( wantCurrent, 1 );
			if ( perturb == 2 ) wantVelocity = bump( wantVelocity, 1 );
			if ( gotCurrent != wantCurrent || gotVelocity != wantVelocity ) bad++;
			if ( !perturb ) { mix( gotCurrent ); mix( gotVelocity ); }
			if ( perturb ) break; // one divergence is the signal; carrying it forward proves nothing more
		}
	}

	rngState = 0xB23456780000000BULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixed_t current = (fixed_t)( (int64_t)nextRandom() >> 35 );
		fixed_t target = (fixed_t)( (int64_t)nextRandom() >> 35 );
		fixed_t v0 = (fixed_t)( (int64_t)nextRandom() >> 35 );
		fixed_t smoothTime = (fixed_t)( nextRandom() % ( 4 * FIX_ONE ) ) + FIX_ONE / 64;
		fixed_t deltaTime = (fixed_t)( nextRandom() % ( FIX_ONE / 8 ) ) + 1;

		// the two early-return paths, which take no arithmetic at all
		if ( ( i % 16 ) == 0 ) smoothTime = 0;
		if ( ( i % 16 ) == 1 ) smoothTime = -(fixed_t)( nextRandom() % FIX_ONE ) - 1;
		if ( ( i % 16 ) == 2 ) deltaTime = 0;
		if ( ( i % 16 ) == 3 ) deltaTime = -1;

		fixed_t gotVelocity = v0;
		fixed_t got = fixSmoothCriticallyDamped( current, target, &gotVelocity, smoothTime, deltaTime );

		fixed_t wantVelocity = v0;
		fixed_t want = refSmooth( current, target, &wantVelocity, smoothTime, deltaTime );
		if ( perturb == 1 ) want = bump( want, 1 );
		if ( perturb == 2 ) wantVelocity = bump( wantVelocity, 1 );

		if ( got != want || gotVelocity != wantVelocity ) bad++;
	}

	return bad;
}

static uint64_t sweepSmoothUpDown( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	static const fixed_t times[] = { 0, -1, -FIX_ONE, FIX_ONE / 64, FIX_ONE / 8, FIX_ONE, FIX( 4.0f ) };

	// The whole point of this one is the |target| >= |current| selection, so the
	// structured pass walks current and target across each other through equality.
	for ( int c = -40; c <= 40; c += 1 )
	{
		for ( int t = -40; t <= 40; t += 1 )
		{
			fixed_t current = (fixed_t)c * FIX_ONE / 4;
			fixed_t target = (fixed_t)t * FIX_ONE / 4;
			fixed_t up = times[( c + 40 ) % 7];
			fixed_t down = times[( t + 40 ) % 7];

			fixed_t gotVelocity = (fixed_t)( c - t ) * FIX_ONE / 16;
			fixed_t got = fixSmoothCriticallyDampedUpDown( current, target, &gotVelocity, up, down, FIX_ONE / 60 );

			fixed_t wantVelocity = (fixed_t)( c - t ) * FIX_ONE / 16;
			fixed_t want = refSmoothUpDown( current, target, &wantVelocity, up, down, FIX_ONE / 60 );
			if ( perturb == 1 ) want = bump( want, 1 );
			if ( perturb == 2 ) wantVelocity = bump( wantVelocity, 1 );

			if ( got != want || gotVelocity != wantVelocity ) bad++;
			if ( !perturb ) { mix( got ); mix( gotVelocity ); }
		}
	}

	rngState = 0xC23456780000000CULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixed_t current = (fixed_t)( (int64_t)nextRandom() >> 35 );
		fixed_t target = (fixed_t)( (int64_t)nextRandom() >> 35 );
		fixed_t v0 = (fixed_t)( (int64_t)nextRandom() >> 35 );
		fixed_t up = (fixed_t)( nextRandom() % ( 4 * FIX_ONE ) ) + FIX_ONE / 64;
		fixed_t down = (fixed_t)( nextRandom() % ( 4 * FIX_ONE ) ) + FIX_ONE / 64;
		fixed_t deltaTime = (fixed_t)( nextRandom() % ( FIX_ONE / 8 ) ) + 1;

		// half the samples put |target| and |current| within one unit of each other, so the
		// up/down selection is actually exercised rather than almost always taking one arm
		if ( ( i & 1 ) == 0 ) target = fixAbs( current ) + (fixed_t)( nextRandom() % ( 2 * FIX_ONE ) ) - FIX_ONE;
		if ( ( i % 16 ) == 3 ) deltaTime = 0;
		if ( ( i % 16 ) == 5 ) up = 0;

		fixed_t gotVelocity = v0;
		fixed_t got = fixSmoothCriticallyDampedUpDown( current, target, &gotVelocity, up, down, deltaTime );

		fixed_t wantVelocity = v0;
		fixed_t want = refSmoothUpDown( current, target, &wantVelocity, up, down, deltaTime );
		if ( perturb == 1 ) want = bump( want, 1 );
		if ( perturb == 2 ) wantVelocity = bump( wantVelocity, 1 );

		if ( got != want || gotVelocity != wantVelocity ) bad++;
	}

	return bad;
}

// The reference min/max, perturbed in place and forced out of line: fixInt128Min is a
// header-inline conditional and so is this, which is exactly the shape the optimizer
// folded on pull request #9.
static REF_NOINLINE fixInt128 refInt128Min( fixInt128 a, fixInt128 b, int perturb )
{
	fixInt128 m = fixInt128Lt( a, b ) ? a : b;
	return fixInt128FromUnsigned( fixUInt128Add( fixInt128ToUnsigned( m ), fixUInt128FromU64( (uint64_t)perturb ) ) );
}

static REF_NOINLINE fixInt128 refInt128Max( fixInt128 a, fixInt128 b, int perturb )
{
	fixInt128 m = fixInt128Gt( a, b ) ? a : b;
	return fixInt128FromUnsigned( fixUInt128Add( fixInt128ToUnsigned( m ), fixUInt128FromU64( (uint64_t)perturb ) ) );
}

static uint64_t sweepInt128MinMax( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// 128-bit values built from pairs of 64-bit halves, including both saturation ends,
	// equal operands, and values that differ only in the low or only in the high half
	static const uint64_t halves[] = { 0, 1, UINT64_MAX, ( 1ull << 63 ), ( 1ull << 63 ) - 1, 0x5555555555555555ull };
	for ( unsigned ah = 0; ah < sizeof( halves ) / sizeof( halves[0] ); ah++ )
	{
		for ( unsigned al = 0; al < sizeof( halves ) / sizeof( halves[0] ); al++ )
		{
			for ( unsigned bh = 0; bh < sizeof( halves ) / sizeof( halves[0] ); bh++ )
			{
				for ( unsigned bl = 0; bl < sizeof( halves ) / sizeof( halves[0] ); bl++ )
				{
					fixInt128 a = fixInt128Make( halves[ah], halves[al] );
					fixInt128 b = fixInt128Make( halves[bh], halves[bl] );

					fixInt128 gotMin = fixInt128Min( a, b );
					fixInt128 gotMax = fixInt128Max( a, b );
					fixInt128 wantMin = refInt128Min( a, b, perturb );
					fixInt128 wantMax = refInt128Max( a, b, perturb );

					if ( !fixInt128Eq( gotMin, wantMin ) || !fixInt128Eq( gotMax, wantMax ) ) bad++;
					if ( !perturb ) { mix( fixInt128ToI64( gotMin ) ); mix( (int64_t)fixInt128Hi( gotMax ) ); }
				}
			}
		}
	}

	rngState = 0xD23456780000000DULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		// keep one below the top so the +1 perturbation cannot itself overflow
		fixInt128 a = fixInt128Make( nextRandom() >> 1, nextRandom() );
		fixInt128 b = fixInt128Make( nextRandom() >> 1, nextRandom() );
		if ( ( i & 1 ) == 0 ) a = fixInt128Neg( a );
		if ( ( i & 2 ) == 0 ) b = fixInt128Neg( b );

		fixInt128 gotMin = fixInt128Min( a, b );
		fixInt128 gotMax = fixInt128Max( a, b );
		fixInt128 wantMin = refInt128Min( a, b, perturb );
		fixInt128 wantMax = refInt128Max( a, b, perturb );

		if ( !fixInt128Eq( gotMin, wantMin ) || !fixInt128Eq( gotMax, wantMax ) ) bad++;
	}

	return bad;
}

// ================================================================================

typedef uint64_t ( *SweepFcn )( int perturb, uint64_t samples );

// Run one sweep clean, then once per negative-control arm. Clean must find nothing and
// every control arm must find something.
static void run( const char* name, SweepFcn sweep, uint64_t samples, int controlArms )
{
	uint64_t clean = sweep( 0, samples );
	if ( clean != 0 )
	{
		printf( "FAIL %s: %llu mismatches against the fixed3d reference\n", name, (unsigned long long)clean );
		fails++;
		return;
	}

	for ( int arm = 1; arm <= controlArms; arm++ )
	{
		uint64_t dirty = sweep( arm, samples );

		// Not merely "some mismatch". A one-quantum shift changes EVERY comparison, so a
		// live sweep reports at least one mismatch per random sample on top of whatever
		// the structured pass finds. Accepting any non-zero count here would pass a sweep
		// whose random loop never ran, or ran over an empty domain, on the strength of a
		// single structured edge case.
		if ( dirty < samples )
		{
			printf( "FAIL %s: negative control arm %d found %llu mismatches, expected at least %llu -- "
					"part of the sweep is blind\n",
					name, arm, (unsigned long long)dirty, (unsigned long long)samples );
			fails++;
			return;
		}
	}

	printf( "%s\n", name );
	if ( printCounts )
	{
		printf( "        samples=%llu structured+random, control arms=%d\n", (unsigned long long)samples,
				controlArms );
	}
}

int main( int argc, char** argv )
{
	for ( int i = 1; i < argc; i++ )
	{
		if ( strcmp( argv[i], "--counts" ) == 0 ) printCounts = 1;
	}

	buildDomain();

	run( "fixLog2 matches fixed3d b3FixLog2", sweepLog2, SAMPLES( 250000 ), 1 );
	run( "fixExp2 matches fixed3d b3FixExp2", sweepExp2, SAMPLES( 400000 ), 1 );
	run( "fixPow matches fixed3d b3FixPow", sweepPow, SAMPLES( 200000 ), 1 );
	run( "fixAtan2 matches fixed3d b3Atan2", sweepAtan2, SAMPLES( 400000 ), 1 );
	run( "fixLerp matches fixed3d b3FixLerp", sweepLerp, SAMPLES( 800000 ), 1 );
	run( "fix30FromFix matches fixed3d b3Fix30FromFix", sweepFix30FromFix, SAMPLES( 800000 ), 1 );
	run( "fixFromFix30 matches fixed3d b3FixFromFix30", sweepFixFromFix30, SAMPLES( 800000 ), 1 );
	run( "fix30ToDouble matches fixed3d b3Fix30ToDouble", sweepFix30ToDouble, SAMPLES( 500000 ), 1 );
	run( "fix30FromDouble matches fixed3d b3Fix30FromDouble", sweepFix30FromDouble, SAMPLES( 500000 ), 1 );
	run( "fixNormalizeComponent30 matches fixed3d b3NormalizeComponent30", sweepNormalize30, SAMPLES( 400000 ), 1 );
	run( "fixSmoothCriticallyDamped matches fixed3d b3SmoothCriticallyDamped", sweepSmooth, SAMPLES( 200000 ), 2 );
	run( "fixSmoothCriticallyDampedUpDown matches fixed3d b3SmoothCriticallyDampedUpDown", sweepSmoothUpDown,
		 SAMPLES( 200000 ), 2 );
	run( "fixInt128Min and fixInt128Max match fixed3d b3W_min128 and b3W_max128", sweepInt128MinMax,
		 SAMPLES( 500000 ), 1 );

	// fixLerp must be reachable from fixed_math.h alone, which is where it now lives.
	// A consumer including only the scalar math header should not need the vector one.
	{
		fixed_t l = fixLerp( 0, FIX_ONE, FIX_HALF );
		if ( l != FIX_HALF )
		{
			printf( "FAIL fixLerp from fixed_math.h: got %lld\n", (long long)l );
			fails++;
		}
		else
		{
			printf( "fixLerp is declared in fixed_math.h\n" );
		}
	}

	if ( EXPECTED_PORT_HASH == 0ULL )
	{
		printf( "port structured sweep hash = 0x%016llx (capture mode: not yet frozen)\n",
				(unsigned long long)fnv );
	}
	else if ( fnv != (uint64_t)EXPECTED_PORT_HASH )
	{
		printf( "FAIL port structured sweep hash = 0x%016llx, expected 0x%016llx\n", (unsigned long long)fnv,
				(unsigned long long)EXPECTED_PORT_HASH );
		fails++;
	}
	else
	{
		printf( "port structured sweep hash is unchanged across platforms\n" );
	}

	if ( fails )
	{
		printf( "FAILED: %d check(s)\n", fails );
		return 1;
	}

	return 0;
}
