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
static void mix( b3Fixed v )
{
	const uint8_t* b = (const uint8_t*)&v;
	for ( int i = 0; i < 8; i++ ) { fnv ^= b[i]; fnv *= 0x00000100000001B3ULL; }
}

static b3Fixed MUL( b3Fixed a, b3Fixed b )
{
	b3Fixed r = b3FixMul( a, b );
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

// Independent truncating 128-bit division reference: shift-subtract on
// magnitudes, no native / (which needs compiler-rt on Windows ClangCL) and no
// dependence on the library's own b3Int128Div.
static b3Int128 ref_div_128( b3Int128 a, b3Int128 b )
{
	b3UInt128 un = a < 0 ? -(b3UInt128)a : (b3UInt128)a;
	b3UInt128 ud = b < 0 ? -(b3UInt128)b : (b3UInt128)b;
	b3UInt128 q = 0;
	b3UInt128 r = 0;
	for ( int i = 127; i >= 0; i-- )
	{
		r = ( r << 1 ) | ( ( un >> i ) & 1 );
		if ( r >= ud )
		{
			r -= ud;
			q |= (b3UInt128)1 << i;
		}
	}
	return ( a < 0 ) != ( b < 0 ) ? -(b3Int128)q : (b3Int128)q;
}

int main( void )
{
	// ---- A. constants -----------------------------------------------------
	CHECK( B3_FIXED_ONE == 65536 );
	CHECK( B3_FIXED_HALF == 32768 );
	CHECK( B3_FIXED_EPSILON == 1 );
	CHECK( B3_FIXED_MAX == INT64_MAX );
	CHECK( B3_FIXED_MIN == -INT64_MAX );
	CHECK( B3_FIX( 1.0 ) == B3_FIXED_ONE );
	CHECK( B3_FIX( -0.5 ) == -B3_FIXED_HALF );

	// ---- B. hand-verified exact goldens ----------------------------------
	CHECK( MUL( B3_FIX( 1.5 ), B3_FIX( 2.5 ) ) == B3_FIX( 3.75 ) );
	CHECK( MUL( B3_FIX( -1.5 ), B3_FIX( 2.5 ) ) == B3_FIX( -3.75 ) );
	CHECK( MUL( B3_FIX( -1.5 ), B3_FIX( -2.5 ) ) == B3_FIX( 3.75 ) );
	CHECK( b3FixDiv( B3_FIX( 7.0 ), B3_FIX( 2.0 ) ) == B3_FIX( 3.5 ) );
	CHECK( b3FixDiv( B3_FIX( 1.0 ), B3_FIX( 3.0 ) ) == 21845 ); // trunc(65536/3 * 65536 / 65536)
	CHECK( b3FixDiv( B3_FIX( -1.0 ), B3_FIX( 3.0 ) ) == -21845 ); // truncation toward zero
	CHECK( b3FixSqrt( 0 ) == 0 );
	CHECK( b3FixSqrt( B3_FIX( 1.0 ) ) == B3_FIX( 1.0 ) );
	CHECK( b3FixSqrt( B3_FIX( 4.0 ) ) == B3_FIX( 2.0 ) );
	CHECK( b3FixSqrt( B3_FIX( 144.0 ) ) == B3_FIX( 12.0 ) );

	// ---- C. division: defined semantics at the edges ---------------------
	CHECK( b3FixDiv( B3_FIX( 1.0 ), 0 ) == B3_FIXED_MAX );  // div-by-zero is DEFINED
	CHECK( b3FixDiv( B3_FIX( -1.0 ), 0 ) == B3_FIXED_MIN );
	CHECK( b3FixDiv( 0, 0 ) == 0 );

	// fast path (|a| < 2^47) vs slow 128-bit path must agree with the same
	// truncating reference across the boundary
	{
		const int64_t edge = (int64_t)1 << 47;
		b3Fixed as[] = { edge - 2, edge - 1, edge, edge + 1, edge + 2,
						 -( edge - 1 ), -edge, -( edge + 1 ), 12345, -98765 };
		b3Fixed bs[] = { B3_FIX( 1.0 ), B3_FIX( 2.0 ), B3_FIX( -3.0 ), B3_FIX( 0.5 ), 3, -7, B3_FIX( 1234.5 ) };
		for ( unsigned i = 0; i < sizeof( as ) / sizeof( as[0] ); i++ )
		{
			for ( unsigned j = 0; j < sizeof( bs ) / sizeof( bs[0] ); j++ )
			{
				b3Fixed a = as[i], b = bs[j];
				b3Int128 ref128 = ref_div_128( ( (b3Int128)a << B3_FIXED_FRACTION_BITS ), b ); // trunc-toward-zero
				b3Fixed got = b3FixDiv( a, b );
				CHECK( got == (b3Fixed)ref128 );
				mix( got );
			}
		}
	}

	// ---- D. round / trunc / floor / ceil, incl. negatives and exact halves
	CHECK( b3FixFloor( B3_FIX( 1.5 ) ) == B3_FIX( 1.0 ) );
	CHECK( b3FixFloor( B3_FIX( -1.5 ) ) == B3_FIX( -2.0 ) ); // floor -> toward -inf
	CHECK( b3FixFloor( B3_FIX( 2.0 ) ) == B3_FIX( 2.0 ) );   // integral fixed point unchanged
	CHECK( b3FixCeil( B3_FIX( 1.5 ) ) == B3_FIX( 2.0 ) );
	CHECK( b3FixCeil( B3_FIX( -1.5 ) ) == B3_FIX( -1.0 ) );
	CHECK( b3FixCeil( B3_FIX( -2.0 ) ) == B3_FIX( -2.0 ) );
	CHECK( b3FixTruncToInt( B3_FIX( 1.9 ) ) == 1 );
	CHECK( b3FixTruncToInt( B3_FIX( -1.9 ) ) == -1 );  // trunc -> toward zero
	CHECK( b3FixFloorToInt( B3_FIX( -1.5 ) ) == -2 );  // floor -> toward -inf
	CHECK( b3FixFloorToInt( B3_FIX( 1.5 ) ) == 1 );
	CHECK( b3FixRoundToInt( B3_FIX( 1.5 ) ) == 2 );    // half rounds toward +inf
	CHECK( b3FixRoundToInt( B3_FIX( -1.5 ) ) == -1 );
	CHECK( b3FixRoundToInt( B3_FIX( 2.5 ) ) == 3 );
	CHECK( b3FixRoundToInt( B3_FIX( -2.5 ) ) == -2 );
	CHECK( b3FixRoundToInt( B3_FIX( 1.49 ) ) == 1 );
	CHECK( b3FixRoundToInt( B3_FIX( -1.49 ) ) == -1 );

	// ---- E. conversions ---------------------------------------------------
	{
		int64_t ks[] = { -2000000000LL, -100000, -1, 0, 1, 12345, 100000, 2000000000LL };
		for ( unsigned i = 0; i < sizeof( ks ) / sizeof( ks[0] ); i++ )
		{
			// int roundtrip stays within int range
			if ( ks[i] >= -2147483647LL && ks[i] <= 2147483647LL )
				CHECK( b3FixTruncToInt( b3FixFromInt( ks[i] ) ) == (int)ks[i] );
		}
		double ds[] = { 0.0, 1.0, -1.0, 0.5, -0.5, 3.14159, -2.71828, 1000.125, -99999.99 };
		for ( unsigned i = 0; i < sizeof( ds ) / sizeof( ds[0] ); i++ )
		{
			double back = b3FixToDouble( b3FixFromDouble( ds[i] ) );
			double err = back - ds[i];
			if ( err < 0 ) err = -err;
			CHECK( err <= 1.0 / 65536.0 );
		}
		CHECK( b3FixFromDouble( 1.0 ) == B3_FIXED_ONE );
		CHECK( b3FixFromDouble( -0.5 ) == -B3_FIXED_HALF );
	}

	// ---- F. property + reference sweep (LCG, deterministic) --------------
	{
		uint64_t s = 0x9E3779B97F4A7C15ULL;
		for ( int i = 0; i < 300000; i++ )
		{
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			b3Fixed a = (b3Fixed)( (int64_t)( s >> 16 ) % ( (int64_t)1 << 38 ) );
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			b3Fixed b = (b3Fixed)( (int64_t)( s >> 16 ) % ( (int64_t)1 << 38 ) );
			if ( b == 0 ) b = B3_FIXED_ONE;

			// multiply: commutative, and matches the round-to-nearest reference
			b3Fixed ab = MUL( a, b );
			CHECK( ab == MUL( b, a ) );
			{
				b3Int128 ref = ( (b3Int128)a * b + B3_FIXED_HALF ) >> B3_FIXED_FRACTION_BITS;
#ifndef FIXED_NEGATIVE_CONTROL
				CHECK( ab == (b3Fixed)ref );
#endif
			}
			// identities
			b3Fixed oneId = MUL( a, B3_FIXED_ONE );
#ifndef FIXED_NEGATIVE_CONTROL
			CHECK( oneId == a );
#endif
			mix( oneId );
			CHECK( b3FixDiv( a, B3_FIXED_ONE ) == a );
			// floor/ceil bracket
			b3Fixed fl = b3FixFloor( a ), ce = b3FixCeil( a );
			CHECK( fl <= a && a <= ce );
			CHECK( ce - fl == 0 || ce - fl == B3_FIXED_ONE );
			b3Fixed frac = a - fl;
			CHECK( frac >= 0 && frac < B3_FIXED_ONE );
			// abs/min/max/clamp laws
			CHECK( b3FixAbs( a ) >= 0 );
			CHECK( b3FixAbs( -a ) == b3FixAbs( a ) );
			CHECK( b3FixMin( a, b ) <= b3FixMax( a, b ) );
			CHECK( b3FixMin( a, b ) == b3FixMin( b, a ) );
			b3Fixed lo = b3FixMin( a, b ), hi = b3FixMax( a, b );
			b3Fixed cl = b3FixClamp( ab, lo, hi );
			CHECK( cl >= lo && cl <= hi );
			// sqrt on |a|: non-negative, square within rounding slack
			b3Fixed sa = b3FixAbs( a );
			b3Fixed r = b3FixSqrt( sa );
			CHECK( r >= 0 );
			{
				double rd = b3FixToDouble( r ), xd = b3FixToDouble( sa );
				double diff = rd * rd - xd;
				if ( diff < 0 ) diff = -diff;
				CHECK( diff <= ( 2.0 * rd + 2.0 ) / 65536.0 );
			}
			// shift is arithmetic-equivalent via the unsigned route
			CHECK( b3FixShiftLeft( a, 3 ) == (b3Fixed)( (uint64_t)a << 3 ) );

			mix( ab ); mix( b3FixDiv( a, b ) ); mix( r );
			mix( fl ); mix( ce ); mix( cl );
		}
	}

	// ---- G. edge-value hash (defined-deterministic on all our targets) ---
	{
		b3Fixed edges[] = { 0, 1, -1, B3_FIXED_HALF, -B3_FIXED_HALF, B3_FIXED_ONE, -B3_FIXED_ONE,
							B3_FIXED_MAX, B3_FIXED_MIN, B3_FIXED_MAX / 2, B3_FIXED_MIN / 2 };
		unsigned n = sizeof( edges ) / sizeof( edges[0] );
		for ( unsigned i = 0; i < n; i++ )
		{
			for ( unsigned j = 0; j < n; j++ )
			{
				mix( b3FixDiv( edges[i], edges[j] ) );
				mix( b3FixMin( edges[i], edges[j] ) );
				mix( b3FixMax( edges[i], edges[j] ) );
			}
			mix( b3FixAbs( edges[i] ) );
			mix( b3FixFloor( edges[i] ) );
			mix( b3FixCeil( edges[i] ) );
			mix( b3FixSqrt( edges[i] ) );
		}
	}

	// ---- H. vector / quaternion / transform properties (exact + ulp-bounded) ----
	// These ops are compositions of the hash-frozen scalar core plus exact integer
	// adds, so properties (not another frozen hash) are the right coverage.
	{
		uint64_t s = 0x243F6A8885A308D3ULL;
		for ( int i = 0; i < 20000; i++ )
		{
			b3Vec3 a, b, p;
			int64_t* comps[9] = { &a.x, &a.y, &a.z, &b.x, &b.y, &b.z, &p.x, &p.y, &p.z };
			for ( int c = 0; c < 9; c++ )
			{
				s = s * 6364136223846793005ULL + 1442695040888963407ULL;
				*comps[c] = (b3Fixed)( (int64_t)( s >> 20 ) % ( (int64_t)1 << 24 ) ) - ( (int64_t)1 << 23 );
			}
			// exact integer laws
			b3Vec3 sum = b3Add( a, b );
			b3Vec3 back = b3Sub( sum, b );
			CHECK( back.x == a.x && back.y == a.y && back.z == a.z );
			b3Vec3 nn = b3Neg( b3Neg( a ) );
			CHECK( nn.x == a.x && nn.y == a.y && nn.z == a.z );
			CHECK( b3Dot( a, b ) == b3Dot( b, a ) );
			b3Vec3 cab = b3Cross( a, b ), cba = b3Cross( b, a );
			CHECK( cab.x == -cba.x && cab.y == -cba.y && cab.z == -cba.z );
			b3Vec3 sv = b3MulSV( B3_FIXED_ONE, a );
			CHECK( sv.x == a.x && sv.y == a.y && sv.z == a.z );
			// normalize: unit length within a few ulps (skip near-zero vectors)
			if ( b3LengthSquared( a ) > B3_FIX( 0.01 ) )
			{
				b3Fixed len = b3Length( b3Normalize( a ) );
				b3Fixed d = len - B3_FIXED_ONE; if ( d < 0 ) d = -d;
				CHECK( d <= 8 );
			}
			// quaternion laws
			b3Quat q = b3MakeQuatFromAxisAngle( b3Vec3_axisY, b3FixShiftLeft( (b3Fixed)( i % 200 ) - 100, 9 ) );
			b3Quat qi = b3MulQuat( q, b3Quat_identity );
			CHECK( qi.v.x == q.v.x && qi.v.y == q.v.y && qi.v.z == q.v.z && qi.s == q.s );
			b3Vec3 rv = b3RotateVector( b3Quat_identity, p );
			CHECK( rv.x == p.x && rv.y == p.y && rv.z == p.z );
			b3Vec3 rt = b3InvRotateVector( q, b3RotateVector( q, p ) );
			{
				b3Fixed dx = rt.x - p.x, dy = rt.y - p.y, dz = rt.z - p.z;
				if ( dx < 0 ) dx = -dx; if ( dy < 0 ) dy = -dy; if ( dz < 0 ) dz = -dz;
				CHECK( dx <= 96 && dy <= 96 && dz <= 96 ); // rotation roundtrip within sub-milliunit
			}
			// transform roundtrip
			b3Transform t; t.p = a; t.q = q;
			b3Vec3 tp = b3InvTransformPoint( t, b3TransformPoint( t, p ) );
			{
				b3Fixed dx = tp.x - p.x, dy = tp.y - p.y, dz = tp.z - p.z;
				if ( dx < 0 ) dx = -dx; if ( dy < 0 ) dy = -dy; if ( dz < 0 ) dz = -dz;
				CHECK( dx <= 96 && dy <= 96 && dz <= 96 );
			}
		}
		// constants sanity
		CHECK( b3Vec3_zero.x == 0 && b3Vec3_one.y == B3_FIXED_ONE && b3Quat_identity.s == B3_FIXED_ONE );
		CHECK( B3_PI == B3_FIX( 3.14159265359f ) );
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
