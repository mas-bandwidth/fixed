// SPDX-FileCopyrightText: 2025-2026 Erin Catto -- derived from Box3D (https://github.com/erincatto/box3d)
// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC -- fixed-point conversion
// SPDX-License-Identifier: MIT
// Deterministic fixed-point transcendentals. Cross-platform bit-identical:
// integer-only rational/minimax approximations, no libm.
#pragma once
#include "fixed/base.h"
#include "fixed/fixed.h"

// ---------------------------------------------------------------------------------------------
// Correctly rounded double/float to Q48.16 conversion.
//
// The existing fixFromDouble and fixFromFloat in fixed.h use the old formula (scale by
// 65536, add or subtract 0.5, then truncate). That formula has two defects: (1) the +/- 0.5
// add is itself a double, so it rounds near a half-integer (e.g. -0.49999999999999994 - 0.5
// rounds to -1.0, giving -1 where the correct answer is 0) and above 2^52 the add does
// nothing, leaving the result off by one raw; (2) truncation toward zero misrounds negatives.
// Their results are pinned by existing tests and frozen hashes (geometry_test, usage_test,
// quantize_test, port_equality among others), so they are left bit for bit as they are and
// these Rounded entry points sit beside them.
//
// ROUNDING RULE: round half to even (the CORE-MATH / llrint reference rule). A value exactly
// halfway between two raws goes to the one with an even last bit, positive or negative.
//
// WHY THIS FORM. The scale by 65536 is exact in double (a power-of-two multiply), so the
// integer part and the exact fractional remainder are split without a rounding add. For
// |d| >= 2^52 the scaled value is already an integer and needs no rounding. No libm and no
// FPU rounding-mode change. NaN and out-of-range inputs behave as they do in fixFromDouble
// today: the scaled double is cast straight to fixed_t, so those are unspecified.
// Test: test/from_double_test.c.
// ---------------------------------------------------------------------------------------------

/// Convert a float to Q48.16, rounding half to even (the rule in the block above). A
/// boundary helper. Test: test/from_double_test.c.
FIX_INLINE fixed_t fixFromFloatRounded( float x )
{
	double d = (double)x * (double)FIX_ONE;
	if ( d >= 9007199254740992.0 || d <= -9007199254740992.0 )
	{
		return (fixed_t)d;
	}
	fixed_t n = (fixed_t)d;       // integer part, truncation toward zero
	double f = d - (double)n;     // exact remainder in (-1, 1)
	if ( f > 0.5 )
	{
		n += 1;
	}
	else if ( f < -0.5 )
	{
		n -= 1;
	}
	else if ( f == 0.5 || f == -0.5 )
	{
		if ( n & 1 )
		{
			n += ( f > 0.0 ) ? 1 : -1;
		}
	}
	return n;
}

/// Convert a double to Q48.16, rounding half to even (the rule in the block above). A
/// boundary helper. Test: test/from_double_test.c.
FIX_INLINE fixed_t fixFromDoubleRounded( double x )
{
	double d = x * (double)FIX_ONE;
	if ( d >= 9007199254740992.0 || d <= -9007199254740992.0 )
	{
		return (fixed_t)d;
	}
	fixed_t n = (fixed_t)d;
	double f = d - (double)n;
	if ( f > 0.5 )
	{
		n += 1;
	}
	else if ( f < -0.5 )
	{
		n -= 1;
	}
	else if ( f == 0.5 || f == -0.5 )
	{
		if ( n & 1 )
		{
			n += ( f > 0.0 ) ? 1 : -1;
		}
	}
	return n;
}

/// Cosine and sine pair.
typedef struct fixCosSin
{
	fixed_t cosine;
	fixed_t sine;
} fixCosSin;

/* Wide Q2.62 sine/cosine pair used by exact fixed-point quaternion construction. */
typedef struct fixCosSinWide
{
	int64_t cosineQ62;
	int64_t sineQ62;
} fixCosSinWide;

/// atan2 accurate to ~0.0023 degrees. Deterministic (unlike the standard library).
FIX_API fixed_t fixAtan2( fixed_t y, fixed_t x );

/// atan2 within 1 raw Q48.16 unit of the correctly rounded value for every pair, monotone in each
/// argument wherever the true atan2 is, fixAtan2Exact( 0, 0 ) = 0, INT64_MIN taken as -INT64_MAX.
/// The ratio in Q2.62 at 128 bits, one degree-20 polynomial on [0, 1] evaluated at 128 bits, the
/// octant map in Q.64 radians and one final rounding (design and error budget in src/fixed_math.c).
/// Checked against CORE-MATH cr_atan2 at the pin in test/quat_angles_exact_test.c. Integer only,
/// bit-identical on the native and the emulated 128-bit arms. A new function: fixAtan2 keeps its
/// frozen outputs.
FIX_API fixed_t fixAtan2Exact( fixed_t y, fixed_t x );

/// 2 * atan2( y, x ) for |y| = sqrt( ySquared ) and |x| = sqrt( xSquared ) with the given signs,
/// rounded once to Q48.16 from fixAtan2Exact's wide value: within 1 raw of the correctly rounded
/// value. The squared magnitudes are exact 128-bit sums of squares (fixDotRaw), so no floored length
/// enters. The entry point of the exact quaternion angles in fixed_vec.h (fixGetQuatAngleExact and
/// its siblings); checked in test/quat_angles_exact_test.c.
FIX_API fixed_t fixDoubleAtan2ExactOfSquares( fixUInt128 ySquared, bool yNegative, fixUInt128 xSquared, bool xNegative );

/// Cosine and sine of an angle in radians. Deterministic.
FIX_API fixCosSin fixComputeCosSin( fixed_t radians );

/// Cosine and sine to within one ulp of Q48.16 (|error| <= 2^-16 against the true cosine and
/// sine of the input angle) over the whole representable range. Range reduction by pi/2 in Q2.62
/// at 128 bits: the angle itself is exact there, and the one error is pi/2 rounded to 62 fraction
/// bits (0.38 of its last unit), which the quadrant count k multiplies. That shifts the reduced
/// angle by up to 7.5e-6 rad, 0.49 ulp of Q48.16, at the top of the range (|radians| near 2^47;
/// about 2^-17 ulp at 2^31 radians), on top of the 0.5 ulp of the final rounding. Then degree 11/12
/// Taylor polynomials in Q32.32. Deterministic. About
/// four times the cost of fixComputeCosSin, whose error is ~0.0017: use this one where the
/// result feeds geometry (tessellation, rotation matrices built from angles).
FIX_API fixCosSin fixComputeCosSinPrecise( fixed_t radians );

FIX_INLINE fixed_t fixSin( fixed_t radians ) { fixCosSin cs = fixComputeCosSin( radians ); return cs.sine; }
FIX_INLINE fixed_t fixCos( fixed_t radians ) { fixCosSin cs = fixComputeCosSin( radians ); return cs.cosine; }

/// Cosine and sine within 2 raw Q48.16 units of the correctly rounded value at EVERY input angle
/// (measured within 1 against CORE-MATH). The reduction is exact to 2^-61 rad for every int64
/// input: the quarter-turn count is the angle times 112 bits of 2/pi, in integers, so its error
/// does not grow with the angle. The polynomials are those of fixComputeCosSinPrecise. Integer
/// only, bit-identical on the native and the emulated 128-bit arms. These are new functions:
/// fixSin, fixCos, fixComputeCosSin and fixComputeCosSinPrecise keep their frozen outputs.
FIX_API fixCosSin fixComputeCosSinExact( fixed_t radians );

/* Exact wide sine/cosine of radians / 2, before Q48.16 rounding. */
FIX_API fixCosSinWide fixComputeCosSinHalfExactWide( fixed_t radians );

FIX_INLINE fixed_t fixSinExact( fixed_t radians ) { fixCosSin cs = fixComputeCosSinExact( radians ); return cs.sine; }
FIX_INLINE fixed_t fixCosExact( fixed_t radians ) { fixCosSin cs = fixComputeCosSinExact( radians ); return cs.cosine; }

/// Convert any angle into the range [-pi, pi]. The original function remains frozen because
/// fixComputeCosSinPrecise and existing hash-pinned consumers use its outputs.
FIX_INLINE fixed_t fixUnwindAngle( fixed_t radians )
{
	const fixed_t twoPi = FIX( 6.28318530718 );
	int64_t n = ( fixDiv( radians, twoPi ) + FIX_HALF ) >> FIX_FRACTION_BITS;
	return (fixed_t)fixInt128ToI64( fixInt128Sub( fixInt128FromI64( radians ), fixInt128MulI64( n, twoPi ) ) );
}

/// Exact reduction into [-pi, pi], within 1 raw of x - 2*pi*round(x/(2*pi)) for every input.
/// Integer-only and bit-identical on native/emulated 128-bit arms. See test/unwind_angle_test.c.
FIX_API fixed_t fixUnwindAngleExact( fixed_t radians );

// Saturating replacements for the Q48.16 -> int32 conversions and for the top-edge
// wrap of fixCeil. The originals live in include/fixed/fixed.h and are frozen:
// determinism_test's hash mixes the results of fixTruncToInt, fixFloorToInt and
// fixRoundToInt, and exhaustive_test's hash mixes fixFloor and fixCeil. Replacing
// them in place would move those hashes, so they are left bit for bit as they are
// and the Sat names below are added beside them. Current behavior of the originals:
//
//   fixTruncToInt   (int)( a / FIX_ONE )            truncates toward zero, then the
//                                                   int64 -> int32 cast drops the high
//                                                   bits, so it wraps above +-2^31.
//   fixFloorToInt   (int)( a >> 16 )                arithmetic floor, then the same
//                                                   wrapping cast.
//   fixRoundToInt   (int)( (a + FIX_HALF) >> 16 )   half-up toward +infinity, also with
//                                                   the wrapping cast (and an unsigned
//                                                   add so the raw edge wraps).
//   fixFloor        a & ~(FIX_ONE - 1)              already defined for every input,
//                                                   including INT64_MIN and INT64_MAX.
//   fixCeil         (a + FIX_ONE - 1) & ~mask       in unsigned arithmetic: defined,
//                                                   but at the top edge it rounds past
//                                                   INT64_MAX and wraps to INT64_MIN.
//
// The three ToInt replacements clamp to INT32_MIN/INT32_MAX. fixFloor needs no
// replacement. fixCeilSat saturates to FIX_MAX instead of wrapping. All of them keep
// the old truncation/floor/rounding rule and use only defined signed arithmetic, so
// every in-range result is bit-identical to the original. Tested by test/toint_test.c.

/// fixTruncToInt with saturation: truncate toward zero and clamp to INT32_MIN/INT32_MAX.
/// In-range results are identical to fixTruncToInt. Tested by test/toint_test.c.
FIX_INLINE int fixTruncToIntSat( fixed_t a )
{
	int64_t q = a / FIX_ONE;
	if ( q > INT32_MAX )
	{
		return INT32_MAX;
	}
	if ( q < INT32_MIN )
	{
		return INT32_MIN;
	}
	return (int)q;
}

/// fixFloorToInt with saturation: round toward negative infinity and clamp to
/// INT32_MIN/INT32_MAX. In-range results are identical to fixFloorToInt.
/// Tested by test/toint_test.c.
FIX_INLINE int fixFloorToIntSat( fixed_t a )
{
	int64_t q = a / FIX_ONE;
	int64_t r = a % FIX_ONE;
	if ( r != 0 && a < 0 )
	{
		q--;
	}
	if ( q > INT32_MAX )
	{
		return INT32_MAX;
	}
	if ( q < INT32_MIN )
	{
		return INT32_MIN;
	}
	return (int)q;
}

/// fixRoundToInt with saturation: round half-up toward +infinity (floor(a/FIX_ONE +
/// 0.5)) and clamp to INT32_MIN/INT32_MAX. The q/r decomposition reproduces the old
/// (a + FIX_HALF) >> 16 result on every in-range input without the unsigned wrap.
/// Tested by test/toint_test.c.
FIX_INLINE int fixRoundToIntSat( fixed_t a )
{
	int64_t q = a / FIX_ONE;
	int64_t r = a % FIX_ONE;
	int64_t adj = a >= 0 ? ( r >= FIX_HALF ? 1 : 0 ) : ( r < -FIX_HALF ? -1 : 0 );
	q += adj;
	if ( q > INT32_MAX )
	{
		return INT32_MAX;
	}
	if ( q < INT32_MIN )
	{
		return INT32_MIN;
	}
	return (int)q;
}

/// fixCeil with saturation: smallest integral fixed-point value not less than a (like
/// ceilf), saturating at the top edge. If rounding up would pass INT64_MAX the result
/// is FIX_MAX instead of wrapping to INT64_MIN (the behavior of fixCeil, which is kept
/// unchanged for the frozen exhaustive hash). Below that the low fraction bits are
/// dropped the same way fixCeil does, so every in-range result is identical. At
/// INT64_MIN the result is INT64_MIN itself. Tested by test/toint_test.c.
FIX_INLINE fixed_t fixCeilSat( fixed_t a )
{
	if ( a >= 0 )
	{
		if ( a > FIX_MAX - ( FIX_ONE - 1 ) )
		{
			return FIX_MAX;
		}
		return (fixed_t)( (uint64_t)a + (uint64_t)( FIX_ONE - 1 ) ) & ~(uint64_t)( FIX_ONE - 1 );
	}
	// a < 0: ceil = trunc(a/FIX_ONE)*FIX_ONE = a - (a % FIX_ONE); the remainder is in
	// (-FIX_ONE, 0], so the subtraction stays in [INT64_MIN, 0] and never overflows.
	return a - ( a % FIX_ONE );
}

/// Interpolate a scalar.
FIX_INLINE fixed_t fixLerp( fixed_t a, fixed_t b, fixed_t alpha )
{
	// INT64_MIN policy: the subtract is unsigned so INT64_MIN is taken as FIX_MIN, not a trap.
	fixed_t oneMinusAlpha = (fixed_t)( (uint64_t)FIX_ONE - (uint64_t)alpha );
	// INT64_MIN policy: the sum is unsigned so extremes wrap in two's complement, never signed overflow.
	return (fixed_t)( (uint64_t)fixMul( oneMinusAlpha, a ) + (uint64_t)fixMul( alpha, b ) );
}

// ---------------------------------------------------------------------------------------------
// The exp2 / log2 / pow ladder. Determinism over accuracy: every operation is pure integer
// arithmetic, so the results are bit-identical on every platform, which libm's pow is not.
// ---------------------------------------------------------------------------------------------

/// Base-2 logarithm. The mantissa's log2 bits are produced exactly by repeated squaring, so
/// this is deterministic. Returns INT64_MIN for a <= 0, log2 having no value there.
FIX_API fixed_t fixLog2( fixed_t a );

/// Base-2 exponential, by binary exponentiation over the fraction bits. Saturates to
/// INT64_MAX at and above 2^47 (the top of the Q48.16 whole-unit domain) and returns 0
/// below the smallest representable value.
FIX_API fixed_t fixExp2( fixed_t a );

/// Base-2 exponential within 2 raw units of correctly rounded over the whole Q48.16 domain,
/// integer only. Saturates to INT64_MAX at and above 47 rad and returns 0 where the
/// correctly rounded result is 0. The bound of 2 raw over the whole domain is checked
/// against CORE-MATH at the pin by test/exp2_exact_test.c.
FIX_API fixed_t fixExp2Exact( fixed_t a );

/// base raised to exponent, as fixExp2( exponent * fixLog2( base ) ). Returns 0 for
/// base <= 0 and one for a zero exponent.
FIX_API fixed_t fixPow( fixed_t base, fixed_t exponent );

/// base raised to exponent within 2 raw units of correctly rounded, for every base > 0 and
/// every exponent, integer only: 2^( exponent * log2( base ) ) with log2 carried to up to 120
/// fraction bits and the product held wide, so it never wraps. Saturates to INT64_MAX when the
/// result is at or above 2^47, returns 0 where the correctly rounded result is 0, FIX_ONE for
/// a zero exponent, and 0 for base <= 0. The bound of 2 raw for every base > 0 is checked
/// against CORE-MATH at the pin by test/pow_exact_test.c.
FIX_API fixed_t fixPowExact( fixed_t base, fixed_t exponent );

// ---------------------------------------------------------------------------------------------
// Q2.30 normalization
// ---------------------------------------------------------------------------------------------

/// One normalized Q2.30 component: raw * 2^30 / length, rounded to nearest and clamped to
/// [-1, 1] (the rounded divide can otherwise overshoot one by an ulp). length must be
/// non-zero; a zero length has no direction to normalize toward.
FIX_API int32_t fixNormalizeComponent30( int64_t raw, uint64_t length );

// ---------------------------------------------------------------------------------------------
// Critically damped smoothing on fixed point (deterministic control dynamics).
// Mirrors the double-precision formula: omega = 2 pi / smoothTime;
// velocity = ( velocity - omega^2 * dt * ( current - target ) ) / ( 1 + omega * dt )^2;
// result = current + velocity * dt.
// ---------------------------------------------------------------------------------------------

/// Critically damped smoothing toward a target. Updates velocity in place.
FIX_API fixed_t fixSmoothCriticallyDamped( fixed_t current, fixed_t target, fixed_t* velocity, fixed_t smoothTime,
										   fixed_t deltaTime );

/// Critically damped smoothing with separate smoothing times for moving up (toward a larger
/// magnitude) and down. The time is selected by comparing |target| to |current|.
FIX_API fixed_t fixSmoothCriticallyDampedUpDown( fixed_t current, fixed_t target, fixed_t* velocity,
												 fixed_t smoothTimeUp, fixed_t smoothTimeDown, fixed_t deltaTime );

/// The same formula with the true pi and one rounding per output: omega = 2 pi / smoothTime;
/// velocity' = ( velocity - omega^2 dt ( current - target ) ) / ( 1 + omega dt )^2;
/// result = current + velocity' dt. Both outputs are within 2 raw units of the correctly
/// rounded exact value for current, target and velocity in +-2^20 rad, smoothTime in
/// [1/16, 1024] rad and deltaTime in (0, 1]; outputs past the fixed_t range saturate.
/// The early returns are unchanged: smoothTime <= 0 returns target, deltaTime <= 0 returns
/// current, and velocity is not touched. See test/smooth_damped_exact_test.c.
FIX_API fixed_t fixSmoothCriticallyDampedExact( fixed_t current, fixed_t target, fixed_t* velocity,
												fixed_t smoothTime, fixed_t deltaTime );

/// fixSmoothCriticallyDampedExact with separate smoothing times for moving up (toward a
/// larger magnitude) and down, selected by comparing |target| to |current|.
FIX_API fixed_t fixSmoothCriticallyDampedUpDownExact( fixed_t current, fixed_t target, fixed_t* velocity,
													  fixed_t smoothTimeUp, fixed_t smoothTimeDown, fixed_t deltaTime );
