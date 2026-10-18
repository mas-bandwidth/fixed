// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Frama-C WP wrapper for fixDiv. Read by frama-c only: no build target, no CTest test and
// no CI job compiles this file.
//
// The contract states only what include/fixed/fixed.h documents for fixDiv: it is total
// (division by zero saturates with the sign of the numerator, 0/0 == 0), the quotient past
// the range saturates, it assigns nothing and it has no precondition. The header is not
// edited.
//
// This wrapper exists because the main card's proveFixDiv never left WP's VC generation.
// The suspected cause is -inline-calls @inline pulling the 128-iteration
// fixEmuUInt128DivMod loop (fixed_int128.h:417, no loop assigns, no variant) into the
// proveFixDiv VC. The Makefile therefore runs this wrapper WITHOUT -inline-calls, so the
// wide path is one call to fixInt128Div and the loop stays out of this function's VC; the
// loop's own goals belong to proveInt128Div, which the main card already runs.
//
// Nothing here has been run: the bench has no reachable WP (see notes.md).

#include <stdint.h>

#include "fixed/fixed.h"

/*@ assigns \nothing;
  @ ensures b == 0 && a > 0 ==> \result == 9223372036854775807;
  @ ensures b == 0 && a < 0 ==> \result == -9223372036854775807;
  @ ensures b == 0 && a == 0 ==> \result == 0;
  @ ensures -9223372036854775807 <= \result <= 9223372036854775807;
*/
fixed_t proveFixDiv( fixed_t a, fixed_t b )
{
	return fixDiv( a, b );
}
