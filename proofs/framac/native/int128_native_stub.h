// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// The contract for the x86-64 divq inline asm, written down once so WP has something to
// reason about. Read by frama-c only; no build target includes this header with
// fixed/fixed.h, because the library already defines the asm inside
// fixUInt128DivRemBy64 (include/fixed/fixed_int128.h:629, native arm) and inside
// fixInt128Div (include/fixed/fixed.h:98). Include this header alone, with
// -DFIX_NATIVE_STUB_ONLY, in a translation unit that only proves the contract.
//
// The contract states only what the code documents:
//
//   - the divisor is non-zero (every divide in this library guards it first);
//   - the high half of the dividend is below the divisor, which is exactly the condition
//     that makes the quotient fit in 64 bits and is what keeps divq from faulting
//     (fixed_int128.h, fixUInt128DivRemBy64);
//   - assigns \nothing: divq writes only its register operands, and the remainder is the
//     out-parameter value its caller stores;
//   - the quotient is a / d truncated toward zero and the remainder is a % d with the
//     sign of the dividend, as fixInt128DivMod documents.

#pragma once

#include <stdint.h>

// Only compiled in the stub translation unit. If a proof source includes fixed/fixed.h
// first, that header's own typedefs are the ones in force and this header is not used.
#ifndef FIX_NATIVE_STUB_TYPES
#define FIX_NATIVE_STUB_TYPES
__extension__ typedef unsigned __int128 fixUInt128;
__extension__ typedef __int128 fixInt128;
#endif

// The asm is the hardware 128/64 divide. Precondition: high half below the divisor, so
// the quotient fits in 64 bits (divq's fault condition is precisely the negation of it).
/*@ requires d != 0;
    requires ( a >> 64 ) < d;
    assigns \nothing;
    ensures \result == a / d;
    ensures *remainder == a % d;
*/
uint64_t fixUInt128DivRemBy64( fixUInt128 a, uint64_t d, uint64_t* remainder );

// The signed asm in fixInt128Div is the same instruction, reached only under
// `( ub >> 64 ) == 0 && uhi < v`, i.e. a non-zero divisor whose magnitude fits in 64
// bits and a quotient that fits in 64 bits. This is the contract of that instruction;
// the wrapper below composes it. INT128_MIN / -1 is excluded by the quotient-fit guard
// (its true quotient is 2^127), so it is not part of this contract; the primitive run
// checks the unguarded generic path separately.
/*@ requires b != 0;
    requires ( ( b < 0 ? -(fixUInt128)b : (fixUInt128)b ) >> 64 ) == 0;
    requires ( a < 0 ? -(fixUInt128)a : (fixUInt128)a ) >> 64 <
             ( b < 0 ? -(fixUInt128)b : (fixUInt128)b );
    assigns \nothing;
    ensures \result == a / b;
*/
fixInt128 fixInt128Divq( fixInt128 a, fixInt128 b );
