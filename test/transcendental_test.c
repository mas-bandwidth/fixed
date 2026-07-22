// SPDX-License-Identifier: MIT
// Determinism + correctness for the fixed-point transcendentals.
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix( b3Fixed v ){ const uint8_t* b=(const uint8_t*)&v; for(int i=0;i<8;i++){ fnv^=b[i]; fnv*=0x00000100000001B3ULL; } }

static int fails = 0;
#define CHECK( cond ) \
    do { if ( !( cond ) ) { printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); fails++; } } while ( 0 )

int main( void )
{
    // --- determinism: hash sin/cos/atan2 over a deterministic angle sweep ---
    for ( int i = -100000; i < 100000; i++ )
    {
        b3Fixed ang = b3FixFromDouble( i * 0.0001 );   // ~[-10,10] radians
        b3CosSin cs = b3ComputeCosSin( ang );
        mix( cs.cosine ); mix( cs.sine );
        mix( b3Atan2( ang, b3FixFromDouble( 1.0 ) ) );
    }
    printf( "transcendental determinism hash = 0x%016llx\n", (unsigned long long)fnv );
    // FROZEN 2026-07-22, verified identical on arm64 and x86_64. Enforced in CI cross-OS.
    if ( fnv != 0xb3c01ac05f749d61ULL )
    {
        printf( "DETERMINISM FAILURE: expected 0xb3c01ac05f749d61\n" );
        fails++;
    }

    // --- exact axis values (hand-computed from the Q32.32 constants) ---
    // atan2 on the axes hits the exact quadrant constants: Q32ToFix(HALF_PI)=102944,
    // Q32ToFix(PI)=205887. And (0,0) is defined as 0.
    CHECK( b3Atan2( 0, 0 ) == 0 );
    CHECK( b3Atan2( 0, B3_FIXED_ONE ) == 0 );
    CHECK( b3Atan2( B3_FIXED_ONE, 0 ) == 102944 );
    CHECK( b3Atan2( -B3_FIXED_ONE, 0 ) == -102944 );
    CHECK( b3Atan2( 0, -B3_FIXED_ONE ) == 205887 );

    // --- unwind: always falls within [-pi, pi], including HUGE angles (the 128-bit path) ---
    {
        b3Fixed piBound = 205888; // pi in Q48.16 (+1 ulp headroom)
        b3Fixed huges[] = { B3_FIXED_MAX, B3_FIXED_MIN, B3_FIXED_MAX / 2, B3_FIXED_MIN / 2,
                            B3_FIXED_MAX - 12345, ( (int64_t)1 << 60 ), -( (int64_t)1 << 60 ), 0 };
        for ( unsigned i = 0; i < sizeof( huges ) / sizeof( huges[0] ); i++ )
        {
            b3Fixed u = b3UnwindAngle( huges[i] );
            CHECK( u >= -piBound && u <= piBound );
        }
        // periodicity: unwind(x) == unwind(x + 2*pi*k) within 1 ulp per turn
        b3Fixed twoPi = B3_FIX( 6.28318530718 );
        for ( int k = 1; k <= 8; k++ )
        {
            b3Fixed x = B3_FIX( 1.25 );
            b3Fixed d = b3UnwindAngle( x + k * twoPi ) - b3UnwindAngle( x );
            if ( d < 0 ) d = -d;
            CHECK( d <= (b3Fixed)k );
        }
    }

    // --- identities over a sweep: cos^2+sin^2 == 1 (the normalization guarantee),
    // --- sine odd / cosine even, all within a couple of ulps
    {
        b3Fixed maxUnit = 0, maxOdd = 0, maxEven = 0;
        for ( int i = -6283; i <= 6283; i += 7 )
        {
            b3Fixed x = b3FixFromDouble( i * 0.001 );
            b3CosSin p = b3ComputeCosSin( x );
            b3CosSin m = b3ComputeCosSin( -x );
            b3Fixed unit = b3FixMul( p.cosine, p.cosine ) + b3FixMul( p.sine, p.sine ) - B3_FIXED_ONE;
            if ( unit < 0 ) unit = -unit;
            if ( unit > maxUnit ) maxUnit = unit;
            b3Fixed odd = p.sine + m.sine;   if ( odd < 0 ) odd = -odd;
            b3Fixed even = p.cosine - m.cosine; if ( even < 0 ) even = -even;
            if ( odd > maxOdd ) maxOdd = odd;
            if ( even > maxEven ) maxEven = even;
        }
        printf( "identity bounds (ulps): unit-circle=%lld odd=%lld even=%lld\n",
                (long long)maxUnit, (long long)maxOdd, (long long)maxEven );
        CHECK( maxUnit <= 4 );
        CHECK( maxOdd <= 2 );
        CHECK( maxEven <= 2 );
    }

    // --- correctness vs libm (must be a sane approximation, not just deterministic) ---
    double maxCosErr=0, maxSinErr=0, maxAtanErr=0;
    for ( int i = -3140; i <= 3140; i++ )
    {
        double a = i * 0.001;
        b3CosSin cs = b3ComputeCosSin( b3FixFromDouble( a ) );
        double ce = fabs( b3FixToDouble(cs.cosine) - cos(a) );
        double se = fabs( b3FixToDouble(cs.sine)   - sin(a) );
        if ( ce>maxCosErr ) maxCosErr=ce;
        if ( se>maxSinErr ) maxSinErr=se;
        double at = b3FixToDouble( b3Atan2( b3FixFromDouble(a), b3FixFromDouble(1.0) ) );
        double ae = fabs( at - atan2(a,1.0) );
        if ( ae>maxAtanErr ) maxAtanErr=ae;
    }
    printf( "max err vs libm:  cos=%.6f  sin=%.6f  atan2=%.6f rad\n", maxCosErr, maxSinErr, maxAtanErr );
    printf( "sanity: cos(0)=%.5f sin(0)=%.5f cos(pi/2)=%.5f sin(pi/2)=%.5f\n",
        b3FixToDouble(b3Cos(b3FixFromDouble(0))), b3FixToDouble(b3Sin(b3FixFromDouble(0))),
        b3FixToDouble(b3Cos(b3FixFromDouble(1.57079632679))), b3FixToDouble(b3Sin(b3FixFromDouble(1.57079632679))) );
    return ( fails == 0 && maxCosErr < 0.01 && maxSinErr < 0.01 && maxAtanErr < 0.01 ) ? 0 : 1;
}
