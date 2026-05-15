#ifndef EMP_AES_H
#define EMP_AES_H

#include "emp-tool/core/block.h"
#include <cassert>
#include <cstdint>

namespace emp {

typedef struct { block rd_key[11]; } AES_KEY;

#define EMP_AES_ASSERT_ALIGNED(p) \
	assert((reinterpret_cast<uintptr_t>(p) & (alignof(block) - 1)) == 0 \
	       && "pointer must be 16-byte (block) aligned")

// Tiered VAES support on x86_64. The widest tier the build can emit drives
// EMP_AES_TARGET_ATTR (which widens the function-level ISA permission for
// ParaEnc and friends). HAS_VAES256/HAS_VAES512 macros gate which Lane
// traits are visible and which constexpr tile counts kick in.
//
// VAES-512 hosts are a strict superset of VAES-256, so when 512 is available
// we keep the 256 tier active for tail blocks.
#if defined(__x86_64__) && defined(__VAES__) && defined(__AVX512F__)
	#define EMP_AES_TARGET_ATTR __attribute__((target("vaes,avx512f,avx2,aes,sse2")))
	#define EMP_AES_HAS_VAES512 1
	#define EMP_AES_HAS_VAES256 1
#elif defined(__x86_64__) && defined(__VAES__) && defined(__AVX2__)
	#define EMP_AES_TARGET_ATTR __attribute__((target("vaes,avx2,aes,sse2")))
	#define EMP_AES_HAS_VAES512 0
	#define EMP_AES_HAS_VAES256 1
#elif defined(__x86_64__)
	#define EMP_AES_TARGET_ATTR __attribute__((target("aes,sse2")))
	#define EMP_AES_HAS_VAES512 0
	#define EMP_AES_HAS_VAES256 0
#else
	#define EMP_AES_TARGET_ATTR
	#define EMP_AES_HAS_VAES512 0
	#define EMP_AES_HAS_VAES256 0
#endif

template<int NumKeys>
static inline void ks_rounds(AES_KEY * keys, block con, block con3, block mask, int r) {
	for (int i = 0; i < NumKeys; ++i) {
		block key = keys[i].rd_key[r-1];
		block x2 =_mm_shuffle_epi8(key, mask);
		block aux = _mm_aesenclast_si128 (x2, con);

		block globAux=_mm_slli_epi64(key, 32);
		key=_mm_xor_si128(globAux, key);
		globAux=_mm_shuffle_epi8(key, con3);
		key=_mm_xor_si128(globAux, key);
		keys[i].rd_key[r] = _mm_xor_si128(aux, key);
	}
}

/*
 * AES key scheduling for NumKeys keys.
 * [REF] "Fast Garbling of Circuits Under Standard Assumptions"
 * https://eprint.iacr.org/2015/751.pdf
 */
template<int NumKeys>
static inline void AES_opt_key_schedule(const block* user_key, AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(user_key);
	EMP_AES_ASSERT_ALIGNED(keys);
	block con = _mm_set_epi32(1,1,1,1);
	block con2 = _mm_set_epi32(0x1b,0x1b,0x1b,0x1b);
	block con3 = _mm_set_epi32(0x07060504,0x07060504,0x0ffffffff,0x0ffffffff);
	block mask = _mm_set_epi32(0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d);

	for(int i = 0; i < NumKeys; ++i)
		keys[i].rd_key[0] = user_key[i];

	for (int r = 1; r <= 8; ++r) {
		ks_rounds<NumKeys>(keys, con, con3, mask, r);
		con = _mm_slli_epi32(con, 1);
	}
	ks_rounds<NumKeys>(keys, con2, con3, mask, 9);
	con2 = _mm_slli_epi32(con2, 1);
	ks_rounds<NumKeys>(keys, con2, con3, mask, 10);
}

#ifdef __x86_64__
namespace detail {

// Lane traits unify scalar AES-NI (1 block / xmm), VAES+AVX-256 (2 blocks /
// ymm), and VAES+AVX-512 (4 blocks / zmm) under one template. Each tier
// supplies the same six primitives; the tile kernel below uses them
// uniformly. To add a new width (e.g. AVX-1024 if it ever ships), drop in
// another struct with the appropriate intrinsics.
// Each Lane provides:
//   - vec_t / N           : the SIMD vector type and how many AES blocks fit
//   - broadcast / load /
//     store / xorv /
//     aesenc / aesenclast : the six round-loop primitives
//   - ctr_xor_tweak       : build (counter | 0) ⊕ broadcasted-tweak directly
//                           in a vec_t (used by aes_tiles_src callers that
//                           build plaintexts inline rather than reading them
//                           from memory — e.g. fixed-key AES with a per-leaf
//                           tweak in SoftSpoken's sfvole inner loop).

struct Lane128 {
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

#if EMP_AES_HAS_VAES256
struct Lane256 {
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

#if EMP_AES_HAS_VAES512
struct Lane512 {
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

// Encrypt n_tiles tiles of L::N blocks each under a single key, sourcing
// each tile's plaintext via the caller's `src(t)` callable. `src` is
// invoked as `L::vec_t src(int t)` for t in [0, n_tiles); the round-key
// loop is unrolled and round keys are broadcast once per call.
//
// Used directly by callers that build plaintexts inline (e.g. fixed-key
// AES-CTR with tweak); the in-place form `aes_tiles` below is a thin
// wrapper that supplies `L::load` as the source.
template <class L, int n_tiles, class Source>
EMP_AES_TARGET_ATTR
static inline void aes_tiles_src(block *dst, Source&& src, const AES_KEY *kk) {
	if constexpr (n_tiles == 0) return;
	typename L::vec_t rk[11];
	for (int r = 0; r < 11; ++r)
		rk[r] = L::broadcast(kk->rd_key[r]);
	for (int t = 0; t < n_tiles; ++t) {
		auto x = src(t);
		x = L::xorv(x, rk[0]);
		for (int r = 1; r < 10; ++r)
			x = L::aesenc(x, rk[r]);
		x = L::aesenclast(x, rk[10]);
		L::store(dst + (size_t)t * L::N, x);
	}
}

// Encrypt n_tiles tiles of L::N blocks each, under a single key. Callers
// pass src == dst for the in-place case; the out-of-place no-overlap
// contract is communicated by the public ParaEnc wrappers' __restrict__
// parameters, propagated here through inlining.
template <class L, int n_tiles>
EMP_AES_TARGET_ATTR
static inline void aes_tiles(block *dst, const block *src, const AES_KEY *kk) {
	aes_tiles_src<L, n_tiles>(dst,
		[src](int t) -> typename L::vec_t { return L::load(src + (size_t)t * L::N); },
		kk);
}

// Per-platform implementation core. Single body for in-place and out-of-place
// — callers in this file forward through public ParaEnc wrappers that supply
// the __restrict__ annotation where appropriate.
template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
static inline void ParaEnc_impl(block *dst, const block *src, const AES_KEY *keys) {
#if EMP_AES_HAS_VAES512
	constexpr int W4 = 4, N4 = numEncs / 4;
#else
	constexpr int W4 = 0, N4 = 0;
#endif
#if EMP_AES_HAS_VAES256
	constexpr int W2 = 2, N2 = (numEncs - N4 * W4) / 2;
#else
	constexpr int W2 = 0, N2 = 0;
#endif
	constexpr int N1 = numEncs - N4 * W4 - N2 * W2;

	for (size_t k = 0; k < numKeys; ++k) {
		block * const pd = dst + k * (size_t)numEncs;
		const block * const ps = src + k * (size_t)numEncs;
		const AES_KEY * const kk = keys + k;
#if EMP_AES_HAS_VAES512
		if constexpr (N4 > 0) aes_tiles<Lane512, N4>(pd, ps, kk);
#endif
#if EMP_AES_HAS_VAES256
		if constexpr (N2 > 0) aes_tiles<Lane256, N2>(pd + N4 * W4, ps + N4 * W4, kk);
#endif
		if constexpr (N1 > 0) aes_tiles<Lane128, N1>(pd + N4 * W4 + N2 * W2, ps + N4 * W4 + N2 * W2, kk);
	}
}

}  // namespace detail
#endif  // __x86_64__

/*
 * With numKeys keys, use each key to encrypt numEncs blocks.
 *
 * Performance: the kernel is fully unrolled, so all K*N blocks and all K*11
 * round keys are expected to live in SIMD registers simultaneously. Targets
 * with 32 SIMD registers (modern ARMv8, AVX-512 x86) tolerate K*N up to
 * about 16 before the round keys plus working state exceed the register
 * file and the kernel starts spilling; throughput then drops 2-3x. Sweet
 * spot is K*N <= 16 - for larger work, dispatch through the runtime
 * ParaEnc(...) below, which tiles into {<1,8>,<1,4>,<1,2>,<1,1>}.
 *
 * On x86_64 the per-key stream is partitioned at compile time into:
 *   - 4-block zmm tiles (VAES + AVX-512), then
 *   - 2-block ymm tiles (VAES + AVX2),     then
 *   - 1-block scalar tail (AES-NI).
 * Tiers absent at build time fall through to the next available width.
 */
#ifdef __aarch64__
namespace detail {
// aarch64 ParaEnc core. Round 0 reads from src; rounds 1..9 operate on dst.
// In-place callers pass src == dst, which collapses round 0 to the original
// in-place behavior (*cur = vaesmcq_u8(vaeseq_u8(*cur, K0))).
template<int numKeys, int numEncs>
static inline void ParaEnc_impl(block *dst, const block *src, const AES_KEY *keys) {
	uint8x16_t * first_dst = (uint8x16_t*)(dst);
	const uint8x16_t * first_src = (const uint8x16_t*)(src);

	{
		auto cur_dst = first_dst;
		auto cur_src = first_src;
		for(size_t i = 0; i < numKeys; ++i) {
			uint8x16_t K = vreinterpretq_u8_m128i(keys[i].rd_key[0]);
			for(size_t j = 0; j < numEncs; ++j, ++cur_dst, ++cur_src)
			   *cur_dst = vaesmcq_u8(vaeseq_u8(*cur_src, K));
		}
	}

	for (unsigned int r = 1; r < 9; ++r) {
		auto cur = first_dst;
		for(size_t i = 0; i < numKeys; ++i) {
			uint8x16_t K = vreinterpretq_u8_m128i(keys[i].rd_key[r]);
			for(size_t j = 0; j < numEncs; ++j, ++cur)
			   *cur = vaesmcq_u8(vaeseq_u8(*cur, K));
		}
	}

	auto cur = first_dst;
	for(size_t i = 0; i < numKeys; ++i) {
		uint8x16_t K = vreinterpretq_u8_m128i(keys[i].rd_key[9]);
		uint8x16_t K2 = vreinterpretq_u8_m128i(keys[i].rd_key[10]);
		for(size_t j = 0; j < numEncs; ++j, ++cur)
			*cur = vaeseq_u8(*cur, K) ^ K2;
	}
}
}  // namespace detail
#endif

// Public templated ParaEnc — in-place form (blks ← AES(blks)).
template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
static inline void ParaEnc(block *blks, const AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_impl<numKeys, numEncs>(blks, blks, keys);
}

// Public templated ParaEnc — out-of-place form (dst ← AES(src)).
// dst and src must not overlap; the __restrict__ propagates into the impl
// via inlining so the compiler can reorder loads/stores across iterations.
template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
static inline void ParaEnc(block * __restrict__ dst,
                           const block * __restrict__ src,
                           const AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(dst);
	EMP_AES_ASSERT_ALIGNED(src);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_impl<numKeys, numEncs>(dst, src, keys);
}

/*
 * Runtime-sized companion to ParaEnc<K, N>: encrypts N blocks under each
 * of K keys with the same K-major layout (blks[k*N..k*N+N) under keys[k]).
 * Dispatches per key into compile-time tiles {8, 4, 2, 1} so each tile
 * still goes through the unrolled, register-resident templated kernel.
 *
 * Cross-key ILP is not exploited here — preserving K-major layout while
 * tiling on N would require interleaved layouts. Hot callers that know
 * (K, N) at compile time should use ParaEnc<K, N> directly.
 */
namespace detail {
EMP_AES_TARGET_ATTR
inline void ParaEnc_runtime_impl(block *dst, const block *src, const AES_KEY *keys, int K, int N) {
	for (int k = 0; k < K; ++k) {
		block *pd = dst + (size_t)k * N;
		const block *ps = src + (size_t)k * N;
		int n = N;
		while (n >= 8) { ParaEnc_impl<1, 8>(pd, ps, keys + k); pd += 8; ps += 8; n -= 8; }
		if (n >= 4)    { ParaEnc_impl<1, 4>(pd, ps, keys + k); pd += 4; ps += 4; n -= 4; }
		if (n >= 2)    { ParaEnc_impl<1, 2>(pd, ps, keys + k); pd += 2; ps += 2; n -= 2; }
		if (n >= 1)      ParaEnc_impl<1, 1>(pd, ps, keys + k);
	}
}
}  // namespace detail

// Runtime in-place form.
EMP_AES_TARGET_ATTR
inline void ParaEnc(block *blks, const AES_KEY *keys, int K, int N) {
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_runtime_impl(blks, blks, keys, K, N);
}

// Runtime out-of-place form. dst and src must not overlap.
EMP_AES_TARGET_ATTR
inline void ParaEnc(block * __restrict__ dst,
                    const block * __restrict__ src,
                    const AES_KEY *keys, int K, int N) {
	EMP_AES_ASSERT_ALIGNED(dst);
	EMP_AES_ASSERT_ALIGNED(src);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_runtime_impl(dst, src, keys, K, N);
}

// --- Single-key encrypt/decrypt wrappers ---

inline void AES_set_encrypt_key(const block& userkey, AES_KEY *key) {
	AES_opt_key_schedule<1>(&userkey, key);
}

template<int N>
inline void AES_ecb_encrypt_blks(block *blks, const AES_KEY *key) {
	ParaEnc<1, N>(blks, key);
}

inline void AES_ecb_encrypt_blks(block *blks, unsigned nblks, const AES_KEY *key) {
	ParaEnc(blks, key, 1, (int)nblks);
}

}  // namespace emp
#endif
