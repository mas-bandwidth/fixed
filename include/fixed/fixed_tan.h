// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// tan, correctly rounded on every Q48.16 angle.
//
// fixTanExact returns the correctly rounded (round to nearest) Q48.16 value of
// tan( x * 2^-16 ) for every fixed_t x. It is odd exactly: fixTanExact( -x ) ==
// -fixTanExact( x ). Where the exact result is at or beyond the signed 64-bit range it
// saturates to +/-INT64_MAX (never INT64_MIN), signed by the input and, on an odd
// quarter turn, by the side of the pole.
//
// The method, the tables, the kernel and the error budgets are the tan family of
// docs/design/transcendentals.md, sections 3.1 to 3.4. The Payne-Hanek reduction is
// src/exact/tan_reduce.h; the table and the degree-13 kernel are src/exact/tan_kernel.h.
// Integer only: the native and emulated 128-bit arms give the same bits, and no
// compiler builtin or variable-length array is used.
#pragma once

#include "fixed/base.h"
#include "fixed/fixed.h"

/// tan( x * 2^-16 ) correctly rounded to Q48.16. Odd exactly. Saturates to
/// +/-INT64_MAX (never INT64_MIN). Integer only, bit-identical on the native and
/// emulated 128-bit arms. Test: test/exact/tan_test.c and the MPFR sweep.
FIX_API fixed_t fixTanExact( fixed_t x );
