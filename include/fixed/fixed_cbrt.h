// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT

#pragma once

#include "fixed/base.h"
#include "fixed/fixed.h"

/// Correctly rounded cube root of a Q48.16 value, in Q48.16 raw units.
///
/// Returns RN( cbrt( x ) ) for every int64 input. The computation is the exact integer
/// cube root of |raw| * 2^32 followed by an exact nearest-even rounding decision, so the
/// result is correctly rounded by construction rather than within a bound. The function
/// is odd exactly: fixCbrtExact( -x ) == -fixCbrtExact( x ), including for INT64_MIN.
FIX_API fixed_t fixCbrtExact( fixed_t x );
