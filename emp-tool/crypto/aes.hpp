#ifndef EMP_AES_HPP
#define EMP_AES_HPP

// Inline definitions for aes.h's API. Included via aes.h; do not include
// directly. AesLane<N> for N ∈ {1, 2, 4} lives in simd_tier.h; only
// specializations the build can emit are defined, so the VAES guards
// inside ParaEnc_impl compile out the unavailable tiers.

#include "emp-tool/core/simd_tier.h"

namespace emp {

namespace detail {

template<int NumKeys>
static inline void ks_rounds(AES_KEY* keys, block con, block con3, block mask, int r) {
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

// Per-key tile decomposition: split numEncs into {AesLane<4>,
// AesLane<2>, AesLane<1>} chunks at compile time, picking the widest
// available tier per slot. Each tile runs the fully-unrolled
// aes_tiles<L, n> kernel so all K*N working blocks + 11 round keys
// stay in SIMD registers (sweet spot K*N <= 16).
//
// On aarch64 only AesLane<1> exists, so the VAES guards compile out
// and the body collapses to a single aes_tiles<AesLane<1>, numEncs>
// call per key.
template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
static inline void ParaEnc_impl(block *dst, const block *src, const AES_KEY *keys) {
#if EMP_HAS_VAES512
	constexpr int W4 = 4, N4 = numEncs / 4;
#else
	constexpr int W4 = 0, N4 = 0;
#endif
#if EMP_HAS_VAES256
	constexpr int W2 = 2, N2 = (numEncs - N4 * W4) / 2;
#else
	constexpr int W2 = 0, N2 = 0;
#endif
	constexpr int N1 = numEncs - N4 * W4 - N2 * W2;

	for (size_t k = 0; k < numKeys; ++k) {
		block * const pd = dst + k * (size_t)numEncs;
		const block * const ps = src + k * (size_t)numEncs;
		const AES_KEY * const kk = keys + k;
#if EMP_HAS_VAES512
		if constexpr (N4 > 0) aes_tiles<AesLane<4>, N4>(pd, ps, kk);
#endif
#if EMP_HAS_VAES256
		if constexpr (N2 > 0) aes_tiles<AesLane<2>, N2>(pd + N4 * W4, ps + N4 * W4, kk);
#endif
		if constexpr (N1 > 0) aes_tiles<AesLane<1>, N1>(pd + N4 * W4 + N2 * W2, ps + N4 * W4 + N2 * W2, kk);
	}
}

// Runtime dispatcher: tile each per-key stream into compile-time tiles
// {8, 4, 2, 1} so each tile still goes through the unrolled
// ParaEnc_impl. Cross-key ILP is not exploited (would require an
// interleaved layout); hot callers that know (K, N) at compile time
// should use ParaEnc<K, N> directly.
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

// AES-CTR fill: dst[i] = AES_K(counter + i) for i in [0, W). Plaintext
// counters are built in-register via AesLane<>::ctr_xor_tweak (with a
// zero tweak), so the kernel only writes dst once — no intermediate
// counter-write pass through the buffer.
template <int W>
EMP_AES_TARGET_ATTR
static inline void aes_ctr_fill(block *dst, int64_t counter, const AES_KEY *kk) {
#if EMP_HAS_VAES512
	if constexpr (W % 4 == 0) {
		using L = AesLane<4>;
		const auto z = L::broadcast(_mm_setzero_si128());
		aes_tiles_src<L, W/4>(dst,
			[&](int t) { return L::ctr_xor_tweak(counter, t, z); }, kk);
		return;
	}
#endif
#if EMP_HAS_VAES256
	if constexpr (W % 2 == 0) {
		using L = AesLane<2>;
		const auto z = L::broadcast(_mm_setzero_si128());
		aes_tiles_src<L, W/2>(dst,
			[&](int t) { return L::ctr_xor_tweak(counter, t, z); }, kk);
		return;
	}
#endif
	using L = AesLane<1>;
	const auto z = L::broadcast(_mm_setzero_si128());
	aes_tiles_src<L, W>(dst,
		[&](int t) { return L::ctr_xor_tweak(counter, t, z); }, kk);
}

EMP_AES_TARGET_ATTR
inline void ParaCtrEnc(block *dst, int64_t counter, const AES_KEY *kk, int n) {
	while (n >= 8) { aes_ctr_fill<8>(dst, counter, kk); dst += 8; counter += 8; n -= 8; }
	if (n >= 4)    { aes_ctr_fill<4>(dst, counter, kk); dst += 4; counter += 4; n -= 4; }
	if (n >= 2)    { aes_ctr_fill<2>(dst, counter, kk); dst += 2; counter += 2; n -= 2; }
	if (n >= 1)      aes_ctr_fill<1>(dst, counter, kk);
}

}  // namespace detail

// Schedule NumKeys AES-128 round keys per "Fast Garbling of Circuits
// Under Standard Assumptions" (https://eprint.iacr.org/2015/751.pdf).
template<int NumKeys>
inline void AES_opt_key_schedule(const block* user_key, AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(user_key);
	EMP_AES_ASSERT_ALIGNED(keys);
	block con = _mm_set_epi32(1,1,1,1);
	block con2 = _mm_set_epi32(0x1b,0x1b,0x1b,0x1b);
	block con3 = _mm_set_epi32(0x07060504,0x07060504,0x0ffffffff,0x0ffffffff);
	block mask = _mm_set_epi32(0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d);

	for(int i = 0; i < NumKeys; ++i)
		keys[i].rd_key[0] = user_key[i];

	for (int r = 1; r <= 8; ++r) {
		detail::ks_rounds<NumKeys>(keys, con, con3, mask, r);
		con = _mm_slli_epi32(con, 1);
	}
	detail::ks_rounds<NumKeys>(keys, con2, con3, mask, 9);
	con2 = _mm_slli_epi32(con2, 1);
	detail::ks_rounds<NumKeys>(keys, con2, con3, mask, 10);
}

template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
inline void ParaEnc(block *blks, const AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_impl<numKeys, numEncs>(blks, blks, keys);
}

template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
inline void ParaEnc(block * __restrict__ dst,
                    const block * __restrict__ src,
                    const AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(dst);
	EMP_AES_ASSERT_ALIGNED(src);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_impl<numKeys, numEncs>(dst, src, keys);
}

EMP_AES_TARGET_ATTR
inline void ParaEnc(block *blks, const AES_KEY *keys, int K, int N) {
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_runtime_impl(blks, blks, keys, K, N);
}

EMP_AES_TARGET_ATTR
inline void ParaEnc(block * __restrict__ dst,
                    const block * __restrict__ src,
                    const AES_KEY *keys, int K, int N) {
	EMP_AES_ASSERT_ALIGNED(dst);
	EMP_AES_ASSERT_ALIGNED(src);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_runtime_impl(dst, src, keys, K, N);
}

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
