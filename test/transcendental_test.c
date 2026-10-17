// SPDX-License-Identifier: MIT
// Determinism + correctness for the fixed-point transcendentals.
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix( fixed_t v ){ const uint8_t* b=(const uint8_t*)&v; for(int i=0;i<8;i++){ fnv^=b[i]; fnv*=0x00000100000001B3ULL; } }

static int fails = 0;
#define CHECK( cond ) \
    do { if ( !( cond ) ) { printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); fails++; } } while ( 0 )

int main( void )
{
    // --- determinism: hash sin/cos/atan2 over a deterministic angle sweep ---
    for ( int i = -100000; i < 100000; i++ )
    {
        fixed_t ang = fixFromDouble( i * 0.0001 );   // ~[-10,10] radians
        fixCosSin cs = fixComputeCosSin( ang );
        mix( cs.cosine ); mix( cs.sine );
        mix( fixAtan2( ang, fixFromDouble( 1.0 ) ) );
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
    CHECK( fixAtan2( 0, 0 ) == 0 );
    CHECK( fixAtan2( 0, FIX_ONE ) == 0 );
    CHECK( fixAtan2( FIX_ONE, 0 ) == 102944 );
    CHECK( fixAtan2( -FIX_ONE, 0 ) == -102944 );
    CHECK( fixAtan2( 0, -FIX_ONE ) == 205887 );

    // --- unwind: always falls within [-pi, pi], including HUGE angles (the 128-bit path) ---
    {
        fixed_t piBound = 205888; // pi in Q48.16 (+1 ulp headroom)
        fixed_t huges[] = { FIX_MAX, FIX_MIN, FIX_MAX / 2, FIX_MIN / 2,
                            FIX_MAX - 12345, ( (int64_t)1 << 60 ), -( (int64_t)1 << 60 ), 0 };
        for ( unsigned i = 0; i < sizeof( huges ) / sizeof( huges[0] ); i++ )
        {
            fixed_t u = fixUnwindAngle( huges[i] );
            CHECK( u >= -piBound && u <= piBound );
        }
        // periodicity: unwind(x) == unwind(x + 2*pi*k) within 1 ulp per turn
        fixed_t twoPi = FIX( 6.28318530718 );
        for ( int k = 1; k <= 8; k++ )
        {
            fixed_t x = FIX( 1.25 );
            fixed_t d = fixUnwindAngle( x + k * twoPi ) - fixUnwindAngle( x );
            if ( d < 0 ) d = -d;
            CHECK( d <= (fixed_t)k );
        }
    }

    // --- identities over a sweep: cos^2+sin^2 == 1 (the normalization guarantee),
    // --- sine odd / cosine even, all within a couple of ulps
    {
        fixed_t maxUnit = 0, maxOdd = 0, maxEven = 0;
        for ( int i = -6283; i <= 6283; i += 7 )
        {
            fixed_t x = fixFromDouble( i * 0.001 );
            fixCosSin p = fixComputeCosSin( x );
            fixCosSin m = fixComputeCosSin( -x );
            fixed_t unit = fixMul( p.cosine, p.cosine ) + fixMul( p.sine, p.sine ) - FIX_ONE;
            if ( unit < 0 ) unit = -unit;
            if ( unit > maxUnit ) maxUnit = unit;
            fixed_t odd = p.sine + m.sine;   if ( odd < 0 ) odd = -odd;
            fixed_t even = p.cosine - m.cosine; if ( even < 0 ) even = -even;
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
        fixCosSin cs = fixComputeCosSin( fixFromDouble( a ) );
        double ce = fabs( fixToDouble(cs.cosine) - cos(a) );
        double se = fabs( fixToDouble(cs.sine)   - sin(a) );
        if ( ce>maxCosErr ) maxCosErr=ce;
        if ( se>maxSinErr ) maxSinErr=se;
        double at = fixToDouble( fixAtan2( fixFromDouble(a), fixFromDouble(1.0) ) );
        double ae = fabs( at - atan2(a,1.0) );
        if ( ae>maxAtanErr ) maxAtanErr=ae;
    }
    printf( "max err vs libm:  cos=%.6f  sin=%.6f  atan2=%.6f rad\n", maxCosErr, maxSinErr, maxAtanErr );
    printf( "sanity: cos(0)=%.5f sin(0)=%.5f cos(pi/2)=%.5f sin(pi/2)=%.5f\n",
        fixToDouble(fixCos(fixFromDouble(0))), fixToDouble(fixSin(fixFromDouble(0))),
        fixToDouble(fixCos(fixFromDouble(1.57079632679))), fixToDouble(fixSin(fixFromDouble(1.57079632679))) );

    // --- fixComputeCosSinPrecise: within one ulp of the true value of the input angle ---
    // The reference is libm at double precision on the exact double value of the fixed input,
    // which is itself within 1e-16 of the truth, so a one-ulp (2^-16) bound is a real bound.
    {
        static uint64_t pfnv = 0xCBF29CE484222325ULL;
        #define PMIX( v ) do { fixed_t pv = ( v ); const uint8_t* pb = (const uint8_t*)&pv; \
            for ( int pi = 0; pi < 8; pi++ ) { pfnv ^= pb[pi]; pfnv *= 0x00000100000001B3ULL; } } while ( 0 )
        double maxErr = 0.0;
        fixed_t maxUnitP = 0;
        // every raw angle step across two turns near zero, then a strided sweep of the range
        for ( int64_t raw = -( (int64_t)1 << 19 ); raw <= ( (int64_t)1 << 19 ); raw += 3 )
        {
            fixCosSin cs = fixComputeCosSinPrecise( (fixed_t)raw );
            double a = fixToDouble( (fixed_t)raw );
            double e = fmax( fabs( fixToDouble( cs.cosine ) - cos( a ) ), fabs( fixToDouble( cs.sine ) - sin( a ) ) );
            if ( e > maxErr ) maxErr = e;
            fixed_t unit = fixMul( cs.cosine, cs.cosine ) + fixMul( cs.sine, cs.sine ) - FIX_ONE;
            if ( unit < 0 ) unit = -unit;
            if ( unit > maxUnitP ) maxUnitP = unit;
            PMIX( cs.cosine ); PMIX( cs.sine );
        }
        uint64_t st = 0x2545F4914F6CDD1DULL;
        for ( int i = 0; i < 200000; i++ )
        {
            st = st * 6364136223846793005ULL + 1442695040888963407ULL;
            // angles up to about +/-2^30 radians, signed
            fixed_t raw = (fixed_t)( (int64_t)st >> 17 );
            fixCosSin cs = fixComputeCosSinPrecise( raw );
            double a = fixToDouble( raw );
            double e = fmax( fabs( fixToDouble( cs.cosine ) - cos( a ) ), fabs( fixToDouble( cs.sine ) - sin( a ) ) );
            if ( e > maxErr ) maxErr = e;
            PMIX( cs.cosine ); PMIX( cs.sine );
        }
        // the extremes of the range, against libm while the raw value fits a double exactly
        fixed_t extremes[] = { FIX_MAX, FIX_MIN, FIX_MAX / 3, -( FIX_MAX / 7 ), ( (int64_t)1 << 53 ) - 1, -( ( (int64_t)1 << 53 ) - 1 ), 1, -1, 0 };
        for ( unsigned i = 0; i < sizeof( extremes ) / sizeof( extremes[0] ); i++ )
        {
            fixCosSin cs = fixComputeCosSinPrecise( extremes[i] );
            double a = fixToDouble( extremes[i] );
            // the double reference is exact only while the raw value fits 53 bits
            if ( extremes[i] > -( (int64_t)1 << 53 ) && extremes[i] < ( (int64_t)1 << 53 ) )
            {
                double e = fmax( fabs( fixToDouble( cs.cosine ) - cos( a ) ), fabs( fixToDouble( cs.sine ) - sin( a ) ) );
                if ( e > maxErr ) maxErr = e;
            }
            PMIX( cs.cosine ); PMIX( cs.sine );
        }
        // Past 2^30 radians, out to the top of the range. libm on the double of the angle is no
        // reference there (the raw value no longer fits 53 bits), so reduce by pi/2 exactly in
        // the test: x - k pi/2 with pi/2 to 126 fraction bits, in the compiler's native 128-bit
        // integers, then libm on the small remainder. Not mixed into the frozen hash above.
#if defined( __SIZEOF_INT128__ )
        {
            __extension__ typedef __int128 RefInt128;
            const RefInt128 H = (RefInt128)INT64_C( 0x6487ed5110b4611a );  // floor( pi/2 * 2^62 )
            const RefInt128 L = (RefInt128)INT64_C( 0x62633145c06e0e68 );  // the next 64 bits
            double maxHigh = 0.0;
            fixed_t worstAngle = 0;
            uint64_t sh = 0x9C6E2F0D3B1A5E47ULL;
            for ( int i = 0; i < 400000; i++ )
            {
                sh = sh * 6364136223846793005ULL + 1442695040888963407ULL;
                fixed_t raw;
                if ( i < 64 ) raw = i < 32 ? FIX_MAX - i * INT64_C( 9876543211 ) : -( FIX_MAX - ( i - 32 ) * INT64_C( 1234567891 ) );
                else raw = (fixed_t)( (int64_t)sh >> ( 1 + i % 17 ) ); // |radians| from 2^30 to 2^47
                RefInt128 x = (RefInt128)raw * ( (RefInt128)1 << 46 );    // Q2.62, exact
                RefInt128 k = ( x + ( x < 0 ? -H / 2 : H / 2 ) ) / H;      // nearest quadrant
                RefInt128 I = x - k * H, kL = k * L;                       // r * 2^62 = I - kL / 2^64
                double r = ( (double)I - (double)kL * 0x1p-64 ) * 0x1p-62;
                double c = cos( r ), sn = sin( r );
                int quadrant = (int)( (int64_t)k & 3 );
                double rc = quadrant == 0 ? c : quadrant == 1 ? -sn : quadrant == 2 ? -c : sn;
                double rs = quadrant == 0 ? sn : quadrant == 1 ? c : quadrant == 2 ? -sn : -c;
                fixCosSin cs = fixComputeCosSinPrecise( raw );
                double e = fmax( fabs( fixToDouble( cs.cosine ) - rc ), fabs( fixToDouble( cs.sine ) - rs ) );
                if ( e > maxHigh ) { maxHigh = e; worstAngle = raw; }
            }
            printf( "precise cos/sin past 2^30 rad, to the top of the range: max err %.3e (%.3f ulp) at %.6e rad\n", maxHigh,
                    maxHigh * 65536.0, (double)worstAngle / 65536.0 );
            CHECK( maxHigh <= 1.0 / 65536.0 );
        }
#else
        printf( "precise cos/sin past 2^30 rad: skipped, no native 128-bit integer for the reference\n" );
#endif
        CHECK( fixComputeCosSinPrecise( 0 ).cosine == FIX_ONE && fixComputeCosSinPrecise( 0 ).sine == 0 );
        printf( "precise cos/sin: max err vs libm %.3e (%.3f ulp), unit-circle %lld ulp\n", maxErr, maxErr * 65536.0,
                (long long)maxUnitP );
        CHECK( maxErr <= 1.0 / 65536.0 );
        CHECK( maxUnitP <= 2 );
        printf( "precise cos/sin determinism hash = 0x%016llx\n", (unsigned long long)pfnv );
        // FROZEN 2026-10-17 (rowan/stone-needs), verified identical on x86-64 (hetzner) and arm64 (macOS).
        if ( pfnv != 0x71f0b7ede7f72b06ULL )
        {
            printf( "DETERMINISM FAILURE (precise cos/sin): expected 0x71f0b7ede7f72b06\n" );
            fails++;
        }
    }
    return ( fails == 0 && maxCosErr < 0.01 && maxSinErr < 0.01 && maxAtanErr < 0.01 ) ? 0 : 1;
}
