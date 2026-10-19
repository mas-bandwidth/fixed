// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// A negative control: the zero branch really returns zero, but this wrapper
// claims one. WP must leave planted_zero_result non-Valid and strict_check
// must report its actual verdict, without calling a Timeout a Failed.
#include <stdint.h>
#include "fixed/fixed.h"
#include "helpers_acsl.h"

/*@ requires a == 0 && b == 0;
  @ assigns \nothing;
  @ ensures planted_zero_result: \result == 1;
*/
fixed_t proveFixDiv( fixed_t a, fixed_t b )
{
    return fixDiv( a, b );
}
