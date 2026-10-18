// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// asin and acos, correctly rounded on every input in [-1, 1].
//
// Both return the correctly rounded (round to nearest, ties to even) Q48.16 value of the
// exact real result. An input outside [-1, 1] is clamped to the nearer end before the
// function is applied: fixAsinExact gives RN(-pi/2) or RN(pi/2), fixAcosExact gives RN(pi)
// or 0. fixAsinExact is odd exactly.
//
// The method, the error budget and the provenance of the polynomial are in the comment on
// src/exact/asin_acos.c. Integer only: native __int128 and the emulated pair produce the
// same bits, and no compiler builtin or variable-length array is used.
#pragma once

#include "fixed/base.h"
#include "fixed/fixed.h"

/// asin( x ) correctly rounded to Q48.16 for every raw in [-1, 1]; outside that domain x is
/// clamped to the nearer end first. Odd exactly. Integer only, bit-identical on the native
/// and emulated 128-bit arms. Test: test/exact/asin_acos_test.c and the MPFR sweep.
FIX_API fixed_t fixAsinExact( fixed_t x );

/// acos( x ) correctly rounded to Q48.16 for every raw in [-1, 1]; outside that domain x is
/// clamped to the nearer end first, giving RN(pi) below -1 and 0 above 1. Integer only,
/// bit-identical on the native and emulated 128-bit arms. Test: test/exact/asin_acos_test.c
/// and the MPFR sweep.
FIX_API fixed_t fixAcosExact( fixed_t x );
