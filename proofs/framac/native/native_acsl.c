// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Frama-C WP wrappers for the NATIVE __int128 arm. Read by frama-c only: no build
// target, no CTest test and no CI job compiles this file. See results.md.
//
// The main card's arm (proofs/framac/kernels_acsl.c) ran without a size define, and
// Frama-C's preprocessing does not define __SIZEOF_INT128__, so fixed_int128.h silently
// selected the emulated two-lane path. This file is compiled with
// -D__SIZEOF_INT128__=16. The #error below is load-bearing: a run that reaches the
// emulated branch fails before any goal, so a success can only be the native arm. The
// Makefile's `branch` target prints the macros that prove the same thing to the reader.
//
// Each wrapper calls one library kernel and carries a contract that states only what
// the library already documents: the input ranges it names as preconditions, assigns
// \nothing (every kernel is a pure function of its arguments), and the saturation
// results where they are simple. The library headers are not edited; every annotation
// lives in this file or in int128_native_stub.h. The Makefile runs
//
//   frama-c -cpp-extra-args="-D__SIZEOF_INT128__=16" ...
//
// The divq asm stays in the front end. WP cannot read its body, so the asm's contract
// lives in int128_native_stub.h (proved as a declaration by the `divq` target), and the
// native INT128_MIN / -1 undefined behaviour is shown with a C program under UBSan in
// results.md.

#if defined( FIX_NATIVE_STUB_ONLY )

// One translation unit that proves the divq stub contracts compose. The library headers
// are deliberately not included here: fixed_int128.h and fixed.h define the same names
// with the asm bodies, and this unit is only about the documented contract.
#include "int128_native_stub.h"

/*@ requires d != 0;
    requires ( a >> 64 ) < d;
    assigns *remainder;
    ensures \result == a / d;
    ensures *remainder == a % d;
*/
uint64_t proveUInt128DivRemBy64( fixUInt128 a, uint64_t d, uint64_t* remainder )
{
	return fixUInt128DivRemBy64( a, d, remainder );
}

/*@ requires b != 0;
    requires ( ( b < 0 ? -(fixUInt128)b : (fixUInt128)b ) >> 64 ) == 0;
    requires ( a < 0 ? -(fixUInt128)a : (fixUInt128)a ) >> 64 <
             ( b < 0 ? -(fixUInt128)b : (fixUInt128)b );
    assigns \nothing;
    ensures \result == a / b;
*/
fixInt128 proveInt128Divq( fixInt128 a, fixInt128 b )
{
	return fixInt128Divq( a, b );
}

#else

#include <stdint.h>

// The seed of fixISqrt128High is __builtin_sqrt (FIX_SQRT_SEED in fixed.h). Frama-C has
// no contract for the gcc builtin, so this is the one assumption the proof takes: the
// IEEE square root of a finite non-negative double is finite, non-negative, and
// monotone, so it maps the two seed ranges the kernel feeds it (below 2^104 and below
// 2^64) to at most their square roots, 2^52 and 2^32 (both exact in binary64). The seed
// VALUE is irrelevant to correctness (the repair loops fix any seed); only its range
// matters, for the double-to-uint64 conversion that RTE checks.
/*@ requires \is_finite( x ) && x >= 0.0;
    assigns \result \from x;
    ensures \is_finite( \result ) && \result >= 0.0;
    ensures x <= 20282409603651670423947251286016.0 ==> \result <= 4503599627370496.0;
    ensures x <= 18446744073709551616.0 ==> \result <= 4294967296.0;
*/
double __builtin_sqrt( double x );

#include "fixed/fixed.h"

// If this fires, the define did not reach the preprocessor and this run would be a
// second copy of the emulated arm (the bug this card exists to fix).
#if FIX_INT128_EMULATED
#error "native_acsl.c reached the emulated path: __SIZEOF_INT128__ is not defined (see results.md)"
#endif

// ---- the kernels -----------------------------------------------------------------

// fixMul: round half up, arithmetic shift. Unchecked by default (wraps in two's
// complement through the unsigned round trip, fixed.h); with FIX_SATURATE the result
// saturates to [FIX_MIN, FIX_MAX] = [-INT64_MAX, INT64_MAX].
#if defined( FIX_SATURATE )
/*@ assigns \nothing;
  @ ensures -9223372036854775807 <= \result <= 9223372036854775807;
*/
#else
/*@ assigns \nothing; */
#endif
fixed_t proveFixMul( fixed_t a, fixed_t b )
{
	return fixMul( a, b );
}

// fixISqrt128High: exact integer square root of any unsigned 128-bit value hi:lo.
/*@ assigns \nothing;
  @ ensures hi == 0 && lo == 0 ==> \result == 0;
*/
uint64_t proveFixISqrt128High( uint64_t hi, uint64_t lo )
{
	return fixISqrt128High( hi, lo );
}

// ---- the int128 primitives those kernels use -------------------------------------

// The widening multiply cannot overflow: the largest product of two int64 is 2^126.
/*@ assigns \nothing; */
fixInt128 proveInt128MulI64( int64_t a, int64_t b )
{
	return fixInt128MulI64( a, b );
}

// Addition wraps through the unsigned type on both arms (fixed_int128.h).
/*@ assigns \nothing; */
fixInt128 proveInt128Add( fixInt128 a, fixInt128 b )
{
	return fixInt128Add( a, b );
}

// Native __int128 shifts are undefined outside [0, 127]; the header says the library
// never produces such a count (fixed_int128.h:683), so the range is a precondition.
/*@ requires 0 <= shift <= 127;
  @ assigns \nothing;
*/
fixInt128 proveInt128Shr( fixInt128 a, int shift )
{
	return fixInt128Shr( a, shift );
}

/*@ requires 0 <= shift <= 127;
  @ assigns \nothing;
*/
fixInt128 proveInt128ShiftLeft( fixInt128 a, int shift )
{
	return fixInt128ShiftLeft( a, shift );
}

/*@ requires 0 <= shift <= 127;
  @ assigns \nothing;
*/
fixUInt128 proveUInt128Shl( fixUInt128 a, int shift )
{
	return fixUInt128Shl( a, shift );
}

/*@ requires 0 <= shift <= 127;
  @ assigns \nothing;
*/
fixUInt128 proveUInt128Shr( fixUInt128 a, int shift )
{
	return fixUInt128Shr( a, shift );
}

/*@ assigns \nothing; */
fixUInt128 proveUInt128MulU64( uint64_t a, uint64_t b )
{
	return fixUInt128MulU64( a, b );
}

/*@ assigns \nothing; */
fixUInt128 proveUInt128Add( fixUInt128 a, fixUInt128 b )
{
	return fixUInt128Add( a, b );
}

/*@ assigns \nothing; */
fixUInt128 proveUInt128Sub( fixUInt128 a, fixUInt128 b )
{
	return fixUInt128Sub( a, b );
}

// Division by zero is undefined on both arms (fixed_int128.h: "Every divide in this
// library guards its divisor first"), so b != 0 is the documented precondition. The
// native arm additionally has undefined behaviour for INT128_MIN / -1; that goal is
// deliberately NOT excluded here, so WP reports it, and results.md records it as real
// undefined behaviour shown under UBSan. The emulated arm documents a wrap there.
/*@ requires b != 0;
  @ assigns \nothing;
*/
fixInt128 proveInt128Div( fixInt128 a, fixInt128 b )
{
	return fixInt128Div( a, b );
}

/*@ assigns \nothing; */
int64_t proveInt128ToI64( fixInt128 a )
{
	return fixInt128ToI64( a );
}

/*@ assigns \nothing; */
int proveInt128Compare( fixInt128 a, fixInt128 b )
{
	return ( fixInt128Gt( a, b ) ? 1 : 0 ) + ( fixInt128Lt( a, b ) ? 2 : 0 );
}

/*@ assigns \nothing; */
int proveUInt128Compare( fixUInt128 a, fixUInt128 b )
{
	return ( fixUInt128Gt( a, b ) ? 1 : 0 ) + ( fixUInt128Le( a, b ) ? 2 : 0 ) + ( fixUInt128Ge( a, b ) ? 4 : 0 ) +
		   ( fixUInt128Eq( a, b ) ? 8 : 0 );
}

#endif
