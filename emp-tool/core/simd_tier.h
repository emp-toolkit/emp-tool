#ifndef EMP_SIMD_TIER_H__
#define EMP_SIMD_TIER_H__

#include "emp-tool/core/block.h"
#include <cstdint>

// Centralized SIMD tier detection + Lane abstractions used by emp-tool's
// AES and F2K kernels. Two sections:
//   1. ISA tier macros: EMP_HAS_AVX2 / AVX512F / VAES / VPCLMULQDQ and the
//      composed flags EMP_HAS_VAES{256,512}, EMP_HAS_VPCLMUL{256,512}, plus
//      per-module __attribute__((target)) packs EMP_AES_TARGET_ATTR /
//      EMP_F2K_TARGET_ATTR.
//   2. SIMD Lane traits. AesLane<N> and ClmulLane<N> share N = blocks per
//      vector (1 = xmm, 2 = ymm, 4 = zmm). The struct bodies expose the
//      per-tier intrinsics the kernels need; no shared base, since the
//      module-specific primitives diverge.

// --- Section 1: ISA tier macros ------------------------------------------

#if defined(__x86_64__) && defined(__AVX2__)
  #define EMP_HAS_AVX2 1
#else
  #define EMP_HAS_AVX2 0
#endif

#if defined(__x86_64__) && defined(__AVX512F__)
  #define EMP_HAS_AVX512F 1
#else
  #define EMP_HAS_AVX512F 0
#endif

#if defined(__x86_64__) && defined(__VAES__)
  #define EMP_HAS_VAES 1
#else
  #define EMP_HAS_VAES 0
#endif

#if defined(__x86_64__) && defined(__VPCLMULQDQ__)
  #define EMP_HAS_VPCLMULQDQ 1
#else
  #define EMP_HAS_VPCLMULQDQ 0
#endif

#define EMP_HAS_VAES256    (EMP_HAS_VAES       && EMP_HAS_AVX2)
#define EMP_HAS_VAES512    (EMP_HAS_VAES       && EMP_HAS_AVX512F)
#define EMP_HAS_VPCLMUL256 (EMP_HAS_VPCLMULQDQ && EMP_HAS_AVX2)
#define EMP_HAS_VPCLMUL512 (EMP_HAS_VPCLMULQDQ && EMP_HAS_AVX512F)

#if EMP_HAS_VAES512
  #define EMP_AES_TARGET_ATTR __attribute__((target("vaes,avx512f,avx2,aes,sse2")))
#elif EMP_HAS_VAES256
  #define EMP_AES_TARGET_ATTR __attribute__((target("vaes,avx2,aes,sse2")))
#elif defined(__x86_64__)
  #define EMP_AES_TARGET_ATTR __attribute__((target("aes,sse2")))
#else
  #define EMP_AES_TARGET_ATTR
#endif

#if EMP_HAS_VPCLMUL512
  #define EMP_F2K_TARGET_ATTR __attribute__((target("vpclmulqdq,avx512f,avx2,sse2,pclmul")))
#elif EMP_HAS_VPCLMUL256
  #define EMP_F2K_TARGET_ATTR __attribute__((target("vpclmulqdq,avx2,sse2,pclmul")))
#elif defined(__x86_64__)
  #define EMP_F2K_TARGET_ATTR __attribute__((target("sse2,pclmul")))
#else
  #define EMP_F2K_TARGET_ATTR
#endif

// --- Section 2: SIMD Lane traits -----------------------------------------
//
// AesLane<N> lives in emp:: (public). ClmulLane<N> lives in emp::detail.

#ifdef __x86_64__

namespace emp {

// AesLane<N>: AES-round primitives over N AES blocks per vector register.
// AesLane<1> = xmm (AES-NI baseline); <2> = ymm (VAES+AVX2); <4> = zmm
// (VAES+AVX512F). ctr_xor_tweak builds N counters in one vec_t XORed
// with a broadcasted tweak (used by fixed-key-AES-CTR callers that
// generate plaintexts inline).
template<int N> struct AesLane;

template<>
struct AesLane<1> {
	static constexpr int N = 1;
	using vec_t = block;
	EMP_AES_TARGET_ATTR static vec_t broadcast(__m128i k)             { return k; }
	EMP_AES_TARGET_ATTR static vec_t load(const block *p)             { return _mm_loadu_si128((const __m128i *)p); }
	EMP_AES_TARGET_ATTR static void  store(block *p, vec_t v)         { _mm_storeu_si128((__m128i *)p, v); }
	EMP_AES_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)           { return _mm_xor_si128(a, b); }
	EMP_AES_TARGET_ATTR static vec_t aesenc(vec_t s, vec_t k)         { return _mm_aesenc_si128(s, k); }
	EMP_AES_TARGET_ATTR static vec_t aesenclast(vec_t s, vec_t k)     { return _mm_aesenclast_si128(s, k); }
	EMP_AES_TARGET_ATTR static vec_t ctr_xor_tweak(int64_t b0, int slot, vec_t tw) {
		return _mm_xor_si128(_mm_set_epi64x(0, b0 + slot), tw);
	}
};

#if EMP_HAS_VAES256
template<>
struct AesLane<2> {
	static constexpr int N = 2;
	using vec_t = __m256i;
	EMP_AES_TARGET_ATTR static vec_t broadcast(__m128i k)             { return _mm256_broadcastsi128_si256(k); }
	EMP_AES_TARGET_ATTR static vec_t load(const block *p)             { return _mm256_loadu_si256((const __m256i *)p); }
	EMP_AES_TARGET_ATTR static void  store(block *p, vec_t v)         { _mm256_storeu_si256((__m256i *)p, v); }
	EMP_AES_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)           { return _mm256_xor_si256(a, b); }
	EMP_AES_TARGET_ATTR static vec_t aesenc(vec_t s, vec_t k)         { return _mm256_aesenc_epi128(s, k); }
	EMP_AES_TARGET_ATTR static vec_t aesenclast(vec_t s, vec_t k)     { return _mm256_aesenclast_epi128(s, k); }
	EMP_AES_TARGET_ATTR static vec_t ctr_xor_tweak(int64_t b0, int slot, vec_t tw) {
		return _mm256_xor_si256(
			_mm256_set_epi64x(0, b0 + 2 * slot + 1, 0, b0 + 2 * slot), tw);
	}
};
#endif

#if EMP_HAS_VAES512
template<>
struct AesLane<4> {
	static constexpr int N = 4;
	using vec_t = __m512i;
	EMP_AES_TARGET_ATTR static vec_t broadcast(__m128i k)             { return _mm512_broadcast_i32x4(k); }
	EMP_AES_TARGET_ATTR static vec_t load(const block *p)             { return _mm512_loadu_si512((const __m512i *)p); }
	EMP_AES_TARGET_ATTR static void  store(block *p, vec_t v)         { _mm512_storeu_si512((__m512i *)p, v); }
	EMP_AES_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)           { return _mm512_xor_si512(a, b); }
	EMP_AES_TARGET_ATTR static vec_t aesenc(vec_t s, vec_t k)         { return _mm512_aesenc_epi128(s, k); }
	EMP_AES_TARGET_ATTR static vec_t aesenclast(vec_t s, vec_t k)     { return _mm512_aesenclast_epi128(s, k); }
	EMP_AES_TARGET_ATTR static vec_t ctr_xor_tweak(int64_t b0, int slot, vec_t tw) {
		return _mm512_xor_si512(
			_mm512_set_epi64(0, b0 + 4 * slot + 3, 0, b0 + 4 * slot + 2,
			                 0, b0 + 4 * slot + 1, 0, b0 + 4 * slot), tw);
	}
};
#endif

}  // namespace emp

namespace emp { namespace detail {

// ClmulLane<N>: carryless-multiply primitives over N AES blocks per
// vector. <1> = scalar PCLMULQDQ (universal on x86_64), <2> = ymm
// VPCLMULQDQ + AVX2, <4> = zmm VPCLMULQDQ + AVX-512. The four
// clmul_*_* methods bake the imm8 selector at compile time (the
// intrinsic requires a constant). fold_to_xmm reduces the lane-wide
// accumulator to a single xmm via halving XOR-reductions.
template<int N> struct ClmulLane;

template<>
struct ClmulLane<1> {
	static constexpr int N = 1;
	using vec_t = block;
	EMP_F2K_TARGET_ATTR static vec_t zero()                            { return _mm_setzero_si128(); }
	EMP_F2K_TARGET_ATTR static vec_t loadu(const block *p)             { return _mm_loadu_si128((const __m128i *)p); }
	EMP_F2K_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)            { return _mm_xor_si128(a, b); }
	EMP_F2K_TARGET_ATTR static vec_t xor3(vec_t a, vec_t b, vec_t c)   { return _mm_xor_si128(a, _mm_xor_si128(b, c)); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_lo_lo(vec_t a, vec_t b)     { return _mm_clmulepi64_si128(a, b, 0x00); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_hi_hi(vec_t a, vec_t b)     { return _mm_clmulepi64_si128(a, b, 0x11); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_hi_lo(vec_t a, vec_t b)     { return _mm_clmulepi64_si128(a, b, 0x10); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_lo_hi(vec_t a, vec_t b)     { return _mm_clmulepi64_si128(a, b, 0x01); }
	EMP_F2K_TARGET_ATTR static vec_t bslli_8(vec_t v)                  { return _mm_slli_si128(v, 8); }
	EMP_F2K_TARGET_ATTR static vec_t bsrli_8(vec_t v)                  { return _mm_srli_si128(v, 8); }
	EMP_F2K_TARGET_ATTR static block fold_to_xmm(vec_t v)              { return v; }
};

#if EMP_HAS_VPCLMUL256
template<>
struct ClmulLane<2> {
	static constexpr int N = 2;
	using vec_t = __m256i;
	EMP_F2K_TARGET_ATTR static vec_t zero()                            { return _mm256_setzero_si256(); }
	EMP_F2K_TARGET_ATTR static vec_t loadu(const block *p)             { return _mm256_loadu_si256((const __m256i *)p); }
	EMP_F2K_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)            { return _mm256_xor_si256(a, b); }
	// Three-way XOR. AVX-512VL would let us use _mm256_ternarylogic_epi64
	// here, but VPCLMULQDQ + AVX2 hosts typically lack AVX-512VL.
	EMP_F2K_TARGET_ATTR static vec_t xor3(vec_t a, vec_t b, vec_t c)   { return _mm256_xor_si256(a, _mm256_xor_si256(b, c)); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_lo_lo(vec_t a, vec_t b)     { return _mm256_clmulepi64_epi128(a, b, 0x00); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_hi_hi(vec_t a, vec_t b)     { return _mm256_clmulepi64_epi128(a, b, 0x11); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_hi_lo(vec_t a, vec_t b)     { return _mm256_clmulepi64_epi128(a, b, 0x10); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_lo_hi(vec_t a, vec_t b)     { return _mm256_clmulepi64_epi128(a, b, 0x01); }
	EMP_F2K_TARGET_ATTR static vec_t bslli_8(vec_t v)                  { return _mm256_bslli_epi128(v, 8); }
	EMP_F2K_TARGET_ATTR static vec_t bsrli_8(vec_t v)                  { return _mm256_bsrli_epi128(v, 8); }
	EMP_F2K_TARGET_ATTR static block fold_to_xmm(vec_t v) {
		return _mm_xor_si128(_mm256_castsi256_si128(v),
		                     _mm256_extracti128_si256(v, 1));
	}
};
#endif

#if EMP_HAS_VPCLMUL512
template<>
struct ClmulLane<4> {
	static constexpr int N = 4;
	using vec_t = __m512i;
	EMP_F2K_TARGET_ATTR static vec_t zero()                            { return _mm512_setzero_si512(); }
	EMP_F2K_TARGET_ATTR static vec_t loadu(const block *p)             { return _mm512_loadu_si512((const __m512i *)p); }
	EMP_F2K_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)            { return _mm512_xor_si512(a, b); }
	// vpternlogq with truth table 0x96 fuses the mid-accumulator update
	// (mid ^= p10 ^ p01) into one instruction.
	EMP_F2K_TARGET_ATTR static vec_t xor3(vec_t a, vec_t b, vec_t c)   { return _mm512_ternarylogic_epi64(a, b, c, 0x96); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_lo_lo(vec_t a, vec_t b)     { return _mm512_clmulepi64_epi128(a, b, 0x00); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_hi_hi(vec_t a, vec_t b)     { return _mm512_clmulepi64_epi128(a, b, 0x11); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_hi_lo(vec_t a, vec_t b)     { return _mm512_clmulepi64_epi128(a, b, 0x10); }
	EMP_F2K_TARGET_ATTR static vec_t clmul_lo_hi(vec_t a, vec_t b)     { return _mm512_clmulepi64_epi128(a, b, 0x01); }
	EMP_F2K_TARGET_ATTR static vec_t bslli_8(vec_t v)                  { return _mm512_bslli_epi128(v, 8); }
	EMP_F2K_TARGET_ATTR static vec_t bsrli_8(vec_t v)                  { return _mm512_bsrli_epi128(v, 8); }
	EMP_F2K_TARGET_ATTR static block fold_to_xmm(vec_t v) {
		__m256i y = _mm256_xor_si256(_mm512_castsi512_si256(v),
		                              _mm512_extracti64x4_epi64(v, 1));
		return _mm_xor_si128(_mm256_castsi256_si128(y),
		                     _mm256_extracti128_si256(y, 1));
	}
};
#endif

}}  // namespace emp::detail

#endif  // __x86_64__

#ifdef __aarch64__

namespace emp {

// aarch64 AesLane<1>. NEON has no widening VAES, so only N=1 exists.
// aesenc / aesenclast use x86-compatible "key-after" semantics —
// vaeseq_u8 with a zero key, then XOR the real key. This breaks
// AESE/AESMC fusion (the natural NEON pattern XORs the key BEFORE
// the round), so the abstraction is heavier than a hand-rolled NEON
// loop would be.
template<int N> struct AesLane;

template<>
struct AesLane<1> {
	static constexpr int N = 1;
	using vec_t = block;
	static vec_t broadcast(__m128i k)                                  { return k; }
	static vec_t load(const block *p)                                  { return _mm_loadu_si128((const __m128i *)p); }
	static void  store(block *p, vec_t v)                              { _mm_storeu_si128((__m128i *)p, v); }
	static vec_t xorv(vec_t a, vec_t b)                                { return _mm_xor_si128(a, b); }
	static vec_t aesenc(vec_t s, vec_t k) {
		const uint8x16_t zero = vdupq_n_u8(0);
		uint8x16_t r = vaesmcq_u8(vaeseq_u8(vreinterpretq_u8_m128i(s), zero));
		return _mm_xor_si128(vreinterpretq_m128i_u8(r), k);
	}
	static vec_t aesenclast(vec_t s, vec_t k) {
		const uint8x16_t zero = vdupq_n_u8(0);
		uint8x16_t r = vaeseq_u8(vreinterpretq_u8_m128i(s), zero);
		return _mm_xor_si128(vreinterpretq_m128i_u8(r), k);
	}
	static vec_t ctr_xor_tweak(int64_t b0, int slot, vec_t tw) {
		return _mm_xor_si128(_mm_set_epi64x(0, b0 + slot), tw);
	}
};

}  // namespace emp

namespace emp { namespace detail {

// ClmulLane<1> aarch64: NEON poly64 carryless multiply, one block per
// iter.
template<int N> struct ClmulLane;

template<>
struct ClmulLane<1> {
	static constexpr int N = 1;
	using vec_t = block;
	static vec_t zero()                                                { return _mm_setzero_si128(); }
	static vec_t loadu(const block *p)                                 { return _mm_loadu_si128((const __m128i *)p); }
	static vec_t xorv(vec_t a, vec_t b)                                { return _mm_xor_si128(a, b); }
	static vec_t xor3(vec_t a, vec_t b, vec_t c)                       { return _mm_xor_si128(a, _mm_xor_si128(b, c)); }
	static vec_t clmul_lo_lo(vec_t a, vec_t b) {
		poly64_t al = (poly64_t)vget_low_u64(vreinterpretq_u64_m128i(a));
		poly64_t bl = (poly64_t)vget_low_u64(vreinterpretq_u64_m128i(b));
		return (block)vmull_p64(al, bl);
	}
	static vec_t clmul_hi_hi(vec_t a, vec_t b) {
		poly64_t ah = (poly64_t)vget_high_u64(vreinterpretq_u64_m128i(a));
		poly64_t bh = (poly64_t)vget_high_u64(vreinterpretq_u64_m128i(b));
		return (block)vmull_p64(ah, bh);
	}
	static vec_t clmul_hi_lo(vec_t a, vec_t b) {
		poly64_t ah = (poly64_t)vget_high_u64(vreinterpretq_u64_m128i(a));
		poly64_t bl = (poly64_t)vget_low_u64(vreinterpretq_u64_m128i(b));
		return (block)vmull_p64(ah, bl);
	}
	static vec_t clmul_lo_hi(vec_t a, vec_t b) {
		poly64_t al = (poly64_t)vget_low_u64(vreinterpretq_u64_m128i(a));
		poly64_t bh = (poly64_t)vget_high_u64(vreinterpretq_u64_m128i(b));
		return (block)vmull_p64(al, bh);
	}
	static vec_t bslli_8(vec_t v)                                      { return _mm_slli_si128(v, 8); }
	static vec_t bsrli_8(vec_t v)                                      { return _mm_srli_si128(v, 8); }
	static block fold_to_xmm(vec_t v)                                  { return v; }
};

}}  // namespace emp::detail

#endif  // __aarch64__

#endif  // EMP_SIMD_TIER_H__
