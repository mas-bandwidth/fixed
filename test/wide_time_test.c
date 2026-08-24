// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// Tests for the wide (Q112.16) primitives and Q32.32 time. The ops are pure
// integer, so the coverage is exact hand values, algebraic properties over an
// LCG sweep, and a frozen FNV hash over the saturation boundaries — the hash
// pins the int128 narrowing behavior bit-for-bit across compilers and OSes.

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include "fixed/fixed_time.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

static int fails = 0;
#define CHECK( cond )                                                                                                            \
	do                                                                                                                           \
	{                                                                                                                            \
		if ( !( cond ) )                                                                                                         \
		{                                                                                                                        \
			printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond );                                                             \
			fails++;                                                                                                             \
		}                                                                                                                        \
	} while ( 0 )

// Frozen: captured on arm64 clang, must match on every platform forever.
#define EXPECTED_WIDE_HASH 0xeedc16ea642ffb5fULL

// 128-bit values, comparisons and shifts spelled through the seam rather than with bare
// operators, so this suite compiles on the emulated arm as well as the native one. That
// is not a formality. The test suite is a CONSUMER, and a consumer on plain MSVC cannot
// write `(fixedWide_t)1 << 100` any more than this file can -- so anything awkward to say
// here is awkward for every consumer, which makes this the file where the wide
// vocabulary's ergonomics get checked rather than assumed.
static fixedWide_t W( int64_t v )
{
	return fixWideFromFixed( v );
}

static fixedWide_t WSHL( int64_t v, int shift )
{
	return fixInt128ShiftLeft( fixWideFromFixed( v ), shift );
}

#define WEQ( a, b ) fixWideEq( ( a ), ( b ) )

// The 128-bit REFERENCE below is written on the compiler's own __int128, not on this
// library's seam. Spelling an oracle in the same vocabulary as the implementation makes
// it share the implementation's transformation, and a shared transformation cannot detect
// a wrong one -- an emulated multiply with a broken sign extension would break both sides
// identically and the comparison would still pass. Where the compiler has no native
// 128-bit type there is no independent oracle to write, so the results feed a frozen hash
// instead, and that hash is what holds plain MSVC.
#if defined( __SIZEOF_INT128__ )
	#define HAS_NATIVE_INT128 1
__extension__ typedef __int128 refInt128;
#else
	#define HAS_NATIVE_INT128 0
#endif

// Frozen: the Q32.32-by-Q48.16 scaling sweep, so the compiler with no native reference
// still has one number to be held to.
#define EXPECTED_TIME_SCALE_HASH 0xd8bad996bb5722cfULL

int main( void )
{
	// ---- wide: exact hand values ----
	CHECK( fixWideToFixed( fixWideFromFixed( 0 ) ) == 0 );
	CHECK( fixWideToFixed( fixWideFromFixed( FIX_ONE ) ) == FIX_ONE );
	CHECK( fixWideToFixed( fixWideFromFixed( INT64_MAX ) ) == INT64_MAX );
	CHECK( fixWideToFixed( fixWideFromFixed( INT64_MIN ) ) == INT64_MIN );

	// saturation: one past local range in each direction
	CHECK( fixWideToFixed( fixWideAdd( W( INT64_MAX ), W( 1 ) ) ) == INT64_MAX );
	CHECK( fixWideToFixed( fixWideSub( W( INT64_MIN ), W( 1 ) ) ) == INT64_MIN );
	CHECK( fixWideToFixed( WSHL( 1, 100 ) ) == INT64_MAX );
	CHECK( fixWideToFixed( fixWideNeg( WSHL( 1, 100 ) ) ) == INT64_MIN );

	// the boundary subtract: nearby pair at a ludicrous base is exact
	{
		fixedWide_t base = WSHL( 1, 90 ); // far past Q48.16 range
		fixedWide_t a = fixWideOffset( base, FIX( 3.25 ) );
		fixedWide_t b = fixWideOffset( base, FIX( 1.0 ) );
		CHECK( fixWideSubToFixed( a, b ) == FIX( 2.25 ) );
		CHECK( fixWideSubToFixed( b, a ) == -FIX( 2.25 ) );
		// a pair separated by more than local range saturates, loudly defined
		CHECK( fixWideSubToFixed( fixWideAdd( base, WSHL( 1, 70 ) ), base ) == INT64_MAX );
	}

	// ---- wide: min/max ----
	// These exist to be CALLED. Their first version compiled fine and shipped green
	// while being unreachable — they sat inside the `#if !FIX_HAS_INT128` block, which
	// is the block that #errors, so on every real platform they did not exist. Nothing
	// referenced them, so CI had nothing to say. A header-only helper with no caller is
	// not covered by a passing build; it is invisible to it.
	{
		CHECK( WEQ( fixWideMin( W( 3 ), W( 7 ) ), W( 3 ) ) );
		CHECK( WEQ( fixWideMax( W( 3 ), W( 7 ) ), W( 7 ) ) );
		CHECK( WEQ( fixWideMin( W( -7 ), W( -3 ) ), W( -7 ) ) );
		CHECK( WEQ( fixWideMax( W( -7 ), W( -3 ) ), W( -3 ) ) );
		CHECK( WEQ( fixWideMin( W( 5 ), W( 5 ) ), W( 5 ) ) );
		CHECK( WEQ( fixWideMax( W( 5 ), W( 5 ) ), W( 5 ) ) );

		// the point of the wide type: ordering past Q48.16 range, where a narrow
		// min/max would have saturated both operands to INT64_MAX and tied
		fixedWide_t lo = WSHL( 1, 90 );
		fixedWide_t hi = WSHL( 1, 100 );
		CHECK( WEQ( fixWideMin( lo, hi ), lo ) );
		CHECK( WEQ( fixWideMax( lo, hi ), hi ) );
		CHECK( WEQ( fixWideMin( fixWideNeg( hi ), fixWideNeg( lo ) ), fixWideNeg( hi ) ) );
		CHECK( WEQ( fixWideMax( fixWideNeg( hi ), fixWideNeg( lo ) ), fixWideNeg( lo ) ) );
	}

	// ---- wide: properties over an LCG sweep ----
	{
		uint64_t s = 0x9E3779B97F4A7C15ULL;
		for ( int i = 0; i < 100000; i++ )
		{
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			fixed_t x = (fixed_t)s >> 8;
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			fixed_t d = (fixed_t)s >> 8;
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			// fixInt128ShiftLeft routes through the unsigned lanes: left-shifting a
			// negative value is undefined behavior, and s is signed here on purpose.
			fixedWide_t base = fixInt128ShiftLeft( W( (int64_t)s ), (int)( s % 60 ) );

			// widen/narrow roundtrip is exact for every int64
			CHECK( fixWideToFixed( fixWideFromFixed( x ) ) == x );
			// offset then difference recovers the delta exactly, at any base
			fixedWide_t w = fixWideOffset( base, x );
			CHECK( fixWideSubToFixed( fixWideOffset( w, d ), w ) == d );
			// add/sub inverse
			fixedWide_t y = fixWideFromFixed( d );
			CHECK( WEQ( fixWideSub( fixWideAdd( w, y ), y ), w ) );
		}
	}

	// ---- wide: vector wrappers ----
	{
		fixVec3 v = { FIX( 1.5 ), -FIX( 2.0 ), FIX( 100.0 ) };
		fixVec3 d = { FIX( 0.25 ), FIX( 3.0 ), -FIX( 7.5 ) };
		fixPosWide p = fixPosWideFromVec3( v );
		fixVec3 back = fixPosWideToVec3( p );
		CHECK( back.x == v.x && back.y == v.y && back.z == v.z );
		fixVec3 diff = fixPosWideSub( fixPosWideOffset( p, d ), p );
		CHECK( diff.x == d.x && diff.y == d.y && diff.z == d.z );
	}

	// ---- time: exact hand values ----
	CHECK( FIX_TIME( 1.0 ) == FIX_TIME_ONE );
	CHECK( FIX_TIME( 0.5 ) == FIX_TIME_HALF );
	CHECK( FIX_TIME( -1.0 ) == -FIX_TIME_ONE );
	CHECK( fixTimeFromFixed( FIX_ONE ) == FIX_TIME_ONE );
	CHECK( fixTimeFromFixed( -FIX_ONE ) == -FIX_TIME_ONE );
	CHECK( fixTimeToFixed( FIX_TIME_ONE ) == FIX_ONE );
	CHECK( fixTimeFromSeconds( 2.5 ) == ( (fixTime)5 << 31 ) );
	CHECK( fixTimeToSeconds( FIX_TIME_ONE ) == 1.0 );

	// 60 exact adds of a 1/60s tick: drift is only dt's one-time rounding,
	// under 60 * 2^-32 s — that is the whole argument for Q32.32 time.
	{
		fixTime tick = FIX_TIME( 1.0 / 60.0 );
		fixTime acc = 0;
		for ( int i = 0; i < 60; i++ )
		{
			acc += tick;
		}
		fixTime drift = acc - FIX_TIME_ONE;
		if ( drift < 0 )
		{
			drift = -drift;
		}
		CHECK( drift <= 60 );
		// and the same accumulation at Q48.16 would drift ~2^16 times worse:
		// the reason time gets its own format.
	}

	// time scaling against a 128-bit reference
	{
		uint64_t scaleHash = 1469598103934665603ULL;
		uint64_t s = 0xDEADBEEFCAFEF00DULL;
		for ( int i = 0; i < 100000; i++ )
		{
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			fixTime t = (fixTime)s >> 4;
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			fixed_t f = (fixed_t)s >> 24;
			fixTime got = fixTimeMulFixed( t, f );
#if HAS_NATIVE_INT128
			refInt128 ref = ( (refInt128)t * f + FIX_HALF ) >> 16;
			CHECK( got == (fixTime)ref );
#endif
			scaleHash = ( scaleHash ^ (uint64_t)got ) * 1099511628211ULL;
		}
		printf( "time scale hash = 0x%016" PRIx64 "\n", scaleHash );
		if ( EXPECTED_TIME_SCALE_HASH != 0 )
		{
			CHECK( scaleHash == EXPECTED_TIME_SCALE_HASH );
		}
		else
		{
			printf( "  (capture mode: freeze this hash into EXPECTED_TIME_SCALE_HASH)\n" );
		}
	}

	// ---- frozen hash over the saturation boundaries ----
	// Pins the int128 comparison/narrowing edge behavior bit-for-bit.
	{
		uint64_t fnv = 1469598103934665603ULL;
		fixedWide_t probes[] = {
			W( 0 ),
			W( INT64_MAX ),
			fixWideAdd( W( INT64_MAX ), W( 1 ) ),
			W( INT64_MIN ),
			fixWideSub( W( INT64_MIN ), W( 1 ) ),
			WSHL( 1, 64 ),
			fixWideNeg( WSHL( 1, 64 ) ),
			// Kept at 1<<125 so every pairwise sum and difference in the sweep
			// stays inside int128 -- 1<<126 probes made a+b overflow, which is
			// UB that gcc and clang compile differently (the ubuntu CI hash
			// mismatch that caught it). Far beyond the Q112.16 world contract
			// (~2^111) either way.
			WSHL( 1, 125 ),
			fixWideNeg( WSHL( 1, 125 ) ),
		};
		int n = (int)( sizeof( probes ) / sizeof( probes[0] ) );
		for ( int i = 0; i < n; i++ )
		{
			for ( int j = 0; j < n; j++ )
			{
				uint64_t v = (uint64_t)fixWideSubToFixed( probes[i], probes[j] );
				fnv = ( fnv ^ v ) * 1099511628211ULL;
				v = (uint64_t)fixWideToFixed( fixWideAdd( probes[i], probes[j] ) );
				fnv = ( fnv ^ v ) * 1099511628211ULL;
			}
		}
		printf( "wide boundary hash = 0x%016" PRIx64 "\n", fnv );
		if ( EXPECTED_WIDE_HASH != 0 )
		{
			CHECK( fnv == EXPECTED_WIDE_HASH );
		}
		else
		{
			printf( "  (capture mode: freeze this hash into EXPECTED_WIDE_HASH)\n" );
		}
	}

	if ( fails == 0 )
	{
		printf( "wide+time tests passed\n" );
	}
	return fails == 0 ? 0 : 1;
}
