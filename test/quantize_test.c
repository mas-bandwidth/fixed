// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Bit-equality between this library's domain-crossing family and the delta bodies it was
// ported from.
//
// delta (mas-bandwidth/delta, delta_quantize.h) is about to depend on fixed and delete
// its own copies of these functions. "Ported" therefore has to mean THE SAME BITS, not
// "close enough": delta's whole reason for existing is that a client and a server compute
// the same numbers, and a one-unit disagreement in narrow is a rotation that decodes to a
// value the authority never held. So each function is checked against a verbatim
// transcription of delta's body over a structured domain sweep plus deterministic
// pseudo-random sampling.
//
// NEGATIVE CONTROL, INLINE AND ALWAYS ON, in the manner of port_equality_test.c. Every
// sweep runs twice: once against the reference, and once against the reference perturbed
// by one quantum. The first must find zero mismatches and the second must find at least
// one per random sample. A comparison that only ever runs its passing arm cannot tell you
// it is comparing anything.
//
// REF_NOINLINE IS LOAD-BEARING. These references are transcriptions of code that is
// header-inline in this library, so the compiler can see both sides, prove them
// equivalent and delete the sweep -- measured on pull request #9, where 200 million
// comparisons folded to nothing and ran in 0.00s while reporting zero mismatches. The
// negative control did NOT catch it, because a folded comparison folds identically on the
// perturbed arm. Keeping the references out of line forces both sides to execute, and
// main() prints the elapsed time so a suspiciously instant run is visible.
//
// THE SUITE ALSO CHECKS THE GENERALIZATION CLAIM. This family is supposed to be the
// general form of converters fixed already had at pinned formats. That is an assertion
// about behaviour, so it is checked as one: fixFromFix30 must BE fixNarrow at
// FIX30_SHIFT, and fixFromDouble must BE fixQuantize at FIX_ONE, for every input. If they
// ever diverge, the library has two rounding rules wearing one description.

#include "fixed/fixed.h"
#include "fixed/fixed_quantize.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef QUANTIZE_SWEEP_SCALE
	#define QUANTIZE_SWEEP_SCALE 1
#endif
#define SAMPLES( n ) ( (uint64_t)( n ) * (uint64_t)QUANTIZE_SWEEP_SCALE )

// Negative control in the other direction, built as a separate WILL_FAIL target in the
// manner of exhaustive_test.c. The inline controls perturb the REFERENCE and prove the
// sweeps can see a difference; this one perturbs the LIBRARY, which is where a real
// regression would come from. The macro does not expand recursively, so the call inside
// resolves to the real function.
#ifdef QUANTIZE_NEGATIVE_CONTROL
	#define fixNarrow( raw, shift ) ( fixNarrow( raw, shift ) + 1 )
#endif

#if defined( _MSC_VER ) && !defined( __clang__ )
	#define REF_NOINLINE __declspec( noinline )
#else
	#define REF_NOINLINE __attribute__( ( noinline ) )
#endif

static int fails = 0;
static int printCounts = 0;

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix( int64_t v )
{
	const uint8_t* b = (const uint8_t*)&v;
	for ( int i = 0; i < 8; i++ ) { fnv ^= b[i]; fnv *= 0x00000100000001B3ULL; }
}

// FROZEN 2026-08-24. Every OS and compiler in CI must reproduce this exactly, on both the
// native and the emulated 128-bit arm. If a legitimate change alters it, re-capture
// deliberately and say why in the pull request; never loosen it to make a mismatch go away.
#ifndef EXPECTED_QUANTIZE_HASH
	#define EXPECTED_QUANTIZE_HASH 0xbfe0bfe2728c1e5fULL
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

// ================================================================================
// The reference implementations, transcribed from delta_quantize.h.
//
// The arithmetic is untouched. Two changes, both stated because they are the only ones:
// delta_assert is dropped (an assertion is not part of the value contract, and the sweeps
// stay inside the asserted domain anyway), and where delta's body relies on
// signed-overflow or negative-shift undefined behaviour the transcription uses the
// equivalent unsigned form -- the transcription has to be runnable under the UBSan job,
// and the two agree on every input a defined execution can observe.
// ================================================================================

static REF_NOINLINE int64_t refQuantize( double value, int64_t scale )
{
	const double scaled = value * (double)scale;

	return ( scaled >= 0.0 ) ? (int64_t)( scaled + 0.5 ) : -(int64_t)( -scaled + 0.5 );
}

static REF_NOINLINE double refDequantize( int64_t raw, int64_t scale )
{
	return (double)raw / (double)scale;
}

static REF_NOINLINE int64_t refQuantizeClamped( double value, int64_t scale, int64_t minRaw, int64_t maxRaw )
{
	int64_t raw = refQuantize( value, scale );

	if ( raw < minRaw )
	{
		raw = minRaw;
	}
	else if ( raw > maxRaw )
	{
		raw = maxRaw;
	}

	return raw;
}

static REF_NOINLINE bool refFits( int64_t raw, int64_t minRaw, int64_t maxRaw )
{
	return raw >= minRaw && raw <= maxRaw;
}

static REF_NOINLINE int64_t refNarrow( int64_t raw, int shift )
{
	const int64_t half = ( (int64_t)1 << shift ) >> 1;

	// delta: ( raw + half ) >> shift. Unsigned add, same bits inside the asserted domain.
	return (int64_t)( (uint64_t)raw + (uint64_t)half ) >> shift;
}

static REF_NOINLINE int64_t refWiden( int64_t raw, int shift )
{
	// delta: raw << shift. Unsigned shift, same bits, and defined for negative raw.
	return (int64_t)( (uint64_t)raw << shift );
}

// One quantum applied to a reference value. The add is unsigned because a reference value
// can sit at INT64_MAX, where +1 as a signed value is overflow -- the negative control
// must not itself be undefined behaviour.
static int64_t bump( int64_t v, int perturb )
{
	return (int64_t)( (uint64_t)v + (uint64_t)(int64_t)perturb );
}

// ================================================================================
// The domain.
// ================================================================================

// Scales a real consumer names: raw integers, hundredths, a rotation at 10 fraction bits,
// this library's own Q48.16 and Q2.30 units, and a couple of wide ones. Capped at 2^40 so
// that value * scale below cannot leave the int64 range a double-to-int64 conversion is
// defined for.
static const int64_t scales[] = {
	1, 2, 10, 100, 1000, 1024, 65536 /* FIX_ONE */, ( (int64_t)1 << 30 ), ( (int64_t)1 << 40 ),
};
#define SCALE_COUNT ( (int)( sizeof( scales ) / sizeof( scales[0] ) ) )

// Values chosen for the tie: half away from zero is only distinguishable from half up, or
// from truncation, exactly at .5 and exactly at -.5.
static const double values[] = {
	0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999999, -0.49999999, 0.50000001, -0.50000001,
	1.0, -1.0, 3.25, -3.25, 1000.5, -1000.5, 0.0001, -0.0001,
};
#define VALUE_COUNT ( (int)( sizeof( values ) / sizeof( values[0] ) ) )

// ================================================================================
// The sweeps.
// ================================================================================

static uint64_t sweepQuantize( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int v = 0; v < VALUE_COUNT; v++ )
	{
		for ( int s = 0; s < SCALE_COUNT; s++ )
		{
			int64_t got = fixQuantize( values[v], scales[s] );
			int64_t want = bump( refQuantize( values[v], scales[s] ), perturb );

			if ( got != want ) bad++;
			if ( !perturb ) mix( got );
		}
	}

	rngState = 0x51000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int64_t scale = scales[nextRandom() % (uint64_t)SCALE_COUNT];

		// A magnitude below 1e6, so value * scale stays well inside the range a
		// double-to-int64 conversion is defined for even at the widest scale.
		double value = (double)( (int64_t)( nextRandom() % 2000000001ULL ) - 1000000000 ) / 1000.0;

		// Half the samples land exactly on a tie, which is the only place the rounding
		// rule is observable at all. Random doubles almost never do.
		if ( ( i & 1 ) == 0 )
		{
			int64_t k = (int64_t)( nextRandom() % 2000001ULL ) - 1000000;
			value = ( (double)k + 0.5 ) / (double)scale;
		}

		int64_t got = fixQuantize( value, scale );
		int64_t want = bump( refQuantize( value, scale ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepDequantize( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// The perturbation goes on the reference's INPUT: a double has no natural quantum to
	// add, and one raw unit is exactly the step this function is defined by, so the
	// perturbed reference differs by 1/scale on every single call.
	for ( int v = -60; v <= 60; v++ )
	{
		for ( int s = 0; s < SCALE_COUNT; s++ )
		{
			int64_t raw = (int64_t)v * 1000 + v;

			double got = fixDequantize( raw, scales[s] );
			double want = refDequantize( bump( raw, perturb ), scales[s] );

			if ( got != want ) bad++;
			if ( !perturb )
			{
				int64_t bits;
				memcpy( &bits, &got, sizeof( bits ) );
				mix( bits );
			}
		}
	}

	rngState = 0x52000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int64_t raw = (int64_t)nextRandom() >> 12;
		int64_t scale = scales[nextRandom() % (uint64_t)SCALE_COUNT];

		double got = fixDequantize( raw, scale );
		double want = refDequantize( bump( raw, perturb ), scale );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepQuantizeClamped( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int v = 0; v < VALUE_COUNT; v++ )
	{
		for ( int s = 0; s < SCALE_COUNT; s++ )
		{
			// Bounds that put the unclamped answer below, inside, and above, so both
			// clamp arms and the pass-through arm all run.
			int64_t centre = refQuantize( values[v], scales[s] );
			for ( int b = 0; b < 3; b++ )
			{
				int64_t minRaw = centre - ( b == 0 ? 100 : ( b == 1 ? 0 : -50 ) );
				int64_t maxRaw = centre + ( b == 0 ? 100 : ( b == 1 ? -0 : -10 ) );
				if ( minRaw > maxRaw ) { int64_t t = minRaw; minRaw = maxRaw; maxRaw = t; }

				int64_t got = fixQuantizeClamped( values[v], scales[s], minRaw, maxRaw );
				int64_t want = bump( refQuantizeClamped( values[v], scales[s], minRaw, maxRaw ), perturb );

				if ( got != want ) bad++;
				if ( !perturb ) mix( got );
			}
		}
	}

	rngState = 0x53000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int64_t scale = scales[nextRandom() % (uint64_t)SCALE_COUNT];
		double value = (double)( (int64_t)( nextRandom() % 2000000001ULL ) - 1000000000 ) / 1000.0;

		int64_t centre = refQuantize( value, scale );
		int64_t spread = (int64_t)( nextRandom() % 2000ULL );
		int64_t offset = (int64_t)( nextRandom() % 4001ULL ) - 2000;
		int64_t minRaw = centre + offset - spread;
		int64_t maxRaw = centre + offset + spread;

		int64_t got = fixQuantizeClamped( value, scale, minRaw, maxRaw );
		int64_t want = bump( refQuantizeClamped( value, scale, minRaw, maxRaw ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepFits( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// The structured pass walks a raw value across a fixed window from well below to well
	// above, so every ordering the predicate can see is covered.
	for ( int64_t minRaw = -3; minRaw <= 3; minRaw++ )
	{
		for ( int64_t maxRaw = minRaw; maxRaw <= minRaw + 6; maxRaw++ )
		{
			for ( int64_t raw = minRaw - 4; raw <= maxRaw + 4; raw++ )
			{
				bool got = fixFits( raw, minRaw, maxRaw );
				bool want = refFits( bump( raw, perturb ), minRaw, maxRaw );

				if ( got != want ) bad++;
				if ( !perturb ) mix( got ? 1 : 0 );
			}
		}
	}

	// THE RANDOM SAMPLES SIT ON A BOUND ON PURPOSE, and the negative control is the
	// reason. A predicate has no result to perturb -- flipping the answer bit would prove
	// nothing about whether the operands ever reached the comparison -- so the control
	// perturbs the raw value the REFERENCE sees. That only changes an answer when the
	// value is already at the edge, so every sample sits exactly on a bound and is nudged
	// off it: the upper bound moves up and out, the lower bound moves down and out. Both
	// bounds get covered and the control fires on every single sample.
	rngState = 0x54000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int64_t minRaw = (int64_t)nextRandom() >> 20;
		int64_t maxRaw = minRaw + (int64_t)( nextRandom() % 10000ULL );

		bool onUpper = ( i & 1 ) != 0;
		int64_t raw = onUpper ? maxRaw : minRaw;
		int64_t nudged = bump( raw, onUpper ? perturb : -perturb );

		bool got = fixFits( raw, minRaw, maxRaw );
		bool want = refFits( nudged, minRaw, maxRaw );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepNarrow( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// Every legal shift against values around zero and around the rounding boundary. The
	// boundary is the whole point of the function: half toward positive infinity is only
	// distinguishable from half away from zero on the negative side of it.
	static const int64_t raws[] = { 0, 1, -1, 2, -2, 3, -3, 7, -7, 8, -8, 12345, -12345, 65536, -65536, 65535, -65535 };
	const int rawCount = (int)( sizeof( raws ) / sizeof( raws[0] ) );

	for ( int shift = 0; shift < 63; shift++ )
	{
		for ( int r = 0; r < rawCount; r++ )
		{
			int64_t got = fixNarrow( raws[r], shift );
			int64_t want = bump( refNarrow( raws[r], shift ), perturb );

			if ( got != want ) bad++;
			if ( !perturb ) mix( got );
		}
	}

	rngState = 0x55000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int shift = (int)( nextRandom() % 63 );
		int64_t half = ( (int64_t)1 << shift ) >> 1;

		int64_t raw;
		if ( ( i & 1 ) == 0 && shift > 0 )
		{
			// Half the samples land within a few units of an exact rounding boundary,
			// where the tie rule is observable, rather than in the vast interior where it
			// is not. The quotient is drawn narrow enough that quotient * 2^shift plus
			// half plus the jitter cannot leave the domain the function asserts -- an
			// earlier version of this loop did leave it, and the assertion in a
			// non-NDEBUG build is what said so.
			int drop = ( shift + 3 < 63 ) ? shift + 3 : 63;
			int64_t quotient = (int64_t)nextRandom() >> drop;
			raw = quotient * ( (int64_t)1 << shift ) + half + ( (int64_t)( nextRandom() % 5ULL ) - 2 );
		}
		else
		{
			raw = (int64_t)nextRandom() >> 2;
		}

		// Kept below INT64_MAX - half, the domain the function asserts. Outside it the
		// reference has no defined value to agree with.
		if ( raw > INT64_MAX - half ) raw = INT64_MAX - half;

		int64_t got = fixNarrow( raw, shift );
		int64_t want = bump( refNarrow( raw, shift ), perturb );
		if ( got != want ) bad++;
	}

	return bad;
}

static uint64_t sweepWiden( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	static const int64_t raws[] = { 0, 1, -1, 2, -2, 3, -3, 12345, -12345, 65536, -65536 };
	const int rawCount = (int)( sizeof( raws ) / sizeof( raws[0] ) );

	for ( int shift = 0; shift < 45; shift++ )
	{
		for ( int r = 0; r < rawCount; r++ )
		{
			int64_t got = fixWiden( raws[r], shift );
			int64_t want = bump( refWiden( raws[r], shift ), perturb );

			if ( got != want ) bad++;
			if ( !perturb ) mix( got );
		}
	}

	rngState = 0x56000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int shift = (int)( nextRandom() % 63 );

		// Inside the asserted lossless range, which is what widen promises.
		int64_t raw = (int64_t)nextRandom();
		raw >>= ( shift + 1 );

		int64_t got = fixWiden( raw, shift );
		int64_t want = bump( refWiden( raw, shift ), perturb );
		if ( got != want ) bad++;

		// Round trip: narrow undoes widen exactly. The other order does not, and that is
		// the whole point of the pair.
		if ( !perturb && fixNarrow( fixWiden( raw, shift ), shift ) != raw ) bad++;
	}

	return bad;
}

// ================================================================================

typedef uint64_t ( *SweepFcn )( int perturb, uint64_t samples );

static void run( const char* name, SweepFcn sweep, uint64_t samples, int controlArms )
{
	uint64_t clean = sweep( 0, samples );
	if ( clean != 0 )
	{
		printf( "FAIL %s: %llu mismatches against the delta reference\n", name, (unsigned long long)clean );
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
		printf( "        samples=%llu structured+random, control arms=%d\n", (unsigned long long)samples, controlArms );
	}
}

// THE GENERALIZATION CLAIM, CHECKED AS BEHAVIOUR.
//
// This family is described as the general form of converters fixed already had at pinned
// formats. That is a claim about every input, so it is tested over every input rather than
// asserted in a comment: if these ever diverge the library has two rounding rules wearing
// one description, and the pinned converters and the general ones would quietly disagree
// on a wire that carries both.
static void checkGeneralizesThePinnedConverters( void )
{
	int before = fails;
	uint64_t local = 0;

	// fixFromFix30 IS fixNarrow at FIX30_SHIFT.
	for ( int64_t k = -100000; k <= 100000; k++ )
	{
		int32_t raw = (int32_t)( k * 20011 );
		fixed30_t packed = { raw };
		if ( fixFromFix30( packed ) != fixNarrow( raw, FIX30_SHIFT ) ) local++;
	}

	// fix30FromFix IS fixWiden at FIX30_SHIFT wherever the value is in range and the
	// packed form therefore does not saturate.
	for ( int64_t k = -100000; k <= 100000; k++ )
	{
		fixed_t a = k * 13;
		if ( a >= ( (fixed_t)2 << FIX_FRACTION_BITS ) || a < -( (fixed_t)2 << FIX_FRACTION_BITS ) ) continue;
		if ( (int64_t)fix30FromFix( a ).raw != fixWiden( a, FIX30_SHIFT ) ) local++;
	}

	// fixFromDouble IS fixQuantize at FIX_ONE.
	rngState = 0x57000000ULL;
	for ( int i = 0; i < 200000; i++ )
	{
		double x = (double)( (int64_t)( nextRandom() % 2000000001ULL ) - 1000000000 ) / 1000.0;
		if ( ( i & 1 ) == 0 )
		{
			int64_t k = (int64_t)( nextRandom() % 2000001ULL ) - 1000000;
			x = ( (double)k + 0.5 ) / (double)FIX_ONE;
		}
		if ( fixFromDouble( x ) != fixQuantize( x, FIX_ONE ) ) local++;
	}

	// fixToDouble IS fixDequantize at FIX_ONE. The library multiplies by the reciprocal
	// and this header divides, which agree exactly here and only here: FIX_ONE is a power
	// of two, so 1/FIX_ONE is exact and the two forms are the same operation.
	for ( int i = 0; i < 200000; i++ )
	{
		fixed_t raw = (fixed_t)nextRandom() >> 12;
		if ( fixToDouble( raw ) != fixDequantize( raw, FIX_ONE ) ) local++;
	}

	if ( local != 0 )
	{
		printf( "FAIL the general family does not agree with the pinned converters: %llu mismatches\n",
				(unsigned long long)local );
		fails++;
	}

	if ( fails == before )
	{
		printf( "fixNarrow, fixWiden, fixQuantize and fixDequantize generalize the pinned Q48.16 and Q2.30 converters\n" );
	}
}

int main( int argc, char** argv )
{
	for ( int i = 1; i < argc; i++ )
	{
		if ( strcmp( argv[i], "--counts" ) == 0 ) printCounts = 1;
	}

	clock_t started = clock();

	run( "fixQuantize matches delta quantize", sweepQuantize, SAMPLES( 300000 ), 1 );
	run( "fixDequantize matches delta dequantize", sweepDequantize, SAMPLES( 300000 ), 1 );
	run( "fixQuantizeClamped matches delta quantize_clamped", sweepQuantizeClamped, SAMPLES( 300000 ), 1 );
	run( "fixFits matches delta fits", sweepFits, SAMPLES( 300000 ), 1 );
	run( "fixNarrow matches delta narrow", sweepNarrow, SAMPLES( 500000 ), 1 );
	run( "fixWiden matches delta widen", sweepWiden, SAMPLES( 500000 ), 1 );

	checkGeneralizesThePinnedConverters();

	if ( EXPECTED_QUANTIZE_HASH == 0ULL )
	{
		printf( "quantize structured sweep hash = 0x%016llx (capture mode: not yet frozen)\n",
				(unsigned long long)fnv );
	}
	else if ( fnv != (uint64_t)EXPECTED_QUANTIZE_HASH )
	{
		printf( "FAIL quantize structured sweep hash = 0x%016llx, expected 0x%016llx\n", (unsigned long long)fnv,
				(unsigned long long)EXPECTED_QUANTIZE_HASH );
		fails++;
	}
	else
	{
		printf( "quantize structured sweep hash is unchanged across platforms\n" );
	}

	double elapsed = (double)( clock() - started ) / (double)CLOCKS_PER_SEC;
	printf( "quantize sweeps ran in %.2fs\n", elapsed );

	if ( fails )
	{
		printf( "FAILED: %d check(s)\n", fails );
		return 1;
	}

	return 0;
}
