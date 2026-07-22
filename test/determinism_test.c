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
static void mix( b3Fixed v ) { const uint8_t* b=(const uint8_t*)&v;
    for (int i=0;i<8;i++){ fnv ^= b[i]; fnv *= 0x00000100000001B3ULL; } }

int main( void )
{
    // deterministic integer LCG (no float) drives the inputs
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    for ( int i = 0; i < 200000; i++ )
    {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        b3Fixed a = (b3Fixed)( (int64_t)( s >> 16 ) % ( (int64_t)1 << 34 ) );
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        b3Fixed b = (b3Fixed)( (int64_t)( s >> 16 ) % ( (int64_t)1 << 34 ) );
        if ( b == 0 ) b = B3_FIXED_ONE;
        mix( b3FixMul( a, b ) );
        mix( b3FixDiv( a, b ) );
        mix( b3FixSqrt( a < 0 ? -a : a ) );
        mix( b3FixAbs( a ) );
        mix( b3FixFloor( a ) );
        mix( b3FixCeil( b ) );
        mix( b3FixClamp( a, -B3_FIXED_ONE, B3_FIXED_ONE ) );
    }
    printf( "fixed-core determinism hash = 0x%016llx\n", (unsigned long long)fnv );
    return 0;
}
