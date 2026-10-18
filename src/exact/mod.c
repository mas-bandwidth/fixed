// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC -- fixed-point exact remainder
// SPDX-License-Identifier: MIT
// fixMod (Lua floored) and fixFmod (C fmod) on Q48.16 raws, exact and integer only.
//
// WHY EVERY STEP IS EXACT. A Q48.16 raw is an integer multiple of 2^-16, so a and b
// are integers in raw units. C's % on signed integers is the truncated remainder and is
// defined for every (a, b) with b != 0; it returns exactly a - trunc(a/b)*b with no
// rounding. The only two undefined or trapping cases are b == 0 (idiv #DE -> SIGFPE)
// and the integer overflow a == INT64_MIN, b == -1 (the quotient 2^63 is not
// representable, so the hardware division traps; the remainder is 0 in both cases).
// Both are guarded before any division. On top of the truncated remainder r, the
// floored remainder is r when r == 0 or a and b have the same sign (trunc == floor),
// and r + b when they differ (floor = trunc - 1, so the remainder is r + b). |r + b|
// is < |b|, so it never overflows. The result is the exact mathematical remainder and
// the integer specification the CBMC card verifies against.
#include "fixed/fixed_mod.h"

#include <limits.h>

fixed_t fixFmod( fixed_t a, fixed_t b )
{
	if ( b == 0 )
	{
		return 0;
	}
	if ( a == (fixed_t)INT64_MIN && b == -1 )
	{
		return 0;
	}
	return a % b;
}

fixed_t fixMod( fixed_t a, fixed_t b )
{
	fixed_t r = fixFmod( a, b );
	if ( r != 0 && ( ( a < 0 ) != ( b < 0 ) ) )
	{
		r += b;
	}
	return r;
}
