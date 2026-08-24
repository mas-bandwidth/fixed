// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// The numeric claims USAGE.md makes, asserted.
//
// Documentation drifts. A guide that says fixDiv truncates toward zero, or that fixNarrow
// rounds half toward positive infinity, is making a claim about the code -- and a claim
// about the code that nothing checks is a claim that will eventually be wrong, quietly,
// in the one place a reader trusts. Every number, rounding rule and boundary value stated
// in USAGE.md that can be written as an expression is written as one here.
//
// This is deliberately NOT a general test of the library; the other suites do that. It is
// a test of the DOCUMENT. If a legitimate change makes one of these fail, the fix is to
// change the code or the prose together, which is exactly the coupling it exists to force.
//
// Each check carries the section of USAGE.md it comes from, so a failure points at the
// paragraph to correct rather than only at a line of C.

#include "fixed/fixed.h"
#include "fixed/fixed_math.h"
#include "fixed/fixed_quantize.h"
#include "fixed/fixed_time.h"
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include <stdint.h>
#include <stdio.h>

static int fails = 0;
#define CLAIM( section, cond )                                                                                                 \
	do                                                                                                                         \
	{                                                                                                                          \
		if ( !( cond ) )                                                                                                       \
		{                                                                                                                      \
			printf( "FAIL USAGE.md \"%s\" claims: %s\n", section, #cond );                                                     \
			fails++;                                                                                                           \
		}                                                                                                                      \
	} while ( 0 )

int main( void )
{
	// ---- "The families": fixed_t -------------------------------------------------
	CLAIM( "The families", FIX_FRACTION_BITS == 16 );
	CLAIM( "The families", FIX_ONE == 65536 );
	CLAIM( "The families", FIX_HALF == 32768 );
	CLAIM( "The families", FIX_EPSILON == 1 );
	CLAIM( "The families", FIX_MAX == INT64_MAX );
	CLAIM( "The families", FIX_MIN == -INT64_MAX );
	// "Range: about +/-1.4e14 units", "Resolution: 1/65536 everywhere"
	CLAIM( "The families", (double)FIX_MAX / (double)FIX_ONE > 1.4e14 );
	CLAIM( "The families", (double)FIX_MAX / (double)FIX_ONE < 1.5e14 );
	// The opening example
	CLAIM( "opening example", fixMul( FIX( 2.5 ), fixFromInt( 4 ) ) == FIX( 10.0 ) );

	// ---- "The families": fixed30_t -----------------------------------------------
	CLAIM( "The families", FIX30_FRACTION_BITS == 30 );
	CLAIM( "The families", FIX30_ONE == ( (int32_t)1 << 30 ) );
	CLAIM( "The families", FIX30_SHIFT == 14 );
	// "Domain [-2, 2)" -- values outside saturate to the domain bounds
	CLAIM( "The families", fix30FromFix( FIX( 4.0 ) ).raw == INT32_MAX );
	CLAIM( "The families", fix30FromFix( FIX( -4.0 ) ).raw == INT32_MIN );

	// ---- "The families": fixTime -------------------------------------------------
	CLAIM( "The families", FIX_TIME_FRACTION_BITS == 32 );
	// "sixty exact adds of a 1/60 s tick drift by at most 60 raw units"
	{
		fixTime tick = FIX_TIME( 1.0 / 60.0 );
		fixTime accumulated = 0;
		for ( int i = 0; i < 60; i++ )
		{
			accumulated += tick;
		}
		fixTime drift = accumulated - FIX_TIME_ONE;
		if ( drift < 0 ) drift = -drift;
		CLAIM( "The families", drift <= 60 );
	}

	// ---- "The families": the 128-bit seam ----------------------------------------
	CLAIM( "The families", FIX_HAS_INT128 == 1 );
	CLAIM( "The families", FIX_INT128_EMULATED == 0 || FIX_INT128_EMULATED == 1 );

	// ---- "Crossing between families": the conversion table -----------------------
	CLAIM( "Crossing between families", fixFromInt( 3 ) == FIX( 3.0 ) );
	// truncate toward zero
	CLAIM( "Crossing between families", fixTruncToInt( FIX( 1.75 ) ) == 1 );
	CLAIM( "Crossing between families", fixTruncToInt( FIX( -1.75 ) ) == -1 );
	// floor toward negative infinity
	CLAIM( "Crossing between families", fixFloorToInt( FIX( 1.75 ) ) == 1 );
	CLAIM( "Crossing between families", fixFloorToInt( FIX( -1.75 ) ) == -2 );
	// round to nearest, ties toward POSITIVE infinity
	CLAIM( "Crossing between families", fixRoundToInt( FIX( 1.5 ) ) == 2 );
	CLAIM( "Crossing between families", fixRoundToInt( FIX( -1.5 ) ) == -1 );
	CLAIM( "Crossing between families", fixFloor( FIX( 1.5 ) ) == FIX( 1.0 ) );
	CLAIM( "Crossing between families", fixFloor( FIX( -1.5 ) ) == FIX( -2.0 ) );
	CLAIM( "Crossing between families", fixCeil( FIX( 1.5 ) ) == FIX( 2.0 ) );
	CLAIM( "Crossing between families", fixCeil( FIX( -1.5 ) ) == FIX( -1.0 ) );
	// float and double to fixed: nearest, ties AWAY from zero
	CLAIM( "Crossing between families", fixFromFloat( 0.5f / 65536.0f ) == 1 );
	CLAIM( "Crossing between families", fixFromFloat( -0.5f / 65536.0f ) == -1 );
	CLAIM( "Crossing between families", fixFromDouble( 0.5 / 65536.0 ) == 1 );
	CLAIM( "Crossing between families", fixFromDouble( -0.5 / 65536.0 ) == -1 );
	// fixed to double is exact
	CLAIM( "Crossing between families", fixToDouble( FIX( 2.5 ) ) == 2.5 );
	CLAIM( "Crossing between families", fixToFloat( FIX( 2.5 ) ) == 2.5f );
	// Q2.30 round trip, and the nearest-ties-toward-positive-infinity rule on unpack
	CLAIM( "Crossing between families", fix30ToDouble( fix30FromFix( FIX( 0.5 ) ) ) == 0.5 );
	CLAIM( "Crossing between families", fix30FromDouble( 0.5 ).raw == FIX30_ONE / 2 );
	CLAIM( "Crossing between families", fixFromFix30( ( (fixed30_t){ ( 1 << 13 ) } ) ) == 1 );   // exactly half, up
	CLAIM( "Crossing between families", fixFromFix30( ( (fixed30_t){ -( 1 << 13 ) } ) ) == 0 );  // exactly half, up
	// time
	CLAIM( "Crossing between families", fixTimeFromFixed( FIX_ONE ) == FIX_TIME_ONE );
	CLAIM( "Crossing between families", fixTimeToFixed( FIX_TIME_ONE ) == FIX_ONE );
	CLAIM( "Crossing between families", fixTimeFromSeconds( 2.5 ) == ( (fixTime)5 << 31 ) );
	CLAIM( "Crossing between families", fixTimeToSeconds( FIX_TIME_ONE ) == 1.0 );
	// wide, and its saturation
	CLAIM( "Crossing between families", fixWideToFixed( fixWideFromFixed( INT64_MAX ) ) == INT64_MAX );
	CLAIM( "Crossing between families", fixWideToFixed( fixWideAdd( fixWideFromFixed( INT64_MAX ),
																   fixWideFromFixed( 1 ) ) ) == INT64_MAX );
	// positions are the same representation, so these are exact
	CLAIM( "Crossing between families", fixToVec3( fixToPos( fixVec3_axisY ) ).y == FIX_ONE );
	// the 128-bit conversions
	CLAIM( "Crossing between families", fixInt128ToI64( fixInt128FromI64( -7 ) ) == -7 );
	CLAIM( "Crossing between families", fixInt128IsNegative( fixInt128FromI64( -1 ) ) );
	CLAIM( "Crossing between families", fixInt128IsNegative( fixInt128FromU64( UINT64_MAX ) ) == false );

	// ---- "The two rounding rules, and why they differ" ---------------------------
	// fixQuantize: half AWAY from zero, symmetric about the origin
	CLAIM( "The two rounding rules", fixQuantize( 0.5, 1 ) == 1 );
	CLAIM( "The two rounding rules", fixQuantize( -0.5, 1 ) == -1 );
	CLAIM( "The two rounding rules", fixQuantize( 1.5, 1 ) == 2 );
	CLAIM( "The two rounding rules", fixQuantize( -1.5, 1 ) == -2 );
	// fixNarrow: half toward POSITIVE infinity, what the arithmetic shift gives
	CLAIM( "The two rounding rules", fixNarrow( 3, 1 ) == 2 );
	CLAIM( "The two rounding rules", fixNarrow( -3, 1 ) == -1 );
	// the three identities the guide states outright
	CLAIM( "The two rounding rules", fixFromFix30( ( (fixed30_t){ 12345 } ) ) == fixNarrow( 12345, FIX30_SHIFT ) );
	CLAIM( "The two rounding rules", fixFromDouble( 0.3 ) == fixQuantize( 0.3, FIX_ONE ) );
	CLAIM( "The two rounding rules", fixToDouble( 12345 ) == fixDequantize( 12345, FIX_ONE ) );

	// ---- "Scalar arithmetic": the worked examples --------------------------------
	CLAIM( "Scalar arithmetic", fixMul( FIX( 1.5 ), FIX( 2.5 ) ) == FIX( 3.75 ) );
	CLAIM( "Scalar arithmetic", fixDiv( FIX( 7.0 ), FIX( 2.0 ) ) == FIX( 3.5 ) );
	CLAIM( "Scalar arithmetic", fixDiv( FIX( 1.0 ), 0 ) == FIX_MAX );
	CLAIM( "Scalar arithmetic", fixDiv( FIX( -1.0 ), 0 ) == FIX_MIN );
	CLAIM( "Scalar arithmetic", fixDiv( 0, 0 ) == 0 );
	CLAIM( "Scalar arithmetic", fixSqrt( FIX( 144.0 ) ) == FIX( 12.0 ) );
	CLAIM( "Scalar arithmetic", fixSqrt( FIX( -1.0 ) ) == 0 ); // "negative -> 0"
	CLAIM( "Scalar arithmetic", fixAbs( FIX( -2.0 ) ) == FIX( 2.0 ) );
	CLAIM( "Scalar arithmetic", fixShiftLeft( -1, 4 ) == -16 );
	// "fixDiv ... truncated toward zero"
	CLAIM( "Scalar arithmetic", fixDiv( FIX( 1.0 ), FIX( 3.0 ) ) == 21845 );
	CLAIM( "Scalar arithmetic", fixDiv( FIX( -1.0 ), FIX( 3.0 ) ) == -21845 );

	// ---- "Transcendentals" --------------------------------------------------------
	CLAIM( "Transcendentals", fixLog2( 0 ) == INT64_MIN );  // "INT64_MIN for a <= 0"
	CLAIM( "Transcendentals", fixLog2( -FIX_ONE ) == INT64_MIN );
	CLAIM( "Transcendentals", fixPow( 0, FIX( 2.0 ) ) == 0 );      // "0 for base <= 0"
	CLAIM( "Transcendentals", fixPow( FIX( 3.0 ), 0 ) == FIX_ONE ); // "one for a zero exponent"
	CLAIM( "Transcendentals", fixExp2( FIX( 100.0 ) ) == INT64_MAX ); // "saturates at 2^47"
	CLAIM( "Transcendentals", fixExp2( FIX( -100.0 ) ) == 0 );        // "underflows to 0"
	CLAIM( "Transcendentals", fixLerp( 0, FIX_ONE, FIX_HALF ) == FIX_HALF );
	{
		fixCosSin cs = fixComputeCosSin( 0 );
		CLAIM( "Transcendentals", cs.cosine == FIX_ONE && cs.sine == 0 );
		CLAIM( "Transcendentals", fixCos( 0 ) == FIX_ONE && fixSin( 0 ) == 0 );
	}

	// ---- "Vectors" ---------------------------------------------------------------
	CLAIM( "Vectors", fixLength( ( (fixVec3){ FIX( 3.0 ), FIX( 4.0 ), 0 } ) ) == FIX( 5.0 ) );
	CLAIM( "Vectors", fixDot( fixVec3_axisX, fixVec3_axisX ) == FIX_ONE );
	CLAIM( "Vectors", fixDot( fixVec3_axisX, fixVec3_axisY ) == 0 );
	// "zero vector in, zero vector out"
	CLAIM( "Vectors", fixNormalize( fixVec3_zero ).x == 0 );
	// "fixSign ... -1 or 1 componentwise; 1 at zero, on purpose"
	CLAIM( "Vectors", fixSign( fixVec3_zero ).x == FIX_ONE );
	// "fixSafeScale ... keeps |scale| >= FIX_MIN_SCALE, sign preserved"
	CLAIM( "Vectors", FIX_MIN_SCALE == FIX( 0.01f ) );
	CLAIM( "Vectors", fixSafeScale( fixVec3_zero ).x == FIX_MIN_SCALE );
	CLAIM( "Vectors", fixSafeScale( ( (fixVec3){ FIX( -0.001 ), 0, 0 } ) ).x == -FIX_MIN_SCALE );
	// the two-step dot product: exact sign for a sub-resolution result
	{
		fixVec3 tiny = { 1, 0, 0 };
		CLAIM( "Vectors", fixDot( tiny, tiny ) == 0 );
		CLAIM( "Vectors", fixInt128Gt( fixDotRaw( tiny, tiny ), FIX_INT128_ZERO ) );
	}

	// ---- "Quaternions" and "Matrices" --------------------------------------------
	// "zero quaternion in, identity out"
	CLAIM( "Quaternions", fixNormalizeQuat( ( (fixQuat){ fixVec3_zero, 0 } ) ).s == FIX_ONE );
	// "zero vector for the identity"
	{
		fixed_t radians = FIX_ONE;
		fixVec3 axis = fixGetAxisAngle( &radians, fixQuat_identity );
		CLAIM( "Quaternions", axis.x == 0 && axis.y == 0 && axis.z == 0 && radians == 0 );
	}
	// "zero matrix for a singular input"
	{
		fixMatrix3 singular = { { FIX( 1.0 ), FIX( 2.0 ), FIX( 3.0 ) },
								{ FIX( 2.0 ), FIX( 4.0 ), FIX( 6.0 ) },
								{ FIX( 3.0 ), FIX( 6.0 ), FIX( 9.0 ) } };
		CLAIM( "Matrices", fixInvertMatrix( singular ).cx.x == 0 );
		CLAIM( "Matrices", fixDet( fixMat3_identity ) == FIX_ONE );
		CLAIM( "Matrices", fixDet( fixMat3_zero ) == 0 );
	}
	// "fixCofactor128( a, b, c, d ) is a*b - c*d", "fixDivShifted( n, shift, d ) is ( n << shift ) / d"
	CLAIM( "Matrices", fixInt128Eq( fixCofactor128( 3, 5, 2, 4 ), fixInt128FromI64( 7 ) ) );
	CLAIM( "Matrices", fixDivShifted( fixInt128FromI64( 6 ), 16, fixInt128FromI64( 3 ) ) == ( (int64_t)2 << 16 ) );
	// The representability boundary, written in the guide as two lines of code: 65,536 is
	// the largest entry with a nonzero inverse, and one quantum past it the answer is zero.
	{
		fixMatrix3 atLine = fixMat3_zero;
		atLine.cx.x = FIX( 65536.0 );
		atLine.cy.y = FIX( 65536.0 );
		atLine.cz.z = FIX( 65536.0 );
		CLAIM( "Matrices", fixInvertMatrix( atLine ).cx.x == 1 );

		fixMatrix3 pastLine = atLine;
		pastLine.cx.x += 1;
		pastLine.cy.y += 1;
		pastLine.cz.z += 1;
		CLAIM( "Matrices", fixInvertMatrix( pastLine ).cx.x == 0 );
	}

	// ---- "Transforms and world positions" ----------------------------------------
	// "fixRoundDownFloat and fixRoundUpFloat are the identity in fixed point"
	CLAIM( "Transforms", fixRoundDownFloat( FIX( 1.5 ) ) == FIX( 1.5 ) );
	CLAIM( "Transforms", fixRoundUpFloat( FIX( 1.5 ) ) == FIX( 1.5 ) );

	// ---- "Validity predicates" ----------------------------------------------------
	// "false only for INT64_MIN, reserved so negation cannot overflow"
	CLAIM( "Validity predicates", fixIsValidFixed( INT64_MIN ) == false );
	CLAIM( "Validity predicates", fixIsValidFixed( FIX_MAX ) );
	CLAIM( "Validity predicates", fixIsValidFixed( FIX_MIN ) );
	// "representable AND normalized"
	CLAIM( "Validity predicates", fixIsValidQuat( fixQuat_identity ) );
	CLAIM( "Validity predicates", fixIsValidQuat( ( (fixQuat){ fixVec3_one, FIX_ONE } ) ) == false );

	// ---- "Domain crossing" --------------------------------------------------------
	CLAIM( "Domain crossing", fixQuantize( 1.25, 1024 ) == 1280 );
	CLAIM( "Domain crossing", fixDequantize( 1280, 1024 ) == 1.25 );
	CLAIM( "Domain crossing", fixFits( 5, 0, 10 ) && fixFits( 11, 0, 10 ) == false );
	CLAIM( "Domain crossing", fixQuantizeClamped( 100.0, 1, 0, 10 ) == 10 );
	CLAIM( "Domain crossing", fixQuantizeClamped( -100.0, 1, 0, 10 ) == 0 );
	// "fixNarrow( fixWiden( v, n ), n ) is the identity"
	CLAIM( "Domain crossing", fixNarrow( fixWiden( -12345, 13 ), 13 ) == -12345 );
	CLAIM( "Domain crossing", fixNarrow( fixWiden( 987654, 20 ), 20 ) == 987654 );

	// ---- "Wide coordinates" -------------------------------------------------------
	{
		fixedWide_t base = fixInt128ShiftLeft( fixWideFromFixed( 1 ), 90 );
		fixedWide_t near = fixWideOffset( base, FIX( 3.25 ) );
		// "THE boundary operation: exact whenever it fits Q48.16"
		CLAIM( "Wide coordinates", fixWideSubToFixed( near, base ) == FIX( 3.25 ) );
		CLAIM( "Wide coordinates", fixWideLt( base, near ) && fixWideGt( near, base ) );
		CLAIM( "Wide coordinates", fixWideGe( base, base ) && fixWideLe( base, base ) );
		CLAIM( "Wide coordinates", fixWideEq( fixWideNeg( fixWideNeg( base ) ), base ) );
		// the reserved 128-bit minimum is not a coordinate
		CLAIM( "Wide coordinates", fixIsValidWideCoord( fixInt128Make( UINT64_C( 0x8000000000000000 ), 0 ) ) == false );
		CLAIM( "Wide coordinates", fixIsValidWideCoord( base ) );
	}

	// ---- "The 128-bit vocabulary" -------------------------------------------------
	// "Shr is ARITHMETIC"
	CLAIM( "The 128-bit vocabulary", fixInt128Eq( fixInt128Shr( fixInt128FromI64( -8 ), 2 ), fixInt128FromI64( -2 ) ) );
	CLAIM( "The 128-bit vocabulary", fixInt128Eq( fixInt128MulI64( 3, -5 ), fixInt128FromI64( -15 ) ) );
	CLAIM( "The 128-bit vocabulary", fixInt128Eq( fixInt128Neg( fixInt128FromI64( 9 ) ), fixInt128FromI64( -9 ) ) );
	CLAIM( "The 128-bit vocabulary", fixInt128Eq( fixInt128Div( fixInt128FromI64( -7 ), fixInt128FromI64( 2 ) ),
												  fixInt128FromI64( -3 ) ) ); // truncates toward zero
	CLAIM( "The 128-bit vocabulary", fixUInt128Lo( fixUInt128MulU64( UINT64_MAX, 2 ) ) == UINT64_MAX - 1 );
	CLAIM( "The 128-bit vocabulary", fixUInt128Hi( fixUInt128MulU64( UINT64_MAX, 2 ) ) == 1 );
	CLAIM( "The 128-bit vocabulary", fixUInt128Eq( fixUInt128Shl( fixUInt128FromU64( 1 ), 64 ),
												   fixUInt128Make( 1, 0 ) ) );
	CLAIM( "The 128-bit vocabulary", fixInt128Eq( fixInt128Min( fixInt128FromI64( -3 ), fixInt128FromI64( 2 ) ),
												  fixInt128FromI64( -3 ) ) );
	CLAIM( "The 128-bit vocabulary", fixInt128Eq( fixInt128Max( fixInt128FromI64( -3 ), fixInt128FromI64( 2 ) ),
												  fixInt128FromI64( 2 ) ) );
	// fixISqrt128High: "exact integer square root", floor rather than nearest
	CLAIM( "The 128-bit vocabulary", fixISqrt128High( 0, 144 ) == 12 );
	CLAIM( "The 128-bit vocabulary", fixISqrt128High( 0, 143 ) == 11 );
	CLAIM( "The 128-bit vocabulary", fixISqrt128High( 1, 0 ) == UINT64_C( 0x100000000 ) );

	if ( fails )
	{
		printf( "FAILED: USAGE.md and the code disagree in %d place(s)\n", fails );
		return 1;
	}

	printf( "every numeric claim in USAGE.md holds\n" );
	return 0;
}
