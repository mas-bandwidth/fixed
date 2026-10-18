// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC -- fixed-point exact remainder
// SPDX-License-Identifier: MIT
// Exact Q48.16 remainder: fixMod (Lua floored) and fixFmod (C fmod, truncated).
// Both are exact on raws: the remainder of two Q48.16 values is a Q48.16 value, and
// integer division on the raw int64 values computes it bit for bit. No division trap:
// b == 0 returns 0 for both, and a == INT64_MIN with b == -1 returns 0 (never SIGFPE).
#pragma once
#include "fixed/base.h"
#include "fixed/fixed.h"

/// Lua floored modulo: a - floor(a / b) * b. The result has the sign of b, or is 0.
/// Exact on all raw Q48.16 inputs; b == 0 returns 0 and a == INT64_MIN, b == -1
/// returns 0. Test: test/exact/mod_test.c.
FIX_API fixed_t fixMod( fixed_t a, fixed_t b );

/// C fmod (truncated): a - trunc(a / b) * b. The result has the sign of a, or is 0.
/// Exact on all raw Q48.16 inputs; b == 0 returns 0 and a == INT64_MIN, b == -1
/// returns 0. Test: test/exact/mod_test.c.
FIX_API fixed_t fixFmod( fixed_t a, fixed_t b );
