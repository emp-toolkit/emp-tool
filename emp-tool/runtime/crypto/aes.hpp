#ifndef EMP_AES_HPP
#define EMP_AES_HPP

// Inline definitions for aes.h's API. Included via aes.h; do not include
// directly. AesLane<N> for N ∈ {1, 2, 4} lives in simd_tier.h; only
// specializations the build can emit are defined, so the VAES guards
// inside ParaEnc_impl compile out the unavailable tiers.

#include "emp-tool/runtime/core/simd_tier.h"
#include <limits>

namespace emp {

// Encrypt n_tiles tiles of L::N blocks each under a single key. Both
// the per-tile plaintext and what to do with each tile's AES output are
// caller-supplied via callables:
//   src(t)            -> L::vec_t        : plaintext for tile t
//   store(t, x, pt)   -> void            : tile t's AES output x and the
//                                          plaintext pt; the store decides
//                                          where/how to write.
// The kernel keeps `pt` live in a SIMD register from src(t) through to
// store(t, …), so Davies–Meyer / CRH stores (`L::store(dst, L::xorv(x, pt))`)
// don't need a separate plaintext stash buffer.
template <class L, int n_tiles, class Source, class Store>
EMP_AES_TARGET_ATTR
inline void aes_tiles_src(Source&& src, Store&& store, const AES_KEY *kk) {
	if constexpr (n_tiles == 0) return;
	typename L::vec_t rk[11];
	for (int r = 0; r < 11; ++r)
		rk[r] = L::broadcast(kk->rd_key[r]);
#if defined(__x86_64__) && EMP_HAS_VAES512
	if constexpr (L::N == 4 && n_tiles > 1 && n_tiles <= 4) {
		typename L::vec_t pt[n_tiles];
		typename L::vec_t x[n_tiles];
		for (int t = 0; t < n_tiles; ++t) {
			pt[t] = src(t);
			x[t] = L::xorv(pt[t], rk[0]);
		}
		for (int r = 1; r < 10; ++r) {
			for (int t = 0; t < n_tiles; ++t)
				x[t] = L::aesenc(x[t], rk[r]);
		}
		for (int t = 0; t < n_tiles; ++t)
			store(t, L::aesenclast(x[t], rk[10]), pt[t]);
		return;
	}
#endif
	for (int t = 0; t < n_tiles; ++t) {
		auto pt = src(t);
#if defined(__aarch64__)
		if constexpr (L::N == 1) {
			uint8x16_t x = vreinterpretq_u8_m128i(pt);
			for (int r = 0; r < 9; ++r)
				x = vaesmcq_u8(vaeseq_u8(x, vreinterpretq_u8_m128i(rk[r])));
			x = vaeseq_u8(x, vreinterpretq_u8_m128i(rk[9]));
			auto out = _mm_xor_si128(vreinterpretq_m128i_u8(x), rk[10]);
			store(t, out, pt);
			continue;
		}
#endif
		auto x = L::xorv(pt, rk[0]);
		for (int r = 1; r < 10; ++r)
			x = L::aesenc(x, rk[r]);
		x = L::aesenclast(x, rk[10]);
		store(t, x, pt);
	}
}

// Convenience overload for the plain-store case: writes AES output to
// dst[t * L::N..(t+1) * L::N) and ignores the plaintext. Kept so existing
// callers that don't need DM stay terse.
template <class L, int n_tiles, class Source>
EMP_AES_TARGET_ATTR
inline void aes_tiles_src(block *dst, Source&& src, const AES_KEY *kk) {
	aes_tiles_src<L, n_tiles>(
		std::forward<Source>(src),
		[dst](int t, typename L::vec_t x, typename L::vec_t /*pt*/) {
			L::store(dst + (size_t)t * L::N, x);
		},
		kk);
}

// Encrypt n_tiles tiles of L::N blocks each, under a single key. Callers
// pass src == dst for the in-place case; the out-of-place no-overlap
// contract is communicated by the public ParaEnc wrappers' __restrict__
// parameters, propagated here through inlining.
template <class L, int n_tiles>
EMP_AES_TARGET_ATTR
inline void aes_tiles(block *dst, const block *src, const AES_KEY *kk) {
	aes_tiles_src<L, n_tiles>(dst,
		[src](int t) -> typename L::vec_t { return L::load(src + (size_t)t * L::N); },
		kk);
}

// Memory-source sibling of aes_tiles: writes AES(src) ^ src. Used by
// Davies-Meyer / CRH callers that already have plaintext blocks in memory.
template <class L, int n_tiles>
EMP_AES_TARGET_ATTR
inline void aes_tiles_xor_input(block *dst, const block *src, const AES_KEY *kk) {
	aes_tiles_src<L, n_tiles>(
		[src](int t) -> typename L::vec_t { return L::load(src + (size_t)t * L::N); },
		[dst](int t, typename L::vec_t x, typename L::vec_t pt) {
			L::store(dst + (size_t)t * L::N, L::xorv(x, pt));
		},
		kk);
}

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

struct AesMemPlainTile {
	template<class L, int n_tiles>
	EMP_AES_TARGET_ATTR
	static void apply(block *dst, const block *src, const AES_KEY *kk) {
		aes_tiles<L, n_tiles>(dst, src, kk);
	}
};

struct AesMemXorInputTile {
	template<class L, int n_tiles>
	EMP_AES_TARGET_ATTR
	static void apply(block *dst, const block *src, const AES_KEY *kk) {
		aes_tiles_xor_input<L, n_tiles>(dst, src, kk);
	}
};

// Davies-Meyer-over-σ tile: dst = AES(σ(src)) ⊕ σ(src). σ is applied in the
// tile source, so σ(src) stays live in-register as the XOR-back operand —
// no σ stash buffer and no second pass. Shared by CCRH's compile-time H<n>
// (via ParaEnc_mem_tiles_impl) and runtime Hn (via MemTileOp + drain_tiles).
struct AesMemXorSigmaTile {
	template<class L, int n_tiles>
	EMP_AES_TARGET_ATTR
	static void apply(block *dst, const block *src, const AES_KEY *kk) {
		aes_tiles_src<L, n_tiles>(
			[src](int t) { return L::sigma(L::load(src + (size_t)t * L::N)); },
			[dst](int t, typename L::vec_t x, typename L::vec_t pt) {
				L::store(dst + (size_t)t * L::N, L::xorv(x, pt));
			}, kk);
	}
};

// Emit a MemTile op over n_tiles AesLane<4> tiles in groups of <= 4. Each
// group lands on aes_tiles_src's VAES-512 interleaved path (gated n_tiles<=4)
// instead of the serial fallback that a single n_tiles>4 call would take.
// Byte-identical to one apply<L,n_tiles>: the blocks are independent, only the
// AES-pipelining grouping changes. Used for compile-time numEncs > 16, where
// N4 = numEncs/4 exceeds 4; for n_tiles <= 4 it is a single apply as before.
template<class MemTile, class L, int n_tiles>
EMP_AES_TARGET_ATTR
static inline void apply_grouped(block *dst, const block *src, const AES_KEY *kk) {
	constexpr int G = 4;
	if constexpr (n_tiles > G) {
		MemTile::template apply<L, G>(dst, src, kk);
		apply_grouped<MemTile, L, n_tiles - G>(
			dst + (size_t)G * L::N, src + (size_t)G * L::N, kk);
	} else if constexpr (n_tiles > 0) {
		MemTile::template apply<L, n_tiles>(dst, src, kk);
	}
}

// Per-key tile decomposition: split numEncs into {AesLane<4>,
// AesLane<2>, AesLane<1>} chunks at compile time, picking the widest
// available tier per slot. Each tile runs the fully-unrolled
// aes_tiles<L, n> kernel so all K*N working blocks + 11 round keys
// stay in SIMD registers (sweet spot K*N <= 16).
//
// On aarch64 only AesLane<1> exists, so the VAES guards compile out
// and the body collapses to a single AesLane<1> memory-input tile op
// call per key.
template<class MemTile, int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
static inline void ParaEnc_mem_tiles_impl(block *dst, const block *src, const AES_KEY *keys) {
	static_assert(numKeys >= 0 && numEncs >= 0,
	              "ParaEnc: dimensions must be non-negative");
	if constexpr (numKeys == 0 || numEncs == 0) return;
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
		if constexpr (N4 > 0) apply_grouped<MemTile, AesLane<4>, N4>(pd, ps, kk);
#endif
#if EMP_HAS_VAES256
		if constexpr (N2 > 0) MemTile::template apply<AesLane<2>, N2>(pd + N4 * W4, ps + N4 * W4, kk);
#endif
		if constexpr (N1 > 0) MemTile::template apply<AesLane<1>, N1>(pd + N4 * W4 + N2 * W2, ps + N4 * W4 + N2 * W2, kk);
	}
}

template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
static inline void ParaEnc_impl(block *dst, const block *src, const AES_KEY *keys) {
	ParaEnc_mem_tiles_impl<AesMemPlainTile, numKeys, numEncs>(dst, src, keys);
}

// Same layout/tiling as ParaEnc_impl, but stores AES(src) ^ src.
template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
static inline void ParaEncXorInput_impl(block *dst, const block *src, const AES_KEY *keys) {
	ParaEnc_mem_tiles_impl<AesMemXorInputTile, numKeys, numEncs>(dst, src, keys);
}

// Single runtime tile ladder shared by every runtime-sized AES path
// (ECB / XorInput / σ-CRH / CTR). Peels the widest available tile first
// ({16, 8, 4, 2, 1} on VAES512; otherwise {8, 4, 2, 1}); `op.run<W>()`
// emits one compile-time tile and advances the op's own cursor. Keeping
// the ladder in one place stops the CTR and memory paths from drifting
// to different tile widths.
template<class Op>
EMP_AES_TARGET_ATTR
inline void drain_tiles(Op &op, int64_t n) {
#if EMP_HAS_VAES512
	while (n >= 16) { op.template run<16>(); n -= 16; }
#endif
	while (n >= 8)  { op.template run<8>();  n -= 8; }
	if (n >= 4)     { op.template run<4>();  n -= 4; }
	if (n >= 2)     { op.template run<2>();  n -= 2; }
	if (n >= 1)       op.template run<1>();
}

// Per-key memory tile op for drain_tiles. Each run<W> goes through the
// unrolled, register-resident ParaEnc_mem_tiles_impl. Cross-key ILP is not
// exploited (would require an interleaved layout); hot callers that know
// (K, N) at compile time should use ParaEnc<K, N> directly.
template<class MemTile>
struct MemTileOp {
	block *pd; const block *ps; const AES_KEY *kk;
	template<int W> EMP_AES_TARGET_ATTR void run() {
		ParaEnc_mem_tiles_impl<MemTile, 1, W>(pd, ps, kk); pd += W; ps += W;
	}
};

template<class MemTile>
EMP_AES_TARGET_ATTR
inline void ParaEnc_mem_tiles_runtime_impl(block *dst, const block *src, const AES_KEY *keys, int64_t K, int64_t N) {
	for (int64_t k = 0; k < K; ++k) {
		MemTileOp<MemTile> op{ dst + k * N, src + k * N, keys + k };
		drain_tiles(op, N);
	}
}

inline bool ParaEnc_runtime_empty(int64_t K, int64_t N) {
	expecting(K >= 0 && N >= 0, "ParaEnc: dimensions must be non-negative");
	expecting(N == 0 || K <= std::numeric_limits<int64_t>::max() / N,
	          "ParaEnc: dimensions overflow int64_t");
	return K == 0 || N == 0;
}

EMP_AES_TARGET_ATTR
inline void ParaEnc_runtime_impl(block *dst, const block *src, const AES_KEY *keys, int64_t K, int64_t N) {
	ParaEnc_mem_tiles_runtime_impl<AesMemPlainTile>(dst, src, keys, K, N);
}

EMP_AES_TARGET_ATTR
inline void ParaEncXorInput_runtime_impl(block *dst, const block *src, const AES_KEY *keys, int64_t K, int64_t N) {
	ParaEnc_mem_tiles_runtime_impl<AesMemXorInputTile>(dst, src, keys, K, N);
}

// AES-CTR fill over W blocks, one lane L. Counter plaintexts are built
// in-register via L::ctr_xor_tweak, so the kernel only writes dst once —
// no intermediate counter-write pass. Plain stores dst[i] = AES_K(ctr_i);
// XorBack stores the Davies-Meyer dst[i] = AES_K(ctr_i ⊕ tweak) ⊕ (ctr_i ⊕
// tweak), with the XOR done in-register at store time (no pt[] stash).
enum class CtrStore { Plain, XorBack };

template <class L, int W, CtrStore St>
EMP_AES_TARGET_ATTR
static inline void aes_ctr_fill_lane(block *dst, uint64_t counter,
                                     const AES_KEY *kk, block tweak) {
	// Build the tweak broadcast from a compile-time zero on the Plain path so
	// the per-counter XOR folds away. Threading `tweak` (= zero_block) through
	// as a runtime value instead leaves a live XOR that ~halves single-block
	// random_block on the VAES-512 target. XorBack genuinely needs the tweak.
	aes_tiles_src<L, W / L::N>(
		[&](int t) {
			if constexpr (St == CtrStore::XorBack)
				return L::ctr_xor_tweak(counter, t, L::broadcast(tweak));
			else
				return L::ctr_xor_tweak(counter, t, L::broadcast(_mm_setzero_si128()));
		},
		[dst](int t, typename L::vec_t x, typename L::vec_t pt) {
			if constexpr (St == CtrStore::XorBack) x = L::xorv(x, pt);
			L::store(dst + (size_t)t * L::N, x);
		}, kk);
}

template <int W, CtrStore St>
EMP_AES_TARGET_ATTR
static inline void aes_ctr_fill_impl(block *dst, uint64_t counter,
                                     const AES_KEY *kk, block tweak) {
#if EMP_HAS_VAES512
	if constexpr (W % 4 == 0) { aes_ctr_fill_lane<AesLane<4>, W, St>(dst, counter, kk, tweak); return; }
#endif
#if EMP_HAS_VAES256
	if constexpr (W % 2 == 0) { aes_ctr_fill_lane<AesLane<2>, W, St>(dst, counter, kk, tweak); return; }
#endif
	aes_ctr_fill_lane<AesLane<1>, W, St>(dst, counter, kk, tweak);
}

template <int W>
EMP_AES_TARGET_ATTR
static inline void aes_ctr_fill(block *dst, uint64_t counter, const AES_KEY *kk) {
	aes_ctr_fill_impl<W, CtrStore::Plain>(dst, counter, kk, zero_block);
}

struct CtrFillOp {
	block *dst; uint64_t counter; const AES_KEY *kk;
	template<int W> EMP_AES_TARGET_ATTR void run() {
		aes_ctr_fill<W>(dst, counter, kk); dst += W;
		counter += static_cast<uint64_t>(W);
	}
};

EMP_AES_TARGET_ATTR
inline void ParaCtrEnc(block *dst, uint64_t counter, const AES_KEY *kk, int64_t n) {
	CtrFillOp op{dst, counter, kk};
	drain_tiles(op, n);
}

}  // namespace detail

// Davies–Meyer / CRH counter-mode fill:
//   dst[i] = AES_K(counter + i ⊕ tweak) ⊕ (counter + i ⊕ tweak),  i ∈ [0, W).
// Correlation-robust under a *public* AES key (fixed-key AES modelled as
// an ideal cipher) — use this when callers want the AES key public for
// schedule-amortization and rely on the XOR-back for output PRG-ness.
// For a secret AES key (regular PRG), use detail::aes_ctr_fill instead.
template <int W>
EMP_AES_TARGET_ATTR
inline void aes_ctr_fill_dm(block *dst, uint64_t counter,
                            const AES_KEY *kk, block tweak = zero_block) {
	static_assert(W >= 0, "aes_ctr_fill_dm: output width must be non-negative");
	if constexpr (W == 0) return;
	detail::aes_ctr_fill_impl<W, detail::CtrStore::XorBack>(dst, counter, kk, tweak);
}

// Schedule NumKeys AES-128 round keys per "Fast Garbling of Circuits
// Under Standard Assumptions" (https://eprint.iacr.org/2015/751.pdf).
template<int NumKeys>
inline void AES_opt_key_schedule(const block* user_key, AES_KEY *keys) {
	static_assert(NumKeys >= 0, "AES_opt_key_schedule: key count must be non-negative");
	if constexpr (NumKeys == 0) return;
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
	if constexpr (numKeys == 0 || numEncs == 0) return;
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_impl<numKeys, numEncs>(blks, blks, keys);
}

template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
inline void ParaEnc(block * __restrict__ dst,
                    const block * __restrict__ src,
                    const AES_KEY *keys) {
	if constexpr (numKeys == 0 || numEncs == 0) return;
	EMP_AES_ASSERT_ALIGNED(dst);
	EMP_AES_ASSERT_ALIGNED(src);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_impl<numKeys, numEncs>(dst, src, keys);
}

EMP_AES_TARGET_ATTR
inline void ParaEnc(block *blks, const AES_KEY *keys, int64_t K, int64_t N) {
	if (detail::ParaEnc_runtime_empty(K, N)) return;
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
	detail::ParaEnc_runtime_impl(blks, blks, keys, K, N);
}

EMP_AES_TARGET_ATTR
inline void ParaEnc(block * __restrict__ dst,
                    const block * __restrict__ src,
                    const AES_KEY *keys, int64_t K, int64_t N) {
	if (detail::ParaEnc_runtime_empty(K, N)) return;
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

inline void AES_ecb_encrypt_blks(block *blks, int64_t nblks, const AES_KEY *key) {
	ParaEnc(blks, key, 1, nblks);
}

}  // namespace emp
#endif
