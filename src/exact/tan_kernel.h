// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// The tan kernel of docs/design/transcendentals.md, section 3.3. Internal to src/exact/;
// not installed. Integer only, and every 128-bit operation goes through the fixUInt128 /
// fixInt128 vocabulary so the native and emulated arms agree bit for bit.
//
// tan(s) = s + sum_{k=3,5,..,13} C_k * 2^-127 * s^k on |s| <= 2^-8 + 2^-100, the s
// coefficient exactly 1. The coefficients and the 102-entry table below are the integers
// printed by sollya/tan_kernel.sollya and committed in sollya/tan_kernel.out. Regenerate
// with:
//
//     sollya sollya/tan_kernel.sollya
//
// CERTIFICATE. Relative 2^-134.23, absolute 2^-142.32, over the whole kernel interval:
// the reconstructed s + p is certified in both the absolute error (the table path) and
// the relative error (the reciprocal, pole path); see sollya/tan_kernel.out, which prints
// supnorm |s + p - tan| absolute 2^-143.08 and |(s + p)/tan - 1| relative 2^-133.01 before
// the Horner truncations. Sollya is a build-time tool only; this header never links it.
//
// s is sign-magnitude Q0.128: the integer part is the value times 2^128, and the kernel
// is odd, so it runs on |s| and applies the sign at the end. |s| < 2^120, and the result
// is below 2^120, so both fit the signed 128-bit type without overflow.
#ifndef FIX_TAN_KERNEL_H
#define FIX_TAN_KERNEL_H

#include <stdint.h>

#include "fixed/fixed_int128.h"
#include "fixed/fixed_int256.h"

// ( a * b ) >> 128, the high half of an exact 128 x 128 product, as two
// fixUInt256MulU128ByU64 calls (docs/design/transcendentals.md, section 0).
static inline fixUInt128 fixTanKernelMulHi128( fixUInt128 a, fixUInt128 b )
{
	fixUInt256 product = fixUInt256MulU128ByU64( a, fixUInt128Lo( b ) );
	fixUInt256 high = fixUInt256MulU128ByU64( a, fixUInt128Hi( b ) );
	product = fixUInt256Add( product, fixUInt256Shl( high, 64 ) );
	return product.hi;
}

// TAN_j = round( tan( j / 128 ) * 2^127 ), j = 0..101, unsigned Q1.127, from the
// `T j hi lo` lines of sollya/tan_kernel.out.
static inline fixUInt128 fixTanKernelTable( int j )
{
	static const uint64_t words[102][2] = {
		{ UINT64_C( 0x0000000000000000 ), UINT64_C( 0x0000000000000000 ) }, // 0
		{ UINT64_C( 0x0100015557777aeb ), UINT64_C( 0xb45431e4465196fa ) }, // 1
		{ UINT64_C( 0x02000aaaeef0a915 ), UINT64_C( 0xc36bec0ae8e0dd04 ) }, // 2
		{ UINT64_C( 0x030024020683e9e9 ), UINT64_C( 0x113952de2b3c5cf2 ) }, // 3
		{ UINT64_C( 0x0400555ddebb0215 ), UINT64_C( 0xf48b25fa026180a2 ) }, // 4
		{ UINT64_C( 0x0500a6c4b9740d8e ), UINT64_C( 0xb0371ed907d176ad ) }, // 5
		{ UINT64_C( 0x06012040db911489 ), UINT64_C( 0xec2f08632fe6cca3 ) }, // 6
		{ UINT64_C( 0x0701c9e18fb90b24 ), UINT64_C( 0xb1a84d1d50f595e7 ) }, // 7
		{ UINT64_C( 0x0802abbc2a6f8041 ), UINT64_C( 0x949df31c82bd8634 ) }, // 8
		{ UINT64_C( 0x0903cded0fc8dd2b ), UINT64_C( 0x590f78830090fe57 ) }, // 9
		{ UINT64_C( 0x0a053898bb01d4bc ), UINT64_C( 0x6839e47bfcb8a7c6 ) }, // 10
		{ UINT64_C( 0x0b06f3ecc8417ede ), UINT64_C( 0x9fe28d4739def00c ) }, // 11
		{ UINT64_C( 0x0c09082100ce9cbc ), UINT64_C( 0xa7bca82c7a6c7c2d ) }, // 12
		{ UINT64_C( 0x0d0b7d786a01a3ab ), UINT64_C( 0x4d19c5bcfc356036 ) }, // 13
		{ UINT64_C( 0x0e0e5c42573e7173 ), UINT64_C( 0x706182a479cc9e15 ) }, // 14
		{ UINT64_C( 0x0f11acdb7f41f522 ), UINT64_C( 0x645e395bef9d85cc ) }, // 15
		{ UINT64_C( 0x101577af1511a4e0 ), UINT64_C( 0x459f5b872d4ff350 ) }, // 16
		{ UINT64_C( 0x1119c537e4dc4c9e ), UINT64_C( 0x4b931a3422a36098 ) }, // 17
		{ UINT64_C( 0x121e9e01751d9bf3 ), UINT64_C( 0x868b23d1925bf393 ) }, // 18
		{ UINT64_C( 0x13240aa92c57dc8b ), UINT64_C( 0xe5429df9b2eedb76 ) }, // 19
		{ UINT64_C( 0x142a13df7bb96799 ), UINT64_C( 0xf9adc85f94fe42c7 ) }, // 20
		{ UINT64_C( 0x1530c2690f05c95e ), UINT64_C( 0xcc8eef7446f00781 ) }, // 21
		{ UINT64_C( 0x16381f20021d07cd ), UINT64_C( 0x93e238c304f3b2f2 ) }, // 22
		{ UINT64_C( 0x174032f51c7e3671 ), UINT64_C( 0x01c44737b67d8923 ) }, // 23
		{ UINT64_C( 0x184906f113256809 ), UINT64_C( 0x077e697c04e1462c ) }, // 24
		{ UINT64_C( 0x1952a435d1282806 ), UINT64_C( 0x97cdf8b89521fde1 ) }, // 25
		{ UINT64_C( 0x1a5d13ffc776f56e ), UINT64_C( 0x2460a8e8e35a0210 ) }, // 26
		{ UINT64_C( 0x1b685fa7442dc045 ), UINT64_C( 0x78be82beaf5352aa ) }, // 27
		{ UINT64_C( 0x1c7490a1d1e12d3a ), UINT64_C( 0x5f81c91a6d280f83 ) }, // 28
		{ UINT64_C( 0x1d81b0839f5a638c ), UINT64_C( 0x52333be0ef9abf10 ) }, // 29
		{ UINT64_C( 0x1e8fc900f0376a91 ), UINT64_C( 0xa3959c8e01419033 ) }, // 30
		{ UINT64_C( 0x1f9ee3ef96eaa1c7 ), UINT64_C( 0x1f2f8a843c8ffbb4 ) }, // 31
		{ UINT64_C( 0x20af0b487898abcc ), UINT64_C( 0x72803112cb8517a2 ) }, // 32
		{ UINT64_C( 0x21c049291b593dd5 ), UINT64_C( 0xd96cbf7cd4d97094 ) }, // 33
		{ UINT64_C( 0x22d2a7d53f64ae10 ), UINT64_C( 0x6b03f377d8f0785d ) }, // 34
		{ UINT64_C( 0x23e631b883bdd8a0 ), UINT64_C( 0xb7643a6de73369a8 ) }, // 35
		{ UINT64_C( 0x24faf16816ee08f9 ), UINT64_C( 0xc8ef06f99aeae833 ) }, // 36
		{ UINT64_C( 0x2610f1a4746f0959 ), UINT64_C( 0x2d20fa053354be23 ) }, // 37
		{ UINT64_C( 0x27283d5b2f6651a3 ), UINT64_C( 0x2484660e31eb3a40 ) }, // 38
		{ UINT64_C( 0x2840dfa8cb5b8f72 ), UINT64_C( 0x598bfbf8d0fbe84a ) }, // 39
		{ UINT64_C( 0x295ae3daa39c7123 ), UINT64_C( 0x9dea6e8005a757c3 ) }, // 40
		{ UINT64_C( 0x2a765570e207c39c ), UINT64_C( 0x35a3a7e7e46d2372 ) }, // 41
		{ UINT64_C( 0x2b9340208602959d ), UINT64_C( 0x71445198f6f5dfff ) }, // 42
		{ UINT64_C( 0x2cb1afd57c633b4a ), UINT64_C( 0x5833ae5e84f57987 ) }, // 43
		{ UINT64_C( 0x2dd1b0b4c927c13f ), UINT64_C( 0x8b8ad43a6e422f10 ) }, // 44
		{ UINT64_C( 0x2ef34f1ec3d7a904 ), UINT64_C( 0xa634b743307b6f4c ) }, // 45
		{ UINT64_C( 0x301697b1677bb215 ), UINT64_C( 0xa80279094350ccf4 ) }, // 46
		{ UINT64_C( 0x313b974ab722055d ), UINT64_C( 0x8c3cb6eddeb4731a ) }, // 47
		{ UINT64_C( 0x32625b0b37f15f88 ), UINT64_C( 0x5888520af9ff0d6c ) }, // 48
		{ UINT64_C( 0x338af05881dadcac ), UINT64_C( 0xef14214bbfdf8aa6 ) }, // 49
		{ UINT64_C( 0x34b564dfe807d9ea ), UINT64_C( 0xdd0a3826e0593dd0 ) }, // 50
		{ UINT64_C( 0x35e1c6993a300d17 ), UINT64_C( 0xa1118aacb15c65c2 ) }, // 51
		{ UINT64_C( 0x371023c9a01385da ), UINT64_C( 0x0fcdb9b02b292fbb ) }, // 52
		{ UINT64_C( 0x38408b069064d60d ), UINT64_C( 0xdf0f5e8deecd56a3 ) }, // 53
		{ UINT64_C( 0x39730b38e481319b ), UINT64_C( 0x701d5fea1023b5ef ) }, // 54
		{ UINT64_C( 0x3aa7b3a00a6701f9 ), UINT64_C( 0xa931dbc8693c70f7 ) }, // 55
		{ UINT64_C( 0x3bde93d5566f428f ), UINT64_C( 0x2463d132a13d9e4e ) }, // 56
		{ UINT64_C( 0x3d17bbcf766315d5 ), UINT64_C( 0x5c9b1c97d91f096a ) }, // 57
		{ UINT64_C( 0x3e533be6079d75cd ), UINT64_C( 0xb496b5105eab4f92 ) }, // 58
		{ UINT64_C( 0x3f9124d55200c513 ), UINT64_C( 0x59a0383cbcd0c123 ) }, // 59
		{ UINT64_C( 0x40d187c229a1728e ), UINT64_C( 0x2dd5bb65d15bb1b8 ) }, // 60
		{ UINT64_C( 0x4214763df921f47d ), UINT64_C( 0x4f0fecaca8aee39a ) }, // 61
		{ UINT64_C( 0x435a024af6d9366e ), UINT64_C( 0xea477acd5f65b81c ) }, // 62
		{ UINT64_C( 0x44a23e6086fb5163 ), UINT64_C( 0xd4764a3029198335 ) }, // 63
		{ UINT64_C( 0x45ed3d6fcd1d290c ), UINT64_C( 0x5e59201e20911332 ) }, // 64
		{ UINT64_C( 0x473b12e86f8e7ad8 ), UINT64_C( 0x3e7765e13f6775f9 ) }, // 65
		{ UINT64_C( 0x488bd2bd8f2b34af ), UINT64_C( 0xff228e155eaf220f ) }, // 66
		{ UINT64_C( 0x49df916af66cb630 ), UINT64_C( 0x91d07ecaa290e854 ) }, // 67
		{ UINT64_C( 0x4b3663fa82ae206f ), UINT64_C( 0xc0130ce5572a81f3 ) }, // 68
		{ UINT64_C( 0x4c906009cac441f7 ), UINT64_C( 0x06216c491bfc2f2e ) }, // 69
		{ UINT64_C( 0x4ded9bd0063a3773 ), UINT64_C( 0xb36630d79f8ed199 ) }, // 70
		{ UINT64_C( 0x4f4e2e2438b7c315 ), UINT64_C( 0x5d45f5f599e2706a ) }, // 71
		{ UINT64_C( 0x50b22e83a54de773 ), UINT64_C( 0xf41645c354b217db ) }, // 72
		{ UINT64_C( 0x5219b5188da1c796 ), UINT64_C( 0xf0d058136e47874e ) }, // 73
		{ UINT64_C( 0x5384dac1412d7b18 ), UINT64_C( 0x2b1ee622267694aa ) }, // 74
		{ UINT64_C( 0x54f3b9178112c100 ), UINT64_C( 0x37473ae19b146f9c ) }, // 75
		{ UINT64_C( 0x56666a783d44a30a ), UINT64_C( 0x057d9af1d15e5b23 ) }, // 76
		{ UINT64_C( 0x57dd0a0bb01b92e5 ), UINT64_C( 0xcf6ecca66bb41608 ) }, // 77
		{ UINT64_C( 0x5957b3cdddbbc257 ), UINT64_C( 0xf9a83401149bd165 ) }, // 78
		{ UINT64_C( 0x5ad684977d0fe625 ), UINT64_C( 0x51010d407d730c14 ) }, // 79
		{ UINT64_C( 0x5c599a27507bbe0b ), UINT64_C( 0xb00319a12d4bcb34 ) }, // 80
		{ UINT64_C( 0x5de1132bf4d23100 ), UINT64_C( 0x4393bf9e110fbbc4 ) }, // 81
		{ UINT64_C( 0x5f6d0f4e2d8b2948 ), UINT64_C( 0x9d3c731da13f1634 ) }, // 82
		{ UINT64_C( 0x60fdaf3bb5ae4b11 ), UINT64_C( 0x4dc7a66e6ec7ebbf ) }, // 83
		{ UINT64_C( 0x629314b29d69dd4d ), UINT64_C( 0x66e6bea4d6119c65 ) }, // 84
		{ UINT64_C( 0x642d628d3cd9951b ), UINT64_C( 0x5a65f33165564d65 ) }, // 85
		{ UINT64_C( 0x65ccbccec5184870 ), UINT64_C( 0xb5141ff50de7793f ) }, // 86
		{ UINT64_C( 0x677148b0795ad3e1 ), UINT64_C( 0x1b3621e4194eb36e ) }, // 87
		{ UINT64_C( 0x691b2caf9a82c76b ), UINT64_C( 0x2cd89a710692a452 ) }, // 88
		{ UINT64_C( 0x6aca909c1055e051 ), UINT64_C( 0xfdb7005280e599c0 ) }, // 89
		{ UINT64_C( 0x6c7f9da7dc553899 ), UINT64_C( 0x552afecca3e7f795 ) }, // 90
		{ UINT64_C( 0x6e3a7e77630dcecc ), UINT64_C( 0x242c0520533f0e7d ) }, // 91
		{ UINT64_C( 0x6ffb5f3299ad258d ), UINT64_C( 0x237cd4d92450ef65 ) }, // 92
		{ UINT64_C( 0x71c26d9726a9ed93 ), UINT64_C( 0xec21dbe58238efd0 ) }, // 93
		{ UINT64_C( 0x738fd90b856bd667 ), UINT64_C( 0x30530d7394e1711c ) }, // 94
		{ UINT64_C( 0x7563d2b33e10d470 ), UINT64_C( 0xb53f39d9255c945f ) }, // 95
		{ UINT64_C( 0x773e8d8443bbaf83 ), UINT64_C( 0x0c266bb600b78dc6 ) }, // 96
		{ UINT64_C( 0x79203e5d8d420b0b ), UINT64_C( 0xfab959aaeeb0c4c3 ) }, // 97
		{ UINT64_C( 0x7b091c1efd9a1d8c ), UINT64_C( 0xb6d1fadd1da0fc41 ) }, // 98
		{ UINT64_C( 0x7cf95fc2b3151159 ), UINT64_C( 0xd2c7bcfec06c7196 ) }, // 99
		{ UINT64_C( 0x7ef14477d645fd4e ), UINT64_C( 0x6ef3dde2f0688892 ) }, // 100
		{ UINT64_C( 0x80f107bf03725a03 ), UINT64_C( 0x4fb7eb4048941cf6 ) }, // 101
	};
	return fixUInt128Make( words[j][0], words[j][1] );
}

// C_k * 2^127, k = 3, 5, ..., 13, unsigned 127 fraction bits, from sollya/tan_kernel.out.
static inline fixUInt128 fixTanKernelCoefficient( int k )
{
	switch ( k )
	{
		case 3:
			return fixUInt128Make( UINT64_C( 0x2aaaaaaaaaaaaaaa ), UINT64_C( 0xaaaaaaaaaaa9e8c1 ) );
		case 5:
			return fixUInt128Make( UINT64_C( 0x1111111111111111 ), UINT64_C( 0x1111111cf08f504a ) );
		case 7:
			return fixUInt128Make( UINT64_C( 0x06e86e86e86e86e8 ), UINT64_C( 0x6e4448ee3a2fad02 ) );
		case 9:
			return fixUInt128Make( UINT64_C( 0x02cc9e9105821ff1 ), UINT64_C( 0x48dd40161f305b02 ) );
		case 11:
			return fixUInt128Make( UINT64_C( 0x01226e355d73339f ), UINT64_C( 0x9eab9b8cac3a53f8 ) );
		default:
			return fixUInt128Make( UINT64_C( 0x0075b5a17ac9a030 ), UINT64_C( 0xd3d4d84bc54e3d87 ) );
	}
}

// tan(s) for s in signed Q0.128 with |s| <= 2^-8 + 2^-100, returned in Q0.128.
// Horner in s^2: w is Q.127, s2 and s are Q0.128; mulhi(...) keeps Q.127 where a
// Q.127 factor enters, and the final product is realigned from Q.127 to Q0.128 by one
// exact left shift.
static inline fixInt128 fixTanKernel( fixInt128 s )
{
	bool negative = fixInt128IsNegative( s );
	fixUInt128 x = fixInt128ToUnsigned( s );
	if ( negative )
	{
		x = fixUInt128Neg( x );
	}

	fixUInt128 s2 = fixTanKernelMulHi128( x, x );
	fixUInt128 w = fixTanKernelCoefficient( 13 );
	for ( int k = 11; k >= 3; k -= 2 )
	{
		w = fixUInt128Add( fixTanKernelCoefficient( k ), fixTanKernelMulHi128( w, s2 ) );
	}
	fixUInt128 c = fixTanKernelMulHi128( s2, w );
	fixUInt128 t = fixUInt128Add( x, fixUInt128Shl( fixTanKernelMulHi128( x, c ), 1 ) );

	fixInt128 result = fixInt128FromUnsigned( t );
	return negative ? fixInt128Neg( result ) : result;
}

#endif // FIX_TAN_KERNEL_H
