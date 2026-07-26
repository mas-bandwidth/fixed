// SPDX-License-Identifier: MIT
// Cross-platform determinism known-answer test for the fixed-point core.
// The point of fixed-point math is bit-identical results on every platform/arch.
// This exercises the core ops over a deterministic input stream and hashes every
// result; the hash MUST match across arm64, x86-64, and every target. It is the
// library's foundational guarantee, checked in code.
#include "fixed/fixed.h"
#include <stdint.h>
#include <stdio.h>

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix( fixed_t v ) { const uint8_t* b=(const uint8_t*)&v;
    for (int i=0;i<8;i++){ fnv ^= b[i]; fnv *= 0x00000100000001B3ULL; } }

int main( void )
{
    // deterministic integer LCG (no float) drives the inputs
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    for ( int i = 0; i < 200000; i++ )
    {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        fixed_t a = (fixed_t)( (int64_t)( s >> 16 ) % ( (int64_t)1 << 34 ) );
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        fixed_t b = (fixed_t)( (int64_t)( s >> 16 ) % ( (int64_t)1 << 34 ) );
        if ( b == 0 ) b = FIX_ONE;
        mix( fixMul( a, b ) );
        mix( fixDiv( a, b ) );
        mix( fixSqrt( a < 0 ? -a : a ) );
        mix( fixAbs( a ) );
        mix( fixFloor( a ) );
        mix( fixCeil( b ) );
        mix( fixClamp( a, -FIX_ONE, FIX_ONE ) );
    }
    printf( "fixed-core determinism hash = 0x%016llx\n", (unsigned long long)fnv );
    // FROZEN 2026-07-22, verified identical on arm64 and x86_64. Any platform,
    // compiler, or semantic change that alters a single bit fails here: this is
    // the cross-platform bit-identity guarantee, enforced locally and in CI.
    if ( fnv != 0x3e1c7997594d2019ULL )
    {
        printf( "DETERMINISM FAILURE: expected 0x3e1c7997594d2019\n" );
        return 1;
    }
    return 0;
}
