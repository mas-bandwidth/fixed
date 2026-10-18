/* SPDX-License-Identifier: MIT */
/* log_kernel test: table digits, fixLogMulHi128, and S against Python decimal. */
#include <stdint.h>
#include <stdio.h>
#include "../../src/exact/log_kernel.h"

static int fails = 0;
static void check( int cond, const char* what )
{
    if ( !cond ) { printf( "FAIL: %s\n", what ); ++fails; }
}

static const int t_idx[4] = { 0, 1, 64, 127 };
static const uint64_t t_inv[4] = { 131072, 130056, 87382, 65794 };
static const uint64_t t_hi[4] = { 0ULL, 143546356989327503ULL, 7479370343138796832ULL, 12713830672255972090ULL };
static const uint64_t t_lo[4] = { 0ULL, 1786778475206426255ULL, 3714112314298788900ULL, 14962625340656834887ULL };
static const uint64_t m_hi[6] = {
    1ULL,
    9223372036854775808ULL,
    0ULL,
    206158430208ULL,
    9223372036854775807ULL,
    0ULL,
};
static const uint64_t m_lo[6] = {
    0ULL,
    0ULL,
    9223372036854775808ULL,
    0ULL,
    18446744073709551615ULL,
    1311768467463790320ULL,
};
static const uint64_t n_hi[6] = {
    1ULL,
    9223372036854775808ULL,
    0ULL,
    0ULL,
    9223372036854775807ULL,
    0ULL,
};
static const uint64_t n_lo[6] = {
    0ULL,
    0ULL,
    9223372036854775808ULL,
    5629499534213120ULL,
    18446744073709551615ULL,
    18364758544493064720ULL,
};
static const uint64_t e_hi[6] = {
    0ULL,
    4611686018427387904ULL,
    0ULL,
    0ULL,
    4611686018427387903ULL,
    0ULL,
};
static const uint64_t e_lo[6] = {
    1ULL,
    0ULL,
    0ULL,
    62914560ULL,
    18446744073709551615ULL,
    0ULL,
};

static const int64_t raws[32] = {
    1,
    2,
    65535,
    65536,
    65537,
    66048,
    98304,
    130560,
    1056768,
    1572864,
    2088960,
    1108101562368,
    1649267441664,
    2190433320960,
    9223372036854775807,
    3,
    4,
    6,
    8,
    16,
    64,
    512,
    65538,
    131071,
    131072,
    262144,
    1048576,
    2097152,
    16777216,
    1073741824,
    68719476736,
    1099511627777,
};
static const uint64_t x_hi[32] = {
    17647599783384385637ULL,
    17697546301529708510ULL,
    18446742974189535146ULL,
    0ULL,
    1099503239253ULL,
    560762316719736ULL,
    29216840156602671ULL,
    49664491976935110ULL,
    200346834898011231ULL,
    229002912737894166ULL,
    249450564558226605ULL,
    1199277197804468704ULL,
    1227933275644351639ULL,
    1248380927464684078ULL,
    2347486352830175062ULL,
    17726763141686311182ULL,
    17747492819675031384ULL,
    17776709659831634056ULL,
    17797439337820354258ULL,
    17847385855965677131ULL,
    17947278892256322879ULL,
    18097118446692291500ULL,
    2198989701802ULL,
    49945968387411823ULL,
    49946518145322873ULL,
    99893036290645747ULL,
    199786072581291494ULL,
    249732590726614368ULL,
    399572145162582989ULL,
    699251254034520231ULL,
    998930362906457473ULL,
    1198716435487814504ULL,
};
static const uint64_t x_lo[32] = {
    4855378230108016789ULL,
    17234053641401582476ULL,
    12279814764058430632ULL,
    0ULL,
    6130900512626564610ULL,
    10359253929687892611ULL,
    14617054749879519366ULL,
    6045038858569405763ULL,
    4533723353733500510ULL,
    8791524173925127265ULL,
    219508282615013662ULL,
    12299558621380643235ULL,
    16557359441572269990ULL,
    7985343550262156388ULL,
    9804562857725631306ULL,
    13404364317571550226ULL,
    11165984978985596546ULL,
    7336295655155564296ULL,
    5097916316569610617ULL,
    17476591727863176304ULL,
    5340454403031204445ULL,
    5582992489492798273ULL,
    12009606043016788143ULL,
    79720122041697287ULL,
    12378675411293565687ULL,
    6310606748877579757ULL,
    12621213497755159515ULL,
    6553144835339173585ULL,
    6795682921800767413ULL,
    7280759094723955070ULL,
    7765835267647142726ULL,
    1940304141936936737ULL,
};

int main( void )
{
    for ( int j = 0; j < 4; ++j )
    {
        int i = t_idx[j];
        check( kLogInv[i] == t_inv[j], "INV digit" );
        check( kLogHi[i] == t_hi[j] && kLogLo[i] == t_lo[j], "L digit" );
    }
    for ( int j = 0; j < 6; ++j )
    {
        fixUInt128 a = fixUInt128Make( m_hi[j], m_lo[j] );
        fixUInt128 b = fixUInt128Make( n_hi[j], n_lo[j] );
        fixUInt128 g = fixLogMulHi128( a, b );
        check( fixUInt128Lo( g ) == e_lo[j] && fixUInt128Hi( g ) == e_hi[j], "fixLogMulHi128" );
    }
    {
        fixInt128 s;
        fixLogKernel( 65536, &s );
        check( fixInt128Eq( s, fixInt128FromI64( 0 ) ), "raw 65536 S=0" );
    }
    for ( int j = 0; j < 32; ++j )
    {
        fixInt128 s;
        fixLogKernel( raws[j], &s );
        fixInt128 x = fixInt128Make( x_hi[j], x_lo[j] );
        fixInt128 d = fixInt128Sub( s, x );
        int64_t dv = fixInt128ToI64( d );
        if ( dv < 0 ) dv = -dv;
        if ( dv > ( 1 << 20 ) ) { printf( "FAIL raw=%lld dv=%lld\n", (long long)raws[j], (long long)dv ); ++fails; }
    }
    if ( fails ) { printf( "log kernel test failed: %d failures\n", fails ); return 1; }
    printf( "log kernel test passed\n" );
    return 0;
}
