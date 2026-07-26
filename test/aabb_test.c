// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Tests for the AABB family (both widths), fixPlane, and the position/transform
// validators extracted from box3d.
//
// Every symbol added by the extraction is CALLED here on purpose. A header-only
// helper that nothing references is not covered by a passing build -- it is invisible
// to it, and it can be flatly unreachable while CI stays green. That already happened
// once in this repo (fixWideMin/fixWideMax shipped inside the `#if !FIX_HAS_INT128` block),
// so the rule for this file is: if the extraction added it, this file calls it.
//
// The narrow and wide implementations are NOT the same algorithm -- 128-bit bounds need
// per-component min/max and explicit narrowing where the narrow build gets those free.
// So the correspondence between them is checked directly: for any box that fits local
// range, wide and narrow must agree exactly.

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include <stdio.h>
#include <stdint.h>

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

static fixVec3 V( float x, float y, float z )
{
	fixVec3 v = { FIX( x ), FIX( y ), FIX( z ) };
	return v;
}

static fixAABB Box( float lx, float ly, float lz, float ux, float uy, float uz )
{
	fixAABB a = { V( lx, ly, lz ), V( ux, uy, uz ) };
	return a;
}

static bool VecEq( fixVec3 a, fixVec3 b )
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

// widen a narrow box, optionally re-based at a wide origin
static fixAABBWide Widen( fixAABB a, fixedWide_t base )
{
	fixAABBWide w;
	w.lowerBound = fixPosWideFromVec3( a.lowerBound );
	w.upperBound = fixPosWideFromVec3( a.upperBound );
	w.lowerBound.x += base; w.lowerBound.y += base; w.lowerBound.z += base;
	w.upperBound.x += base; w.upperBound.y += base; w.upperBound.z += base;
	return w;
}

int main( void )
{
	// ---- fixPlane ----
	{
		fixPlane good = { V( 1.0f, 0.0f, 0.0f ), FIX( 2.5f ) };
		CHECK( fixIsValidPlane( good ) );

		// a non-unit normal is not a valid plane, even though every component is finite
		fixPlane fat = { V( 3.0f, 0.0f, 0.0f ), FIX( 2.5f ) };
		CHECK( fixIsValidPlane( fat ) == false );

		fixPlane zero = { V( 0.0f, 0.0f, 0.0f ), FIX( 0.0f ) };
		CHECK( fixIsValidPlane( zero ) == false );
	}

	// ---- narrow AABB: hand values ----
	{
		fixAABB a = Box( -1.0f, -2.0f, -3.0f, 1.0f, 2.0f, 3.0f );

		CHECK( fixIsValidAABB( a ) );
		CHECK( VecEq( fixAABB_Center( a ), V( 0.0f, 0.0f, 0.0f ) ) );
		CHECK( VecEq( fixAABB_Extents( a ), V( 1.0f, 2.0f, 3.0f ) ) );

		// surface area of a 2x4x6 box = 2*(2*4 + 4*6 + 6*2) = 88
		CHECK( fixAABB_Area( a ) == FIX( 88.0f ) );

		fixAABB inner = Box( -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f );
		CHECK( fixAABB_Contains( a, inner ) );
		CHECK( fixAABB_Contains( inner, a ) == false );
		CHECK( fixAABB_Overlaps( a, inner ) );

		fixAABB far_ = Box( 100.0f, 100.0f, 100.0f, 101.0f, 101.0f, 101.0f );
		CHECK( fixAABB_Overlaps( a, far_ ) == false );
		CHECK( fixAABB_Contains( a, far_ ) == false );

		// touching boxes count as overlapping (>= boundary)
		fixAABB touch = Box( 1.0f, -2.0f, -3.0f, 2.0f, 2.0f, 3.0f );
		CHECK( fixAABB_Overlaps( a, touch ) );

		fixAABB u = fixAABB_Union( a, far_ );
		CHECK( VecEq( u.lowerBound, V( -1.0f, -2.0f, -3.0f ) ) );
		CHECK( VecEq( u.upperBound, V( 101.0f, 101.0f, 101.0f ) ) );
		CHECK( fixAABB_Contains( u, a ) && fixAABB_Contains( u, far_ ) );

		fixAABB inf = fixAABB_Inflate( a, FIX( 0.5f ) );
		CHECK( VecEq( inf.lowerBound, V( -1.5f, -2.5f, -3.5f ) ) );
		CHECK( VecEq( inf.upperBound, V( 1.5f, 2.5f, 3.5f ) ) );
		CHECK( fixAABB_Contains( inf, a ) );

		// closest point: inside is itself, outside clamps to the face
		CHECK( VecEq( fixClosestPointToAABB( V( 0.5f, 0.5f, 0.5f ), a ), V( 0.5f, 0.5f, 0.5f ) ) );
		CHECK( VecEq( fixClosestPointToAABB( V( 5.0f, 0.0f, -9.0f ), a ), V( 1.0f, 0.0f, -3.0f ) ) );
	}

	// ---- narrow AABB: fixMakeAABB over a point cloud ----
	{
		fixVec3 pts[4] = { V( 0.0f, 0.0f, 0.0f ), V( 2.0f, -1.0f, 3.0f ), V( -4.0f, 5.0f, 1.0f ), V( 1.0f, 1.0f, -2.0f ) };
		fixAABB a = fixMakeAABB( pts, 4, FIX( 0.0f ) );
		CHECK( VecEq( a.lowerBound, V( -4.0f, -1.0f, -2.0f ) ) );
		CHECK( VecEq( a.upperBound, V( 2.0f, 5.0f, 3.0f ) ) );
		for ( int i = 0; i < 4; ++i )
		{
			CHECK( VecEq( fixClosestPointToAABB( pts[i], a ), pts[i] ) ); // every point is inside
		}

		// radius expands uniformly
		fixAABB r = fixMakeAABB( pts, 4, FIX( 1.0f ) );
		CHECK( VecEq( r.lowerBound, V( -5.0f, -2.0f, -3.0f ) ) );
		CHECK( VecEq( r.upperBound, V( 3.0f, 6.0f, 4.0f ) ) );

		// single point degenerates to a zero-volume but VALID box
		fixAABB one = fixMakeAABB( pts, 1, FIX( 0.0f ) );
		CHECK( fixIsValidAABB( one ) );
		CHECK( fixAABB_Area( one ) == FIX( 0.0f ) );
	}

	// ---- narrow AABB: validity ----
	{
		fixAABB inverted = Box( 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f ); // lower > upper on x
		CHECK( fixIsValidAABB( inverted ) == false );

		fixAABB degenerate = Box( 1.0f, 2.0f, 3.0f, 1.0f, 2.0f, 3.0f ); // a point
		CHECK( fixIsValidAABB( degenerate ) );

		fixAABB bad = { { INT64_MIN, 0, 0 }, { 0, 0, 0 } }; // reserved sentinel coordinate
		CHECK( fixIsValidAABB( bad ) == false );
	}

	// ---- fixAABB_Transform ----
	{
		fixAABB a = Box( -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f );

		// identity transform leaves a symmetric box alone
		fixTransform id = { V( 0.0f, 0.0f, 0.0f ), fixMakeQuatFromAxisAngle( V( 0.0f, 1.0f, 0.0f ), FIX( 0.0f ) ) };
		fixAABB t0 = fixAABB_Transform( id, a );
		CHECK( VecEq( t0.lowerBound, a.lowerBound ) && VecEq( t0.upperBound, a.upperBound ) );

		// pure translation moves the box by exactly the translation
		fixTransform tr = { V( 10.0f, -5.0f, 2.0f ), fixMakeQuatFromAxisAngle( V( 0.0f, 1.0f, 0.0f ), FIX( 0.0f ) ) };
		fixAABB t1 = fixAABB_Transform( tr, a );
		CHECK( VecEq( t1.lowerBound, V( 9.0f, -6.0f, 1.0f ) ) );
		CHECK( VecEq( t1.upperBound, V( 11.0f, -4.0f, 3.0f ) ) );

		// a rotated box is never smaller than the original (the conservative-bound property)
		fixTransform rot = { V( 0.0f, 0.0f, 0.0f ), fixMakeQuatFromAxisAngle( V( 0.0f, 0.0f, 1.0f ), FIX( 0.7f ) ) };
		fixAABB t2 = fixAABB_Transform( rot, a );
		CHECK( fixIsValidAABB( t2 ) );
		CHECK( fixAABB_Area( t2 ) >= fixAABB_Area( a ) );
	}

	// ---- wide: position and transform validators ----
	{
		fixPosWide p = { (fixedWide_t)1 << 90, 0, -( (fixedWide_t)1 << 95 ) };
		CHECK( fixIsValidPosWide( p ) );
		CHECK( fixIsValidWideCoord( (fixedWide_t)0 ) );

		// the reserved 128-bit minimum: negating it would overflow, so it is not a value
		fixedWide_t reserved = (fixedWide_t)( (fixUInt128)1 << 127 );
		CHECK( fixIsValidWideCoord( reserved ) == false );
		fixPosWide bad = { reserved, 0, 0 };
		CHECK( fixIsValidPosWide( bad ) == false );

		fixWorldTransformWide t = { p, fixMakeQuatFromAxisAngle( V( 0.0f, 1.0f, 0.0f ), FIX( 0.25f ) ) };
		CHECK( fixIsValidWorldTransformWide( t ) );

		fixWorldTransformWide tbad = { bad, t.q };
		CHECK( fixIsValidWorldTransformWide( tbad ) == false );
	}

	// ---- wide AABB: agrees with narrow for any box in local range ----
	// This is the property that matters. The two implementations are different code;
	// if they ever disagree on a box both can represent, one of them is wrong.
	{
		const fixAABB cases[5] = {
			{ { FIX( -1.0f ), FIX( -2.0f ), FIX( -3.0f ) }, { FIX( 1.0f ), FIX( 2.0f ), FIX( 3.0f ) } },
			{ { FIX( 0.0f ), FIX( 0.0f ), FIX( 0.0f ) }, { FIX( 1.0f ), FIX( 1.0f ), FIX( 1.0f ) } },
			{ { FIX( 5.0f ), FIX( 5.0f ), FIX( 5.0f ) }, { FIX( 5.0f ), FIX( 5.0f ), FIX( 5.0f ) } },
			{ { FIX( -7.5f ), FIX( 0.25f ), FIX( -0.125f ) }, { FIX( -1.5f ), FIX( 9.75f ), FIX( 0.5f ) } },
			{ { FIX( -100.0f ), FIX( -100.0f ), FIX( -100.0f ) }, { FIX( 100.0f ), FIX( 100.0f ), FIX( 100.0f ) } },
		};

		for ( int i = 0; i < 5; ++i )
		{
			fixAABB n = cases[i];
			fixAABBWide w = Widen( n, 0 );

			CHECK( fixIsValidAABBWide( w ) == fixIsValidAABB( n ) );
			CHECK( VecEq( fixAABBWide_Center( w ), fixAABB_Center( n ) ) );
			CHECK( VecEq( fixAABBWide_Extents( w ), fixAABB_Extents( n ) ) );
			CHECK( fixAABBWide_Area( w ) == fixAABB_Area( n ) );

			for ( int j = 0; j < 5; ++j )
			{
				fixAABBWide wj = Widen( cases[j], 0 );
				CHECK( fixAABBWide_Contains( w, wj ) == fixAABB_Contains( n, cases[j] ) );
				CHECK( fixAABBWide_Overlaps( w, wj ) == fixAABB_Overlaps( n, cases[j] ) );

				fixAABBWide uw = fixAABBWide_Union( w, wj );
				fixAABB un = fixAABB_Union( n, cases[j] );
				CHECK( VecEq( fixPosWideToVec3( uw.lowerBound ), un.lowerBound ) );
				CHECK( VecEq( fixPosWideToVec3( uw.upperBound ), un.upperBound ) );
			}

			fixAABBWide iw = fixAABBWide_Inflate( w, FIX( 0.5f ) );
			fixAABB in_ = fixAABB_Inflate( n, FIX( 0.5f ) );
			CHECK( VecEq( fixPosWideToVec3( iw.lowerBound ), in_.lowerBound ) );
			CHECK( VecEq( fixPosWideToVec3( iw.upperBound ), in_.upperBound ) );

			fixVec3 probe = V( 0.75f, -0.25f, 2.0f );
			CHECK( VecEq( fixClosestPointToAABBWide( probe, w ), fixClosestPointToAABB( probe, n ) ) );
		}
	}

	// ---- wide AABB: the part narrow cannot do ----
	// Ordering, union, containment and overlap past Q48.16 range, where every narrow
	// coordinate would have saturated to INT64_MAX and compared equal.
	{
		fixedWide_t farBase = (fixedWide_t)1 << 90;   // far past local range
		fixedWide_t farther = (fixedWide_t)1 << 100;

		fixAABB unit = Box( 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f );
		fixAABBWide at90 = Widen( unit, farBase );
		fixAABBWide at100 = Widen( unit, farther );

		CHECK( fixIsValidAABBWide( at90 ) && fixIsValidAABBWide( at100 ) );

		// two unit boxes a light-year apart do not overlap and neither contains the other
		CHECK( fixAABBWide_Overlaps( at90, at100 ) == false );
		CHECK( fixAABBWide_Contains( at90, at100 ) == false );
		CHECK( fixAABBWide_Contains( at100, at90 ) == false );

		// their union spans the gap and contains both
		fixAABBWide u = fixAABBWide_Union( at90, at100 );
		CHECK( fixAABBWide_Contains( u, at90 ) );
		CHECK( fixAABBWide_Contains( u, at100 ) );
		CHECK( u.lowerBound.x == farBase );
		CHECK( u.upperBound.x == farther + FIX( 1.0f ) );

		// a box is its own container and overlaps itself, at any distance
		CHECK( fixAABBWide_Contains( at100, at100 ) );
		CHECK( fixAABBWide_Overlaps( at100, at100 ) );

		// extents are local-sized even when the box is astronomically far out
		CHECK( VecEq( fixAABBWide_Extents( at100 ), fixAABB_Extents( unit ) ) );

		// placing a local box at a wide origin is exact, and round-trips
		fixPosWide origin = { farther, farther, farther };
		fixAABBWide placed = fixOffsetAABBWide( unit, origin );
		CHECK( placed.lowerBound.x == farther );
		CHECK( placed.upperBound.x == farther + FIX( 1.0f ) );
		CHECK( fixAABBWide_Contains( placed, placed ) );
		CHECK( VecEq( fixAABBWide_Extents( placed ), fixAABB_Extents( unit ) ) );
	}

	// ---- wide AABB: point cloud ----
	{
		fixedWide_t base = (fixedWide_t)1 << 80;
		fixPosWide pts[3];
		pts[0] = (fixPosWide){ base, base, base };
		pts[1] = (fixPosWide){ base + FIX( 4.0f ), base - FIX( 2.0f ), base + FIX( 1.0f ) };
		pts[2] = (fixPosWide){ base - FIX( 1.0f ), base + FIX( 8.0f ), base };

		fixAABBWide a = fixMakeAABBWide( pts, 3, FIX( 0.0f ) );
		CHECK( a.lowerBound.x == base - FIX( 1.0f ) );
		CHECK( a.lowerBound.y == base - FIX( 2.0f ) );
		CHECK( a.upperBound.x == base + FIX( 4.0f ) );
		CHECK( a.upperBound.y == base + FIX( 8.0f ) );
		CHECK( fixIsValidAABBWide( a ) );

		// every source point lies inside the box built from it
		for ( int i = 0; i < 3; ++i )
		{
			fixAABBWide pt = { pts[i], pts[i] };
			CHECK( fixAABBWide_Contains( a, pt ) );
		}

		fixAABBWide r = fixMakeAABBWide( pts, 3, FIX( 1.0f ) );
		CHECK( r.lowerBound.x == base - FIX( 2.0f ) );
		CHECK( fixAABBWide_Contains( r, a ) );
	}

	// ---- wide: the two completions box3d's ludicrous build actually needs ----
	// fixMakeAABBWide takes WIDE points, but box3d builds boxes from LOCAL mesh/hull
	// vertices at a wide origin -- a different function, and its absence is why the
	// ludicrous build could not be wired to this library.
	{
		fixedWide_t base = (fixedWide_t)1 << 95;
		fixPosWide origin = { base, base, base };
		fixVec3 pts[3] = { V( 0.0f, 0.0f, 0.0f ), V( 2.0f, -1.0f, 3.0f ), V( -4.0f, 5.0f, 1.0f ) };

		fixAABBWide w = fixMakeAABBWideAt( pts, 3, FIX( 0.0f ), origin );
		fixAABB    n = fixMakeAABB( pts, 3, FIX( 0.0f ) );

		// same box, just placed: extents match the local build exactly, at any distance
		CHECK( VecEq( fixAABBWide_Extents( w ), fixAABB_Extents( n ) ) );
		CHECK( w.lowerBound.x == base + n.lowerBound.x );
		CHECK( w.upperBound.z == base + n.upperBound.z );
		CHECK( fixIsValidAABBWide( w ) );

		// and it agrees with widening the narrow box by hand
		fixAABBWide viaOffset = fixOffsetAABBWide( n, origin );
		CHECK( viaOffset.lowerBound.x == w.lowerBound.x );
		CHECK( viaOffset.upperBound.y == w.upperBound.y );
	}

	// fixAABBWide_Transform: conservative-bound property must hold in the wide build too
	{
		fixAABB unit = Box( -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f );
		fixAABBWide w = Widen( unit, 0 );

		fixTransform id = { V( 0.0f, 0.0f, 0.0f ), fixMakeQuatFromAxisAngle( V( 0.0f, 1.0f, 0.0f ), FIX( 0.0f ) ) };
		fixAABBWide t0 = fixAABBWide_Transform( id, w );
		CHECK( VecEq( fixPosWideToVec3( t0.lowerBound ), unit.lowerBound ) );
		CHECK( VecEq( fixPosWideToVec3( t0.upperBound ), unit.upperBound ) );

		// near the origin, wide and narrow transforms must agree exactly
		fixTransform rot = { V( 3.0f, -2.0f, 0.5f ), fixMakeQuatFromAxisAngle( V( 0.0f, 0.0f, 1.0f ), FIX( 0.7f ) ) };
		fixAABBWide tw = fixAABBWide_Transform( rot, w );
		fixAABB     tn = fixAABB_Transform( rot, unit );
		CHECK( VecEq( fixPosWideToVec3( tw.lowerBound ), tn.lowerBound ) );
		CHECK( VecEq( fixPosWideToVec3( tw.upperBound ), tn.upperBound ) );
		CHECK( fixIsValidAABBWide( tw ) );

		// rotating never shrinks the box
		CHECK( fixAABBWide_Area( tw ) >= fixAABBWide_Area( w ) );
	}

	if ( fails > 0 )
	{
		printf( "%d AABB check(s) FAILED\n", fails );
		return 1;
	}
	printf( "aabb tests passed\n" );
	return 0;
}
