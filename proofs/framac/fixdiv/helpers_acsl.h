// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// ACSL contracts for every helper in the call tree of fixDiv, emulated arm
// (-DFIX_FORCE_EMULATED_INT128, the only arm the Makefile proves). Each contract is
// attached by redeclaring the helper after fixed/fixed.h; Frama-C merges the contract
// into the library definition, so no library header is edited.
//
// Status per helper (results.md has the target, the summary lines and each reason):
//   PROVED  : WP proves the library body against the contract (Makefile target `helpers`),
//             callees taken by their contracts. Every helper not marked ASSUMED below.
//   ASSUMED : WP does not prove the whole body; the contract is an assumed summary, held
//             by the boundary-value program helpers_check.c (source in notes.md), run under
//             -fsanitize=undefined against native unsigned/signed __int128. Six helpers:
//             fixEmuUInt128DivMod, fixEmuInt128DivMod, fixEmuInt128IsNegative,
//             fixEmuUInt128Shl, fixShiftLeft, fixInt128ShiftLeft.
//
// Call tree of fixDiv on the emulated arm (each node has a contract here):
//   fixDiv -> fixShiftLeft (fast path)
//          -> fixInt128FromI64 -> fixEmuInt128FromI64 -> fixEmuInt128Make
//          -> fixInt128ShiftLeft -> fixInt128ToUnsigned -> fixEmuToUnsigned -> fixEmuUInt128Make
//                                -> fixUInt128Shl -> fixEmuUInt128Shl -> fixEmuUInt128Make
//                                -> fixInt128FromUnsigned -> fixEmuToSigned -> fixEmuInt128Make
//          -> fixInt128Div -> fixEmuInt128Div -> fixEmuInt128DivMod
//                 -> fixEmuInt128IsNegative, fixEmuToUnsigned, fixEmuToSigned,
//                    fixEmuUInt128Neg -> fixEmuUInt128Sub -> fixEmuUInt128Make,
//                    fixEmuUInt128DivMod -> fixEmuUInt128Make, fixEmuUInt128FromU64,
//                        fixEmuUInt128Shl, fixEmuUInt128Ge -> fixEmuUInt128Lt,
//                        fixEmuUInt128Sub, fixEmuUInt128Or
//          -> fixInt128Gt -> fixEmuInt128Gt -> fixEmuInt128Lt
//          -> fixInt128Lt -> fixEmuInt128Lt
//          -> fixInt128ToI64 -> fixEmuInt128ToI64
//
// FIX_ACSL_PLANT is the planted fault: 0 for the real proofs. The `planted` target sets it
// to k, which falsifies the main ensures of helper k only; WP must then fail that helper.
// k = 100 and 101 plant faults in the fixDiv proof itself (see fixdiv_acsl.c and the
// fixShiftLeft contract, whose value facts k = 101 switches off).

#pragma once

#include <stdint.h>
#include "fixed/fixed.h"

#ifndef FIX_ACSL_PLANT
#define FIX_ACSL_PLANT 0
#endif
// FIX_PLANT(k) is 1 only in the planted run for helper k, so a fault in one helper's
// contract never leaks into the contracts its callers rely on.
#define FIX_PLANT( k ) ( ( FIX_ACSL_PLANT ) == ( k ) ? 1 : 0 )

/*@ logic integer FIX_TWO63 = 9223372036854775808;
  @ logic integer FIX_TWO64 = 18446744073709551616;
  @ logic integer FIX_TWO127 = 170141183460469231731687303715884105728;
  @ logic integer FIX_TWO128 = 340282366920938463463374607431768211456;
  @
  @ // Value of an emulated unsigned pair: hi * 2^64 + lo, in [0, 2^128).
  @ logic integer fix_u128(fixEmuUInt128 x) = x.hi * FIX_TWO64 + x.lo;
  @
  @ // Value of an emulated signed pair: two's complement, top bit of hi is the sign.
  @ logic integer fix_i128(fixEmuInt128 x) =
  @   (x.hi < FIX_TWO63 ? x.hi : x.hi - FIX_TWO64) * FIX_TWO64 + x.lo;
  @
  @ // Two's complement reinterpretation between [-2^127, 2^127) and [0, 2^128).
  @ logic integer fix_s2u(integer v) = v >= 0 ? v : v + FIX_TWO128;
  @ logic integer fix_u2s(integer v) = v < FIX_TWO127 ? v : v - FIX_TWO128;
  @
  @ // 64-bit reinterpretation, for fixShiftLeft.
  @ logic integer fix_s2u64(integer v) = v >= 0 ? v : v + FIX_TWO64;
  @ logic integer fix_u2s64(integer v) = v < FIX_TWO63 ? v : v - FIX_TWO64;
  @*/

// ---- construction and lane conversion ----------------------------------------------

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures lanes: \result.hi == hi && \result.lo == lo + FIX_PLANT(1);
  @*/
FIX_ALWAYS_INLINE fixEmuUInt128 fixEmuUInt128Make( uint64_t hi, uint64_t lo );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures lanes: \result.hi == hi && \result.lo == lo + FIX_PLANT(2);
  @*/
FIX_ALWAYS_INLINE fixEmuInt128 fixEmuInt128Make( uint64_t hi, uint64_t lo );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures lanes: \result.hi == 0 && \result.lo == v + FIX_PLANT(3);
  @ ensures value: fix_u128(\result) == v;
  @*/
FIX_ALWAYS_INLINE fixEmuUInt128 fixEmuUInt128FromU64( uint64_t v );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: fix_i128(\result) == v + FIX_PLANT(4);
  @ ensures lanes: \result.lo == fix_s2u64(v) && \result.hi == (v < 0 ? FIX_TWO64 - 1 : 0);
  @*/
FIX_ALWAYS_INLINE fixEmuInt128 fixEmuInt128FromI64( int64_t v );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: fix_i128(\result) == v + FIX_PLANT(5);
  @ ensures lanes: \result.lo == fix_s2u64(v) && \result.hi == (v < 0 ? FIX_TWO64 - 1 : 0);
  @*/
FIX_ALWAYS_INLINE fixInt128 fixInt128FromI64( int64_t v );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures lanes: \result.hi == a.hi && \result.lo == a.lo + FIX_PLANT(6);
  @ ensures value: fix_u128(\result) == fix_s2u(fix_i128(a));
  @*/
FIX_ALWAYS_INLINE fixEmuUInt128 fixEmuToUnsigned( fixEmuInt128 a );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures lanes: \result.hi == a.hi && \result.lo == a.lo + FIX_PLANT(7);
  @ ensures value: fix_i128(\result) == fix_u2s(fix_u128(a));
  @*/
FIX_ALWAYS_INLINE fixEmuInt128 fixEmuToSigned( fixEmuUInt128 a );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures lanes: \result.hi == a.hi && \result.lo == a.lo + FIX_PLANT(8);
  @ ensures value: fix_u128(\result) == fix_s2u(fix_i128(a));
  @*/
FIX_ALWAYS_INLINE fixUInt128 fixInt128ToUnsigned( fixInt128 a );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures lanes: \result.hi == a.hi && \result.lo == a.lo + FIX_PLANT(9);
  @ ensures value: fix_i128(\result) == fix_u2s(fix_u128(a));
  @*/
FIX_ALWAYS_INLINE fixInt128 fixInt128FromUnsigned( fixUInt128 a );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures exact: \result == fix_u2s64(a.lo) + FIX_PLANT(10);
  @ ensures in_range: -FIX_TWO63 <= fix_i128(a) < FIX_TWO63 ==> \result == fix_i128(a);
  @*/
FIX_ALWAYS_INLINE int64_t fixEmuInt128ToI64( fixEmuInt128 a );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures exact: \result == fix_u2s64(a.lo) + FIX_PLANT(11);
  @ ensures in_range: -FIX_TWO63 <= fix_i128(a) < FIX_TWO63 ==> \result == fix_i128(a);
  @*/
FIX_ALWAYS_INLINE int64_t fixInt128ToI64( fixInt128 a );

// ---- comparison ----------------------------------------------------------------------

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: \result != 0 <==> (fix_i128(a) < 0 || FIX_PLANT(22) != 0);
  @*/
// ASSUMED: ensures value times out on Alt-Ergo and Z3 (also at 120 s): neither relates
// the shift a.hi >> 63 to a.hi >= 2^63; a lemma stating it was not proved either.
FIX_ALWAYS_INLINE bool fixEmuInt128IsNegative( fixEmuInt128 a );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: \result != 0 <==> (fix_u128(a) < fix_u128(b) || FIX_PLANT(23) != 0);
  @*/
FIX_ALWAYS_INLINE bool fixEmuUInt128Lt( fixEmuUInt128 a, fixEmuUInt128 b );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: \result != 0 <==> (fix_u128(a) >= fix_u128(b) && FIX_PLANT(28) == 0);
  @*/
FIX_ALWAYS_INLINE bool fixEmuUInt128Ge( fixEmuUInt128 a, fixEmuUInt128 b );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: \result != 0 <==> (fix_i128(a) < fix_i128(b) || FIX_PLANT(24) != 0);
  @*/
FIX_ALWAYS_INLINE bool fixEmuInt128Lt( fixEmuInt128 a, fixEmuInt128 b );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: \result != 0 <==> (fix_i128(a) > fix_i128(b) || FIX_PLANT(25) != 0);
  @*/
FIX_ALWAYS_INLINE bool fixEmuInt128Gt( fixEmuInt128 a, fixEmuInt128 b );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: \result != 0 <==> (fix_i128(a) < fix_i128(b) || FIX_PLANT(26) != 0);
  @*/
FIX_ALWAYS_INLINE bool fixInt128Lt( fixInt128 a, fixInt128 b );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: \result != 0 <==> (fix_i128(a) > fix_i128(b) || FIX_PLANT(27) != 0);
  @*/
FIX_ALWAYS_INLINE bool fixInt128Gt( fixInt128 a, fixInt128 b );

// ---- bitwise, shifts and unsigned arithmetic -----------------------------------------

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures lanes: \result.hi == (a.hi | b.hi) && \result.lo == (a.lo | b.lo) + FIX_PLANT(12);
  @*/
FIX_ALWAYS_INLINE fixEmuUInt128 fixEmuUInt128Or( fixEmuUInt128 a, fixEmuUInt128 b );

// ASSUMED: out_of_range and the body's RTE goals prove; in_range times out (also at
// 120 s): the value of the lane shifts and OR as (u * 2^shift) mod 2^128 for a symbolic
// shift count is non-linear modular arithmetic that Alt-Ergo and Z3 do not close.
// Shift counts outside [0, 127] yield zero (documented at fixEmuUInt128Shl).
/*@ terminates \true;
  @ assigns \nothing;
  @ ensures in_range: 0 <= shift < 128 ==>
  @   fix_u128(\result) == (fix_u128(a) * (1 << shift)) % FIX_TWO128 + FIX_PLANT(13);
  @ ensures out_of_range: shift < 0 || shift >= 128 ==> fix_u128(\result) == 0;
  @*/
FIX_ALWAYS_INLINE fixEmuUInt128 fixEmuUInt128Shl( fixEmuUInt128 a, int shift );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures in_range: 0 <= shift < 128 ==>
  @   fix_u128(\result) == (fix_u128(a) * (1 << shift)) % FIX_TWO128 + FIX_PLANT(14);
  @ ensures out_of_range: shift < 0 || shift >= 128 ==> fix_u128(\result) == 0;
  @*/
FIX_ALWAYS_INLINE fixUInt128 fixUInt128Shl( fixUInt128 a, int shift );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: fix_u128(\result) == (fix_u128(a) + FIX_TWO128 - fix_u128(b)) % FIX_TWO128 + FIX_PLANT(15);
  @*/
FIX_ALWAYS_INLINE fixEmuUInt128 fixEmuUInt128Sub( fixEmuUInt128 a, fixEmuUInt128 b );

/*@ terminates \true;
  @ assigns \nothing;
  @ ensures value: fix_u128(\result) == (FIX_TWO128 - fix_u128(a)) % FIX_TWO128 + FIX_PLANT(16);
  @*/
FIX_ALWAYS_INLINE fixEmuUInt128 fixEmuUInt128Neg( fixEmuUInt128 a );

// ASSUMED: exact and in_range time out (also at 120 s), the same non-linear modular limit
// as fixEmuUInt128Shl ((uint64_t)a << shift for a symbolic shift). RTE goals prove.
// fixShiftLeft: a shift of 64 or more on uint64_t is undefined, so the count is required in
// [0, 63]. The result is the low 64 bits of a * 2^shift, reinterpreted as signed.
/*@ requires 0 <= shift < 64;
  @ terminates \true;
  @ assigns \nothing;
  @ ensures exact: FIX_PLANT(101) != 0 ||
  @   \result == fix_u2s64((fix_s2u64(a) * (1 << shift)) % FIX_TWO64) + FIX_PLANT(17);
  @ ensures in_range: FIX_PLANT(101) != 0 ||
  @   (-FIX_TWO63 <= a * (1 << shift) < FIX_TWO63 ==> \result == a * (1 << shift));
  @*/
FIX_ALWAYS_INLINE fixed_t fixShiftLeft( fixed_t a, int shift );

// ASSUMED: exact and out_of_range prove from the callee contracts; in_range times out
// (also at 120 s): it needs (s2u(v) * 2^s) mod 2^128 == v * 2^s + 2^128 for negative v,
// non-linear modular arithmetic.
// fixInt128ShiftLeft: the low 128 bits of a * 2^shift, reinterpreted as signed; counts
// outside [0, 127] yield zero, as fixUInt128Shl documents.
/*@ terminates \true;
  @ assigns \nothing;
  @ ensures exact: 0 <= shift < 128 ==>
  @   fix_i128(\result) == fix_u2s((fix_s2u(fix_i128(a)) * (1 << shift)) % FIX_TWO128) + FIX_PLANT(18);
  @ ensures in_range: 0 <= shift < 128 && -FIX_TWO127 <= fix_i128(a) * (1 << shift) < FIX_TWO127 ==>
  @   fix_i128(\result) == fix_i128(a) * (1 << shift);
  @ ensures out_of_range: shift < 0 || shift >= 128 ==> fix_i128(\result) == 0;
  @*/
FIX_ALWAYS_INLINE fixInt128 fixInt128ShiftLeft( fixInt128 a, int shift );

// ---- division ------------------------------------------------------------------------

// ASSUMED (not proved by WP): the 128-iteration shift-subtract loop at fixed_int128.h:417
// has no loop invariant or variant, and one cannot be attached without editing the library
// header, which this proof may not do. WP cannot reason about an unannotated loop, so the
// body stays unproved; the contract is checked by helpers_check.c (notes.md).
// Division by zero is UNDEFINED by the header's own contract (the zero result is "an
// implementation detail and not a contract"), so a non-zero divisor is required.
// Out-parameters may be NULL (documented); both are written when non-NULL.
/*@ requires divisor_nonzero: fix_u128(divisor) != 0;
  @ requires quotientOut == \null || \valid(quotientOut);
  @ requires remainderOut == \null || \valid(remainderOut);
  @ requires quotientOut == \null || remainderOut == \null || \separated(quotientOut, remainderOut);
  @ terminates \true;
  @ assigns *(quotientOut + (0 .. (quotientOut == \null ? -1 : 0))),
  @         *(remainderOut + (0 .. (remainderOut == \null ? -1 : 0)));
  @ ensures quotient: quotientOut != \null ==>
  @   fix_u128(*quotientOut) == fix_u128(dividend) / fix_u128(divisor);
  @ ensures remainder: remainderOut != \null ==>
  @   fix_u128(*remainderOut) == fix_u128(dividend) % fix_u128(divisor);
  @*/
FIX_ALWAYS_INLINE void fixEmuUInt128DivMod( fixEmuUInt128 dividend, fixEmuUInt128 divisor, fixEmuUInt128* quotientOut,
											fixEmuUInt128* remainderOut );

// ASSUMED (fixEmuInt128DivMod only; fixEmuInt128Div and fixInt128Div are PROVED from it):
// quotient and remainder time out (also at 120 s). They prove with lemmas on truncating
// division ((-x)/y == -(x/y), x/(-y) == -(x/y)), but Alt-Ergo and Z3 prove neither lemma,
// so the lemmas are not kept and the contract stays an assumed summary.
// Signed division truncates toward zero, as ACSL's / and % do on integers.
// INT128_MIN / -1: the true quotient 2^127 is not representable. Since card
// fixed-int128-div-min-by-minus-one-defined both arms wrap it to INT128_MIN, which is
// defined but is not the exact quotient, so the exact-value contract requires the pair
// (dividend == INT128_MIN, divisor == -1) to be excluded, and says so here.
/*@ requires divisor_nonzero: fix_i128(divisor) != 0;
  @ requires not_min_by_minus_one: !(fix_i128(dividend) == -FIX_TWO127 && fix_i128(divisor) == -1);
  @ requires quotientOut == \null || \valid(quotientOut);
  @ requires remainderOut == \null || \valid(remainderOut);
  @ requires quotientOut == \null || remainderOut == \null || \separated(quotientOut, remainderOut);
  @ terminates \true;
  @ assigns *(quotientOut + (0 .. (quotientOut == \null ? -1 : 0))),
  @         *(remainderOut + (0 .. (remainderOut == \null ? -1 : 0)));
  @ ensures quotient: quotientOut != \null ==>
  @   fix_i128(*quotientOut) == fix_i128(dividend) / fix_i128(divisor) + FIX_PLANT(19);
  @ ensures remainder: remainderOut != \null ==>
  @   fix_i128(*remainderOut) == fix_i128(dividend) % fix_i128(divisor);
  @*/
FIX_ALWAYS_INLINE void fixEmuInt128DivMod( fixEmuInt128 dividend, fixEmuInt128 divisor, fixEmuInt128* quotientOut,
										   fixEmuInt128* remainderOut );

/*@ requires divisor_nonzero: fix_i128(b) != 0;
  @ requires not_min_by_minus_one: !(fix_i128(a) == -FIX_TWO127 && fix_i128(b) == -1);
  @ terminates \true;
  @ assigns \nothing;
  @ ensures value: fix_i128(\result) == fix_i128(a) / fix_i128(b) + FIX_PLANT(20);
  @*/
FIX_ALWAYS_INLINE fixEmuInt128 fixEmuInt128Div( fixEmuInt128 a, fixEmuInt128 b );

/*@ requires divisor_nonzero: fix_i128(b) != 0;
  @ requires not_min_by_minus_one: !(fix_i128(a) == -FIX_TWO127 && fix_i128(b) == -1);
  @ terminates \true;
  @ assigns \nothing;
  @ ensures value: fix_i128(\result) == fix_i128(a) / fix_i128(b) + FIX_PLANT(21);
  @*/
FIX_ALWAYS_INLINE fixInt128 fixInt128Div( fixInt128 a, fixInt128 b );
