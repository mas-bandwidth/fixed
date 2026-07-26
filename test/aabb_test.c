// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Tests for the AABB family (both widths), b3Plane, and the position/transform
// validators extracted from box3d.
//
// Every symbol added by the extraction is CALLED here on purpose. A header-only
// helper that nothing references is not covered by a passing build -- it is invisible
// to it, and it can be flatly unreachable while CI stays green. That already happened
// once in this repo (b3WideMin/b3WideMax shipped inside the `#if !B3_HAS_INT128` block),
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

static b3Vec3 V( float x, float y, float z )
{
	b3Vec3 v = { B3_FIX( x ), B3_FIX( y ), B3_FIX( z ) };
	return v;
}

static b3AABB Box( float lx, float ly, float lz, float ux, float uy, float uz )
{
	b3AABB a = { V( lx, ly, lz ), V( ux, uy, uz ) };
	return a;
}

static bool VecEq( b3Vec3 a, b3Vec3 b )
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

// widen a narrow box, optionally re-based at a wide origin
static b3AABBWide Widen( b3AABB a, b3FixedWide base )
{
	b3AABBWide w;
	w.lowerBound = b3PosWideFromVec3( a.lowerBound );
	w.upperBound = b3PosWideFromVec3( a.upperBound );
	w.lowerBound.x += base; w.lowerBound.y += base; w.lowerBound.z += base;
	w.upperBound.x += base; w.upperBound.y += base; w.upperBound.z += base;
	return w;
}

int main( void )
{
	// ---- b3Plane ----
	{
		b3Plane good = { V( 1.0f, 0.0f, 0.0f ), B3_FIX( 2.5f ) };
		CHECK( b3IsValidPlane( good ) );

		// a non-unit normal is not a valid plane, even though every component is finite
		b3Plane fat = { V( 3.0f, 0.0f, 0.0f ), B3_FIX( 2.5f ) };
		CHECK( b3IsValidPlane( fat ) == false );

		b3Plane zero = { V( 0.0f, 0.0f, 0.0f ), B3_FIX( 0.0f ) };
		CHECK( b3IsValidPlane( zero ) == false );
	}

	// ---- narrow AABB: hand values ----
	{
		b3AABB a = Box( -1.0f, -2.0f, -3.0f, 1.0f, 2.0f, 3.0f );

		CHECK( b3IsValidAABB( a ) );
		CHECK( VecEq( b3AABB_Center( a ), V( 0.0f, 0.0f, 0.0f ) ) );
		CHECK( VecEq( b3AABB_Extents( a ), V( 1.0f, 2.0f, 3.0f ) ) );

		// surface area of a 2x4x6 box = 2*(2*4 + 4*6 + 6*2) = 88
		CHECK( b3AABB_Area( a ) == B3_FIX( 88.0f ) );

		b3AABB inner = Box( -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f );
		CHECK( b3AABB_Contains( a, inner ) );
		CHECK( b3AABB_Contains( inner, a ) == false );
		CHECK( b3AABB_Overlaps( a, inner ) );

		b3AABB far_ = Box( 100.0f, 100.0f, 100.0f, 101.0f, 101.0f, 101.0f );
		CHECK( b3AABB_Overlaps( a, far_ ) == false );
		CHECK( b3AABB_Contains( a, far_ ) == false );

		// touching boxes count as overlapping (>= boundary)
		b3AABB touch = Box( 1.0f, -2.0f, -3.0f, 2.0f, 2.0f, 3.0f );
		CHECK( b3AABB_Overlaps( a, touch ) );

		b3AABB u = b3AABB_Union( a, far_ );
		CHECK( VecEq( u.lowerBound, V( -1.0f, -2.0f, -3.0f ) ) );
		CHECK( VecEq( u.upperBound, V( 101.0f, 101.0f, 101.0f ) ) );
		CHECK( b3AABB_Contains( u, a ) && b3AABB_Contains( u, far_ ) );

		b3AABB inf = b3AABB_Inflate( a, B3_FIX( 0.5f ) );
		CHECK( VecEq( inf.lowerBound, V( -1.5f, -2.5f, -3.5f ) ) );
		CHECK( VecEq( inf.upperBound, V( 1.5f, 2.5f, 3.5f ) ) );
		CHECK( b3AABB_Contains( inf, a ) );

		// closest point: inside is itself, outside clamps to the face
		CHECK( VecEq( b3ClosestPointToAABB( V( 0.5f, 0.5f, 0.5f ), a ), V( 0.5f, 0.5f, 0.5f ) ) );
		CHECK( VecEq( b3ClosestPointToAABB( V( 5.0f, 0.0f, -9.0f ), a ), V( 1.0f, 0.0f, -3.0f ) ) );
	}

	// ---- narrow AABB: b3MakeAABB over a point cloud ----
	{
		b3Vec3 pts[4] = { V( 0.0f, 0.0f, 0.0f ), V( 2.0f, -1.0f, 3.0f ), V( -4.0f, 5.0f, 1.0f ), V( 1.0f, 1.0f, -2.0f ) };
		b3AABB a = b3MakeAABB( pts, 4, B3_FIX( 0.0f ) );
		CHECK( VecEq( a.lowerBound, V( -4.0f, -1.0f, -2.0f ) ) );
		CHECK( VecEq( a.upperBound, V( 2.0f, 5.0f, 3.0f ) ) );
		for ( int i = 0; i < 4; ++i )
		{
			CHECK( VecEq( b3ClosestPointToAABB( pts[i], a ), pts[i] ) ); // every point is inside
		}

		// radius expands uniformly
		b3AABB r = b3MakeAABB( pts, 4, B3_FIX( 1.0f ) );
		CHECK( VecEq( r.lowerBound, V( -5.0f, -2.0f, -3.0f ) ) );
		CHECK( VecEq( r.upperBound, V( 3.0f, 6.0f, 4.0f ) ) );

		// single point degenerates to a zero-volume but VALID box
		b3AABB one = b3MakeAABB( pts, 1, B3_FIX( 0.0f ) );
		CHECK( b3IsValidAABB( one ) );
		CHECK( b3AABB_Area( one ) == B3_FIX( 0.0f ) );
	}

	// ---- narrow AABB: validity ----
	{
		b3AABB inverted = Box( 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f ); // lower > upper on x
		CHECK( b3IsValidAABB( inverted ) == false );

		b3AABB degenerate = Box( 1.0f, 2.0f, 3.0f, 1.0f, 2.0f, 3.0f ); // a point
		CHECK( b3IsValidAABB( degenerate ) );

		b3AABB bad = { { INT64_MIN, 0, 0 }, { 0, 0, 0 } }; // reserved sentinel coordinate
		CHECK( b3IsValidAABB( bad ) == false );
	}

	// ---- b3AABB_Transform ----
	{
		b3AABB a = Box( -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f );

		// identity transform leaves a symmetric box alone
		b3Transform id = { V( 0.0f, 0.0f, 0.0f ), b3MakeQuatFromAxisAngle( V( 0.0f, 1.0f, 0.0f ), B3_FIX( 0.0f ) ) };
		b3AABB t0 = b3AABB_Transform( id, a );
		CHECK( VecEq( t0.lowerBound, a.lowerBound ) && VecEq( t0.upperBound, a.upperBound ) );

		// pure translation moves the box by exactly the translation
		b3Transform tr = { V( 10.0f, -5.0f, 2.0f ), b3MakeQuatFromAxisAngle( V( 0.0f, 1.0f, 0.0f ), B3_FIX( 0.0f ) ) };
		b3AABB t1 = b3AABB_Transform( tr, a );
		CHECK( VecEq( t1.lowerBound, V( 9.0f, -6.0f, 1.0f ) ) );
		CHECK( VecEq( t1.upperBound, V( 11.0f, -4.0f, 3.0f ) ) );

		// a rotated box is never smaller than the original (the conservative-bound property)
		b3Transform rot = { V( 0.0f, 0.0f, 0.0f ), b3MakeQuatFromAxisAngle( V( 0.0f, 0.0f, 1.0f ), B3_FIX( 0.7f ) ) };
		b3AABB t2 = b3AABB_Transform( rot, a );
		CHECK( b3IsValidAABB( t2 ) );
		CHECK( b3AABB_Area( t2 ) >= b3AABB_Area( a ) );
	}

	// ---- wide: position and transform validators ----
	{
		b3PosWide p = { (b3FixedWide)1 << 90, 0, -( (b3FixedWide)1 << 95 ) };
		CHECK( b3IsValidPosWide( p ) );
		CHECK( b3IsValidWideCoord( (b3FixedWide)0 ) );

		// the reserved 128-bit minimum: negating it would overflow, so it is not a value
		b3FixedWide reserved = (b3FixedWide)( (b3UInt128)1 << 127 );
		CHECK( b3IsValidWideCoord( reserved ) == false );
		b3PosWide bad = { reserved, 0, 0 };
		CHECK( b3IsValidPosWide( bad ) == false );

		b3WorldTransformWide t = { p, b3MakeQuatFromAxisAngle( V( 0.0f, 1.0f, 0.0f ), B3_FIX( 0.25f ) ) };
		CHECK( b3IsValidWorldTransformWide( t ) );

		b3WorldTransformWide tbad = { bad, t.q };
		CHECK( b3IsValidWorldTransformWide( tbad ) == false );
	}

	// ---- wide AABB: agrees with narrow for any box in local range ----
	// This is the property that matters. The two implementations are different code;
	// if they ever disagree on a box both can represent, one of them is wrong.
	{
		const b3AABB cases[5] = {
			{ { B3_FIX( -1.0f ), B3_FIX( -2.0f ), B3_FIX( -3.0f ) }, { B3_FIX( 1.0f ), B3_FIX( 2.0f ), B3_FIX( 3.0f ) } },
			{ { B3_FIX( 0.0f ), B3_FIX( 0.0f ), B3_FIX( 0.0f ) }, { B3_FIX( 1.0f ), B3_FIX( 1.0f ), B3_FIX( 1.0f ) } },
			{ { B3_FIX( 5.0f ), B3_FIX( 5.0f ), B3_FIX( 5.0f ) }, { B3_FIX( 5.0f ), B3_FIX( 5.0f ), B3_FIX( 5.0f ) } },
			{ { B3_FIX( -7.5f ), B3_FIX( 0.25f ), B3_FIX( -0.125f ) }, { B3_FIX( -1.5f ), B3_FIX( 9.75f ), B3_FIX( 0.5f ) } },
			{ { B3_FIX( -100.0f ), B3_FIX( -100.0f ), B3_FIX( -100.0f ) }, { B3_FIX( 100.0f ), B3_FIX( 100.0f ), B3_FIX( 100.0f ) } },
		};

		for ( int i = 0; i < 5; ++i )
		{
			b3AABB n = cases[i];
			b3AABBWide w = Widen( n, 0 );

			CHECK( b3IsValidAABBWide( w ) == b3IsValidAABB( n ) );
			CHECK( VecEq( b3AABBWide_Center( w ), b3AABB_Center( n ) ) );
			CHECK( VecEq( b3AABBWide_Extents( w ), b3AABB_Extents( n ) ) );
			CHECK( b3AABBWide_Area( w ) == b3AABB_Area( n ) );

			for ( int j = 0; j < 5; ++j )
			{
				b3AABBWide wj = Widen( cases[j], 0 );
				CHECK( b3AABBWide_Contains( w, wj ) == b3AABB_Contains( n, cases[j] ) );
				CHECK( b3AABBWide_Overlaps( w, wj ) == b3AABB_Overlaps( n, cases[j] ) );

				b3AABBWide uw = b3AABBWide_Union( w, wj );
				b3AABB un = b3AABB_Union( n, cases[j] );
				CHECK( VecEq( b3PosWideToVec3( uw.lowerBound ), un.lowerBound ) );
				CHECK( VecEq( b3PosWideToVec3( uw.upperBound ), un.upperBound ) );
			}

			b3AABBWide iw = b3AABBWide_Inflate( w, B3_FIX( 0.5f ) );
			b3AABB in_ = b3AABB_Inflate( n, B3_FIX( 0.5f ) );
			CHECK( VecEq( b3PosWideToVec3( iw.lowerBound ), in_.lowerBound ) );
			CHECK( VecEq( b3PosWideToVec3( iw.upperBound ), in_.upperBound ) );

			b3Vec3 probe = V( 0.75f, -0.25f, 2.0f );
			CHECK( VecEq( b3ClosestPointToAABBWide( probe, w ), b3ClosestPointToAABB( probe, n ) ) );
		}
	}

	// ---- wide AABB: the part narrow cannot do ----
	// Ordering, union, containment and overlap past Q48.16 range, where every narrow
	// coordinate would have saturated to INT64_MAX and compared equal.
	{
		b3FixedWide farBase = (b3FixedWide)1 << 90;   // far past local range
		b3FixedWide farther = (b3FixedWide)1 << 100;

		b3AABB unit = Box( 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f );
		b3AABBWide at90 = Widen( unit, farBase );
		b3AABBWide at100 = Widen( unit, farther );

		CHECK( b3IsValidAABBWide( at90 ) && b3IsValidAABBWide( at100 ) );

		// two unit boxes a light-year apart do not overlap and neither contains the other
		CHECK( b3AABBWide_Overlaps( at90, at100 ) == false );
		CHECK( b3AABBWide_Contains( at90, at100 ) == false );
		CHECK( b3AABBWide_Contains( at100, at90 ) == false );

		// their union spans the gap and contains both
		b3AABBWide u = b3AABBWide_Union( at90, at100 );
		CHECK( b3AABBWide_Contains( u, at90 ) );
		CHECK( b3AABBWide_Contains( u, at100 ) );
		CHECK( u.lowerBound.x == farBase );
		CHECK( u.upperBound.x == farther + B3_FIX( 1.0f ) );

		// a box is its own container and overlaps itself, at any distance
		CHECK( b3AABBWide_Contains( at100, at100 ) );
		CHECK( b3AABBWide_Overlaps( at100, at100 ) );

		// extents are local-sized even when the box is astronomically far out
		CHECK( VecEq( b3AABBWide_Extents( at100 ), b3AABB_Extents( unit ) ) );

		// placing a local box at a wide origin is exact, and round-trips
		b3PosWide origin = { farther, farther, farther };
		b3AABBWide placed = b3OffsetAABBWide( unit, origin );
		CHECK( placed.lowerBound.x == farther );
		CHECK( placed.upperBound.x == farther + B3_FIX( 1.0f ) );
		CHECK( b3AABBWide_Contains( placed, placed ) );
		CHECK( VecEq( b3AABBWide_Extents( placed ), b3AABB_Extents( unit ) ) );
	}

	// ---- wide AABB: point cloud ----
	{
		b3FixedWide base = (b3FixedWide)1 << 80;
		b3PosWide pts[3];
		pts[0] = (b3PosWide){ base, base, base };
		pts[1] = (b3PosWide){ base + B3_FIX( 4.0f ), base - B3_FIX( 2.0f ), base + B3_FIX( 1.0f ) };
		pts[2] = (b3PosWide){ base - B3_FIX( 1.0f ), base + B3_FIX( 8.0f ), base };

		b3AABBWide a = b3MakeAABBWide( pts, 3, B3_FIX( 0.0f ) );
		CHECK( a.lowerBound.x == base - B3_FIX( 1.0f ) );
		CHECK( a.lowerBound.y == base - B3_FIX( 2.0f ) );
		CHECK( a.upperBound.x == base + B3_FIX( 4.0f ) );
		CHECK( a.upperBound.y == base + B3_FIX( 8.0f ) );
		CHECK( b3IsValidAABBWide( a ) );

		// every source point lies inside the box built from it
		for ( int i = 0; i < 3; ++i )
		{
			b3AABBWide pt = { pts[i], pts[i] };
			CHECK( b3AABBWide_Contains( a, pt ) );
		}

		b3AABBWide r = b3MakeAABBWide( pts, 3, B3_FIX( 1.0f ) );
		CHECK( r.lowerBound.x == base - B3_FIX( 2.0f ) );
		CHECK( b3AABBWide_Contains( r, a ) );
	}

	if ( fails > 0 )
	{
		printf( "%d AABB check(s) FAILED\n", fails );
		return 1;
	}
	printf( "aabb tests passed\n" );
	return 0;
}
