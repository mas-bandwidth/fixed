// SPDX-License-Identifier: MIT
// Exhaustive test for the fixed-point core: hand-verified goldens, algebraic
// properties, edge/boundary behavior, and a comprehensive op-sweep hash that is
// asserted against a frozen constant so ANY semantic or platform divergence
// fails loudly (locally and in CI, cross-OS).
//
// Negative control: built a second time with -DFIXED_NEGATIVE_CONTROL, which
// injects a deliberate 1-ulp error into one multiply inside the sweep. That
// build MUST fail (ctest WILL_FAIL) — proving this suite detects breakage.
#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"
#include <stdio.h>
#include <stdint.h>

static int fails = 0;
#define CHECK( cond ) \
	do { if ( !( cond ) ) { printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); fails++; } } while ( 0 )

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix( fixed_t v )
{
	const uint8_t* b = (const uint8_t*)&v;
	for ( int i = 0; i < 8; i++ ) { fnv ^= b[i]; fnv *= 0x00000100000001B3ULL; }
}

static fixed_t MUL( fixed_t a, fixed_t b )
{
	fixed_t r = fixMul( a, b );
#ifdef FIXED_NEGATIVE_CONTROL
	r += 1; // deliberate 1-ulp error: the sweep hash must catch this
#endif
	return r;
}

// FROZEN 2026-07-22 after a captured run verified identical on arm64 and x86_64.
// Every OS/compiler in CI must reproduce this exactly — cross-platform bit-identity,
// enforced. If a legitimate semantic change alters it, re-capture on TWO arches and
// update deliberately; never loosen it to make a mismatch go away.
#ifndef EXPECTED_SWEEP_HASH
#define EXPECTED_SWEEP_HASH 0x5a4aac80ad1b6c5dULL
#endif

// THE 128-BIT REFERENCES IN THIS FILE ARE WRITTEN ON THE COMPILER'S OWN __int128, NOT ON
// THIS LIBRARY'S SEAM, and that is deliberate.
//
// fixInt128 is native __int128 on most compilers but an emulated pair of 64-bit lanes on
// plain MSVC (and on any build with FIX_FORCE_EMULATED_INT128). An oracle spelled in the
// seam's vocabulary would share the implementation's transformation: an emulated add with
// a broken carry would break the reference and the implementation in exactly the same
// way, and the comparison would keep passing. So where the compiler has a native 128-bit
// type these references use it, which makes them genuinely independent -- including, and
// especially, on the FIX_FORCE_EMULATED_INT128 build, where native is present and the
// library is not using it. Where the compiler has no native type there is no independent
// oracle to write, and the frozen sweep hash at the end of this file is what holds these
// results instead.
#if defined( __SIZEOF_INT128__ )
	#define HAS_NATIVE_INT128 1
__extension__ typedef __int128 refInt128;
__extension__ typedef unsigned __int128 refUInt128;

// Truncating 128-bit division: shift-subtract on the magnitudes, avoiding native `/`
// (which needs compiler-rt on Windows ClangCL) and avoiding the library's fixInt128Div.
static refInt128 ref_div_128( refInt128 a, refInt128 b )
{
	refUInt128 un = a < 0 ? -(refUInt128)a : (refUInt128)a;
	refUInt128 ud = b < 0 ? -(refUInt128)b : (refUInt128)b;
	refUInt128 q = 0;
	refUInt128 r = 0;
	for ( int i = 127; i >= 0; i-- )
	{
		r = ( r << 1 ) | ( ( un >> i ) & 1 );
		if ( r >= ud )
		{
			r -= ud;
			q |= (refUInt128)1 << i;
		}
	}
	return ( a < 0 ) != ( b < 0 ) ? -(refInt128)q : (refInt128)q;
}
#else
	#define HAS_NATIVE_INT128 0
#endif

int main( void )
{
	// ---- A. constants -----------------------------------------------------
	CHECK( FIX_ONE == 65536 );
	CHECK( FIX_HALF == 32768 );
	CHECK( FIX_EPSILON == 1 );
	CHECK( FIX_MAX == INT64_MAX );
	CHECK( FIX_MIN == -INT64_MAX );
	CHECK( FIX( 1.0 ) == FIX_ONE );
	CHECK( FIX( -0.5 ) == -FIX_HALF );

	// ---- B. hand-verified exact goldens ----------------------------------
	CHECK( MUL( FIX( 1.5 ), FIX( 2.5 ) ) == FIX( 3.75 ) );
	CHECK( MUL( FIX( -1.5 ), FIX( 2.5 ) ) == FIX( -3.75 ) );
	CHECK( MUL( FIX( -1.5 ), FIX( -2.5 ) ) == FIX( 3.75 ) );
	CHECK( fixDiv( FIX( 7.0 ), FIX( 2.0 ) ) == FIX( 3.5 ) );
	CHECK( fixDiv( FIX( 1.0 ), FIX( 3.0 ) ) == 21845 ); // trunc(65536/3 * 65536 / 65536)
	CHECK( fixDiv( FIX( -1.0 ), FIX( 3.0 ) ) == -21845 ); // truncation toward zero
	CHECK( fixSqrt( 0 ) == 0 );
	CHECK( fixSqrt( FIX( 1.0 ) ) == FIX( 1.0 ) );
	CHECK( fixSqrt( FIX( 4.0 ) ) == FIX( 2.0 ) );
	CHECK( fixSqrt( FIX( 144.0 ) ) == FIX( 12.0 ) );

	// ---- C. division: defined semantics at the edges ---------------------
	CHECK( fixDiv( FIX( 1.0 ), 0 ) == FIX_MAX );  // div-by-zero is DEFINED
	CHECK( fixDiv( FIX( -1.0 ), 0 ) == FIX_MIN );
	CHECK( fixDiv( 0, 0 ) == 0 );

	// fast path (|a| < 2^47) vs slow 128-bit path must agree with the same
	// truncating reference across the boundary
	{
		const int64_t edge = (int64_t)1 << 47;
		fixed_t as[] = { edge - 2, edge - 1, edge, edge + 1, edge + 2,
						 -( edge - 1 ), -edge, -( edge + 1 ), 12345, -98765 };
		fixed_t bs[] = { FIX( 1.0 ), FIX( 2.0 ), FIX( -3.0 ), FIX( 0.5 ), 3, -7, FIX( 1234.5 ) };
		for ( unsigned i = 0; i < sizeof( as ) / sizeof( as[0] ); i++ )
		{
			for ( unsigned j = 0; j < sizeof( bs ) / sizeof( bs[0] ); j++ )
			{
				fixed_t a = as[i], b = bs[j];
				fixed_t got = fixDiv( a, b );
#if HAS_NATIVE_INT128
				// unsigned shift: left-shifting a negative value is UB
				refInt128 num = (refInt128)( (refUInt128)(refInt128)a << FIX_FRACTION_BITS );
				refInt128 ref128 = ref_div_128( num, b ); // trunc-toward-zero
				CHECK( got == (fixed_t)ref128 );
#endif
				mix( got );
			}
		}
	}

	// ---- D. round / trunc / floor / ceil, incl. negatives and exact halves
	CHECK( fixFloor( FIX( 1.5 ) ) == FIX( 1.0 ) );
	CHECK( fixFloor( FIX( -1.5 ) ) == FIX( -2.0 ) ); // floor -> toward -inf
	CHECK( fixFloor( FIX( 2.0 ) ) == FIX( 2.0 ) );   // integral fixed point unchanged
	CHECK( fixCeil( FIX( 1.5 ) ) == FIX( 2.0 ) );
	CHECK( fixCeil( FIX( -1.5 ) ) == FIX( -1.0 ) );
	CHECK( fixCeil( FIX( -2.0 ) ) == FIX( -2.0 ) );
	CHECK( fixTruncToInt( FIX( 1.9 ) ) == 1 );
	CHECK( fixTruncToInt( FIX( -1.9 ) ) == -1 );  // trunc -> toward zero
	CHECK( fixFloorToInt( FIX( -1.5 ) ) == -2 );  // floor -> toward -inf
	CHECK( fixFloorToInt( FIX( 1.5 ) ) == 1 );
	CHECK( fixRoundToInt( FIX( 1.5 ) ) == 2 );    // half rounds toward +inf
	CHECK( fixRoundToInt( FIX( -1.5 ) ) == -1 );
	CHECK( fixRoundToInt( FIX( 2.5 ) ) == 3 );
	CHECK( fixRoundToInt( FIX( -2.5 ) ) == -2 );
	CHECK( fixRoundToInt( FIX( 1.49 ) ) == 1 );
	CHECK( fixRoundToInt( FIX( -1.49 ) ) == -1 );

	// ---- E. conversions ---------------------------------------------------
	{
		int64_t ks[] = { -2000000000LL, -100000, -1, 0, 1, 12345, 100000, 2000000000LL };
		for ( unsigned i = 0; i < sizeof( ks ) / sizeof( ks[0] ); i++ )
		{
			// int roundtrip stays within int range
			if ( ks[i] >= -2147483647LL && ks[i] <= 2147483647LL )
				CHECK( fixTruncToInt( fixFromInt( ks[i] ) ) == (int)ks[i] );
		}
		double ds[] = { 0.0, 1.0, -1.0, 0.5, -0.5, 3.14159, -2.71828, 1000.125, -99999.99 };
		for ( unsigned i = 0; i < sizeof( ds ) / sizeof( ds[0] ); i++ )
		{
			double back = fixToDouble( fixFromDouble( ds[i] ) );
			double err = back - ds[i];
			if ( err < 0 ) err = -err;
			CHECK( err <= 1.0 / 65536.0 );
		}
		CHECK( fixFromDouble( 1.0 ) == FIX_ONE );
		CHECK( fixFromDouble( -0.5 ) == -FIX_HALF );
	}

	// ---- F. property + reference sweep (LCG, deterministic) --------------
	{
		uint64_t s = 0x9E3779B97F4A7C15ULL;
		for ( int i = 0; i < 300000; i++ )
		{
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			fixed_t a = (fixed_t)( (int64_t)( s >> 16 ) % ( (int64_t)1 << 38 ) );
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			fixed_t b = (fixed_t)( (int64_t)( s >> 16 ) % ( (int64_t)1 << 38 ) );
			if ( b == 0 ) b = FIX_ONE;

			// multiply: commutative, and matches the round-to-nearest reference
			fixed_t ab = MUL( a, b );
			CHECK( ab == MUL( b, a ) );
			{
#if HAS_NATIVE_INT128 && !defined( FIXED_NEGATIVE_CONTROL )
				refInt128 ref = ( (refInt128)a * b + FIX_HALF ) >> FIX_FRACTION_BITS;
				CHECK( ab == (fixed_t)ref );
#endif
			}
			// identities
			fixed_t oneId = MUL( a, FIX_ONE );
#ifndef FIXED_NEGATIVE_CONTROL
			CHECK( oneId == a );
#endif
			mix( oneId );
			CHECK( fixDiv( a, FIX_ONE ) == a );
			// floor/ceil bracket
			fixed_t fl = fixFloor( a ), ce = fixCeil( a );
			CHECK( fl <= a && a <= ce );
			CHECK( ce - fl == 0 || ce - fl == FIX_ONE );
			fixed_t frac = a - fl;
			CHECK( frac >= 0 && frac < FIX_ONE );
			// abs/min/max/clamp laws
			CHECK( fixAbs( a ) >= 0 );
			CHECK( fixAbs( -a ) == fixAbs( a ) );
			CHECK( fixMin( a, b ) <= fixMax( a, b ) );
			CHECK( fixMin( a, b ) == fixMin( b, a ) );
			fixed_t lo = fixMin( a, b ), hi = fixMax( a, b );
			fixed_t cl = fixClamp( ab, lo, hi );
			CHECK( cl >= lo && cl <= hi );
			// sqrt on |a|: non-negative, square within rounding slack
			fixed_t sa = fixAbs( a );
			fixed_t r = fixSqrt( sa );
			CHECK( r >= 0 );
			{
				double rd = fixToDouble( r ), xd = fixToDouble( sa );
				double diff = rd * rd - xd;
				if ( diff < 0 ) diff = -diff;
				CHECK( diff <= ( 2.0 * rd + 2.0 ) / 65536.0 );
			}
			// shift is arithmetic-equivalent via the unsigned route
			CHECK( fixShiftLeft( a, 3 ) == (fixed_t)( (uint64_t)a << 3 ) );

			mix( ab ); mix( fixDiv( a, b ) ); mix( r );
			mix( fl ); mix( ce ); mix( cl );
		}
	}

	// ---- G. edge-value hash (defined-deterministic on all our targets) ---
	{
		fixed_t edges[] = { 0, 1, -1, FIX_HALF, -FIX_HALF, FIX_ONE, -FIX_ONE,
							FIX_MAX, FIX_MIN, FIX_MAX / 2, FIX_MIN / 2 };
		unsigned n = sizeof( edges ) / sizeof( edges[0] );
		for ( unsigned i = 0; i < n; i++ )
		{
			for ( unsigned j = 0; j < n; j++ )
			{
				mix( fixDiv( edges[i], edges[j] ) );
				mix( fixMin( edges[i], edges[j] ) );
				mix( fixMax( edges[i], edges[j] ) );
			}
			mix( fixAbs( edges[i] ) );
			mix( fixFloor( edges[i] ) );
			mix( fixCeil( edges[i] ) );
			mix( fixSqrt( edges[i] ) );
		}
	}

	// ---- H. vector / quaternion / transform properties (exact + ulp-bounded) ----
	// These ops are compositions of the hash-frozen scalar core plus exact integer
	// adds, so properties (not another frozen hash) are the right coverage.
	{
		uint64_t s = 0x243F6A8885A308D3ULL;
		for ( int i = 0; i < 20000; i++ )
		{
			fixVec3 a, b, p;
			int64_t* comps[9] = { &a.x, &a.y, &a.z, &b.x, &b.y, &b.z, &p.x, &p.y, &p.z };
			for ( int c = 0; c < 9; c++ )
			{
				s = s * 6364136223846793005ULL + 1442695040888963407ULL;
				*comps[c] = (fixed_t)( (int64_t)( s >> 20 ) % ( (int64_t)1 << 24 ) ) - ( (int64_t)1 << 23 );
			}
			// exact integer laws
			fixVec3 sum = fixVecAdd( a, b );
			fixVec3 back = fixVecSub( sum, b );
			CHECK( back.x == a.x && back.y == a.y && back.z == a.z );
			fixVec3 nn = fixVecNeg( fixVecNeg( a ) );
			CHECK( nn.x == a.x && nn.y == a.y && nn.z == a.z );
			CHECK( fixDot( a, b ) == fixDot( b, a ) );
			fixVec3 cab = fixCross( a, b ), cba = fixCross( b, a );
			CHECK( cab.x == -cba.x && cab.y == -cba.y && cab.z == -cba.z );
			fixVec3 sv = fixMulSV( FIX_ONE, a );
			CHECK( sv.x == a.x && sv.y == a.y && sv.z == a.z );
			// normalize: unit length within a few ulps (skip near-zero vectors)
			if ( fixLengthSquared( a ) > FIX( 0.01 ) )
			{
				fixed_t len = fixLength( fixNormalize( a ) );
				fixed_t d = len - FIX_ONE; if ( d < 0 ) d = -d;
				CHECK( d <= 8 );
			}
			// quaternion laws
			fixQuat q = fixMakeQuatFromAxisAngle( fixVec3_axisY, fixShiftLeft( (fixed_t)( i % 200 ) - 100, 9 ) );
			fixQuat qi = fixMulQuat( q, fixQuat_identity );
			CHECK( qi.v.x == q.v.x && qi.v.y == q.v.y && qi.v.z == q.v.z && qi.s == q.s );
			fixVec3 rv = fixRotateVector( fixQuat_identity, p );
			CHECK( rv.x == p.x && rv.y == p.y && rv.z == p.z );
			fixVec3 rt = fixInvRotateVector( q, fixRotateVector( q, p ) );
			{
				fixed_t dx = rt.x - p.x, dy = rt.y - p.y, dz = rt.z - p.z;
				if ( dx < 0 ) dx = -dx; if ( dy < 0 ) dy = -dy; if ( dz < 0 ) dz = -dz;
				CHECK( dx <= 96 && dy <= 96 && dz <= 96 ); // rotation roundtrip within sub-milliunit
			}
			// transform roundtrip
			fixTransform t; t.p = a; t.q = q;
			fixVec3 tp = fixInvTransformPoint( t, fixTransformPoint( t, p ) );
			{
				fixed_t dx = tp.x - p.x, dy = tp.y - p.y, dz = tp.z - p.z;
				if ( dx < 0 ) dx = -dx; if ( dy < 0 ) dy = -dy; if ( dz < 0 ) dz = -dz;
				CHECK( dx <= 96 && dy <= 96 && dz <= 96 );
			}
		}
		// constants sanity
		CHECK( fixVec3_zero.x == 0 && fixVec3_one.y == FIX_ONE && fixQuat_identity.s == FIX_ONE );
		CHECK( FIX_PI == FIX( 3.14159265359f ) );
	}

	printf( "exhaustive sweep hash = 0x%016llx\n", (unsigned long long)fnv );
#if EXPECTED_SWEEP_HASH != 0
	CHECK( fnv == (uint64_t)EXPECTED_SWEEP_HASH );
#else
	printf( "(capture mode: hash not yet frozen)\n" );
#endif

	if ( fails ) { printf( "FAILED: %d check(s)\n", fails ); return 1; }
	printf( "ALL OK\n" );
	return 0;
}
