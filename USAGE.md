# Using `fixed`

`fixed` is a deterministic fixed-point math library in C. Every operation is integer
arithmetic, so the same inputs produce **the same bits on every platform, architecture and
compiler** — which is what lockstep simulation, client/server prediction and replay
require and what floating point cannot give.

This document teaches the whole library by example. [README.md](README.md) is the short
version: what this is and why it exists.

```c
#include "fixed/fixed.h"

fixed_t width  = FIX( 2.5 );          // a compile-time literal
fixed_t height = fixFromInt( 4 );
fixed_t area   = fixMul( width, height );   // exactly 10.0
```

Add the include directory and the two source files, or use the CMake target:

```cmake
add_subdirectory( fixed )
target_link_libraries( my_game PRIVATE fixed )
```

---

## The families

The library has **six numeric families**. They exist because no single format is right for
everything: a world coordinate needs range, a quaternion component needs fraction bits,
and accumulated time needs both. Each section says what the family is and when to reach
for it.

### `fixed_t` — Q48.16, the workhorse

A signed 64-bit integer holding 48 integer bits and 16 fraction bits. Resolution is
1/65536 (about 1.5e-5) **uniformly across the entire range** — precision does not degrade
far from the origin, which is the single biggest practical difference from `float`.

- Range: about ±1.4e14 units
- Resolution: 1/65536 everywhere
- Add, subtract, negate and compare are the plain integer operators
- Multiply and divide go through `fixMul` and `fixDiv`, which use 128-bit intermediates

```c
fixed_t a = FIX( 1.5 );
fixed_t b = FIX( -0.25 );

fixed_t sum        = a + b;              // plain +, exact
fixed_t difference = a - b;              // plain -, exact
fixed_t negated    = -a;                 // plain -, exact
bool    ordered    = a > b;              // plain >, exact

fixed_t product = fixMul( a, b );        // NOT a * b
fixed_t ratio   = fixDiv( a, b );        // NOT a / b
```

**Reach for it for**: essentially everything. Positions, velocities, forces, angles,
scales, times short enough not to accumulate. It is the default and the other families are
specializations.

### `fixed30_t` — Q2.30, unit-interval components

32-bit storage, 2 integer bits (sign included), 30 fraction bits. Domain [-2, 2).

Built for quantities that are always normalized — quaternion components never leave
[-1, 1], so spending 30 of 32 bits on fraction is free accuracy. It is **deliberately a
struct** rather than a bare `int32_t`: Q48.16 and Q2.30 raws differ by a factor of 2^14,
and a bare typedef would let a mixup compile silently. With the struct, arithmetic and
cross-domain assignment refuse to compile and you have to go through a converter.

```c
fixed30_t component = fix30FromFix( FIX( 0.70710678 ) );   // packs, gaining 14 bits
fixed_t   back      = fixFromFix30( component );           // unpacks, rounding to nearest
double    exact     = fix30ToDouble( component );          // exact: every Q2.30 fits a double
```

**Reach for it for**: quaternion components on the wire or in storage, normal vectors,
blend weights, anything with a hard [-1, 1] domain where you want the extra 14 bits.

### `fixTime` — Q32.32, accumulated time

Q32.32 seconds in a signed 64-bit integer: 233 picosecond resolution, ±68 year range.

Time gets more fraction bits than `fixed_t` **because time accumulates**. A 60 Hz
simulation performs thousands of small `dt` additions per minute. Every addition is exact
in integer arithmetic, so the only error is the one-time rounding of `dt` itself — about
2.3e-10 s at Q32.32, against 1.5e-5 s at Q48.16. That is the whole argument, and
`test/wide_time_test.c` checks it: sixty exact adds of a 1/60 s tick drift by at most 60
raw units.

```c
fixTime tick = FIX_TIME( 1.0 / 60.0 );
fixTime now  = 0;
for ( int frame = 0; frame < 3600; frame++ )
{
    now += tick;                    // plain +, exact
}
fixed_t seconds = fixTimeToFixed( now );
```

**Reach for it for**: simulation clocks, timestamps, anything summed over many frames.

Q32.32 is also the library's internal working domain for the transcendentals — `fixAtan2`
and `fixComputeCosSin` evaluate their polynomials there for the headroom, in
`static` helpers in `src/fixed_math.c`. Those are not exported; `fixTime` is Q32.32's
public face.

### `fixInt128` / `fixUInt128` — the 128-bit seam

The fixed-point core needs 128-bit intermediates: `fixMul` widens before it rounds,
`fixDiv` shifts a 64-bit numerator up by 16 before dividing, `fixLength` accumulates an
exact sum of squares, and the wide world family is 128-bit by definition.

**On every compiler, including plain MSVC.** gcc, clang and clang-cl provide `__int128`
and `fixInt128` is that type. Plain MSVC has no such type, so `fixInt128` is an emulated
pair of two 64-bit lanes defined in `include/fixed/fixed_int128.h`. Both arms assert the
same frozen determinism hashes in CI, so "the emulation is bit-identical to native" is a
checked guarantee rather than a claim.

The consequence for your code: **there is no bare 128-bit operator anywhere in this
library, and portable consumer code should not write one either.** Every 128-bit operation
has a named function.

```c
fixInt128 product = fixInt128MulI64( a, b );                   // widening 64x64 -> 128
fixInt128 rounded = fixInt128Shr( fixInt128Add( product,
                        fixInt128FromI64( FIX_HALF ) ), 16 );
fixed_t   result  = (fixed_t)fixInt128ToI64( rounded );
```

Two macros describe the build:

| macro | meaning |
|---|---|
| `FIX_HAS_INT128` | always `1` — this library has 128-bit integers on every compiler |
| `FIX_INT128_EMULATED` | `1` when `fixInt128` is the emulated pair, `0` when it is the compiler's `__int128` |

Define `FIX_FORCE_EMULATED_INT128` to run the emulated path even where native exists. CI
builds the entire suite that way on all three operating systems.

**Reach for it for**: exact intermediates that must not round or overflow — accumulating a
dot product, computing a determinant, anything where you need the product of two 64-bit
values before you decide how to round it.

### `fixedWide_t` / `fixPosWide` — Q112.16, wide world coordinates

A 128-bit scalar sharing `fixed_t`'s 16 fraction bits. Same resolution (1/65536), with all
64 extra bits going to integer range — about ±2.6e33 units.

**Sharing the fraction count is the crux.** Because the fraction points align, the boundary
between wide world space and local Q48.16 space is an exact integer subtract plus a range
check, never an arithmetic rescale. Two objects anywhere in a ludicrous world produce an
exact local difference as long as they are close enough to interact — which any pair that
can collide, join or query is by definition.

```c
fixPosWide shipA = fixPosWideFromVec3( localOffsetA );
fixPosWide shipB = fixPosWideOffset( origin, localOffsetB );

fixVec3 separation = fixPosWideSub( shipB, shipA );   // exact if it fits Q48.16
```

Because `fixedWide_t` may be a struct, **comparisons and arithmetic go through functions**
— `fixWideAdd`, `fixWideLt` and the rest — not `+` and `<`.

**Reach for it for**: world positions in a game whose world exceeds ±1.4e14 units. If your
world fits in `fixed_t`, use `fixVec3` and ignore this family entirely.

### The geometric types on `fixed_t`

`fixVec2`, `fixVec3`, `fixQuat`, `fixMatrix3`, `fixTransform`, `fixAABB` and `fixPlane`,
plus `fixPos` and `fixWorldTransform` (which are `fixVec3` and `fixTransform` — in fixed
point a world position needs no separate representation, because precision is uniform).

```c
fixVec3      position = { FIX( 1.0 ), FIX( 2.0 ), FIX( 3.0 ) };
fixQuat      rotation = fixMakeQuatFromAxisAngle( fixVec3_axisZ, FIX_PI / 2 );
fixTransform frame    = { position, rotation };
fixVec3      world    = fixTransformPoint( frame, localPoint );
```

---

## How to choose

| you have | use | why |
|---|---|---|
| a position, velocity, force, length, angle | `fixed_t` | uniform 1/65536 everywhere |
| a quaternion or normal component, in memory or on the wire | `fixed30_t` | 14 extra fraction bits, free, in half the storage |
| a clock summed over thousands of frames | `fixTime` | accumulation is exact; only `dt` rounds |
| an exact intermediate that must not round | `fixInt128` | 128 bits, no rounding until you say so |
| a world larger than ±1.4e14 units | `fixedWide_t` / `fixPosWide` | 128-bit range, same fraction point, exact boundary |
| a wire format that is none of the above | `fixNarrow` / `fixQuantize` | arbitrary scales and Q-format crossings |

---

## Crossing between families

Every conversion in one place, with its rounding stated. **Rounding is a contract here, not
an implementation detail** — two machines that round differently diverge.

| from | to | call | rounding |
|---|---|---|---|
| `int64_t` | `fixed_t` | `fixFromInt( i )` | exact |
| `fixed_t` | `int` | `fixTruncToInt( a )` | toward zero |
| `fixed_t` | `int` | `fixFloorToInt( a )` | toward negative infinity |
| `fixed_t` | `int` | `fixRoundToInt( a )` | to nearest, ties toward positive infinity |
| `fixed_t` | `fixed_t` | `fixFloor( a )` / `fixCeil( a )` | to the integral value below / above |
| `float` | `fixed_t` | `fixFromFloat( x )` | to nearest, ties away from zero |
| `double` | `fixed_t` | `fixFromDouble( x )` | to nearest, ties away from zero |
| `fixed_t` | `float` | `fixToFloat( a )` | IEEE round-to-nearest |
| `fixed_t` | `double` | `fixToDouble( a )` | exact |
| `fixed_t` | `fixed30_t` | `fix30FromFix( a )` | exact in range; saturates outside [-2, 2) |
| `fixed30_t` | `fixed_t` | `fixFromFix30( a )` | to nearest, ties toward positive infinity |
| `fixed30_t` | `double` | `fix30ToDouble( a )` | exact |
| `double` | `fixed30_t` | `fix30FromDouble( x )` | to nearest, ties away from zero, saturating |
| `fixed_t` | `fixTime` | `fixTimeFromFixed( s )` | exact (left shift) |
| `fixTime` | `fixed_t` | `fixTimeToFixed( t )` | to nearest, ties toward positive infinity |
| `double` | `fixTime` | `fixTimeFromSeconds( s )` | to nearest, ties away from zero |
| `fixTime` | `double` | `fixTimeToSeconds( t )` | IEEE round-to-nearest |
| `fixed_t` | `fixedWide_t` | `fixWideFromFixed( a )` | exact |
| `fixedWide_t` | `fixed_t` | `fixWideToFixed( a )` | exact in range; saturates outside |
| `fixVec3` | `fixPosWide` | `fixPosWideFromVec3( v )` | exact |
| `fixPosWide` | `fixVec3` | `fixPosWideToVec3( p )` | exact in range; saturates per component |
| `fixVec3` | `fixPos` | `fixToPos( v )` | exact (same representation) |
| `fixPos` | `fixVec3` | `fixToVec3( p )` | exact (same representation) |
| `int64_t` | `fixInt128` | `fixInt128FromI64( v )` | sign-extends |
| `uint64_t` | `fixInt128` | `fixInt128FromU64( v )` | zero-extends |
| `fixInt128` | `int64_t` | `fixInt128ToI64( a )` | truncates to the low lane |
| any scale | raw | `fixQuantize( value, scale )` | to nearest, **ties away from zero** |
| raw | any scale | `fixDequantize( raw, scale )` | IEEE round-to-nearest |
| Q(i.f) | Q(i.f-n) | `fixNarrow( raw, n )` | to nearest, **ties toward positive infinity** |
| Q(i.f) | Q(i.f+n) | `fixWiden( raw, n )` | exact |

### The two rounding rules, and why they differ

`fixQuantize` rounds **half away from zero**. It is symmetric about the origin, which a
signed world needs — half up would bias every negative coordinate toward the origin and
every positive one away from it. It runs on **one** machine, the authority, so it can
afford symmetry. It also takes a `double`, so it could not be made portable anyway.

`fixNarrow` rounds **half toward positive infinity**, because that is what the arithmetic
shift gives for free. **Both sides run it**, so it can afford only agreement, and it is
integer-only for exactly that reason.

The pinned converters are the general ones at fixed scales, and the test suite checks that
over every input rather than asserting it in a comment:

```c
fixFromFix30( a )   ==  fixNarrow( a.raw, FIX30_SHIFT )
fixFromDouble( x )  ==  fixQuantize( x, FIX_ONE )
fixToDouble( raw )  ==  fixDequantize( raw, FIX_ONE )
```

### One rule about float boundaries

> **Quantize on the authority, once, and treat the result as the truth.**

The quantized integer is what goes into the baseline, what a delta is computed against and
what the client decodes. If instead you keep the float as the truth and quantize
independently on each side, you have two simulations rounding separately, the baselines
drift apart, and the client decodes a value the server never held. That failure is subtle,
delayed, and invisible until scale.

`fixFromFloat`, `fixFromDouble`, `fixToFloat`, `fixToDouble`, `fixQuantize`,
`fixDequantize`, `fix30FromDouble`, `fix30ToDouble`, `fixTimeFromSeconds` and
`fixTimeToSeconds` are **boundary helpers**. Keep them out of simulation code.

---

## Every function, by family

### Constants and literals

| name | value |
|---|---|
| `FIX( x )` | compile-time literal, rounds to nearest — works in static initializers |
| `FIX_FRACTION_BITS` | 16 |
| `FIX_ONE` | 1.0 (65536) |
| `FIX_HALF` | 0.5 (32768) |
| `FIX_EPSILON` | the smallest positive increment (1) |
| `FIX_MAX` / `FIX_MIN` | `INT64_MAX` / `-INT64_MAX`, the saturation values |
| `FIX_PI` | pi (`fixed_vec.h`) |
| `FIX_MIN_SCALE` | 0.01, the floor `fixSafeScale` keeps scales away from zero |
| `FIX30_FRACTION_BITS` / `FIX30_ONE` / `FIX30_SHIFT` | 30 / 1.0 in Q2.30 / 14 |
| `FIX_TIME( x )` / `FIX_TIME_FRACTION_BITS` / `FIX_TIME_ONE` / `FIX_TIME_HALF` | the Q32.32 set |

Fixed point has **no NaN and no infinity**. `FIX_MAX` plays the role `FLT_MAX` did, and
division by zero saturates with the sign of the numerator (`0 / 0 == 0`) rather than
producing a value that poisons everything it touches.

Named vectors and rotations: `fixVec3_zero`, `fixVec3_one`, `fixVec3_axisX`,
`fixVec3_axisY`, `fixVec3_axisZ`, `fixQuat_identity`, `fixTransform_identity`,
`fixMat3_zero`, `fixMat3_identity`, `fixPos_zero`, `fixWorldTransform_identity`.

### Scalar arithmetic — `fixed/fixed.h`

```c
fixMul( FIX( 1.5 ), FIX( 2.5 ) )       // 3.75, round-half-up, 128-bit interior
fixDiv( FIX( 7.0 ), FIX( 2.0 ) )       // 3.5, truncated toward zero
fixDiv( FIX( 1.0 ), 0 )                // FIX_MAX — division by zero is DEFINED
fixDiv( 0, 0 )                         // 0
fixSqrt( FIX( 144.0 ) )                // 12.0, exact, rounds toward zero; negative -> 0
fixAbs( FIX( -2.0 ) )                  // 2.0
fixMin( a, b )  fixMax( a, b )         // branchless csel on arm64
fixClamp( a, lower, upper )
fixFloor( FIX( 1.5 ) )                 // 1.0
fixCeil( FIX( 1.5 ) )                  // 2.0
fixShiftLeft( a, n )                   // << that is defined for negative values
```

`fixMul` does not check for overflow by default: simulation quantities sit far below the
±1.4e14 range and the check costs real time in a solver. **Define `FIX_SATURATE`** to
saturate instead of wrapping.

`fixISqrt128High( hi, lo )` is the exact integer square root of an unsigned 128-bit value
that `fixSqrt` and `fixLength` are built on. Exposed because a consumer accumulating its
own sum of squares at 128 bits needs it.

### Transcendentals — `fixed/fixed_math.h`

Integer-only, so `pow` is deterministic here where `libm`'s is not.

```c
fixCosSin cs = fixComputeCosSin( FIX( 0.7 ) );   // both at once, the cheap form
fixed_t s = fixSin( radians );                   // convenience wrappers over the above
fixed_t c = fixCos( radians );
fixed_t angle = fixAtan2( y, x );                // ~0.0023 degrees
fixed_t wrapped = fixUnwindAngle( radians );     // into [-pi, pi]

fixed_t l = fixLog2( a );        // INT64_MIN for a <= 0
fixed_t e = fixExp2( a );        // saturates at 2^47, underflows to 0
fixed_t p = fixPow( base, exp ); // exp2( exp * log2( base ) ); 0 for base <= 0
fixed_t m = fixLerp( a, b, alpha );
```

Accuracy against `libm`: cos and sin within 0.0017, atan2 within 0.00004 rad.

Critically damped smoothing, for deterministic control dynamics:

```c
fixed_t velocity = 0;
fixed_t smoothed = fixSmoothCriticallyDamped( current, target, &velocity,
                                              FIX( 0.25 ), deltaTime );

// separate times for growing and shrinking in magnitude, selected by |target| vs |current|
fixed_t v2 = 0;
fixed_t s2 = fixSmoothCriticallyDampedUpDown( current, target, &v2,
                                              upTime, downTime, deltaTime );
```

`fixNormalizeComponent30( raw, length )` produces one normalized Q2.30 component —
`raw * 2^30 / length`, rounded to nearest and clamped to [-1, 1]. `length` must be
non-zero.

### Vectors — `fixed/fixed_vec.h`

```c
fixVecAdd( a, b )   fixVecSub( a, b )   fixVecNeg( a )     // exact
fixVecMul( a, b )                                          // componentwise product
fixVecAbs( a )      fixVecMin( a, b )   fixVecMax( a, b )
fixVecClamp( a, lower, upper )
fixSign( a )                        // -1 or 1 componentwise; 1 at zero, on purpose
fixSafeScale( a )                   // keeps |scale| >= FIX_MIN_SCALE, sign preserved

fixMulSV( s, a )                    // s * a
fixMulAdd( a, s, b )                // a + s * b
fixMulSub( a, s, b )                // a - s * b
fixBlend2( s, a, t, b )             // s * a + t * b
fixVecLerp( a, b, alpha )

fixDot( a, b )                      // 128-bit accumulation, ONE rounding
fixCross( a, b )
fixLength( a )                      // exact 128-bit sum of squares, then integer sqrt
fixLengthSquared( a )
fixDistance( a, b )                 fixDistanceSquared( a, b )
fixNormalize( a )                   // zero vector in, zero vector out
fixIsNormalized( a )
fixPerp( a )                        // a unit vector perpendicular to a
```

The two-step dot product exists because a rounded dot loses the sign of a sub-resolution
result:

```c
fixInt128 raw = fixDotRaw( a, b );          // exact, scaled by 2^32
bool      sameSide = fixInt128Gt( raw, FIX_INT128_ZERO );   // exact sign test
fixed_t   rounded  = fixFromDotRaw( raw );  // one rounding, matching fixMul
```

`fixGetLengthAndNormalize( &length, a )` returns both in one pass.

The `fix`/`fixVec` distinction is deliberate and load-bearing: `fixMin` is scalar,
`fixVecMin` is componentwise. In fixed3d these were `b3FixMin` and `b3Min`, a one-token
difference for two different operations.

Float and integer helpers, for rendering and UI rather than simulation:
`fixMinFloat`, `fixMaxFloat`, `fixClampFloat`, `fixMinInt`, `fixMaxInt`, `fixClampInt`.

### Quaternions

```c
fixQuat q = fixMakeQuatFromAxisAngle( fixVec3_axisZ, FIX_PI / 2 );

fixMulQuat( q1, q2 )        // fused 128-bit reduction, one rounding per component
fixInvMulQuat( q1, q2 )     // inv(q1) * q2, the relative rotation
fixConjugate( q )           // the cheap inverse for a unit quaternion
fixNegateQuat( q )          // the same rotation, opposite polarity
fixNormalizeQuat( q )       // zero quaternion in, identity out
fixDotQuat( a, b )          // negative means opposite polarity
fixIsNormalizedQuat( q )

fixRotateVector( q, v )     fixInvRotateVector( q, v )
fixNLerp( q1, q2, alpha )   // normalized lerp; flips polarity to take the short arc

fixed_t radians;
fixVec3 axis  = fixGetAxisAngle( &radians, q );   // zero vector for the identity
fixed_t angle = fixGetQuatAngle( q );
fixed_t twist = fixGetTwistAngle( q );            // about z, for revolute limits
fixed_t swing = fixGetSwingAngle( q );            // away from z, for cone limits

fixQuat between = fixComputeQuatBetweenUnitVectors( from, to );  // handles anti-parallel
fixQuat fromMat = fixMakeQuatFromMatrix( &m );
```

`fixRotateVector` is kept in the two-cross form on purpose. Fused single-rounding variants
of it, `fixVecLerp`, `fixMulMV` and `fixCross` perturb knife-edge equilibria — mesh-drop
sleep and convex pile SAT caching — so the extra rounding is a deliberate choice, not an
oversight.

### Matrices

```c
fixMatrix3 m = fixMakeMatrixFromQuat( q );   // when you will rotate many vectors

fixAddMM( a, b )   fixSubMM( a, b )   fixMulSM( s, a )   fixNegateMat3( a )
fixMulMM( a, b )   fixMulMV( m, v )   fixTranspose( m )  fixAbsMatrix3( m )
fixDet( m )
fixInvertMatrix( m )     // zero matrix for a singular input
fixInvertT( m )          // the inverse transposed
fixSolve3( m, a )        // inv(m) * a, three divisions instead of nine
```

The inverse and the solver work from 128-bit cofactors internally, because a Q48.16
determinant of a matrix with small entries — the inertia tensor of a small body — would
underflow to zero. `fixCofactor128( a, b, c, d )` is `a*b - c*d` at Q32.32 in 128 bits, and
`fixDivShifted( n, shift, d )` is `( n << shift ) / d` truncated to Q48.16; both are
exposed because a consumer building its own solver needs the same pieces.

### Transforms and world positions

```c
fixTransform frame = { position, rotation };

fixTransformPoint( t, v )        fixInvTransformPoint( t, v )
fixMulTransforms( a, b )         // apply b, then a
fixInvMulTransforms( a, b )      // b expressed in a's frame — the narrow-phase boundary
fixInvertTransform( t )
```

World positions in fixed point are the same representation as local vectors, so the world
family is exact integer arithmetic:

```c
fixToPos( v )    fixToVec3( p )
fixSubPos( a, b )                // a - b, the primary precision boundary operation
fixOffsetPos( p, d )             // p + d
fixLerpPosition( a, b, t )

fixMakeWorldTransform( t )       // lossless promotion
fixTransformWorldPoint( w, p )   fixInvTransformWorldPoint( w, p )
fixMulWorldTransforms( w, b )    fixInvMulWorldTransforms( a, b )
fixToRelativeTransform( w, base )
```

`fixRoundDownFloat` and `fixRoundUpFloat` are the identity in fixed point. They exist so
code written for the old large-world float mode still compiles; there is nothing to round.

### Validity predicates

Fixed point has no NaN, so validity means representable, and for rotations it also means
normalized — a non-normalized quaternion is not a rotation.

```c
fixIsValidFixed( a )        // false only for INT64_MIN, reserved so negation cannot overflow
fixIsValidVec3( v )
fixIsValidQuat( q )         // representable AND normalized
fixIsValidTransform( t )
fixIsValidMatrix3( m )
fixIsValidPlane( p )        // normal representable AND unit length
fixIsValidAABB( a )         // both bounds valid, and lower <= upper on every axis
```

### Bounding volumes and planes

```c
fixAABB box = fixMakeAABB( points, count, radius );

fixAABB_Union( a, b )      fixAABB_Contains( a, b )   fixAABB_Overlaps( a, b )
fixAABB_Center( a )        fixAABB_Extents( a )       fixAABB_Area( a )
fixAABB_Inflate( a, extension )
fixAABB_Transform( transform, a )     // conservative bound, can grow the box
fixClosestPointToAABB( point, a )
```

`fixPlane` is a unit normal plus an offset along it.

### Time — `fixed/fixed_time.h`

```c
fixTimeFromFixed( seconds )   fixTimeToFixed( t )
fixTimeFromSeconds( 0.016 )   fixTimeToSeconds( t )   // boundary helpers
fixTimeMulFixed( t, scale )   // scale a time by a Q48.16 factor, 128-bit interior
```

### Domain crossing — `fixed/fixed_quantize.h`

```c
int64_t raw   = fixQuantize( 1.25, 1024 );              // 1280, half away from zero
double  value = fixDequantize( raw, 1024 );

int64_t bounded = fixQuantizeClamped( x, scale, minRaw, maxRaw );
bool    inside  = fixFits( raw, minRaw, maxRaw );       // the clamp's predicate

int64_t coarse = fixNarrow( raw, 20 );   // 30 fraction bits -> 10, half toward +inf
int64_t fine   = fixWiden( coarse, 20 ); // exact and lossless
```

`fixNarrow( fixWiden( v, n ), n )` is the identity. The other order is not, and that is the
whole point — narrowing is where the bits go.

### Wide coordinates — `fixed/fixed_wide.h`

```c
fixWideFromFixed( a )       fixWideToFixed( w )       // saturates out of local range
fixWideAdd( a, b )          fixWideSub( a, b )        fixWideNeg( a )
fixWideMin( a, b )          fixWideMax( a, b )
fixWideEq( a, b )           fixWideLt( a, b )         fixWideGt( a, b )
fixWideLe( a, b )           fixWideGe( a, b )         // `<` does not compile on MSVC
fixWideOffset( w, localDelta )
fixWideSubToFixed( a, b )   // THE boundary operation: exact whenever it fits Q48.16

fixPosWideFromVec3( v )     fixPosWideToVec3( p )
fixPosWideSub( a, b )       fixPosWideOffset( p, d )
fixLerpPositionWide( a, b, t )

fixIsValidWideCoord( x )    fixIsValidPosWide( p )    fixIsValidWorldTransformWide( t )
```

`fixLerpPositionWide` is **deliberately not** a mechanical widening of the narrow form. The
narrow build computes `(1-t)*a + t*b`; widened directly that multiplies an absolute 128-bit
coordinate and truncates. Reformulated as `a + t*(b-a)`, the difference is in local range
so the multiply is a safe `fixMul`. That is one rounding instead of two, so the wide answer
is **more accurate and not bit-identical to the narrow one**. A consumer running both
widths carries separate goldens for it.

Wide transforms and boxes:

```c
fixMakeWorldTransformWide( t )      fixMulWorldTransformsWide( A, B )
fixTransformWorldPointWide( t, p )  fixInvTransformWorldPointWide( t, p )
fixInvMulWorldTransformsWide( A, B )   fixToRelativeTransformWide( t, base )

fixMakeAABBWide( widePoints, count, radius )
fixMakeAABBWideAt( localPoints, count, radius, origin )   // the shape you usually want
fixOffsetAABBWide( localBox, origin )
fixAABBWide_Union( a, b )       fixAABBWide_Contains( a, b )   fixAABBWide_Overlaps( a, b )
fixAABBWide_Center( a )         fixAABBWide_Extents( a )       fixAABBWide_Area( a )
fixAABBWide_Inflate( a, e )     fixAABBWide_Transform( t, a )
fixClosestPointToAABBWide( localPoint, a )
fixIsValidAABBWide( a )
```

`fixAABBWide_Extents` differences in 128 bits and then narrows, which is a **deliberate bug
fix** relative to box3d: narrowing both bounds first makes a perfectly ordinary distant box
report zero extents, at exactly the distances the wide mode exists to serve. For any box
whose bounds both fit local range the two forms agree bit-for-bit.

`fixAABBWide_Center` and `fixAABBWide_Transform` keep box3d's inherited limitation: a box
whose *centre* exceeds Q48.16 range saturates. Extents survive at any distance.

### The 128-bit vocabulary — `fixed/fixed_int128.h`

```c
// build and inspect
fixInt128Make( hi, lo )      fixInt128FromI64( v )     fixInt128FromU64( v )
fixInt128ToI64( a )          fixInt128Lo( a )          fixInt128Hi( a )
fixInt128ToUnsigned( a )     fixInt128FromUnsigned( a )
fixUInt128Make( hi, lo )     fixUInt128FromU64( v )
fixUInt128Lo( a )            fixUInt128Hi( a )

// signed arithmetic (two's complement, wraps on overflow, defined on both arms)
fixInt128Add( a, b )         fixInt128Sub( a, b )      fixInt128Mul( a, b )
fixInt128Neg( a )
fixInt128MulI64( a, b )      // widening 64x64 -> 128, the hot one
fixInt128ShiftLeft( a, n )   fixInt128Shr( a, n )      // Shr is ARITHMETIC
fixInt128Div( a, b )         fixInt128Min( a, b )      fixInt128Max( a, b )
fixInt128Eq( a, b )          fixInt128Lt( a, b )       fixInt128Gt( a, b )
fixInt128Le( a, b )          fixInt128Ge( a, b )       fixInt128IsNegative( a )

// unsigned arithmetic
fixUInt128Add( a, b )        fixUInt128Sub( a, b )     fixUInt128Mul( a, b )
fixUInt128Neg( a )           fixUInt128MulU64( a, b )
fixUInt128Shl( a, n )        fixUInt128Shr( a, n )
fixUInt128And( a, b )        fixUInt128Or( a, b )      fixUInt128Xor( a, b )
fixUInt128Not( a )
fixUInt128Eq( a, b )         fixUInt128Lt( a, b )      fixUInt128Gt( a, b )
fixUInt128Le( a, b )         fixUInt128Ge( a, b )

FIX_INT128_ZERO    FIX_UINT128_ZERO
```

Shift counts must be in [0, 127]. Outside that range the native arm is undefined behaviour
exactly as `__int128` is, so do not do it; the emulated arm is total (zero, or all sign
bits) only so a mistake cannot trap.

**Division by zero is undefined**, exactly as it is for native `__int128` — arm64 and
x86-64 do not even agree with each other. Every divide in this library guards its divisor
first, and so should yours.

The emulated pair itself — `fixEmuInt128`, `fixEmuUInt128` and their `fixEmu*` operations
— is compiled on **every** platform, including the ones that never need it, so
`test/int128_test.c` can check every emulated operation against the native one on the
machines that have both. You will not normally name those types; they are the seam's
implementation, exposed so it can be tested rather than trusted.

---

## The determinism contract

This is the library's whole reason for existing, and it is enforced in code rather than
promised in prose.

**Frozen hashes.** Seven test suites run structured sweeps and hash every result against a
constant frozen into the source — nine constants in all, because two of the suites carry
two apiece. Any change that alters a single bit fails, on every
platform:

| suite | what it pins |
|---|---|
| `determinism_test.c` | the core ops over two input streams, one of them signed |
| `exhaustive_test.c` | goldens, algebraic properties, edge and boundary behaviour |
| `port_equality_test.c` | bit-equality against the fixed3d bodies this was ported from |
| `quantize_test.c` | bit-equality against the delta bodies the crossing family came from |
| `geometry_test.c` | the vector, quaternion, matrix and transform layer |
| `int128_test.c` | the emulated 128-bit pair, operation by operation |
| `wide_time_test.c` | the Q112.16 saturation boundaries and Q32.32 scaling |

**Negative controls.** Five targets build a suite with a deliberate one-ulp error injected
and are marked `WILL_FAIL`. If one of them ever *passes*, the suite it guards has gone
blind. That machinery has already caught a control that was injecting nothing.

**The CI matrix.** Every arm asserts the same frozen hashes:

| arm | 128-bit |
|---|---|
| ubuntu (gcc/clang, x86-64) | native `__int128` |
| macos (clang, arm64) | native `__int128` |
| windows, ClangCL | native `__int128` |
| **windows, plain MSVC** | **emulated** |
| every arm, second pass | emulated, via `FIX_FORCE_EMULATED_INT128` |

**UBSan.** Undefined behaviour is how "bit-identical" dies quietly: a signed overflow or a
negative shift that happens to agree today diverges on the next compiler. Every suite runs
under `-fsanitize=undefined -fno-sanitize-recover=all` on both 128-bit arms, with
assertions enabled.

**What you must do to keep it.** Determinism is a property of the whole computation, not
just of this library:

- never let a `float` or `double` into simulation state — the boundary helpers are named
  above, and they are boundary helpers
- do not reorder floating-point-derived inputs between machines
- compile with `-ffp-contract=off` if you have any float left anywhere (the CMake build
  does this for you)
- quantize on the authority, once

---

## Consumer hooks

Every macro is guarded **separately**. A consumer that supplies only `FIX_API` — the common
case, because `FIX_API` is the one carrying an export decoration — keeps the library's own
definitions for everything it did not mention. `test/override_api_test.c` is the reader for
that: it defines exactly one macro and nothing else, and it is a compile-and-run test so
the guarding cannot silently regress.

| macro | default | override it to |
|---|---|---|
| `FIX_API` | empty (C) / `extern "C"` (C++) | add an export decoration |
| `FIX_INLINE` | `static inline` / `inline` | change inline linkage |
| `FIX_FORCE_INLINE` | `__forceinline` / `always_inline` | change forced inlining |
| `FIX_LITERAL( T )` | `(T)` / `T` | adapt compound literals |
| `FIX_ZERO_INIT` | `{0}` / `{}` | adapt zero initialization |
| `FIX_ASSERT( ... )` | `assert( ... )` | route assertions into your diagnostics |
| `FIX_VALIDATE( ... )` | no-op | opt into the heavier validation points |

```c
#define FIX_API __declspec( dllexport )
#include "fixed/fixed.h"          // everything else keeps the library's definitions
```

Build-time switches:

| macro | effect |
|---|---|
| `FIX_SATURATE` | `fixMul` and `fixFromDotRaw` saturate on overflow instead of wrapping |
| `FIX_FORCE_EMULATED_INT128` | use the emulated 128-bit pair even where `__int128` exists |
| `NDEBUG` | as usual — **without it, `FIX_ASSERT` compiles into hot math**, including `fixVecLerp`, `fixMakeQuatFromAxisAngle`, `fixNarrow` and `fixWiden` |

---

## Where things live

| header | contents |
|---|---|
| `fixed/fixed.h` | `fixed_t`, `fixed30_t`, scalar arithmetic, conversions; pulls in the seam |
| `fixed/fixed_int128.h` | `fixInt128`/`fixUInt128`, the vocabulary, the emulated pair |
| `fixed/fixed_math.h` | transcendentals, the exp/log ladder, smoothing, `fixLerp` |
| `fixed/fixed_vec.h` | vectors, quaternions, matrices, transforms, AABBs, planes, validators |
| `fixed/fixed_wide.h` | Q112.16 scalars, wide positions, wide transforms and boxes |
| `fixed/fixed_time.h` | Q32.32 time |
| `fixed/fixed_quantize.h` | arbitrary-scale conversion and Q-format crossing |
| `fixed/base.h` | the overridable macro set |
