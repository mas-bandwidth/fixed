// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// A consumer that overrides exactly ONE macro.
//
// This is the common case, not an exotic one: FIX_API is the macro that carries an
// export decoration, so a library vendoring this one supplies FIX_API and nothing else.
// It should get the library's own definitions for everything it did not touch.
//
// The header used to guard its whole macro block on a single `#ifndef FIX_API`, which
// meant defining that one macro silently suppressed FIX_INLINE, FIX_FORCE_INLINE,
// FIX_LITERAL and FIX_ZERO_INIT along with it. The result was 21 compile errors pointing
// at this library, from a consumer who reasonably expected to override one thing. box3d
// hit exactly this while vendoring and had to hand the entire set down to work around it.
//
// The guards are now per-macro. THIS FILE IS THE READER FOR THAT: it is a compile-only
// test, and it exists because the fix is otherwise invisible -- nothing in the library's
// own build overrides anything, so the whole override path is dark code from in here. If
// someone re-collapses the guards into one block to tidy them, this stops compiling
// immediately instead of the next consumer discovering it.
//
// Deliberately overriding only FIX_API and nothing else. Adding more would weaken the
// test: the bug was specifically about what you DON'T mention.

#define FIX_API /* deliberately empty: a consumer's export decoration goes here */

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include "fixed/fixed_math.h"
#include "fixed/fixed_time.h"
#include <stdio.h>

// Every macro the library must still have defined for itself. If the guards regress,
// compilation fails above before reaching these, but assert them anyway so a partial
// regression (one macro re-collapsed, not all) is also caught.
#if !defined( FIX_INLINE )
	#error "FIX_INLINE was suppressed by a consumer defining only FIX_API"
#endif
#if !defined( FIX_FORCE_INLINE )
	#error "FIX_FORCE_INLINE was suppressed by a consumer defining only FIX_API"
#endif
#if !defined( FIX_LITERAL )
	#error "FIX_LITERAL was suppressed by a consumer defining only FIX_API"
#endif
#if !defined( FIX_ZERO_INIT )
	#error "FIX_ZERO_INIT was suppressed by a consumer defining only FIX_API"
#endif
#if !defined( FIX_ASSERT )
	#error "FIX_ASSERT was suppressed by a consumer defining only FIX_API"
#endif

int main( void )
{
	// Exercise something from each header, so this is a real translation unit and not
	// just a preprocessor exercise. A consumer that cannot actually CALL the library
	// after overriding FIX_API has the same problem by a different route.
	fixVec3 a = { FIX( 1.0f ), FIX( 2.0f ), FIX( 3.0f ) };
	fixVec3 b = { FIX( 4.0f ), FIX( 5.0f ), FIX( 6.0f ) };

	fixed_t d = fixDot( a, b );
	fixVec3 c = fixCross( a, b );
	fixed_t s = fixSqrt( FIX( 4.0f ) );
	fixedWide_t w = fixWideFromFixed( d );
	fixTime t = fixTimeFromFixed( FIX_ONE );
	fixAABB box = { a, b };

	// fixWideEq rather than ==: fixedWide_t is a struct on the compilers with no
	// __int128, and this file exists to be a REAL consumer of every header.
	if ( d == 0 || c.x == 0 || s != FIX( 2.0f ) || fixWideEq( w, fixWideFromFixed( 0 ) ) || t == 0 ||
		 fixAABB_Area( box ) == 0 )
	{
		printf( "override_api: unexpected value\n" );
		return 1;
	}

	printf( "override_api: a consumer overriding only FIX_API compiles and runs\n" );
	return 0;
}
