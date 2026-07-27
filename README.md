# fixed

A small, standalone, deterministic fixed-point math library. The core type is a
64-bit `fixed_t` (Q48.16), with pure-integer arithmetic and integer-only
transcendentals — so results are **bit-identical on every platform and
architecture**, which floating point is not.

## Why

Deterministic lockstep and client/server simulations require every machine to
compute the same numbers from the same inputs. Floating point breaks this:
fused multiply-add, `libm` transcendentals, and `-ffp-contract` differ across
compilers and architectures. Fixed-point removes the whole class of divergence.

This library is extracted from the fixed-point core of [fixed3d](https://github.com/mas-bandwidth/fixed3d), 
which is a fixed point port of [box3d](https://github.com/erincatto/box3d) by Erin Catto (see `LICENSE`), so it
can be used independently of the physics engine.

## Guarantee, checked in code

`test/determinism_test.c` runs the core ops over a deterministic input stream and
hashes every result. The hash is identical across arm64 and x86-64 (validated),
and CI extends that to Linux/libstdc++ and Windows/MSVC.

## Status

- [x] Core scalar type + arithmetic (`fixed.h`): mul, div, sqrt, abs, floor,
      ceil, clamp, conversions — extracted and cross-arch determinism-validated.
- [x] Integer transcendentals (`fixComputeCosSin`, `fixAtan2`, `fixSin`, `fixCos`,
      `fixUnwindAngle`) — pure fixed-point, cross-arch determinism-validated, and
      correct vs libm (cos/sin < 0.0017, atan2 < 0.00004 rad).
- [x] Vector / quaternion / matrix / transform / position types on `fixed_t`
      (`fixed_vec.h`), with their arithmetic, and the validity predicates
      (`fixIsValidVec3`, `fixIsValidQuat`, `fixIsValidMatrix3`, `fixIsValidTransform`,
      `fixIsValidFixed`).
- [x] Fixed-point time (`fixed_time.h`): Q32.32, with exact conversion to and
      from `fixed_t`.
- [x] Wide 128-bit primitives (`fixed_wide.h`): the Q112.16 scalar and position
      types, and the boundary vocabulary that moves values between wide world
      space and local Q48.16 by exact integer subtract.
- [x] Bounding volumes and planes (`fixAABB`, `fixAABBWide`, `fixPlane`) with their
      operations and validity predicates. Both widths are exported unconditionally:
      a consumer selects narrow or wide by which type it names, never by a compile
      flag that silently changes an ABI.
- [ ] box3d depends on `fixed` for its fixed-point core.

Deliberately **not** here: geometric queries (`b3SegmentDistance`, `b3LineDistance`,
`b3PointToSegmentDistance`) and inertia (`b3Steiner`). Those are physics that happens
to be written in fixed point, not fixed-point math, and they belong with the solver.
Nor `b3IsBoundedAABB` / `b3IsSaneAABB`, which resolve `B3_HUGE` through box3d's mutable
`b3GetLengthUnitsPerMeter()` global: they encode world-scale *policy*, and a math
library should hold no opinion about how big a world is.

## Naming

Every symbol carries a `fix` / `FIX_` prefix. Nothing here is named `b3`, because
none of it belongs to box3d any more — the fixed-point types are this library's, and
a consumer that wants its own vocabulary wraps them in its own types. box3d does
exactly that.

Two families that read alike are deliberately distinguished: scalar operations are
`fixMin`, `fixMax`, `fixAbs`, `fixClamp`, `fixLerp`, `fixMul`; the componentwise
vector forms are `fixVecMin`, `fixVecMax`, `fixVecAbs`, `fixVecClamp`, `fixVecLerp`,
`fixVecMul`. In box3d these were `b3FixMin` and `b3Min`, a one-token difference for
two different operations.

## Provenance & license

Derived from [Box3D](https://github.com/erincatto/box3d) by Erin Catto (MIT). Box3D's
copyright and MIT license (`LICENSE`) are retained and apply to all Box3D-derived
material. The fixed-point conversion — the `fixed_t` type and its arithmetic, the
deterministic integer transcendentals, and this library's assembly — is derivative work
by Más Bandwidth LLC.

The library is offered under MIT; the Box3D-derived portions remain MIT in every case.
Whether additional or alternative terms apply to the fixed-point additions is under review
with counsel. See `NOTICE`.
