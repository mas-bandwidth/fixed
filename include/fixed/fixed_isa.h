// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// RUNTIME INSTRUCTION-SET DISPATCH for the batch kernels (fixMulBatch, fixDotBatch3).
//
// ONE BUILD CARRIES EVERY VARIANT. Each variant lives in its own translation unit whose
// functions carry their own target attribute (src/fixed_isa_avx512.c); no global -m or
// -march flag is ever needed, so the baseline objects never contain a wide instruction and
// the same library runs on every CPU of its architecture.
//
// DETECTION RUNS ONCE, before main, single-threaded (a static constructor in
// src/fixed_isa.c): cpuid plus XGETBV on x86, getauxval(AT_HWCAP/AT_HWCAP2) on Linux arm64,
// sysctlbyname("hw.optional.arm.FEAT_*") on macOS. The words it reads go through the PURE
// parsers below, which is what the unit tests drive with faked words. The best compiled
// variant whose facts the host has is chosen and its kernels are written into the kernel
// table: one pointer per batch operation. A batch call is one indirect call through that
// table; nothing is dispatched per element.
//
// THE OVERRIDE. FIXED_ISA=<name> in the environment, read at that same init, or
// fixForceIsa(name) from code. Names: scalar, avx2, avx512, avx512ifma, neon, sme2. A name
// that is not compiled into this build, or whose facts this host lacks, is REFUSED BY NAME
// and never silently replaced: fixForceIsa returns the reason and leaves the table alone; a
// refused FIXED_ISA prints the reason to stderr and aborts, because an A/B run that asked for
// a variant and silently measured another would be a lie. Names this build does not compile
// (today: avx2, avx512ifma, neon, sme2) are reserved, so a later variant cannot take a name
// with a different meaning.
//
// EVERY VARIANT RETURNS THE SAME BITS AS SCALAR. That is the library's promise, held by
// test/isa_dispatch_test.c (every variant the host can run, against scalar, on the frozen
// batch oracle rows and 100 million seeded lanes) and by the output oracle run under each
// FIXED_ISA. FIX_SATURATE builds compile scalar only: the kernels wrap, and saturation is a
// different function.
//
// THREADING CONTRACT. The table is written by the init (before main) and by fixForceIsa.
// fixForceIsa is a setup call, like setlocale: make it before any thread is making batch
// calls. USAGE.md ("Runtime ISA dispatch") says why that leaves no state machine to model.
#pragma once
#include <stdint.h>
#include "fixed/base.h"

/// The variants, in order of preference within an architecture. The order is the ABI of
/// the names below; a new variant is appended, never inserted.
typedef enum fixIsa
{
	FIX_ISA_SCALAR = 0,
	FIX_ISA_AVX2,
	FIX_ISA_AVX512,
	FIX_ISA_AVX512IFMA,
	FIX_ISA_NEON,
	FIX_ISA_SME2,
	FIX_ISA_COUNT
} fixIsa;

/// Host facts, one bit each. A fact is true only when the CPU has the feature AND the
/// operating system saves its register state (for AVX and AVX-512 that is XCR0, read with
/// XGETBV; a CPU flag alone is not enough, a kernel that does not save zmm state would
/// corrupt it on every context switch).
enum
{
	FIX_ISA_FACT_AVX2 = 1u << 0,
	FIX_ISA_FACT_OS_YMM = 1u << 1,
	FIX_ISA_FACT_AVX512F = 1u << 2,
	FIX_ISA_FACT_AVX512DQ = 1u << 3,
	FIX_ISA_FACT_AVX512VL = 1u << 4,
	FIX_ISA_FACT_AVX512IFMA = 1u << 5,
	FIX_ISA_FACT_OS_ZMM = 1u << 6,
	FIX_ISA_FACT_NEON = 1u << 7,
	FIX_ISA_FACT_SVE = 1u << 8,
	FIX_ISA_FACT_SVE2 = 1u << 9,
	FIX_ISA_FACT_SME = 1u << 10,
	FIX_ISA_FACT_SME2 = 1u << 11,
	FIX_ISA_FACT_COUNT = 12
};

/// Why fixForceIsa (or a FIXED_ISA override) did or did not take effect.
typedef enum fixIsaResult
{
	FIX_ISA_OK = 0,
	FIX_ISA_UNKNOWN_NAME,  ///< not a variant name at all
	FIX_ISA_NOT_COMPILED,  ///< a reserved or known name this build does not carry
	FIX_ISA_HOST_LACKS     ///< compiled, but this host lacks one of its facts
} fixIsaResult;

/// The name of the variant the kernel table currently holds ("scalar", "avx512", ...).
FIX_API const char* fixIsaName( void );

/// The variant the kernel table currently holds.
FIX_API fixIsa fixIsaSelected( void );

/// Force a variant by name. FIX_ISA_OK and the table now holds it; anything else and the
/// table is unchanged. A setup call: see THREADING CONTRACT above.
FIX_API fixIsaResult fixForceIsa( const char* name );

/// The facts detected on this host at init.
FIX_API uint32_t fixIsaHostFacts( void );

// --- Pure functions: no host access, no state. The unit tests drive these. ---

/// The name of a variant, or NULL for an out-of-range value.
FIX_API const char* fixIsaVariantName( fixIsa isa );

/// The name of one fact bit ("avx512f", "os-zmm-state", ...), or NULL.
FIX_API const char* fixIsaFactName( uint32_t fact );

/// Parse a variant name. Returns 1 and stores the variant, or 0 for an unknown name.
FIX_API int fixIsaFromName( const char* name, fixIsa* out );

/// Whether this build compiled the variant.
FIX_API int fixIsaCompiled( fixIsa isa );

/// The facts a variant needs.
FIX_API uint32_t fixIsaRequiredFacts( fixIsa isa );

/// Whether a variant may run on a host with these facts, in this build. On
/// FIX_ISA_HOST_LACKS, *missing (if non-NULL) receives the missing facts.
FIX_API fixIsaResult fixIsaCheck( fixIsa isa, uint32_t facts, uint32_t* missing );

/// The best compiled variant whose facts hold. Scalar when nothing else does.
FIX_API fixIsa fixIsaBest( uint32_t facts );

/// x86: facts from the raw words. maxLeaf is CPUID.0:EAX; leaf1Ecx is CPUID.1:ECX;
/// leaf7Ebx is CPUID.(7,0):EBX; xcr0 is XGETBV(0), and is ignored unless leaf1Ecx says
/// OSXSAVE (the caller cannot execute XGETBV without it, and passes 0).
FIX_API uint32_t fixIsaParseX86( uint32_t maxLeaf, uint32_t leaf1Ecx, uint32_t leaf7Ebx, uint64_t xcr0 );

/// Linux arm64: facts from getauxval(AT_HWCAP) and getauxval(AT_HWCAP2).
FIX_API uint32_t fixIsaParseLinuxArm64( uint64_t hwcap, uint64_t hwcap2 );

/// macOS arm64: facts from the sysctl values hw.optional.arm.AdvSIMD (or its older names
/// hw.optional.AdvSIMD, hw.optional.neon), hw.optional.arm.FEAT_SME and
/// hw.optional.arm.FEAT_SME2 (0 or 1 each; a missing sysctl is 0).
FIX_API uint32_t fixIsaParseDarwinArm64( int advSimd, int featSme, int featSme2 );
