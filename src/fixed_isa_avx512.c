// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// THE AVX-512 VARIANT of the batch kernels (AVX-512F + AVX-512DQ, 512-bit lanes only).
//
// Every function in this file that contains a wide instruction carries FIX_ISA_AVX512_TARGET,
// a per-function target attribute, so this translation unit needs no special compile flag and
// the rest of the library is never compiled for AVX-512 (no global -m flag anywhere). The
// functions are reached only through the kernel table in src/fixed_isa.c, and only after
// detection found AVX512F, AVX512DQ and the OS zmm state (XCR0) on the host. Tails go to
// the scalar kernels in that file, so a tail is the scalar definition, not a copy.
//
// THE BIT ARGUMENT (benchmark/findings/spike-batch-mul-avx2-avx512.md, from the spike these
// kernels were adopted from). b = (b >> 16) * 2^16 + (b & 65535), so modulo 2^64
//   (a*b + half) >> 16 = a*(b >> 16) + ((a*(b & 65535) + half) >> 16).
// With a = a_hi*2^32 + a_lo (a_hi signed, a_lo unsigned) the second term is
//   (a_hi*(b & 65535) << 16) + ((a_lo*(b & 65535) + half) >> 16),
// where a_lo*(b & 65535) + half fits in 48 bits. vpmullq gives the first term's low 64
// bits; vpmuldq and vpmuludq give the two 32x32 products. For the dot product each exact
// product is q_i*2^16 + r_i with 0 <= r_i < 2^16: summing the three q_i and adding
// (r_x + r_y + r_z + half) >> 16 rounds once, as fixDot does, including when the 128-bit
// sum wraps. The low 16 bits of a*b are the low 16 bits of vpmuludq(a, b), which is r_i.
// FIX_SATURATE builds compile none of this: saturation is a different function.
#include "fixed/fixed_vec.h"

#if ( defined( __x86_64__ ) || defined( _M_X64 ) ) && !defined( FIX_SATURATE ) && \
	( defined( __GNUC__ ) || defined( __clang__ ) || defined( _MSC_VER ) )

	#include <immintrin.h>

	#if defined( __GNUC__ ) || defined( __clang__ )
		#define FIX_ISA_AVX512_TARGET __attribute__( ( target( "avx512f,avx512dq" ) ) )
	#else
		// MSVC compiles AVX-512 intrinsics without an /arch flag.
		#define FIX_ISA_AVX512_TARGET
	#endif

void fixIsaMulBatchScalar( fixed_t* out, const fixed_t* a, const fixed_t* b, size_t n );
void fixIsaDotBatch3Scalar( fixed_t* out, const fixed_t* ax, const fixed_t* ay, const fixed_t* az,
							const fixed_t* bx, const fixed_t* by, const fixed_t* bz, size_t n );
void fixIsaMulBatchAvx512( fixed_t* out, const fixed_t* a, const fixed_t* b, size_t n );
void fixIsaDotBatch3Avx512( fixed_t* out, const fixed_t* ax, const fixed_t* ay, const fixed_t* az,
							const fixed_t* bx, const fixed_t* by, const fixed_t* bz, size_t n );

FIX_ISA_AVX512_TARGET static inline __m512i fixIsaMulShiftAvx512( __m512i a, __m512i b, __m512i half )
{
	const __m512i mask = _mm512_set1_epi64( 0xffff );
	__m512i first = _mm512_mullo_epi64( a, _mm512_srai_epi64( b, 16 ) );
	__m512i blo = _mm512_and_si512( b, mask );
	__m512i high = _mm512_slli_epi64( _mm512_mul_epi32( _mm512_srli_epi64( a, 32 ), blo ), 16 );
	__m512i low = _mm512_srli_epi64( _mm512_add_epi64( _mm512_mul_epu32( a, blo ), half ), 16 );
	return _mm512_add_epi64( first, _mm512_add_epi64( high, low ) );
}

FIX_ISA_AVX512_TARGET void fixIsaMulBatchAvx512( fixed_t* out, const fixed_t* a, const fixed_t* b, size_t n )
{
	const __m512i half = _mm512_set1_epi64( ( int64_t )FIX_HALF );
	size_t i = 0;
	for ( ; i + 8 <= n; i += 8 )
	{
		__m512i x = _mm512_loadu_si512( ( const void* )( a + i ) );
		__m512i y = _mm512_loadu_si512( ( const void* )( b + i ) );
		_mm512_storeu_si512( ( void* )( out + i ), fixIsaMulShiftAvx512( x, y, half ) );
	}
	fixIsaMulBatchScalar( out + i, a + i, b + i, n - i );
}

FIX_ISA_AVX512_TARGET void fixIsaDotBatch3Avx512( fixed_t* out, const fixed_t* ax, const fixed_t* ay,
												  const fixed_t* az, const fixed_t* bx, const fixed_t* by,
												  const fixed_t* bz, size_t n )
{
	const __m512i mask = _mm512_set1_epi64( 0xffff );
	const __m512i zero = _mm512_setzero_si512();
	const __m512i half = _mm512_set1_epi64( ( int64_t )FIX_HALF );
	size_t i = 0;
	for ( ; i + 8 <= n; i += 8 )
	{
		__m512i x = _mm512_loadu_si512( ( const void* )( ax + i ) ), u = _mm512_loadu_si512( ( const void* )( bx + i ) );
		__m512i y = _mm512_loadu_si512( ( const void* )( ay + i ) ), v = _mm512_loadu_si512( ( const void* )( by + i ) );
		__m512i z = _mm512_loadu_si512( ( const void* )( az + i ) ), w = _mm512_loadu_si512( ( const void* )( bz + i ) );
		__m512i q = _mm512_add_epi64( _mm512_add_epi64( fixIsaMulShiftAvx512( x, u, zero ), fixIsaMulShiftAvx512( y, v, zero ) ),
									  fixIsaMulShiftAvx512( z, w, zero ) );
		__m512i rem = _mm512_add_epi64( _mm512_add_epi64( _mm512_and_si512( _mm512_mul_epu32( x, u ), mask ),
														  _mm512_and_si512( _mm512_mul_epu32( y, v ), mask ) ),
										_mm512_and_si512( _mm512_mul_epu32( z, w ), mask ) );
		q = _mm512_add_epi64( q, _mm512_srli_epi64( _mm512_add_epi64( rem, half ), 16 ) );
		_mm512_storeu_si512( ( void* )( out + i ), q );
	}
	fixIsaDotBatch3Scalar( out + i, ax + i, ay + i, az + i, bx + i, by + i, bz + i, n - i );
}

#else
// Not compiled on this target (or a FIX_SATURATE build). ISO C forbids an empty
// translation unit.
typedef int fixIsaAvx512NotCompiled;
#endif
