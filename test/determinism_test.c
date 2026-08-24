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

static uint64_t fnv2 = 0xCBF29CE484222325ULL;
static void mix2( fixed_t v ) { const uint8_t* b=(const uint8_t*)&v;
    for (int i=0;i<8;i++){ fnv2 ^= b[i]; fnv2 *= 0x00000100000001B3ULL; } }

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

    // A SECOND STREAM, WITH NEGATIVE OPERANDS.
    //
    // The stream above cannot produce one. `s >> 16` is a uint64 below 2^48, so casting
    // it to int64 and taking it modulo 2^34 yields a NON-NEGATIVE value every time --
    // which means the library's foundational determinism test had never fed fixMul or
    // fixDiv a negative input. That went unnoticed while the 128-bit intermediate was a
    // compiler builtin and the sign extension was the hardware's problem. It stopped
    // being invisible when the intermediate became code this library owns: a deliberately
    // broken sign extension in the emulated widening multiply left this hash untouched.
    //
    // Kept as a separate loop with its own frozen constant rather than folded into the
    // stream above, so the hash frozen in 2026-07 keeps meaning exactly what it meant.
    fnv2 = 0xCBF29CE484222325ULL;
    s = 0xD1B54A32D192ED03ULL;
    for ( int i = 0; i < 200000; i++ )
    {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        fixed_t a = (fixed_t)( (int64_t)s >> 30 );
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        fixed_t b = (fixed_t)( (int64_t)s >> 30 );
        if ( b == 0 ) b = -FIX_ONE;
        mix2( fixMul( a, b ) );
        mix2( fixDiv( a, b ) );
        mix2( fixSqrt( a ) ); // negative input returns 0, which is part of the contract
        mix2( fixAbs( a ) );
        mix2( fixFloor( a ) );
        mix2( fixCeil( b ) );
        mix2( fixClamp( a, -FIX_ONE, FIX_ONE ) );
        mix2( fixFromInt( (int64_t)a >> 20 ) );
        mix2( (fixed_t)fixRoundToInt( a ) );
        mix2( (fixed_t)fixFloorToInt( a ) );
        mix2( (fixed_t)fixTruncToInt( a ) );
    }
    printf( "fixed-core signed determinism hash = 0x%016llx\n", (unsigned long long)fnv2 );
    // FROZEN 2026-08-24. Captured on arm64 and confirmed by every arm of the CI matrix,
    // including the emulated-128-bit arm, which is the one this stream exists to hold.
    if ( fnv2 != 0x19889034915c2911ULL )
    {
        printf( "DETERMINISM FAILURE (signed stream): expected 0x19889034915c2911\n" );
        return 1;
    }
    return 0;
}
