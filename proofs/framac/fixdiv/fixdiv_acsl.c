// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Frama-C WP wrappers for the production fixDiv. No library header is edited.
// The total-function contract below states only documented saturation and range.
// Branch wrappers isolate the zero, fast and wide paths for bounded diagnostics.
// Proofs inline fixDiv itself; helper functions remain separate proof obligations.

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

/*@ requires b == 0;
  @ assigns \nothing;
  @ ensures a > 0 ==> \result == 9223372036854775807;
  @ ensures a < 0 ==> \result == -9223372036854775807;
  @ ensures a == 0 ==> \result == 0;
*/
fixed_t proveFixDivZero( fixed_t a, fixed_t b )
{
	return fixDiv( a, b );
}

/*@ requires b != 0;
  @ requires -140737488355328 < a < 140737488355328;
  @ assigns \nothing;
  @ ensures -9223372036854775807 <= \result <= 9223372036854775807;
*/
fixed_t proveFixDivFast( fixed_t a, fixed_t b )
{
	return fixDiv( a, b );
}

/*@ requires b != 0;
  @ requires a <= -140737488355328 || a >= 140737488355328;
  @ assigns \nothing;
  @ ensures -9223372036854775807 <= \result <= 9223372036854775807;
*/
fixed_t proveFixDivWide( fixed_t a, fixed_t b )
{
	return fixDiv( a, b );
}
