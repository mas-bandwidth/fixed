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
#define EXPECTED_WIDE_HASH 0x478cc0823625290dULL

int main( void )
{
	// ---- wide: exact hand values ----
	CHECK( b3WideToFixed( b3WideFromFixed( 0 ) ) == 0 );
	CHECK( b3WideToFixed( b3WideFromFixed( B3_FIXED_ONE ) ) == B3_FIXED_ONE );
	CHECK( b3WideToFixed( b3WideFromFixed( INT64_MAX ) ) == INT64_MAX );
	CHECK( b3WideToFixed( b3WideFromFixed( INT64_MIN ) ) == INT64_MIN );

	// saturation: one past local range in each direction
	CHECK( b3WideToFixed( (b3FixedWide)INT64_MAX + 1 ) == INT64_MAX );
	CHECK( b3WideToFixed( (b3FixedWide)INT64_MIN - 1 ) == INT64_MIN );
	CHECK( b3WideToFixed( (b3FixedWide)1 << 100 ) == INT64_MAX );
	CHECK( b3WideToFixed( -( (b3FixedWide)1 << 100 ) ) == INT64_MIN );

	// the boundary subtract: nearby pair at a ludicrous base is exact
	{
		b3FixedWide base = (b3FixedWide)1 << 90; // far past Q48.16 range
		b3FixedWide a = b3WideOffset( base, B3_FIX( 3.25 ) );
		b3FixedWide b = b3WideOffset( base, B3_FIX( 1.0 ) );
		CHECK( b3WideSubToFixed( a, b ) == B3_FIX( 2.25 ) );
		CHECK( b3WideSubToFixed( b, a ) == -B3_FIX( 2.25 ) );
		// a pair separated by more than local range saturates, loudly defined
		CHECK( b3WideSubToFixed( base + ( (b3FixedWide)1 << 70 ), base ) == INT64_MAX );
	}

	// ---- wide: properties over an LCG sweep ----
	{
		uint64_t s = 0x9E3779B97F4A7C15ULL;
		for ( int i = 0; i < 100000; i++ )
		{
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			b3Fixed x = (b3Fixed)s >> 8;
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			b3Fixed d = (b3Fixed)s >> 8;
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			b3FixedWide base = (b3FixedWide)(int64_t)s << ( s % 60 );

			// widen/narrow roundtrip is exact for every int64
			CHECK( b3WideToFixed( b3WideFromFixed( x ) ) == x );
			// offset then difference recovers the delta exactly, at any base
			b3FixedWide w = b3WideOffset( base, x );
			CHECK( b3WideSubToFixed( b3WideOffset( w, d ), w ) == d );
			// add/sub inverse
			b3FixedWide y = b3WideFromFixed( d );
			CHECK( b3WideSub( b3WideAdd( w, y ), y ) == w );
		}
	}

	// ---- wide: vector wrappers ----
	{
		b3Vec3 v = { B3_FIX( 1.5 ), -B3_FIX( 2.0 ), B3_FIX( 100.0 ) };
		b3Vec3 d = { B3_FIX( 0.25 ), B3_FIX( 3.0 ), -B3_FIX( 7.5 ) };
		b3PosWide p = b3PosWideFromVec3( v );
		b3Vec3 back = b3PosWideToVec3( p );
		CHECK( back.x == v.x && back.y == v.y && back.z == v.z );
		b3Vec3 diff = b3PosWideSub( b3PosWideOffset( p, d ), p );
		CHECK( diff.x == d.x && diff.y == d.y && diff.z == d.z );
	}

	// ---- time: exact hand values ----
	CHECK( B3_TIME( 1.0 ) == B3_TIME_ONE );
	CHECK( B3_TIME( 0.5 ) == B3_TIME_HALF );
	CHECK( B3_TIME( -1.0 ) == -B3_TIME_ONE );
	CHECK( b3TimeFromFixed( B3_FIXED_ONE ) == B3_TIME_ONE );
	CHECK( b3TimeFromFixed( -B3_FIXED_ONE ) == -B3_TIME_ONE );
	CHECK( b3TimeToFixed( B3_TIME_ONE ) == B3_FIXED_ONE );
	CHECK( b3TimeFromSeconds( 2.5 ) == ( (b3Time)5 << 31 ) );
	CHECK( b3TimeToSeconds( B3_TIME_ONE ) == 1.0 );

	// 60 exact adds of a 1/60s tick: drift is only dt's one-time rounding,
	// under 60 * 2^-32 s — that is the whole argument for Q32.32 time.
	{
		b3Time tick = B3_TIME( 1.0 / 60.0 );
		b3Time acc = 0;
		for ( int i = 0; i < 60; i++ )
		{
			acc += tick;
		}
		b3Time drift = acc - B3_TIME_ONE;
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
		uint64_t s = 0xDEADBEEFCAFEF00DULL;
		for ( int i = 0; i < 100000; i++ )
		{
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			b3Time t = (b3Time)s >> 4;
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			b3Fixed f = (b3Fixed)s >> 24;
			b3Int128 ref = ( (b3Int128)t * f + B3_FIXED_HALF ) >> 16;
			CHECK( b3TimeMulFixed( t, f ) == (b3Time)ref );
		}
	}

	// ---- frozen hash over the saturation boundaries ----
	// Pins the int128 comparison/narrowing edge behavior bit-for-bit.
	{
		uint64_t fnv = 1469598103934665603ULL;
		b3FixedWide probes[] = {
			0,
			(b3FixedWide)INT64_MAX,
			(b3FixedWide)INT64_MAX + 1,
			(b3FixedWide)INT64_MIN,
			(b3FixedWide)INT64_MIN - 1,
			( (b3FixedWide)1 << 64 ),
			-( (b3FixedWide)1 << 64 ),
			( (b3FixedWide)1 << 126 ),
			-( (b3FixedWide)1 << 126 ),
		};
		int n = (int)( sizeof( probes ) / sizeof( probes[0] ) );
		for ( int i = 0; i < n; i++ )
		{
			for ( int j = 0; j < n; j++ )
			{
				uint64_t v = (uint64_t)b3WideSubToFixed( probes[i], probes[j] );
				fnv = ( fnv ^ v ) * 1099511628211ULL;
				v = (uint64_t)b3WideToFixed( probes[i] + probes[j] );
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
