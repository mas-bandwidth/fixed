// SPDX-License-Identifier: MIT
// Determinism + correctness for the fixed-point transcendentals.
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix( b3Fixed v ){ const uint8_t* b=(const uint8_t*)&v; for(int i=0;i<8;i++){ fnv^=b[i]; fnv*=0x00000100000001B3ULL; } }

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
    return ( maxCosErr<0.01 && maxSinErr<0.01 && maxAtanErr<0.01 ) ? 0 : 1;
}
