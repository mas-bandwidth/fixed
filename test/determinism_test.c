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

#if defined( __SIZEOF_INT128__ )
// The fixMulDiv contract, a * b / c with one truncation toward zero, computed by the compiler's
// own 128-bit integers: independent of the library's fixInt128 seam, native or emulated.
__extension__ typedef __int128 RefInt128;
static fixed_t refMulDiv( fixed_t a, fixed_t b, fixed_t c )
{
    RefInt128 p = (RefInt128)a * (RefInt128)b;
    if ( c == 0 ) return p == 0 ? 0 : ( p < 0 ? FIX_MIN : FIX_MAX );
    RefInt128 q = p / c; // C division truncates toward zero
    if ( q > (RefInt128)FIX_MAX ) return FIX_MAX;
    if ( q < (RefInt128)FIX_MIN ) return FIX_MIN;
    return (fixed_t)q;
}
#define HAVE_REF_MULDIV 1
#endif

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

    // A THIRD STREAM: fixMulDiv, the single-rounding a * b / c. Checked against the compiler's
    // native __int128 where it has one (refMulDiv above: an independent implementation, which
    // the emulated-128-bit build is held to as well), and against known answers everywhere,
    // then hashed with its own frozen constant.
    {
        uint64_t fnv3 = 0xCBF29CE484222325ULL;
        int bad = 0, checked = 0;
        s = 0x8CB92BA72F3D8DD7ULL;
        for ( int i = 0; i < 200000; i++ )
        {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            fixed_t a = (fixed_t)( (int64_t)s >> ( 20 + ( i % 24 ) ) );
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            fixed_t b = (fixed_t)( (int64_t)s >> ( 20 + ( ( i / 24 ) % 24 ) ) );
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            fixed_t c = (fixed_t)( (int64_t)s >> ( 16 + ( ( i / 7 ) % 40 ) ) );
            if ( i % 997 == 0 ) c = 0;
            fixed_t r = fixMulDiv( a, b, c );
#if defined( HAVE_REF_MULDIV )
            if ( r != refMulDiv( a, b, c ) ) bad++;
            checked++;
#endif
            const uint8_t* bytes = (const uint8_t*)&r;
            for ( int k = 0; k < 8; k++ ) { fnv3 ^= bytes[k]; fnv3 *= 0x00000100000001B3ULL; }
        }
        // known answers: exact, truncating toward zero, single rounding, zero divisor
        if ( fixMulDiv( FIX( 3.0 ), FIX( 4.0 ), FIX( 2.0 ) ) != FIX( 6.0 ) ) bad++;
        if ( fixMulDiv( FIX( -1.0 ), FIX( 1.0 ), FIX( 3.0 ) ) != -21845 ) bad++;   // -0.33333 truncated
        if ( fixMulDiv( 1, 1, 1 ) != 1 ) bad++;                                     // 2^-32 / 2^-16 = 2^-16
        if ( fixMulDiv( FIX( 1.0 ), FIX( 1.0 ), 0 ) != FIX_MAX ) bad++;
        if ( fixMulDiv( FIX( -1.0 ), FIX( 1.0 ), 0 ) != FIX_MIN ) bad++;
        if ( fixMulDiv( 0, FIX( 1.0 ), 0 ) != 0 ) bad++;
        if ( fixMulDiv( FIX_MAX, FIX_MAX, 1 ) != FIX_MAX ) bad++;
        if ( fixMulDiv( FIX_MAX, -FIX_MAX, 1 ) != FIX_MIN ) bad++;
        // a small product keeps its fraction bits where fixDiv( fixMul ) loses them
        if ( fixMulDiv( 3, 5, 7 ) != 2 || fixDiv( fixMul( 3, 5 ), 7 ) != 0 ) bad++;
        // truncation toward zero in every sign combination (raw: 7 * 1 / 2 = 3.5)
        if ( fixMulDiv( 7, 1, 2 ) != 3 || fixMulDiv( -7, 1, 2 ) != -3 || fixMulDiv( 7, -1, 2 ) != -3 ) bad++;
        if ( fixMulDiv( 7, 1, -2 ) != -3 || fixMulDiv( -7, -1, 2 ) != 3 || fixMulDiv( -7, -1, -2 ) != -3 ) bad++;
        // products far past 64 bits, exact quotients in range
        if ( fixMulDiv( FIX_MAX, FIX_MAX, FIX_MAX ) != FIX_MAX ) bad++;
        if ( fixMulDiv( -FIX_MAX, FIX_MAX, FIX_MAX ) != -FIX_MAX ) bad++;
        if ( fixMulDiv( (fixed_t)1 << 40, (fixed_t)1 << 40, (fixed_t)1 << 20 ) != (fixed_t)1 << 60 ) bad++;
        if ( fixMulDiv( INT64_C( 3037000499 ), INT64_C( 3037000499 ), INT64_C( 3037000499 ) ) != INT64_C( 3037000499 ) ) bad++;
        // (2^62 + 1) * 3 / 4 = 3458764513820540928.75, truncated
        if ( fixMulDiv( ( (fixed_t)1 << 62 ) + 1, 3, 4 ) != INT64_C( 3458764513820540928 ) ) bad++;
        if ( fixMulDiv( -( ( (fixed_t)1 << 62 ) + 1 ), 3, 4 ) != -INT64_C( 3458764513820540928 ) ) bad++;
        // a quotient one past the range saturates: (2^63 - 1) * 2 / 2 is in range, * 3 / 2 is not
        if ( fixMulDiv( FIX_MAX, 2, 2 ) != FIX_MAX || fixMulDiv( FIX_MAX, 3, 2 ) != FIX_MAX || fixMulDiv( FIX_MAX, -3, 2 ) != FIX_MIN ) bad++;
#if defined( HAVE_REF_MULDIV )
        printf( "fixMulDiv: %d results against native __int128, known answers\n", checked );
#else
        printf( "fixMulDiv: no native __int128 on this compiler; known answers and the hash only\n" );
#endif
        printf( "fixMulDiv determinism hash = 0x%016llx (%d mismatches)\n", (unsigned long long)fnv3, bad );
        // FROZEN 2026-10-17 (rowan/stone-needs), verified identical on x86-64 (hetzner) and arm64 (macOS).
        if ( bad != 0 || fnv3 != 0x53a562ddd9c3ade5ULL )
        {
            printf( "DETERMINISM FAILURE (fixMulDiv stream): expected 0x53a562ddd9c3ade5\n" );
            return 1;
        }
    }
    return 0;
}
