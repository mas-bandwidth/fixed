// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// The geometric layer: vectors, quaternions, matrices, transforms, world positions, the
// validity predicates, and the small scalar helpers that came along with them.
//
// WHY THIS FILE EXISTS. Most of this surface had no test at all. It compiled, so CI was
// green, and a passing build says nothing about a header-only function nobody calls --
// that is not coverage, it is invisibility, and this repository has already shipped a
// flatly unreachable pair of functions (fixWideMin and fixWideMax, inside the block that
// #errors) with CI green the whole time. So the rule here is the same one aabb_test.c
// adopted: if the library declares it, this file calls it and asserts on the result.
//
// HOW EACH FUNCTION IS PINNED, in descending order of strength:
//
//   1. EXACT IDENTITIES, where fixed point gives them. Negation, transpose, conjugate,
//      componentwise min/max/clamp/abs and the pure-integer transforms are exact, so they
//      are checked as equalities with no tolerance at all. Most of this file is these.
//
//   2. HAND VALUES, for the cases where the right answer is known by construction --
//      a quarter turn about z takes +x to +y, the identity matrix is its own inverse.
//
//   3. BOUNDED IDENTITIES, for the operations that round. Rotating a vector and rotating
//      it back is not exactly the identity in Q48.16, so those checks carry a tolerance.
//      EVERY TOLERANCE HERE WAS MEASURED, not guessed: the actual error was printed and
//      the bound set just above it, so a regression that doubles an error is caught
//      rather than absorbed. The measured values are small -- 0 to 2 ULP for the rotation
//      paths, up to 10 for a matrix inverse, and about 220 for the twist and swing angles,
//      where the bound is dominated by fixComputeCosSin's documented 0.0017 accuracy
//      doubled by the 2*atan2 those functions end in.
//
//      A tolerance is still a WEAK oracle and is treated as one: it is never the only
//      thing holding a function, which is what the hash below is for.
//
//   4. A FROZEN CROSS-PLATFORM HASH over a structured sweep of the whole layer, which is
//      what actually catches a one-ulp change. Every operation above feeds it. The
//      tolerance checks would not notice a single-bit drift; the hash notices nothing
//      else.
//
// NEGATIVE CONTROL: built a second time with -DGEOMETRY_NEGATIVE_CONTROL, which injects a
// one-ulp error into ONE FUNCTION PER FAMILY -- the dot product, the cross product,
// quaternion multiplication, matrix-times-vector, and point transformation. That build
// MUST fail (ctest WILL_FAIL). Without it, a suite carrying this many tolerances could go
// blind and never say so.
//
// One injection per family rather than one injection total, because a suite that spans
// five families needs to prove it is watching five families. The first attempt at this
// control perturbed fixMul alone and DID NOT FAIL: a function-like macro only rewrites
// call sites that come after it, so it never reached the library's own header-inline
// callers, and this file never calls fixMul directly. The control was reporting success
// while injecting nothing. That is the exact failure mode the control exists to prevent,
// caught by ctest's WILL_FAIL rather than by inspection.

#include "fixed/fixed.h"
#include "fixed/fixed_math.h"
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include <stdint.h>
#include <stdio.h>

#ifdef GEOMETRY_NEGATIVE_CONTROL
// One quantum on a returned vector, quaternion or transform, so the injections below can
// perturb the composite results this layer actually deals in. Declared before the macros
// so the macros can use them.
static fixVec3 perturbVec( fixVec3 v )
{
	v.x += 1;
	return v;
}
static fixQuat perturbQuat( fixQuat q )
{
	q.s += 1;
	return q;
}

// The injections: one per family. The macros do not expand recursively, so each call
// inside resolves to the real function.
	#define fixDot( a, b ) ( fixDot( a, b ) + 1 )
	#define fixCross( a, b ) perturbVec( fixCross( a, b ) )
	#define fixMulQuat( a, b ) perturbQuat( fixMulQuat( a, b ) )
	#define fixMulMV( m, v ) perturbVec( fixMulMV( m, v ) )
	#define fixTransformPoint( t, v ) perturbVec( fixTransformPoint( t, v ) )
#endif

static int fails = 0;
#define CHECK( cond )                                                                                                          \
	do                                                                                                                         \
	{                                                                                                                          \
		if ( !( cond ) )                                                                                                       \
		{                                                                                                                      \
			printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond );                                                           \
			fails++;                                                                                                           \
		}                                                                                                                      \
	} while ( 0 )

// A section that passed. Printed on its own line, in the manner of the other suites here.
static void section( const char* name )
{
	printf( "%s\n", name );
}

static fixVec3 V( float x, float y, float z )
{
	fixVec3 v = { FIX( x ), FIX( y ), FIX( z ) };
	return v;
}

static fixQuat Q( fixVec3 axis, float radians )
{
	return fixMakeQuatFromAxisAngle( fixNormalize( axis ), FIX( radians ) );
}

static bool VecEq( fixVec3 a, fixVec3 b )
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

static bool QuatEq( fixQuat a, fixQuat b )
{
	return VecEq( a.v, b.v ) && a.s == b.s;
}

static bool MatEq( fixMatrix3 a, fixMatrix3 b )
{
	return VecEq( a.cx, b.cx ) && VecEq( a.cy, b.cy ) && VecEq( a.cz, b.cz );
}

// Componentwise closeness, in ULPs of Q48.16. Results here are bit-identical on every
// platform, so these bounds are not portability slack: they are the measured error of the
// operation, kept tight so a regression cannot hide underneath one.
static bool VecNear( fixVec3 a, fixVec3 b, fixed_t tolerance )
{
	return fixAbs( a.x - b.x ) <= tolerance && fixAbs( a.y - b.y ) <= tolerance && fixAbs( a.z - b.z ) <= tolerance;
}

static bool Near( fixed_t a, fixed_t b, fixed_t tolerance )
{
	return fixAbs( a - b ) <= tolerance;
}

// ---- the frozen cross-platform hash ------------------------------------------------
//
// This is the check that actually has teeth against a one-ulp drift, and the reason every
// structured case below feeds it rather than merely asserting.

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix( fixed_t v )
{
	const uint8_t* b = (const uint8_t*)&v;
	for ( int i = 0; i < 8; i++ ) { fnv ^= b[i]; fnv *= 0x00000100000001B3ULL; }
}
static void mixVec( fixVec3 v ) { mix( v.x ); mix( v.y ); mix( v.z ); }
static void mixQuat( fixQuat q ) { mixVec( q.v ); mix( q.s ); }
static void mixMat( fixMatrix3 m ) { mixVec( m.cx ); mixVec( m.cy ); mixVec( m.cz ); }
static void mixTransform( fixTransform t ) { mixVec( t.p ); mixQuat( t.q ); }

// FROZEN 2026-08-24. Every OS and compiler in CI must reproduce this exactly, on the
// native and the emulated 128-bit arm alike. If a legitimate change alters it, re-capture
// deliberately and say why in the pull request; never loosen it to make a mismatch go away.
//
// RE-CAPTURED 2026-08-24, from 0xd83f7a5bee88e262, when the wide arm of the inverse
// started carrying its full width. Exactly two values moved and both are in the huge
// matrix above: the inverse of it and the solve through it. Every other value in this
// sweep is bit-identical, which was checked directly rather than assumed -- the sweep's
// matrices are of order 5, so they take the 128-bit arm, which did not change.
#ifndef EXPECTED_GEOMETRY_HASH
	#define EXPECTED_GEOMETRY_HASH 0x660211d75929badeULL
#endif

// Deterministic sampling. splitmix64: pure integer, identical on every platform.
static uint64_t rngState = 0x243F6A8885A308D3ULL;
static uint64_t nextRandom( void )
{
	rngState += 0x9E3779B97F4A7C15ULL;
	uint64_t z = rngState;
	z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
	z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
	return z ^ ( z >> 31 );
}

// A vector with components in roughly [-8, 8], the scale simulation geometry actually
// lives at. Deliberately not full-range: the interesting rounding in this layer happens
// where products stay small, and full-range components would saturate every dot product.
static fixVec3 randomVec( void )
{
	fixVec3 v = { (fixed_t)( (int64_t)nextRandom() >> 44 ), (fixed_t)( (int64_t)nextRandom() >> 44 ),
				  (fixed_t)( (int64_t)nextRandom() >> 44 ) };
	return v;
}

static fixQuat randomQuat( void )
{
	fixQuat q = { randomVec(), (fixed_t)( (int64_t)nextRandom() >> 44 ) };
	return fixNormalizeQuat( q );
}

// ================================================================================
// Scalar, integer and float helpers.
// ================================================================================

static void testScalarHelpers( void )
{
	int before = fails;

	CHECK( fixMinFloat( 1.5f, 2.5f ) == 1.5f );
	CHECK( fixMaxFloat( 1.5f, 2.5f ) == 2.5f );
	CHECK( fixMinFloat( -1.5f, -2.5f ) == -2.5f );
	CHECK( fixMaxFloat( -1.5f, -2.5f ) == -1.5f );
	CHECK( fixClampFloat( 3.0f, -1.0f, 1.0f ) == 1.0f );
	CHECK( fixClampFloat( -3.0f, -1.0f, 1.0f ) == -1.0f );
	CHECK( fixClampFloat( 0.25f, -1.0f, 1.0f ) == 0.25f );

	CHECK( fixMinInt( 3, 7 ) == 3 );
	CHECK( fixMaxInt( 3, 7 ) == 7 );
	CHECK( fixMinInt( -3, -7 ) == -7 );
	CHECK( fixMaxInt( -3, -7 ) == -3 );
	CHECK( fixClampInt( 10, -2, 2 ) == 2 );
	CHECK( fixClampInt( -10, -2, 2 ) == -2 );
	CHECK( fixClampInt( 1, -2, 2 ) == 1 );

	// The float boundary helpers. Exact for values that are exactly representable in both
	// domains, which is what these cases are.
	CHECK( fixToFloat( FIX( 1.0f ) ) == 1.0f );
	CHECK( fixToFloat( FIX( -2.5f ) ) == -2.5f );
	CHECK( fixToFloat( 0 ) == 0.0f );
	CHECK( fixFromFloat( 1.0f ) == FIX_ONE );
	CHECK( fixFromFloat( -2.5f ) == -FIX( 2.5f ) );
	CHECK( fixFromFloat( 0.0f ) == 0 );

	// Round to nearest, away from zero at the tie -- the same rule fixFromDouble uses.
	CHECK( fixFromFloat( 0.5f / 65536.0f ) == 1 );
	CHECK( fixFromFloat( -0.5f / 65536.0f ) == -1 );

	for ( int i = -40; i <= 40; i++ )
	{
		fixed_t a = (fixed_t)i * FIX_ONE / 8;
		mix( fixFromFloat( fixToFloat( a ) ) );
	}

	if ( fails == before ) section( "scalar, integer and float helpers" );
}

// ================================================================================
// Vector arithmetic.
// ================================================================================

static void testVectors( void )
{
	int before = fails;

	fixVec3 a = V( 1.5f, -2.0f, 0.25f );
	fixVec3 b = V( -0.5f, 4.0f, 3.0f );

	// Exact: these are integer adds, subtracts and negations.
	CHECK( VecEq( fixVecAdd( a, b ), V( 1.0f, 2.0f, 3.25f ) ) );
	CHECK( VecEq( fixVecSub( a, b ), V( 2.0f, -6.0f, -2.75f ) ) );
	CHECK( VecEq( fixVecNeg( a ), V( -1.5f, 2.0f, -0.25f ) ) );
	CHECK( VecEq( fixVecAdd( a, fixVecNeg( a ) ), fixVec3_zero ) );
	CHECK( VecEq( fixVecMul( a, b ), V( -0.75f, -8.0f, 0.75f ) ) );
	CHECK( VecEq( fixVecAbs( a ), V( 1.5f, 2.0f, 0.25f ) ) );
	CHECK( VecEq( fixVecMin( a, b ), V( -0.5f, -2.0f, 0.25f ) ) );
	CHECK( VecEq( fixVecMax( a, b ), V( 1.5f, 4.0f, 3.0f ) ) );
	CHECK( VecEq( fixVecClamp( a, V( 0.0f, 0.0f, 0.0f ), V( 1.0f, 1.0f, 1.0f ) ), V( 1.0f, 0.0f, 0.25f ) ) );

	// fixSign is 1 at zero, not 0. That is the documented behaviour and fixSafeScale
	// depends on it: a zero scale must come out positive, not vanish.
	CHECK( VecEq( fixSign( V( 3.0f, -3.0f, 0.0f ) ), V( 1.0f, -1.0f, 1.0f ) ) );
	CHECK( VecEq( fixSafeScale( V( 2.0f, -2.0f, 0.0f ) ), V( 2.0f, -2.0f, FIX_MIN_SCALE / 65536.0f ) ) );
	CHECK( fixSafeScale( V( 0.0f, 0.0f, 0.0f ) ).x == FIX_MIN_SCALE );
	CHECK( fixSafeScale( V( -0.001f, 0.0f, 0.0f ) ).x == -FIX_MIN_SCALE );

	// Scaling and the fused forms.
	CHECK( VecEq( fixMulSV( FIX( 2.0f ), a ), V( 3.0f, -4.0f, 0.5f ) ) );
	CHECK( VecEq( fixMulAdd( a, FIX( 2.0f ), b ), V( 0.5f, 6.0f, 6.25f ) ) );
	CHECK( VecEq( fixMulSub( a, FIX( 2.0f ), b ), V( 2.5f, -10.0f, -5.75f ) ) );
	CHECK( VecEq( fixBlend2( FIX( 2.0f ), a, FIX( 3.0f ), b ), V( 1.5f, 8.0f, 9.5f ) ) );
	CHECK( VecEq( fixVecLerp( a, b, 0 ), a ) );
	CHECK( VecEq( fixVecLerp( a, b, FIX_ONE ), b ) );
	CHECK( VecEq( fixVecLerp( a, b, FIX_HALF ), V( 0.5f, 1.0f, 1.625f ) ) );

	// Dot, cross and the raw accumulator.
	CHECK( fixDot( a, b ) == FIX( -8.0f ) );
	CHECK( fixDot( fixVec3_axisX, fixVec3_axisY ) == 0 );
	CHECK( fixDot( fixVec3_axisX, fixVec3_axisX ) == FIX_ONE );
	CHECK( fixFromDotRaw( fixDotRaw( a, b ) ) == fixDot( a, b ) );

	// The raw form is exact where the rounded one is not: two vectors whose dot product
	// is a quarter of a resolution unit round to zero, but the sign is still knowable.
	{
		fixVec3 tiny = { 1, 0, 0 };
		fixVec3 alsoTiny = { 1, 0, 0 };
		CHECK( fixDot( tiny, alsoTiny ) == 0 );
		CHECK( fixInt128Gt( fixDotRaw( tiny, alsoTiny ), FIX_INT128_ZERO ) );
	}

	CHECK( VecEq( fixCross( fixVec3_axisX, fixVec3_axisY ), fixVec3_axisZ ) );
	CHECK( VecEq( fixCross( fixVec3_axisY, fixVec3_axisX ), fixVecNeg( fixVec3_axisZ ) ) );
	CHECK( VecEq( fixCross( a, a ), fixVec3_zero ) );

	// Length, distance and their squared forms.
	CHECK( fixLength( V( 3.0f, 4.0f, 0.0f ) ) == FIX( 5.0f ) );
	CHECK( fixLength( fixVec3_zero ) == 0 );
	CHECK( fixLengthSquared( V( 3.0f, 4.0f, 0.0f ) ) == FIX( 25.0f ) );
	CHECK( fixDistance( V( 1.0f, 2.0f, 3.0f ), V( 4.0f, 6.0f, 3.0f ) ) == FIX( 5.0f ) );
	CHECK( fixDistanceSquared( V( 1.0f, 2.0f, 3.0f ), V( 4.0f, 6.0f, 3.0f ) ) == FIX( 25.0f ) );
	CHECK( fixDistance( a, b ) == fixDistance( b, a ) );
	CHECK( fixDistanceSquared( a, a ) == 0 );

	// Normalization. Unit length to within an ulp is the documented guarantee.
	{
		fixVec3 n = fixNormalize( V( 3.0f, 4.0f, 0.0f ) );
		CHECK( VecEq( n, V( 0.6f, 0.8f, 0.0f ) ) || VecNear( n, V( 0.6f, 0.8f, 0.0f ), 1 ) );
		CHECK( fixIsNormalized( n ) );
		CHECK( VecEq( fixNormalize( fixVec3_zero ), fixVec3_zero ) );
		CHECK( fixIsNormalized( fixVec3_axisX ) );
		CHECK( fixIsNormalized( V( 2.0f, 0.0f, 0.0f ) ) == false );

		fixed_t length = 0;
		fixVec3 m = fixGetLengthAndNormalize( &length, V( 0.0f, 0.0f, 2.5f ) );
		CHECK( length == FIX( 2.5f ) );
		CHECK( VecEq( m, fixVec3_axisZ ) );

		fixed_t zeroLength = FIX_ONE;
		CHECK( VecEq( fixGetLengthAndNormalize( &zeroLength, fixVec3_zero ), fixVec3_zero ) );
		CHECK( zeroLength == 0 );
	}

	// fixPerp: perpendicular and unit, on both arms of its branch. The branch is on
	// |a.x| > 0.5, so an x-dominant vector and a z-dominant one take different paths.
	{
		fixVec3 axes[4] = { fixVec3_axisX, fixVec3_axisY, fixVec3_axisZ, fixNormalize( V( 1.0f, 1.0f, 1.0f ) ) };
		for ( int i = 0; i < 4; i++ )
		{
			fixVec3 p = fixPerp( axes[i] );
			CHECK( fixIsNormalized( p ) );
			CHECK( Near( fixDot( p, axes[i] ), 0, 4 ) );
			mixVec( p );
		}
	}

	// The structured sweep that feeds the hash.
	for ( int i = -12; i <= 12; i++ )
	{
		for ( int j = -12; j <= 12; j++ )
		{
			fixVec3 u = { (fixed_t)i * FIX_ONE / 4, (fixed_t)j * FIX_ONE / 8, (fixed_t)( i - j ) * FIX_ONE / 16 };
			fixVec3 w = { (fixed_t)j * FIX_ONE / 2, (fixed_t)( i + j ) * FIX_ONE / 4, (fixed_t)i * FIX_ONE / 8 };

			mixVec( fixVecAdd( u, w ) );
			mixVec( fixVecMul( u, w ) );
			mixVec( fixCross( u, w ) );
			mixVec( fixVecLerp( u, w, FIX_HALF ) );
			mixVec( fixBlend2( FIX( 0.25f ), u, FIX( 0.75f ), w ) );
			mixVec( fixMulAdd( u, FIX( 0.5f ), w ) );
			mixVec( fixMulSub( u, FIX( 0.5f ), w ) );
			mixVec( fixNormalize( u ) );
			mixVec( fixSafeScale( u ) );
			mixVec( fixVecClamp( u, fixVecNeg( fixVec3_one ), fixVec3_one ) );
			mix( fixDot( u, w ) );
			mix( fixLength( u ) );
			mix( fixLengthSquared( w ) );
			mix( fixDistance( u, w ) );
			mix( fixDistanceSquared( u, w ) );
		}
	}

	if ( fails == before ) section( "vector arithmetic, dot, cross, length and normalization" );
}

// ================================================================================
// Quaternions.
// ================================================================================

static void testQuaternions( void )
{
	int before = fails;

	CHECK( QuatEq( fixMulQuat( fixQuat_identity, fixQuat_identity ), fixQuat_identity ) );
	CHECK( fixIsNormalizedQuat( fixQuat_identity ) );
	CHECK( fixIsNormalizedQuat( fixNegateQuat( fixQuat_identity ) ) );
	CHECK( fixDotQuat( fixQuat_identity, fixQuat_identity ) == FIX_ONE );
	CHECK( fixDotQuat( fixQuat_identity, fixNegateQuat( fixQuat_identity ) ) == -FIX_ONE );

	// Exact, all four of these: integer negation.
	{
		fixQuat q = { V( 0.1f, 0.2f, 0.3f ), FIX( 0.4f ) };
		CHECK( QuatEq( fixConjugate( q ), ( (fixQuat){ V( -0.1f, -0.2f, -0.3f ), FIX( 0.4f ) } ) ) );
		CHECK( QuatEq( fixNegateQuat( q ), ( (fixQuat){ V( -0.1f, -0.2f, -0.3f ), -FIX( 0.4f ) } ) ) );
		CHECK( QuatEq( fixConjugate( fixConjugate( q ) ), q ) );
		CHECK( QuatEq( fixNegateQuat( fixNegateQuat( q ) ), q ) );
	}

	// A quarter turn about z takes +x to +y. Known by construction, so no tolerance is
	// needed on the direction, only on the rounding.
	{
		fixQuat q = Q( V( 0.0f, 0.0f, 1.0f ), 1.57079632679f );
		CHECK( fixIsNormalizedQuat( q ) );
		CHECK( VecNear( fixRotateVector( q, fixVec3_axisX ), fixVec3_axisY, 2 ) ); // measured 0
		CHECK( VecNear( fixRotateVector( q, fixVec3_axisY ), fixVecNeg( fixVec3_axisX ), 2 ) );
		CHECK( VecNear( fixRotateVector( q, fixVec3_axisZ ), fixVec3_axisZ, 2 ) );

		// Inverse rotation undoes rotation, and the conjugate is the inverse.
		fixVec3 v = V( 1.25f, -0.5f, 2.0f );
		CHECK( VecNear( fixInvRotateVector( q, fixRotateVector( q, v ) ), v, 2 ) ); // measured 0
		CHECK( VecNear( fixRotateVector( fixConjugate( q ), v ), fixInvRotateVector( q, v ), 2 ) );

		// inv(q) * q is the identity rotation.
		CHECK( QuatEq( fixInvMulQuat( q, q ), fixQuat_identity ) || fixDotQuat( fixInvMulQuat( q, q ), fixQuat_identity ) > FIX_ONE - 8 );

		// Axis and angle round trip.
		fixed_t radians = 0;
		fixVec3 axis = fixGetAxisAngle( &radians, q );
		CHECK( VecNear( axis, fixVec3_axisZ, 2 ) );
		CHECK( Near( radians, FIX( 1.57079632679f ), 256 ) ); // 2 * fixAtan2, as above
		CHECK( Near( fixGetQuatAngle( q ), radians, 1 ) );

		// A zero-vector part means no axis: the documented answer is the zero vector, and
		// the angle is zero.
		fixed_t identityAngle = FIX_ONE;
		CHECK( VecEq( fixGetAxisAngle( &identityAngle, fixQuat_identity ), fixVec3_zero ) );
		CHECK( identityAngle == 0 );
		CHECK( fixGetQuatAngle( fixQuat_identity ) == 0 );
	}

	// Twist about z and swing away from it, on a rotation that is pure twist and one that
	// is pure swing.
	{
		// 256 ULP is about 0.004 radians, and it is not slack: both of these end in
		// 2 * fixAtan2 applied to components that fixComputeCosSin produced, whose
		// documented accuracy is 0.0017. Doubling that is 0.0034 radians, or 223 ULP --
		// measured worst case over [0.01, 3.0] radians is 219. The angles that should be
		// exactly zero are checked as exactly zero, with no tolerance at all.
		fixQuat twist = Q( V( 0.0f, 0.0f, 1.0f ), 0.75f );
		CHECK( Near( fixGetTwistAngle( twist ), FIX( 0.75f ), 256 ) );
		CHECK( fixGetSwingAngle( twist ) == 0 );

		fixQuat swing = Q( V( 1.0f, 0.0f, 0.0f ), 0.5f );
		CHECK( Near( fixGetSwingAngle( swing ), FIX( 0.5f ), 256 ) );
		CHECK( fixGetTwistAngle( swing ) == 0 );

		// Polarity: negating a quaternion is the same rotation, and the twist accounts
		// for it rather than wrapping past pi.
		CHECK( fixGetTwistAngle( fixNegateQuat( twist ) ) == fixGetTwistAngle( twist ) );
	}

	// Normalization, including the degenerate input.
	{
		fixQuat unnormalized = { V( 2.0f, 0.0f, 0.0f ), FIX( 0.0f ) };
		fixQuat n = fixNormalizeQuat( unnormalized );
		CHECK( fixIsNormalizedQuat( n ) );
		CHECK( VecNear( n.v, fixVec3_axisX, 2 ) );

		fixQuat zero = { fixVec3_zero, 0 };
		CHECK( QuatEq( fixNormalizeQuat( zero ), fixQuat_identity ) );
	}

	// fixNLerp: the endpoints, the midpoint, and the polarity flip that keeps a blend on
	// the short arc.
	{
		fixQuat q0 = fixQuat_identity;
		fixQuat q1 = Q( V( 0.0f, 0.0f, 1.0f ), 1.0f );
		CHECK( fixDotQuat( fixNLerp( q0, q1, 0 ), q0 ) > FIX_ONE - 8 );
		CHECK( fixDotQuat( fixNLerp( q0, q1, FIX_ONE ), q1 ) > FIX_ONE - 8 );
		CHECK( fixIsNormalizedQuat( fixNLerp( q0, q1, FIX_HALF ) ) );

		// Blending against the negated form must land on the same rotation, not the long
		// way round: that is what the polarity test inside fixNLerp is for.
		fixQuat blended = fixNLerp( q0, q1, FIX_HALF );
		fixQuat flipped = fixNLerp( q0, fixNegateQuat( q1 ), FIX_HALF );
		CHECK( fixAbs( fixDotQuat( blended, flipped ) ) > FIX_ONE - 32 );
	}

	// fixComputeQuatBetweenUnitVectors, on both arms: the ordinary case and the
	// nearly-anti-parallel fallback the branch exists for.
	{
		fixVec3 from = fixVec3_axisX;
		fixVec3 to = fixVec3_axisY;
		fixQuat q = fixComputeQuatBetweenUnitVectors( from, to );
		CHECK( fixIsNormalizedQuat( q ) );
		CHECK( VecNear( fixRotateVector( q, from ), to, 8 ) );

		fixQuat same = fixComputeQuatBetweenUnitVectors( from, from );
		CHECK( VecNear( fixRotateVector( same, from ), from, 8 ) );

		fixVec3 opposite = fixVecNeg( fixVec3_axisX );
		fixQuat flip = fixComputeQuatBetweenUnitVectors( from, opposite );
		CHECK( fixIsNormalizedQuat( flip ) );
		CHECK( VecNear( fixRotateVector( flip, from ), opposite, 16 ) );

		// The other arm of the anti-parallel fallback, where |v1.x| <= 0.5.
		fixQuat flipZ = fixComputeQuatBetweenUnitVectors( fixVec3_axisZ, fixVecNeg( fixVec3_axisZ ) );
		CHECK( fixIsNormalizedQuat( flipZ ) );
		CHECK( VecNear( fixRotateVector( flipZ, fixVec3_axisZ ), fixVecNeg( fixVec3_axisZ ), 16 ) );
	}

	// The structured sweep.
	for ( int i = 0; i < 48; i++ )
	{
		fixQuat q1 = Q( V( 1.0f + (float)i, 2.0f, -1.0f ), 0.13f * (float)i );
		fixQuat q2 = Q( V( -1.0f, 0.5f + (float)i, 3.0f ), -0.07f * (float)i );

		mixQuat( fixMulQuat( q1, q2 ) );
		mixQuat( fixInvMulQuat( q1, q2 ) );
		mixQuat( fixConjugate( q1 ) );
		mixQuat( fixNegateQuat( q2 ) );
		mixQuat( fixNormalizeQuat( q1 ) );
		mixQuat( fixNLerp( q1, q2, FIX( 0.25f ) ) );
		mix( fixDotQuat( q1, q2 ) );
		mix( fixGetQuatAngle( q1 ) );
		mix( fixGetTwistAngle( q2 ) );
		mix( fixGetSwingAngle( q2 ) );
		mixVec( fixRotateVector( q1, V( 1.0f, -2.0f, 0.5f ) ) );
		mixVec( fixInvRotateVector( q2, V( 1.0f, -2.0f, 0.5f ) ) );
		mixQuat( fixComputeQuatBetweenUnitVectors( fixNormalize( V( 1.0f + (float)i, 1.0f, 0.0f ) ),
												   fixNormalize( V( 0.0f, 1.0f, 1.0f + (float)i ) ) ) );
	}

	if ( fails == before ) section( "quaternion arithmetic, axis/angle, twist/swing and blending" );
}

// ================================================================================
// Matrices.
// ================================================================================

static void testMatrices( void )
{
	int before = fails;

	fixMatrix3 m = { V( 1.0f, 2.0f, 3.0f ), V( 4.0f, 5.0f, 6.0f ), V( 7.0f, 8.0f, 10.0f ) };
	fixMatrix3 n = { V( 0.5f, -1.0f, 2.0f ), V( 1.0f, 0.0f, -0.5f ), V( -2.0f, 3.0f, 1.0f ) };

	// Exact: integer adds, subtracts and negations, componentwise.
	CHECK( MatEq( fixAddMM( m, n ), ( (fixMatrix3){ V( 1.5f, 1.0f, 5.0f ), V( 5.0f, 5.0f, 5.5f ), V( 5.0f, 11.0f, 11.0f ) } ) ) );
	CHECK( MatEq( fixSubMM( m, n ), ( (fixMatrix3){ V( 0.5f, 3.0f, 1.0f ), V( 3.0f, 5.0f, 6.5f ), V( 9.0f, 5.0f, 9.0f ) } ) ) );
	CHECK( MatEq( fixAddMM( m, fixNegateMat3( m ) ), fixMat3_zero ) );
	CHECK( MatEq( fixSubMM( m, m ), fixMat3_zero ) );
	CHECK( MatEq( fixNegateMat3( fixNegateMat3( m ) ), m ) );
	CHECK( MatEq( fixMulSM( FIX( 2.0f ), m ),
				  ( (fixMatrix3){ V( 2.0f, 4.0f, 6.0f ), V( 8.0f, 10.0f, 12.0f ), V( 14.0f, 16.0f, 20.0f ) } ) ) );
	CHECK( MatEq( fixMulSM( 0, m ), fixMat3_zero ) );
	CHECK( MatEq( fixAbsMatrix3( fixNegateMat3( m ) ), m ) );

	// Transpose is an exact permutation, so it is its own inverse.
	CHECK( MatEq( fixTranspose( fixTranspose( m ) ), m ) );
	CHECK( fixTranspose( m ).cx.y == m.cy.x );
	CHECK( MatEq( fixTranspose( fixMat3_identity ), fixMat3_identity ) );

	// Matrix times vector, and the identity.
	CHECK( VecEq( fixMulMV( fixMat3_identity, V( 1.5f, -2.0f, 3.0f ) ), V( 1.5f, -2.0f, 3.0f ) ) );
	CHECK( VecEq( fixMulMV( m, fixVec3_axisX ), m.cx ) );
	CHECK( VecEq( fixMulMV( m, fixVec3_axisY ), m.cy ) );
	CHECK( VecEq( fixMulMV( m, fixVec3_axisZ ), m.cz ) );
	CHECK( VecEq( fixMulMV( fixMat3_zero, V( 1.0f, 2.0f, 3.0f ) ), fixVec3_zero ) );

	// Matrix product, and the identity on both sides.
	CHECK( MatEq( fixMulMM( fixMat3_identity, m ), m ) );
	CHECK( MatEq( fixMulMM( m, fixMat3_identity ), m ) );
	CHECK( MatEq( fixMulMM( fixMat3_zero, m ), fixMat3_zero ) );

	// Determinant. This one is chosen so the answer is a small exact integer: the columns
	// are (1,2,3), (4,5,6), (7,8,10), whose determinant is -3.
	CHECK( fixDet( m ) == FIX( -3.0f ) );
	CHECK( fixDet( fixMat3_identity ) == FIX_ONE );
	CHECK( fixDet( fixMat3_zero ) == 0 );

	// The 128-bit cofactor helper and the shifted divide underneath the inverse.
	CHECK( fixInt128Eq( fixCofactor128( FIX( 2.0f ), FIX( 3.0f ), FIX( 1.0f ), FIX( 1.0f ) ),
						fixInt128Sub( fixInt128MulI64( FIX( 2.0f ), FIX( 3.0f ) ),
									  fixInt128MulI64( FIX( 1.0f ), FIX( 1.0f ) ) ) ) );
	CHECK( fixDivShifted( fixInt128FromI64( 6 ), 16, fixInt128FromI64( 3 ) ) == 2 * ( (int64_t)1 << 16 ) );
	CHECK( fixDivShifted( fixInt128FromI64( -6 ), 16, fixInt128FromI64( 3 ) ) == -2 * ( (int64_t)1 << 16 ) );

	// Inverse. inv(m) * m is the identity to within the rounding of nine divisions.
	{
		CHECK( MatEq( fixInvertMatrix( fixMat3_identity ), fixMat3_identity ) );

		fixMatrix3 inverse = fixInvertMatrix( m );
		fixMatrix3 product = fixMulMM( inverse, m );
		CHECK( VecNear( product.cx, fixMat3_identity.cx, 16 ) ); // measured 2, 6, 10
		CHECK( VecNear( product.cy, fixMat3_identity.cy, 16 ) );
		CHECK( VecNear( product.cz, fixMat3_identity.cz, 16 ) );

		// A singular matrix has no inverse, and the documented answer is the zero matrix
		// rather than a trap or a garbage value.
		fixMatrix3 singular = { V( 1.0f, 2.0f, 3.0f ), V( 2.0f, 4.0f, 6.0f ), V( 3.0f, 6.0f, 9.0f ) };
		CHECK( MatEq( fixInvertMatrix( singular ), fixMat3_zero ) );

		// fixInvertT is the inverse transposed, which is the inverse itself for the
		// symmetric matrices it is used with.
		CHECK( MatEq( fixInvertT( m ), fixTranspose( fixInvertMatrix( m ) ) ) );
		fixMatrix3 symmetric = { V( 4.0f, 1.0f, 0.5f ), V( 1.0f, 3.0f, 0.25f ), V( 0.5f, 0.25f, 2.0f ) };
		CHECK( MatEq( fixInvertT( symmetric ), fixInvertMatrix( symmetric ) ) );

		// The wide path, where the cofactors leave 64 bits and the whole calculation moves
		// to 256. Reached only by entries far outside ordinary simulation scale, so it is
		// dark code unless named. Every true entry of this inverse is of order 1e-10 to
		// 1e-12, which is far below one Q48.16 quantum, so the exact truncated answer is
		// the zero matrix, and saying so is the contract. inverse_envelope_test.c is where
		// that boundary is walked properly; this case is here so the hash covers the wide arm.
		fixMatrix3 huge = { V( 1e9f, 2e9f, 3e8f ), V( 4e8f, 5e9f, 6e9f ), V( 7e9f, 8e8f, 1e9f ) };
		fixMatrix3 hugeInverse = fixInvertMatrix( huge );
		CHECK( fixIsValidMatrix3( hugeInverse ) );
		CHECK( MatEq( hugeInverse, fixMat3_zero ) );
		mixMat( hugeInverse );
	}

	// Solve. inv(m) * a computed directly, checked by putting the answer back through m.
	{
		fixVec3 target = V( 1.0f, -2.0f, 0.5f );
		fixVec3 solution = fixSolve3( m, target );
		CHECK( VecNear( fixMulMV( m, solution ), target, 4 ) ); // measured 2
		CHECK( VecEq( fixSolve3( fixMat3_identity, target ), target ) );

		// Singular: the documented answer is the zero vector.
		fixMatrix3 singular = { V( 1.0f, 2.0f, 3.0f ), V( 2.0f, 4.0f, 6.0f ), V( 3.0f, 6.0f, 9.0f ) };
		CHECK( VecEq( fixSolve3( singular, target ), fixVec3_zero ) );

		// And the wide path, which solves directly at 256 bits rather than inverting and
		// multiplying: the inverse of a matrix this large is the zero matrix, so the old
		// fallback multiplied through it and returned zero for every target. The direct
		// solve keeps the ratio whole and gets a nonzero answer where one exists.
		fixMatrix3 huge = { V( 1e9f, 2e9f, 3e8f ), V( 4e8f, 5e9f, 6e9f ), V( 7e9f, 8e8f, 1e9f ) };
		mixVec( fixSolve3( huge, target ) );
	}

	// fixMakeMatrixFromQuat and fixMakeQuatFromMatrix are inverses of each other, and the
	// matrix must rotate a vector the same way the quaternion does.
	{
		CHECK( MatEq( fixMakeMatrixFromQuat( fixQuat_identity ), fixMat3_identity ) );

		fixQuat q = Q( V( 1.0f, 2.0f, -0.5f ), 0.9f );
		fixMatrix3 r = fixMakeMatrixFromQuat( q );
		fixVec3 v = V( 0.75f, -1.5f, 2.0f );
		CHECK( VecNear( fixMulMV( r, v ), fixRotateVector( q, v ), 4 ) ); // measured 2

		fixQuat back = fixMakeQuatFromMatrix( &r );
		CHECK( fixIsNormalizedQuat( back ) );
		CHECK( fixAbs( fixDotQuat( back, q ) ) > FIX_ONE - 64 );

		// fixMakeQuatFromMatrix has four arms, selected by the trace and by which
		// diagonal entry is largest. A rotation of pi about each axis drives the trace
		// negative and picks a different arm each time; without these three the function
		// is only ever tested on its first branch.
		fixMatrix3 aboutX = fixMakeMatrixFromQuat( Q( V( 1.0f, 0.0f, 0.0f ), 3.14159265f ) );
		fixMatrix3 aboutY = fixMakeMatrixFromQuat( Q( V( 0.0f, 1.0f, 0.0f ), 3.14159265f ) );
		fixMatrix3 aboutZ = fixMakeMatrixFromQuat( Q( V( 0.0f, 0.0f, 1.0f ), 3.14159265f ) );
		CHECK( fixIsNormalizedQuat( fixMakeQuatFromMatrix( &aboutX ) ) );
		CHECK( fixIsNormalizedQuat( fixMakeQuatFromMatrix( &aboutY ) ) );
		CHECK( fixIsNormalizedQuat( fixMakeQuatFromMatrix( &aboutZ ) ) );
		CHECK( VecNear( fixMulMV( aboutX, fixVec3_axisY ), fixVecNeg( fixVec3_axisY ), 16 ) );
		CHECK( VecNear( fixMulMV( aboutY, fixVec3_axisZ ), fixVecNeg( fixVec3_axisZ ), 16 ) );
		CHECK( VecNear( fixMulMV( aboutZ, fixVec3_axisX ), fixVecNeg( fixVec3_axisX ), 16 ) );
		mixQuat( fixMakeQuatFromMatrix( &aboutX ) );
		mixQuat( fixMakeQuatFromMatrix( &aboutY ) );
		mixQuat( fixMakeQuatFromMatrix( &aboutZ ) );
	}

	// The structured sweep.
	for ( int i = -8; i <= 8; i++ )
	{
		fixMatrix3 s = { V( 1.0f + 0.25f * (float)i, 0.5f * (float)i, -1.0f ),
						 V( 2.0f, 3.0f - 0.5f * (float)i, 0.25f * (float)i ),
						 V( -0.5f * (float)i, 1.0f, 4.0f + 0.125f * (float)i ) };

		mixMat( fixAddMM( s, m ) );
		mixMat( fixSubMM( s, n ) );
		mixMat( fixMulSM( FIX( 0.75f ), s ) );
		mixMat( fixNegateMat3( s ) );
		mixMat( fixTranspose( s ) );
		mixMat( fixAbsMatrix3( s ) );
		mixMat( fixMulMM( s, n ) );
		mixMat( fixInvertMatrix( s ) );
		mixMat( fixInvertT( s ) );
		mixVec( fixMulMV( s, V( 1.0f, -0.5f, 2.0f ) ) );
		mixVec( fixSolve3( s, V( 1.0f, -0.5f, 2.0f ) ) );
		mix( fixDet( s ) );
	}

	if ( fails == before ) section( "matrix arithmetic, determinant, inverse, solve and quaternion conversion" );
}

// ================================================================================
// Transforms and world positions.
// ================================================================================

static void testTransforms( void )
{
	int before = fails;

	fixTransform a = { V( 1.0f, 2.0f, 3.0f ), Q( V( 0.0f, 0.0f, 1.0f ), 0.5f ) };
	fixTransform b = { V( -0.5f, 0.25f, 1.0f ), Q( V( 1.0f, 1.0f, 0.0f ), -0.3f ) };
	fixVec3 point = V( 0.75f, -1.25f, 2.5f );

	// The identity transform does nothing, exactly.
	CHECK( VecEq( fixTransformPoint( fixTransform_identity, point ), point ) );
	CHECK( VecEq( fixInvTransformPoint( fixTransform_identity, point ), point ) );

	// Transform and inverse transform are inverses.
	CHECK( VecNear( fixInvTransformPoint( a, fixTransformPoint( a, point ) ), point, 4 ) ); // measured 2

	// Composition: applying a*b is applying b then a.
	{
		fixTransform composed = fixMulTransforms( a, b );
		CHECK( VecNear( fixTransformPoint( composed, point ), fixTransformPoint( a, fixTransformPoint( b, point ) ), 16 ) );

		// inv(a) * b, the narrow-phase boundary, is what takes a point from b's frame to
		// a's frame.
		fixTransform relative = fixInvMulTransforms( a, b );
		CHECK( VecNear( fixTransformPoint( relative, point ), fixInvTransformPoint( a, fixTransformPoint( b, point ) ), 16 ) );

		// inv(a) * a is the identity.
		fixTransform identity = fixInvMulTransforms( a, a );
		CHECK( VecNear( identity.p, fixVec3_zero, 8 ) );
		CHECK( fixAbs( fixDotQuat( identity.q, fixQuat_identity ) ) > FIX_ONE - 8 );
	}

	// Inversion.
	{
		fixTransform inverse = fixInvertTransform( a );
		CHECK( VecNear( fixTransformPoint( inverse, fixTransformPoint( a, point ) ), point, 16 ) );
		CHECK( QuatEq( fixInvertTransform( fixTransform_identity ).q, fixQuat_identity ) );
		CHECK( VecEq( fixInvertTransform( fixTransform_identity ).p, fixVec3_zero ) );
	}

	// Validity predicates. Fixed point has no NaN, so the only unrepresentable value is
	// INT64_MIN, reserved so that negation cannot overflow.
	CHECK( fixIsValidFixed( 0 ) );
	CHECK( fixIsValidFixed( FIX_MAX ) );
	CHECK( fixIsValidFixed( FIX_MIN ) );
	CHECK( fixIsValidFixed( INT64_MIN ) == false );
	CHECK( fixIsValidVec3( V( 1.0f, -2.0f, 3.0f ) ) );
	CHECK( fixIsValidVec3( ( (fixVec3){ INT64_MIN, 0, 0 } ) ) == false );
	CHECK( fixIsValidVec3( ( (fixVec3){ 0, INT64_MIN, 0 } ) ) == false );
	CHECK( fixIsValidVec3( ( (fixVec3){ 0, 0, INT64_MIN } ) ) == false );
	CHECK( fixIsValidQuat( fixQuat_identity ) );
	CHECK( fixIsValidQuat( ( (fixQuat){ V( 1.0f, 1.0f, 1.0f ), FIX_ONE } ) ) == false ); // representable, not normalized
	CHECK( fixIsValidQuat( ( (fixQuat){ { INT64_MIN, 0, 0 }, FIX_ONE } ) ) == false );
	CHECK( fixIsValidQuat( ( (fixQuat){ fixVec3_zero, INT64_MIN } ) ) == false );
	CHECK( fixIsValidTransform( fixTransform_identity ) );
	CHECK( fixIsValidTransform( ( (fixTransform){ { INT64_MIN, 0, 0 }, fixQuat_identity } ) ) == false );
	CHECK( fixIsValidTransform( ( (fixTransform){ fixVec3_zero, { V( 1.0f, 1.0f, 1.0f ), FIX_ONE } } ) ) == false );
	CHECK( fixIsValidMatrix3( fixMat3_identity ) );
	CHECK( fixIsValidMatrix3( ( (fixMatrix3){ { INT64_MIN, 0, 0 }, fixVec3_zero, fixVec3_zero } ) ) == false );
	CHECK( fixIsValidMatrix3( ( (fixMatrix3){ fixVec3_zero, { 0, INT64_MIN, 0 }, fixVec3_zero } ) ) == false );
	CHECK( fixIsValidMatrix3( ( (fixMatrix3){ fixVec3_zero, fixVec3_zero, { 0, 0, INT64_MIN } } ) ) == false );

	// World positions. In fixed point a world position is the same representation as a
	// local vector, so these are exact conversions and exact integer arithmetic -- which
	// is the whole reason the narrow build needs no directed rounding.
	{
		fixPos p = fixToPos( point );
		CHECK( VecEq( fixToVec3( p ), point ) );
		CHECK( VecEq( fixSubPos( fixOffsetPos( p, V( 1.0f, 2.0f, 3.0f ) ), p ), V( 1.0f, 2.0f, 3.0f ) ) );
		CHECK( VecEq( fixOffsetPos( p, fixVec3_zero ), p ) );
		CHECK( VecEq( fixSubPos( p, p ), fixVec3_zero ) );

		// The world-coordinate narrowing helpers are the identity in fixed point, kept
		// for compatibility with the float era's large-world mode. Named here so the fact
		// that they are the identity is a checked statement rather than a comment.
		CHECK( fixRoundDownFloat( FIX( 1.5f ) ) == FIX( 1.5f ) );
		CHECK( fixRoundUpFloat( FIX( 1.5f ) ) == FIX( 1.5f ) );
		CHECK( fixRoundDownFloat( FIX_MIN ) == FIX_MIN );
		CHECK( fixRoundUpFloat( FIX_MAX ) == FIX_MAX );

		// Position interpolation: exact at the endpoints, and the midpoint of a
		// representable pair is representable.
		fixPos p0 = fixToPos( V( 0.0f, 0.0f, 0.0f ) );
		fixPos p1 = fixToPos( V( 4.0f, -8.0f, 2.0f ) );
		CHECK( VecEq( fixLerpPosition( p0, p1, 0 ), p0 ) );
		CHECK( VecEq( fixLerpPosition( p0, p1, FIX_ONE ), p1 ) );
		CHECK( VecEq( fixLerpPosition( p0, p1, FIX_HALF ), fixToPos( V( 2.0f, -4.0f, 1.0f ) ) ) );
	}

	// World transforms. Same representation again, so promotion is lossless.
	{
		fixWorldTransform w = fixMakeWorldTransform( a );
		CHECK( VecEq( fixToVec3( w.p ), a.p ) );
		CHECK( QuatEq( w.q, a.q ) );

		CHECK( VecNear( fixToVec3( fixTransformWorldPoint( w, point ) ), fixTransformPoint( a, point ), 2 ) );
		CHECK( VecNear( fixInvTransformWorldPoint( w, fixToPos( point ) ), fixInvTransformPoint( a, point ), 2 ) );
		CHECK( VecNear( fixInvTransformWorldPoint( w, fixTransformWorldPoint( w, point ) ), point, 8 ) );

		fixWorldTransform w2 = fixMulWorldTransforms( w, b );
		CHECK( VecNear( fixToVec3( w2.p ), fixMulTransforms( a, b ).p, 2 ) );
		CHECK( QuatEq( w2.q, fixMulTransforms( a, b ).q ) );

		fixTransform relative = fixInvMulWorldTransforms( w, fixMakeWorldTransform( b ) );
		CHECK( VecNear( relative.p, fixInvMulTransforms( a, b ).p, 2 ) );
		CHECK( QuatEq( relative.q, fixInvMulTransforms( a, b ).q ) );

		// Rebasing onto a base position is an exact subtraction; the rotation is untouched.
		fixTransform shifted = fixToRelativeTransform( w, fixToPos( V( 1.0f, 1.0f, 1.0f ) ) );
		CHECK( VecEq( shifted.p, fixVecSub( a.p, V( 1.0f, 1.0f, 1.0f ) ) ) );
		CHECK( QuatEq( shifted.q, a.q ) );
		CHECK( VecEq( fixToRelativeTransform( w, w.p ).p, fixVec3_zero ) );
	}

	// The structured sweep.
	for ( int i = -10; i <= 10; i++ )
	{
		fixTransform s = { V( 0.5f * (float)i, -0.25f * (float)i, 1.0f + 0.125f * (float)i ),
						   Q( V( 1.0f, 0.5f * (float)i, -1.0f ), 0.11f * (float)i ) };
		fixWorldTransform ws = fixMakeWorldTransform( s );

		mixTransform( fixMulTransforms( s, a ) );
		mixTransform( fixInvMulTransforms( s, b ) );
		mixTransform( fixInvertTransform( s ) );
		mixVec( fixTransformPoint( s, point ) );
		mixVec( fixInvTransformPoint( s, point ) );
		mixVec( fixToVec3( fixTransformWorldPoint( ws, point ) ) );
		mixVec( fixInvTransformWorldPoint( ws, fixToPos( point ) ) );
		mixTransform( fixInvMulWorldTransforms( ws, fixMakeWorldTransform( b ) ) );
		mixVec( fixToVec3( fixMulWorldTransforms( ws, b ).p ) );
		mixTransform( fixToRelativeTransform( ws, fixToPos( point ) ) );
		mixVec( fixLerpPosition( fixToPos( s.p ), fixToPos( point ), FIX( 0.25f ) ) );
		mixVec( fixSubPos( fixToPos( s.p ), fixToPos( point ) ) );
		mixVec( fixOffsetPos( fixToPos( s.p ), point ) );
	}

	if ( fails == before ) section( "transforms, world positions and the validity predicates" );
}

// ================================================================================
// The 128-bit seam vocabulary, and the wide comparisons.
//
// These are the PUBLIC wrappers -- fixInt128Add and friends -- which are a different
// thing from the fixEmu* layer test/int128_test.c checks against native. On a native
// build they are the compiler's operators; on an emulated build they forward to fixEmu*.
// Either way they must agree with the emulated pair, which is itself held to native, so
// checking them here pins both arms transitively.
// ================================================================================

static void testSeamVocabulary( void )
{
	int before = fails;

	static const uint64_t lanes[] = {
		0, 1, 2, 0xFFFFFFFFFFFFFFFFULL, 0x8000000000000000ULL, 0x7FFFFFFFFFFFFFFFULL, 0x0123456789ABCDEFULL,
	};
	const int laneCount = (int)( sizeof( lanes ) / sizeof( lanes[0] ) );

	for ( int ah = 0; ah < laneCount; ah++ )
	{
		for ( int al = 0; al < laneCount; al++ )
		{
			for ( int bh = 0; bh < laneCount; bh++ )
			{
				for ( int bl = 0; bl < laneCount; bl++ )
				{
					fixInt128 sa = fixInt128Make( lanes[ah], lanes[al] );
					fixInt128 sb = fixInt128Make( lanes[bh], lanes[bl] );
					fixEmuInt128 ea = fixEmuInt128Make( lanes[ah], lanes[al] );
					fixEmuInt128 eb = fixEmuInt128Make( lanes[bh], lanes[bl] );

					CHECK( fixInt128Lo( fixInt128Add( sa, sb ) ) == fixEmuInt128Lo( fixEmuInt128Add( ea, eb ) ) );
					CHECK( fixInt128Hi( fixInt128Add( sa, sb ) ) == fixEmuInt128Hi( fixEmuInt128Add( ea, eb ) ) );
					CHECK( fixInt128Lo( fixInt128Sub( sa, sb ) ) == fixEmuInt128Lo( fixEmuInt128Sub( ea, eb ) ) );
					CHECK( fixInt128Lo( fixInt128Mul( sa, sb ) ) == fixEmuInt128Lo( fixEmuInt128Mul( ea, eb ) ) );
					CHECK( fixInt128Hi( fixInt128Mul( sa, sb ) ) == fixEmuInt128Hi( fixEmuInt128Mul( ea, eb ) ) );
					CHECK( fixInt128Le( sa, sb ) == fixEmuInt128Le( ea, eb ) );
					CHECK( fixInt128Ge( sa, sb ) == fixEmuInt128Ge( ea, eb ) );
					CHECK( fixInt128IsNegative( sa ) == fixEmuInt128IsNegative( ea ) );

					fixUInt128 ua = fixUInt128Make( lanes[ah], lanes[al] );
					fixUInt128 ub = fixUInt128Make( lanes[bh], lanes[bl] );
					fixEmuUInt128 va = fixEmuUInt128Make( lanes[ah], lanes[al] );
					fixEmuUInt128 vb = fixEmuUInt128Make( lanes[bh], lanes[bl] );

					CHECK( fixUInt128Lo( fixUInt128Sub( ua, ub ) ) == fixEmuUInt128Lo( fixEmuUInt128Sub( va, vb ) ) );
					CHECK( fixUInt128Hi( fixUInt128Mul( ua, ub ) ) == fixEmuUInt128Hi( fixEmuUInt128Mul( va, vb ) ) );
					CHECK( fixUInt128Lo( fixUInt128Neg( ua ) ) == fixEmuUInt128Lo( fixEmuUInt128Neg( va ) ) );
					CHECK( fixUInt128Hi( fixUInt128And( ua, ub ) ) == fixEmuUInt128Hi( fixEmuUInt128And( va, vb ) ) );
					CHECK( fixUInt128Lo( fixUInt128Or( ua, ub ) ) == fixEmuUInt128Lo( fixEmuUInt128Or( va, vb ) ) );
					CHECK( fixUInt128Hi( fixUInt128Xor( ua, ub ) ) == fixEmuUInt128Hi( fixEmuUInt128Xor( va, vb ) ) );
					CHECK( fixUInt128Lo( fixUInt128Not( ua ) ) == fixEmuUInt128Lo( fixEmuUInt128Not( va ) ) );
					CHECK( fixUInt128Eq( ua, ub ) == fixEmuUInt128Eq( va, vb ) );
					CHECK( fixUInt128Lt( ua, ub ) == fixEmuUInt128Lt( va, vb ) );
					CHECK( fixUInt128Gt( ua, ub ) == fixEmuUInt128Gt( va, vb ) );
					CHECK( fixUInt128Le( ua, ub ) == fixEmuUInt128Le( va, vb ) );

					mix( (int64_t)fixInt128Lo( fixInt128Mul( sa, sb ) ) );
					mix( (int64_t)fixUInt128Hi( fixUInt128Sub( ua, ub ) ) );
				}
			}
		}
	}

	// The wide comparisons, which exist because `<` does not compile on the emulated
	// representation and a consumer needs a name for it.
	{
		fixedWide_t low = fixWideFromFixed( -5 );
		fixedWide_t high = fixWideFromFixed( 5 );
		CHECK( fixWideLt( low, high ) );
		CHECK( fixWideGt( high, low ) );
		CHECK( fixWideGe( high, high ) );
		CHECK( fixWideGe( high, low ) );
		CHECK( fixWideLe( low, low ) );
		CHECK( fixWideLt( high, low ) == false );
		CHECK( fixWideGt( low, high ) == false );
		CHECK( fixWideGe( low, high ) == false );
		CHECK( fixWideEq( fixWideNeg( fixWideNeg( high ) ), high ) );
	}

	// fixISqrt128High, the exact integer square root under fixSqrt and fixLength. Both
	// arms: the 64-bit seeded path and the rare 128-bit shift-subtract path.
	{
		CHECK( fixISqrt128High( 0, 0 ) == 0 );
		CHECK( fixISqrt128High( 0, 1 ) == 1 );
		CHECK( fixISqrt128High( 0, 4 ) == 2 );
		CHECK( fixISqrt128High( 0, 8 ) == 2 ); // floor, not nearest
		CHECK( fixISqrt128High( 0, 0xFFFFFFFFFFFFFFFFULL ) == 0xFFFFFFFFu );
		CHECK( fixISqrt128High( 1, 0 ) == 0x100000000ULL );  // sqrt( 2^64 )
		CHECK( fixISqrt128High( 4, 0 ) == 0x200000000ULL );  // sqrt( 2^66 )

		// Exactness across both arms: r*r <= n < (r+1)*(r+1) for every probe.
		for ( int i = 0; i < 200; i++ )
		{
			uint64_t hi = ( i < 100 ) ? 0 : nextRandom();
			uint64_t lo = nextRandom();
			uint64_t r = fixISqrt128High( hi, lo );
			fixUInt128 n = fixUInt128Make( hi, lo );
			CHECK( fixUInt128Le( fixUInt128MulU64( r, r ), n ) );
			CHECK( fixUInt128Gt( fixUInt128MulU64( r + 1, r + 1 ), n ) );
			mix( (int64_t)r );
		}
	}

	if ( fails == before ) section( "the 128-bit seam vocabulary, wide comparisons and the integer square root" );
}

// ================================================================================
// A randomized pass over the whole layer, feeding the hash.
//
// The structured sweeps above walk regular grids, which is exactly the input shape least
// likely to land on a branch boundary. This one draws from the deterministic stream
// instead, so the rarer arms -- a degenerate quaternion, a near-singular matrix, a
// normalization of something tiny -- get traffic. It asserts the invariants that hold for
// every input and leaves the rest to the frozen hash.
// ================================================================================

static void testRandomizedPass( void )
{
	int before = fails;

	for ( int i = 0; i < 4000; i++ )
	{
		fixVec3 u = randomVec();
		fixVec3 w = randomVec();
		fixQuat q1 = randomQuat();
		fixQuat q2 = randomQuat();

		// Invariants that hold for every input, with no tolerance:
		// a cross product is perpendicular to both of its operands in exact arithmetic,
		// so the RAW dot of the two must have the sign of the rounding error only -- but
		// componentwise anticommutativity is exact, so that is what is asserted.
		CHECK( VecEq( fixCross( u, w ), fixVecNeg( fixCross( w, u ) ) ) );
		CHECK( fixDot( u, w ) == fixDot( w, u ) );
		CHECK( fixDotQuat( q1, q2 ) == fixDotQuat( q2, q1 ) );
		CHECK( VecEq( fixVecAbs( fixVecNeg( u ) ), fixVecAbs( u ) ) );
		CHECK( VecEq( fixVecMin( u, w ), fixVecMin( w, u ) ) );
		CHECK( VecEq( fixVecMax( u, w ), fixVecMax( w, u ) ) );
		CHECK( QuatEq( fixConjugate( fixConjugate( q1 ) ), q1 ) );
		CHECK( fixDistanceSquared( u, w ) == fixDistanceSquared( w, u ) );

		// A normalized quaternion stays normalized through composition.
		CHECK( fixIsNormalizedQuat( fixNormalizeQuat( fixMulQuat( q1, q2 ) ) ) );

		mixVec( fixCross( u, w ) );
		mixVec( fixNormalize( u ) );
		mixVec( fixPerp( fixNormalize( u ) ) );
		mixVec( fixRotateVector( q1, u ) );
		mixVec( fixInvRotateVector( q2, w ) );
		mixQuat( fixMulQuat( q1, q2 ) );
		mixQuat( fixInvMulQuat( q1, q2 ) );
		mixQuat( fixNLerp( q1, q2, FIX( 0.375f ) ) );
		mixMat( fixMakeMatrixFromQuat( q1 ) );
		mix( fixLength( u ) );
		mix( fixGetQuatAngle( q1 ) );
		mix( fixDot( u, w ) );
	}

	if ( fails == before ) section( "randomized pass over vectors, quaternions and matrices" );
}

// ================================================================================

int main( void )
{
	testScalarHelpers();
	testVectors();
	testQuaternions();
	testMatrices();
	testTransforms();
	testSeamVocabulary();
	testRandomizedPass();

	if ( EXPECTED_GEOMETRY_HASH == 0ULL )
	{
		printf( "geometry structured sweep hash = 0x%016llx (capture mode: not yet frozen)\n", (unsigned long long)fnv );
	}
	else if ( fnv != (uint64_t)EXPECTED_GEOMETRY_HASH )
	{
		printf( "FAIL geometry structured sweep hash = 0x%016llx, expected 0x%016llx\n", (unsigned long long)fnv,
				(unsigned long long)EXPECTED_GEOMETRY_HASH );
		fails++;
	}
	else
	{
		printf( "geometry structured sweep hash is unchanged across platforms\n" );
	}

	if ( fails )
	{
		printf( "FAILED: %d check(s)\n", fails );
		return 1;
	}

	return 0;
}
