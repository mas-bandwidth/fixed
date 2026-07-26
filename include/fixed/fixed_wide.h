// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// Wide (Q112.16) fixed-point primitives: 128-bit world coordinates that share
// b3Fixed's 16 fraction bits. Sharing the fraction count is the crux — the
// boundary between wide world space and Q48.16 local space is then an exact
// integer subtract plus a range check, never an arithmetic rescale. See
// fixed3d's docs/design/wide-world-positions.md for the architecture these
// primitives serve.
#pragma once

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#if !B3_HAS_INT128
#error "fixed_wide.h requires 128-bit integer support (clang, gcc, or clang-cl)"
#endif

/// Wide fixed-point scalar: Q112.16 in a 128-bit integer. Same resolution as
/// b3Fixed (1/65536); all 64 extra bits go to integer range (~±2.6e33 units).
typedef b3Int128 b3FixedWide;

/// Wide world position: three Q112.16 coordinates.
typedef struct b3PosWide
{
	b3FixedWide x, y, z;
} b3PosWide;

/// Widen a local Q48.16 value to Q112.16. Exact: the fraction points align.
B3_FIXED_INLINE b3FixedWide b3WideFromFixed( b3Fixed a )
{
	return (b3FixedWide)a;
}

/// Narrow a Q112.16 value to local Q48.16, saturating out-of-range values to
/// INT64_MAX/INT64_MIN. Exact whenever the value fits local range.
B3_FIXED_INLINE b3Fixed b3WideToFixed( b3FixedWide a )
{
	if ( a > (b3FixedWide)INT64_MAX )
	{
		return INT64_MAX;
	}
	if ( a < (b3FixedWide)INT64_MIN )
	{
		return INT64_MIN;
	}
	return (b3Fixed)a;
}

/// Wide add. Exact 128-bit integer addition.
B3_FIXED_INLINE b3FixedWide b3WideAdd( b3FixedWide a, b3FixedWide b )
{
	return a + b;
}

/// Wide subtract. Exact 128-bit integer subtraction.
B3_FIXED_INLINE b3FixedWide b3WideSub( b3FixedWide a, b3FixedWide b )
{
	return a - b;
}

/// Min/max on the wide (128-bit) fixed-point type.
///
/// Extracted from fixed3d, where they live behind BOX3D_LUDICROUS_MODE because that is
/// the only build with 128-bit AABB bounds. Here they are unconditional: this library
/// exports the wide type on every build, so a consumer selects narrow or wide by which
/// type it uses, not by a compile flag that changes an ABI.
B3_FIXED_INLINE b3FixedWide b3WideMin( b3FixedWide a, b3FixedWide b )
{
	return a < b ? a : b;
}

B3_FIXED_INLINE b3FixedWide b3WideMax( b3FixedWide a, b3FixedWide b )
{
	return a > b ? a : b;
}

/// Offset a wide coordinate by a local delta (the once-per-step delta-fold).
/// Exact: int128 += widened int64, fraction points aligned.
B3_FIXED_INLINE b3FixedWide b3WideOffset( b3FixedWide a, b3Fixed d )
{
	return a + (b3FixedWide)d;
}

/// The boundary operation: difference two wide world coordinates into local
/// Q48.16 space. Exact whenever the separation fits local range (any contact
/// pair, joint, or reach-bounded query); saturates otherwise. This is the
/// fixed-point replacement for float's entire directed-rounding apparatus.
B3_FIXED_INLINE b3Fixed b3WideSubToFixed( b3FixedWide a, b3FixedWide b )
{
	return b3WideToFixed( a - b );
}

/// Widen a local position/vector to a wide world position. Exact.
B3_FIXED_INLINE b3PosWide b3PosWideFromVec3( b3Vec3 v )
{
	b3PosWide p = { (b3FixedWide)v.x, (b3FixedWide)v.y, (b3FixedWide)v.z };
	return p;
}

/// Narrow a wide position to a local vector, saturating per component.
B3_FIXED_INLINE b3Vec3 b3PosWideToVec3( b3PosWide p )
{
	b3Vec3 v = { b3WideToFixed( p.x ), b3WideToFixed( p.y ), b3WideToFixed( p.z ) };
	return v;
}

/// Difference two wide positions into local space (per-component boundary op).
B3_FIXED_INLINE b3Vec3 b3PosWideSub( b3PosWide a, b3PosWide b )
{
	b3Vec3 v = { b3WideSubToFixed( a.x, b.x ), b3WideSubToFixed( a.y, b.y ), b3WideSubToFixed( a.z, b.z ) };
	return v;
}

/// Offset a wide position by a local delta vector. Exact.
B3_FIXED_INLINE b3PosWide b3PosWideOffset( b3PosWide p, b3Vec3 d )
{
	b3PosWide out = { b3WideOffset( p.x, d.x ), b3WideOffset( p.y, d.y ), b3WideOffset( p.z, d.z ) };
	return out;
}

/// A world transform with a wide translation. Rotation is frame-local and never needs
/// range, so the quaternion stays Q48.16.
typedef struct b3WorldTransformWide
{
	b3PosWide p;
	b3Quat q;
} b3WorldTransformWide;

/// Is this a valid wide coordinate? Mirrors b3IsValidFixed: every value is a legal
/// quantity except the 128-bit minimum, which is reserved so negation cannot overflow.
B3_FIXED_INLINE bool b3IsValidWideCoord( b3FixedWide x )
{
	return x != (b3FixedWide)( (b3UInt128)1 << 127 );
}

B3_FIXED_INLINE bool b3IsValidPosWide( b3PosWide p )
{
	return b3IsValidWideCoord( p.x ) && b3IsValidWideCoord( p.y ) && b3IsValidWideCoord( p.z );
}

B3_FIXED_INLINE bool b3IsValidWorldTransformWide( b3WorldTransformWide t )
{
	return b3IsValidPosWide( t.p ) && b3IsValidQuat( t.q );
}

/// An axis-aligned bounding box in wide (Q112.16) world space.
///
/// The narrow counterpart is b3AABB in fixed_vec.h. Storage, min/max, union, contains
/// and overlap stay 128-bit -- those are the hot broadphase-tree operations. Center and
/// extents narrow to b3Fixed for the b3Vec3-returning API, which is exact whenever the
/// box fits local range.
typedef struct b3AABBWide
{
	b3PosWide lowerBound;
	b3PosWide upperBound;
} b3AABBWide;

/// Get the wide AABB of a wide point cloud, expanded by a uniform local radius.
B3_FIXED_INLINE b3AABBWide b3MakeAABBWide( const b3PosWide* points, int count, b3Fixed radius )
{
	B3_ASSERT( count > 0 );
	b3AABBWide a = { points[0], points[0] };
	for ( int i = 1; i < count; ++i )
	{
		a.lowerBound.x = b3WideMin( a.lowerBound.x, points[i].x );
		a.lowerBound.y = b3WideMin( a.lowerBound.y, points[i].y );
		a.lowerBound.z = b3WideMin( a.lowerBound.z, points[i].z );
		a.upperBound.x = b3WideMax( a.upperBound.x, points[i].x );
		a.upperBound.y = b3WideMax( a.upperBound.y, points[i].y );
		a.upperBound.z = b3WideMax( a.upperBound.z, points[i].z );
	}

	a.lowerBound.x -= radius; a.lowerBound.y -= radius; a.lowerBound.z -= radius;
	a.upperBound.x += radius; a.upperBound.y += radius; a.upperBound.z += radius;

	return a;
}

/// Does a fully contain b?
B3_FIXED_INLINE bool b3AABBWide_Contains( b3AABBWide a, b3AABBWide b )
{
	if ( a.lowerBound.x > b.lowerBound.x || b.upperBound.x > a.upperBound.x ) return false;
	if ( a.lowerBound.y > b.lowerBound.y || b.upperBound.y > a.upperBound.y ) return false;
	if ( a.lowerBound.z > b.lowerBound.z || b.upperBound.z > a.upperBound.z ) return false;
	return true;
}

/// Surface area. The deltas narrow to local range first -- a box whose extent exceeds
/// Q48.16 range has no meaningful area in b3Fixed, and saturating is the honest answer.
B3_FIXED_INLINE b3Fixed b3AABBWide_Area( b3AABBWide a )
{
	b3Fixed dx = b3WideSubToFixed( a.upperBound.x, a.lowerBound.x );
	b3Fixed dy = b3WideSubToFixed( a.upperBound.y, a.lowerBound.y );
	b3Fixed dz = b3WideSubToFixed( a.upperBound.z, a.lowerBound.z );
	return b3FixMul( B3_FIX( 2.0f ) , ( b3FixMul( dx , dy ) + b3FixMul( dy , dz ) + b3FixMul( dz , dx ) ) );
}

/// Center, in local space.
///
/// Ported exactly as box3d has it, INCLUDING the narrowing, because the rounding is
/// load-bearing: box3d narrows both bounds to b3Fixed and then applies the same
/// half-up b3MulSV( 0.5, ... ) the narrow build uses, so a box expressed either way
/// yields the same center. An integer >> 1 here would truncate instead of rounding
/// half-up and would shift the box by up to 1 ULP relative to the narrow build.
///
/// Known limitation, inherited deliberately rather than silently repaired: the center
/// of a box whose coordinates exceed Q48.16 range saturates. Fixing that means
/// returning a wide center, which is a behaviour change and belongs in its own commit
/// with its own goldens -- not smuggled in under the word "extract".
B3_FIXED_INLINE b3Vec3 b3AABBWide_Center( b3AABBWide a )
{
	b3Vec3 lo = b3PosWideToVec3( a.lowerBound );
	b3Vec3 hi = b3PosWideToVec3( a.upperBound );
	return b3MulSV( B3_FIX( 0.5f ), b3Add( hi, lo ) );
}

/// Extents (half-widths) in local space. Exact whenever the box's SIZE fits local
/// range, regardless of how far from the origin the box sits.
///
/// NOT a faithful port -- this is a deliberate BUG FIX, called out because everything
/// else in this extraction is behaviour-preserving. box3d's wide b3AABB_Extents narrows
/// each bound to b3Fixed and then subtracts:
///
///     b3MulSV( B3_FIX( 0.5f ), b3Sub( b3ToVec3( a.upperBound ), b3ToVec3( a.lowerBound ) ) )
///
/// Past Q48.16 range BOTH bounds saturate to INT64_MAX, their difference is zero, and a
/// perfectly ordinary box reports zero extents -- at exactly the distances ludicrous mode
/// exists to serve. b3AABB_Transform consumes Extents, so transformed distant boxes
/// collapse too. box3d's own wide b3AABB_Area already does it the right way round
/// (difference in 128-bit, then narrow), so this restores consistency within that file
/// rather than inventing a convention.
///
/// For any box whose bounds both fit local range the two forms agree bit-for-bit, which
/// the narrow/wide correspondence cases in test/aabb_test.c check directly. The fix is
/// therefore invisible to every build that was already correct.
B3_FIXED_INLINE b3Vec3 b3AABBWide_Extents( b3AABBWide a )
{
	b3Vec3 d = { b3WideSubToFixed( a.upperBound.x, a.lowerBound.x ),
	             b3WideSubToFixed( a.upperBound.y, a.lowerBound.y ),
	             b3WideSubToFixed( a.upperBound.z, a.lowerBound.z ) };
	return b3MulSV( B3_FIX( 0.5f ), d );
}

/// Union of two wide boxes.
B3_FIXED_INLINE b3AABBWide b3AABBWide_Union( b3AABBWide a, b3AABBWide b )
{
	b3AABBWide out;
	out.lowerBound.x = b3WideMin( a.lowerBound.x, b.lowerBound.x );
	out.lowerBound.y = b3WideMin( a.lowerBound.y, b.lowerBound.y );
	out.lowerBound.z = b3WideMin( a.lowerBound.z, b.lowerBound.z );
	out.upperBound.x = b3WideMax( a.upperBound.x, b.upperBound.x );
	out.upperBound.y = b3WideMax( a.upperBound.y, b.upperBound.y );
	out.upperBound.z = b3WideMax( a.upperBound.z, b.upperBound.z );
	return out;
}

/// Add uniform local padding to a wide box.
B3_FIXED_INLINE b3AABBWide b3AABBWide_Inflate( b3AABBWide a, b3Fixed extension )
{
	b3AABBWide out = a;
	out.lowerBound.x -= extension; out.lowerBound.y -= extension; out.lowerBound.z -= extension;
	out.upperBound.x += extension; out.upperBound.y += extension; out.upperBound.z += extension;
	return out;
}

/// Do two wide boxes overlap?
B3_FIXED_INLINE bool b3AABBWide_Overlaps( b3AABBWide a, b3AABBWide b )
{
	if ( a.upperBound.x < b.lowerBound.x || a.lowerBound.x > b.upperBound.x ) return false;
	if ( a.upperBound.y < b.lowerBound.y || a.lowerBound.y > b.upperBound.y ) return false;
	if ( a.upperBound.z < b.lowerBound.z || a.lowerBound.z > b.upperBound.z ) return false;
	return true;
}

/// Place a local box at a wide world origin. Exact: fixed-point addition, aligned
/// fraction points, so no outward rounding is needed -- the translated box is the
/// translated box.
B3_FIXED_INLINE b3AABBWide b3OffsetAABBWide( b3AABB localBox, b3PosWide origin )
{
	b3AABBWide out;
	out.lowerBound = b3PosWideOffset( origin, localBox.lowerBound );
	out.upperBound = b3PosWideOffset( origin, localBox.upperBound );
	return out;
}

/// Closest point on a wide box to a LOCAL point.
///
/// Ported as box3d has it: the bounds narrow to local space and the clamp happens
/// there, because the incoming point is local. Clamping wide instead would be a
/// different function with a different signature, and inventing it here would mean
/// shipping an untested behaviour change wearing an extraction's clothes.
B3_FIXED_INLINE b3Vec3 b3ClosestPointToAABBWide( b3Vec3 point, b3AABBWide a )
{
	b3Vec3 lo = b3PosWideToVec3( a.lowerBound );
	b3Vec3 hi = b3PosWideToVec3( a.upperBound );
	return b3Clamp( point, lo, hi );
}

/// Is this a valid wide AABB? Both bounds valid, and lower <= upper on every axis.
B3_FIXED_INLINE bool b3IsValidAABBWide( b3AABBWide a )
{
	if ( b3IsValidPosWide( a.lowerBound ) == false ) return false;
	if ( b3IsValidPosWide( a.upperBound ) == false ) return false;
	if ( a.lowerBound.x > a.upperBound.x ) return false;
	if ( a.lowerBound.y > a.upperBound.y ) return false;
	if ( a.lowerBound.z > a.upperBound.z ) return false;
	return true;
}
