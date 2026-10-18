// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// tanh, correctly rounded on every Q48.16 input.
//
// The result is RN( tanh( x ) ) in Q48.16 for every raw. tanh is odd exactly, so the
// magnitude is computed and the sign applied at the end; fixTanhExact( INT64_MIN ) is
// -65536. Integer only: native __int128 and the emulated pair produce the same bits, no
// compiler builtin outside the library's wrappers and no variable-length array, so plain
// MSVC builds it too.
#pragma once

#include "fixed/base.h"
#include "fixed/fixed.h"

/// tanh( x ) correctly rounded to Q48.16 for every raw. Odd exactly. |raw| <= 1860
/// returns raw unchanged, |raw| >= 408835 saturates to +/-65536, and everything between
/// is computed from the exp kernel (src/exact/exp_kernel.h). Test:
/// test/exact/tanh_test.c and its emulated arm.
FIX_API fixed_t fixTanhExact( fixed_t x );
