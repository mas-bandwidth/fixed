# fixed

A small, standalone, deterministic fixed-point math library in C. The core type is a
64-bit `fixed_t` (Q48.16), with pure-integer arithmetic and integer-only
transcendentals — so results are **bit-identical on every platform, architecture and
compiler**, which floating point is not.

**[USAGE.md](USAGE.md) is the guide**: every family, every function, by example.

```c
#include "fixed/fixed.h"

fixed_t area = fixMul( FIX( 2.5 ), fixFromInt( 4 ) );   // exactly 10.0
```

## Why

Deterministic lockstep and client/server simulations require every machine to
compute the same numbers from the same inputs. Floating point breaks this:
fused multiply-add, `libm` transcendentals, and `-ffp-contract` differ across
compilers and architectures. Fixed-point removes the whole class of divergence.

This library is extracted from the fixed-point core of [fixed3d](https://github.com/mas-bandwidth/fixed3d), 
which is a fixed point port of [box3d](https://github.com/erincatto/box3d) by Erin Catto (see `LICENSE`), so it
can be used independently of the physics engine.

## Guarantee, checked in code

Eight test suites run structured sweeps and hash every result against a constant frozen
into the source. Any change that alters a single bit fails, on every platform. Seven more
targets build a suite with a deliberate error injected and are marked `WILL_FAIL`,
so a suite that goes blind says so instead of passing quietly.

Every arm of the CI matrix asserts the same frozen hashes:

| arm | 128-bit intermediates |
|---|---|
| ubuntu (x86-64) | native `__int128` |
| macos (arm64) | native `__int128` |
| windows, ClangCL | native `__int128` |
| windows, plain MSVC | emulated |
| every arm, second pass | emulated, via `FIX_FORCE_EMULATED_INT128` |

Plus a UBSan job over every suite on both 128-bit arms, because undefined behaviour is how
"bit-identical" dies quietly.

**Every compiler, including plain MSVC.** The 128-bit intermediates the core needs are
`__int128` where the compiler has it (gcc, clang, clang-cl) and an emulated pair of
64-bit lanes where it does not. Both arms assert the same frozen hashes, so the emulation
is held to bit-identity with native rather than assumed to match it — see
[USAGE.md](USAGE.md) for the seam and `test/int128_test.c` for the operation-by-operation
differential against native. There is no ClangCL floor and no platform this library
excludes by design.

## What is here

- **Core scalar type and arithmetic** (`fixed.h`): `fixMul`, `fixDiv`, `fixSqrt`,
  `fixAbs`, `fixFloor`, `fixCeil`, `fixClamp`, `fixMin`, `fixMax` and the conversions.
- **Integer transcendentals** (`fixed_math.h`): `fixComputeCosSin`, `fixSin`, `fixCos`,
  `fixAtan2`, `fixUnwindAngle` — pure fixed-point, and correct against libm
  (cos/sin < 0.0017, atan2 < 0.00004 rad).
- **The exponential ladder** `fixLog2`, `fixExp2`, `fixPow`, and `fixLerp` — integer-only,
  so `pow` is deterministic where libm's is not.
- **Critically damped smoothing** (`fixSmoothCriticallyDamped`,
  `fixSmoothCriticallyDampedUpDown`) — deterministic control dynamics on fixed point.
- **Q2.30 packed components** (`fixed30_t`): 32-bit storage, 30 fraction bits, for
  always-normalized quantities like quaternion components, with its converters and
  `fixNormalizeComponent30`.
- **Vectors, quaternions, matrices, transforms and world positions** (`fixed_vec.h`):
  the arithmetic, `fixNormalize`, `fixRotateVector`, `fixNLerp`, `fixInvertMatrix`,
  `fixSolve3`, the axis/angle and twist/swing accessors, and the validity predicates.
- **Fixed-point time** (`fixed_time.h`): Q32.32, exact conversion to and from `fixed_t`,
  because time accumulates and accumulation must not round.
- **Wide 128-bit coordinates** (`fixed_wide.h`): the Q112.16 scalar and position types,
  and the boundary vocabulary that moves values between wide world space and local Q48.16
  by exact integer subtract — the fraction points align, so there is no rescale.
- **Bounding volumes and planes** (`fixAABB`, `fixAABBWide`, `fixPlane`) with their
  operations and validity predicates. Both widths are exported unconditionally: a consumer
  selects narrow or wide by which type it names, never by a compile flag that silently
  changes an ABI.
- **128-bit arithmetic on every compiler** (`fixed_int128.h`): native `__int128` where
  available, an emulated pair on plain MSVC, behind one seam the whole library speaks —
  `fixInt128Div`, `fixInt128Min`, `fixInt128Max`, `fixInt128ShiftLeft`, `fixISqrt128High`
  and the full signed and unsigned operation vocabulary.
- **Domain crossing** (`fixed_quantize.h`): arbitrary-scale `fixQuantize`, `fixDequantize`,
  `fixQuantizeClamped` and `fixFits`, and the integer-only Q-format pair `fixNarrow` and
  `fixWiden` — the general form of the pinned Q48.16 and Q2.30 converters, ported from
  [delta](https://github.com/mas-bandwidth/delta) and swept bit-for-bit against it.

Deliberately **not** here: geometric queries (`b3SegmentDistance`, `b3LineDistance`,
`b3PointToSegmentDistance`) and inertia (`b3Steiner`). Those are physics that happens
to be written in fixed point, not fixed-point math, and they belong with the solver.
Nor `b3IsBoundedAABB` / `b3IsSaneAABB`, which resolve `B3_HUGE` through box3d's mutable
`b3GetLengthUnitsPerMeter()` global: they encode world-scale *policy*, and a math
library should hold no opinion about how big a world is.

## Naming

Every symbol carries a `fix` / `FIX_` prefix. Nothing here is named `b3`, because
none of it belongs to box3d or fixed3d any more — the fixed-point types are this library's, and
a consumer that wants its own vocabulary wraps them in its own types. fixed3d does
exactly that.

Two families that read alike are deliberately distinguished: scalar operations are
`fixMin`, `fixMax`, `fixAbs`, `fixClamp`, `fixLerp`, `fixMul`; the componentwise
vector forms are `fixVecMin`, `fixVecMax`, `fixVecAbs`, `fixVecClamp`, `fixVecLerp`,
`fixVecMul`. In fixed3d these were `b3FixMin` and `b3Min`, a one-token difference for
two different operations.

## Building

```cmake
add_subdirectory( fixed )
target_link_libraries( my_game PRIVATE fixed )
```

Or add `include/` and the two files in `src/`. C11 is required — the vector headers return
compound literals, which MSVC's C compiler accepts only under `/std:c11` or later.

Consumer hooks (`FIX_API`, `FIX_ASSERT`, `FIX_SATURATE` and the rest) are documented in
[USAGE.md](USAGE.md); every macro is guarded separately, so overriding one keeps the
library's definitions for the rest.

## Provenance & license

Derived from [Fixed3D](https://github.com/mas-bandwidth/fixed3d) by Mas Bandwidth LLC 
and [Box3D](https://github.com/erincatto/box3d) by Erin Catto (MIT). Box3D's
copyright and MIT license (`LICENSE`) are retained and apply to all Box3D-derived
material. The fixed-point conversion — the `fixed_t` type and its arithmetic, the
deterministic integer transcendentals, and this library's assembly — is derivative work
by Más Bandwidth LLC.

The library is offered under MIT; the Box3D-derived portions remain MIT in every case.
Whether additional or alternative terms apply to the fixed-point additions is under review
with counsel. See `NOTICE`.
