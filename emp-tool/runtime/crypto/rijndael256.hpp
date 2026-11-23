#ifndef EMP_RIJNDAEL256_HPP
#define EMP_RIJNDAEL256_HPP

// Inline definitions for rijndael256.h's API. Included via rijndael256.h; do
// not include directly.
//
// A Rijndael-256 round is an AES round on each 128-bit half of the state after
// one byte permutation P. AESENC runs ShiftRows, SubBytes, MixColumns and
// AddRoundKey on each half; Rijndael-256 differs only in ShiftRows, where row r
// rotates by C_r = 0, 1, 3, 4 of the eight columns, so bytes move between the
// halves. SubBytes acts on each byte alone and commutes with any byte
// permutation, so with SR4 the AES ShiftRows of each half and SR8 the
// Rijndael-256 one, AESENC applied to P(x) with SR4 o P = SR8 is exactly a
// Rijndael-256 round under the unchanged round key. The tiers spell P as:
//   VAES-512 + VBMI  one vpermb on a zmm holding two blocks;
//   VAES-256, AES-NI per half, pblendvb takes the bytes that come from the
//                    other half and pshufb moves them into place, one (mask,
//                    shuffle) pair for both halves. The low halves of the
//                    blocks share one vector and the high halves another, so
//                    no move crosses a 128-bit lane;
//   aarch64          one two-register TBL per half. AESE adds its round key
//                    before ShiftRows, so it sees P(x) ^ P(K) = P(x ^ K): the
//                    schedule stores P(K_r) for r < 14 and K_14 as it is.
// The key schedule (Nk = 8) is the AES-256 expansion run on to 15 round keys.
//
// Decryption is the equivalent inverse cipher, the same shape: AESDEC (AESD
// then AESIMC on aarch64) applied to Pd(x), with InvSR4 o Pd = InvSR8, under
// round keys K_14, InvMixColumns(K_13), .., InvMixColumns(K_1), K_0, which
// the aarch64 schedule stores as Pd(.) for all but the last.

#include "emp-tool/runtime/core/simd_tier.h"
#include "emp-tool/runtime/crypto/aes.h"  // detail::drain_tiles
#include <cstdint>

namespace emp {
namespace detail {

// P in gather form, P(x)[i] = x[r256_perm[i]], over the 32 bytes of a block.
// test_rijndael256 derives all six tables from the two ShiftRows.
alignas(32) inline constexpr uint8_t r256_perm[32] = {
	0x00, 0x11, 0x16, 0x17, 0x04, 0x05, 0x1a, 0x1b, 0x08, 0x09, 0x0e, 0x1f, 0x0c, 0x0d, 0x12, 0x13,
	0x10, 0x01, 0x06, 0x07, 0x14, 0x15, 0x0a, 0x0b, 0x18, 0x19, 0x1e, 0x0f, 0x1c, 0x1d, 0x02, 0x03};
// P per half: half h of P(x) = pshufb(pblendvb(x_h, x_{1-h}, r256_blend_mask),
// r256_shuffle), where pblendvb takes x_{1-h} at the 0xff bytes.
alignas(16) inline constexpr uint8_t r256_blend_mask[16] = {
	0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff};
alignas(16) inline constexpr uint8_t r256_shuffle[16] = {
	0x00, 0x01, 0x06, 0x07, 0x04, 0x05, 0x0a, 0x0b, 0x08, 0x09, 0x0e, 0x0f, 0x0c, 0x0d, 0x02, 0x03};
// Pd, the decryption permutation, in the same three forms.
alignas(32) inline constexpr uint8_t r256_dec_perm[32] = {
	0x00, 0x01, 0x1e, 0x1f, 0x04, 0x05, 0x02, 0x13, 0x08, 0x09, 0x16, 0x17, 0x0c, 0x1d, 0x1a, 0x1b,
	0x10, 0x11, 0x0e, 0x0f, 0x14, 0x15, 0x12, 0x03, 0x18, 0x19, 0x06, 0x07, 0x1c, 0x0d, 0x0a, 0x0b};
alignas(16) inline constexpr uint8_t r256_dec_blend_mask[16] = {
	0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff};
alignas(16) inline constexpr uint8_t r256_dec_shuffle[16] = {
	0x00, 0x01, 0x0e, 0x0f, 0x04, 0x05, 0x02, 0x03, 0x08, 0x09, 0x06, 0x07, 0x0c, 0x0d, 0x0a, 0x0b};

// R256Lane: the build's Rijndael-256 kernel. B Rijndael-256 blocks per vec_t;
// keys<Dec>(rd_key) loads a schedule's round keys and the direction's
// permutation once per call; crypt_n<Dec, M> runs the whole cipher in that
// direction on M vectors, all M advancing one round before the next; max_tile
// is the most blocks one unrolled tile holds. A two-block lane also loads and
// stores a vector holding one block (load_one / store_one).
#if EMP_HAS_R256_VPERMB512

// Two blocks per zmm, in memory order [lo0 hi0 lo1 hi1].
struct R256Lane {
	static constexpr int B = 2;
	static constexpr int max_tile = 16;
	using vec_t = __m512i;
	struct keys_t { vec_t rk[15]; vec_t perm; };
	template<bool Dec>
	EMP_R256_TARGET_ATTR __attribute__((always_inline)) static keys_t keys(const block *rd_key) {
		keys_t s;
		for (int r = 0; r < 15; ++r)
			s.rk[r] = _mm512_broadcast_i64x4(_mm256_loadu_si256((const __m256i *)(rd_key + 2 * r)));
		// The permutation on each 256-bit half: the high half's indices are offset by 32.
		const __m256i p = _mm256_loadu_si256((const __m256i *)(Dec ? r256_dec_perm : r256_perm));
		s.perm = _mm512_inserti64x4(_mm512_castsi256_si512(p),
		                            _mm256_add_epi8(p, _mm256_set1_epi8(32)), 1);
		return s;
	}
	EMP_R256_TARGET_ATTR static vec_t load(const block *p)         { return _mm512_loadu_si512((const void *)p); }
	EMP_R256_TARGET_ATTR static void  store(block *p, vec_t x)     { _mm512_storeu_si512((void *)p, x); }
	// The block in the low 256 bits: the high 256 bits are zero on load and
	// are never written on store, so a masked-off lane does not fault.
	EMP_R256_TARGET_ATTR static vec_t load_one(const block *p)     { return _mm512_maskz_loadu_epi64(0x0F, (const void *)p); }
	EMP_R256_TARGET_ATTR static void  store_one(block *p, vec_t x) { _mm512_mask_storeu_epi64((void *)p, 0x0F, x); }
	template<bool Dec, int M>
	EMP_R256_TARGET_ATTR __attribute__((always_inline)) static void crypt_n(vec_t (&x)[M], const keys_t &k) {
		for (int t = 0; t < M; ++t) x[t] = _mm512_xor_si512(x[t], k.rk[0]);
		for (int r = 1; r < 14; ++r)
			for (int t = 0; t < M; ++t) {
				const vec_t y = _mm512_permutexvar_epi8(k.perm, x[t]);
				if constexpr (Dec) x[t] = _mm512_aesdec_epi128(y, k.rk[r]);
				else               x[t] = _mm512_aesenc_epi128(y, k.rk[r]);
			}
		for (int t = 0; t < M; ++t) {
			const vec_t y = _mm512_permutexvar_epi8(k.perm, x[t]);
			if constexpr (Dec) x[t] = _mm512_aesdeclast_epi128(y, k.rk[14]);
			else               x[t] = _mm512_aesenclast_epi128(y, k.rk[14]);
		}
	}
};

#elif defined(__x86_64__)

// The vector ops of the split layout: `a` holds the low halves of B blocks,
// one per 128-bit lane, and `b` their high halves.
#if EMP_HAS_VAES256
struct R256SplitOps {
	static constexpr int B = 2;
	using v = __m256i;
	EMP_R256_TARGET_ATTR static v bcast(const void *p)    { return _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)p)); }
	EMP_R256_TARGET_ATTR static v xorv(v x, v y)          { return _mm256_xor_si256(x, y); }
	EMP_R256_TARGET_ATTR static v blendv(v x, v y, v m)   { return _mm256_blendv_epi8(x, y, m); }
	EMP_R256_TARGET_ATTR static v shuffle(v x, v s)       { return _mm256_shuffle_epi8(x, s); }
	EMP_R256_TARGET_ATTR static v aesenc(v x, v k)        { return _mm256_aesenc_epi128(x, k); }
	EMP_R256_TARGET_ATTR static v aesenclast(v x, v k)    { return _mm256_aesenclast_epi128(x, k); }
	EMP_R256_TARGET_ATTR static v aesdec(v x, v k)        { return _mm256_aesdec_epi128(x, k); }
	EMP_R256_TARGET_ATTR static v aesdeclast(v x, v k)    { return _mm256_aesdeclast_epi128(x, k); }
	EMP_R256_TARGET_ATTR static void load(const block *p, v &a, v &b) {
		const v m0 = _mm256_loadu_si256((const __m256i *)p), m1 = _mm256_loadu_si256((const __m256i *)(p + 2));
		a = _mm256_permute2x128_si256(m0, m1, 0x20);
		b = _mm256_permute2x128_si256(m0, m1, 0x31);
	}
	EMP_R256_TARGET_ATTR static void store(block *p, v a, v b) {
		_mm256_storeu_si256((__m256i *)p, _mm256_permute2x128_si256(a, b, 0x20));
		_mm256_storeu_si256((__m256i *)(p + 2), _mm256_permute2x128_si256(a, b, 0x31));
	}
	// One block, in lane 0 of a and b; lane 1 is zero and never stored.
	EMP_R256_TARGET_ATTR static void load_one(const block *p, v &a, v &b) {
		a = _mm256_zextsi128_si256(_mm_loadu_si128((const __m128i *)p));
		b = _mm256_zextsi128_si256(_mm_loadu_si128((const __m128i *)(p + 1)));
	}
	EMP_R256_TARGET_ATTR static void store_one(block *p, v a, v b) {
		_mm_storeu_si128((__m128i *)p, _mm256_castsi256_si128(a));
		_mm_storeu_si128((__m128i *)(p + 1), _mm256_castsi256_si128(b));
	}
};
#else
struct R256SplitOps {
	static constexpr int B = 1;
	using v = __m128i;
	EMP_R256_TARGET_ATTR static v bcast(const void *p)    { return _mm_loadu_si128((const __m128i *)p); }
	EMP_R256_TARGET_ATTR static v xorv(v x, v y)          { return _mm_xor_si128(x, y); }
	EMP_R256_TARGET_ATTR static v blendv(v x, v y, v m)   { return _mm_blendv_epi8(x, y, m); }
	EMP_R256_TARGET_ATTR static v shuffle(v x, v s)       { return _mm_shuffle_epi8(x, s); }
	EMP_R256_TARGET_ATTR static v aesenc(v x, v k)        { return _mm_aesenc_si128(x, k); }
	EMP_R256_TARGET_ATTR static v aesenclast(v x, v k)    { return _mm_aesenclast_si128(x, k); }
	EMP_R256_TARGET_ATTR static v aesdec(v x, v k)        { return _mm_aesdec_si128(x, k); }
	EMP_R256_TARGET_ATTR static v aesdeclast(v x, v k)    { return _mm_aesdeclast_si128(x, k); }
	EMP_R256_TARGET_ATTR static void load(const block *p, v &a, v &b) {
		a = _mm_loadu_si128((const __m128i *)p);
		b = _mm_loadu_si128((const __m128i *)(p + 1));
	}
	EMP_R256_TARGET_ATTR static void store(block *p, v a, v b) {
		_mm_storeu_si128((__m128i *)p, a);
		_mm_storeu_si128((__m128i *)(p + 1), b);
	}
	// With one block per vector, one block is the whole vector.
	EMP_R256_TARGET_ATTR static void load_one(const block *p, v &a, v &b) { load(p, a, b); }
	EMP_R256_TARGET_ATTR static void store_one(block *p, v a, v b)        { store(p, a, b); }
};
#endif

struct R256Lane {
	using O = R256SplitOps;
	static constexpr int B = O::B;
	static constexpr int max_tile = 8;
	struct vec_t { O::v a, b; };
	struct keys_t { O::v ka[15], kb[15], mask, shuf; };
	template<bool Dec>
	EMP_R256_TARGET_ATTR __attribute__((always_inline)) static keys_t keys(const block *rd_key) {
		keys_t s;
		for (int r = 0; r < 15; ++r) {
			s.ka[r] = O::bcast(rd_key + 2 * r);
			s.kb[r] = O::bcast(rd_key + 2 * r + 1);
		}
		s.mask = O::bcast(Dec ? r256_dec_blend_mask : r256_blend_mask);
		s.shuf = O::bcast(Dec ? r256_dec_shuffle : r256_shuffle);
		return s;
	}
	EMP_R256_TARGET_ATTR static vec_t load(const block *p)         { vec_t x; O::load(p, x.a, x.b); return x; }
	EMP_R256_TARGET_ATTR static void  store(block *p, vec_t x)     { O::store(p, x.a, x.b); }
	EMP_R256_TARGET_ATTR static vec_t load_one(const block *p)     { vec_t x; O::load_one(p, x.a, x.b); return x; }
	EMP_R256_TARGET_ATTR static void  store_one(block *p, vec_t x) { O::store_one(p, x.a, x.b); }
	// The permutation on the split pair: each half blends in the other's
	// bytes, then shuffles.
	EMP_R256_TARGET_ATTR __attribute__((always_inline)) static vec_t permute(vec_t x, const keys_t &k) {
		return vec_t{O::shuffle(O::blendv(x.a, x.b, k.mask), k.shuf),
		             O::shuffle(O::blendv(x.b, x.a, k.mask), k.shuf)};
	}
	template<bool Dec, int M>
	EMP_R256_TARGET_ATTR __attribute__((always_inline)) static void crypt_n(vec_t (&x)[M], const keys_t &k) {
		for (int t = 0; t < M; ++t) {
			x[t].a = O::xorv(x[t].a, k.ka[0]);
			x[t].b = O::xorv(x[t].b, k.kb[0]);
		}
		for (int r = 1; r < 14; ++r)
			for (int t = 0; t < M; ++t) {
				const vec_t p = permute(x[t], k);
				if constexpr (Dec) {
					x[t].a = O::aesdec(p.a, k.ka[r]);
					x[t].b = O::aesdec(p.b, k.kb[r]);
				} else {
					x[t].a = O::aesenc(p.a, k.ka[r]);
					x[t].b = O::aesenc(p.b, k.kb[r]);
				}
			}
		for (int t = 0; t < M; ++t) {
			const vec_t p = permute(x[t], k);
			if constexpr (Dec) {
				x[t].a = O::aesdeclast(p.a, k.ka[14]);
				x[t].b = O::aesdeclast(p.b, k.kb[14]);
			} else {
				x[t].a = O::aesenclast(p.a, k.ka[14]);
				x[t].b = O::aesenclast(p.b, k.kb[14]);
			}
		}
	}
};

#elif defined(__aarch64__)

// One block per register pair; the schedules store their round keys
// permuted (see the header comment).
struct R256Lane {
	static constexpr int B = 1;
	static constexpr int max_tile = 8;
	using vec_t = uint8x16x2_t;
	struct keys_t { uint8x16_t ka[15], kb[15], p0, p1; };
	template<bool Dec>
	__attribute__((always_inline)) static keys_t keys(const block *rd_key) {
		keys_t s;
		for (int r = 0; r < 15; ++r) {
			s.ka[r] = vreinterpretq_u8_m128i(rd_key[2 * r]);
			s.kb[r] = vreinterpretq_u8_m128i(rd_key[2 * r + 1]);
		}
		const uint8_t *perm = Dec ? r256_dec_perm : r256_perm;
		s.p0 = vld1q_u8(perm);
		s.p1 = vld1q_u8(perm + 16);
		return s;
	}
	static vec_t load(const block *p) {
		const uint8_t *q = reinterpret_cast<const uint8_t *>(p);
		return vec_t{{vld1q_u8(q), vld1q_u8(q + 16)}};
	}
	static void store(block *p, vec_t x) {
		uint8_t *q = reinterpret_cast<uint8_t *>(p);
		vst1q_u8(q, x.val[0]);
		vst1q_u8(q + 16, x.val[1]);
	}
	// AESE(P(x), P(K_r)) then AESMC is round r + 1 up to its key addition, so
	// r = 0..12 are full rounds, round 14 is AESE alone, and K_14 closes.
	// Decryption is the same with AESD and AESIMC.
	template<bool Dec, int M>
	__attribute__((always_inline)) static void crypt_n(vec_t (&x)[M], const keys_t &k) {
		for (int r = 0; r < 13; ++r)
			for (int t = 0; t < M; ++t) {
				const uint8x16_t a = vqtbl2q_u8(x[t], k.p0), b = vqtbl2q_u8(x[t], k.p1);
				if constexpr (Dec) {
					x[t].val[0] = vaesimcq_u8(vaesdq_u8(a, k.ka[r]));
					x[t].val[1] = vaesimcq_u8(vaesdq_u8(b, k.kb[r]));
				} else {
					x[t].val[0] = vaesmcq_u8(vaeseq_u8(a, k.ka[r]));
					x[t].val[1] = vaesmcq_u8(vaeseq_u8(b, k.kb[r]));
				}
			}
		for (int t = 0; t < M; ++t) {
			const uint8x16_t a = vqtbl2q_u8(x[t], k.p0), b = vqtbl2q_u8(x[t], k.p1);
			if constexpr (Dec) {
				x[t].val[0] = veorq_u8(vaesdq_u8(a, k.ka[13]), k.ka[14]);
				x[t].val[1] = veorq_u8(vaesdq_u8(b, k.kb[13]), k.kb[14]);
			} else {
				x[t].val[0] = veorq_u8(vaeseq_u8(a, k.ka[13]), k.ka[14]);
				x[t].val[1] = veorq_u8(vaeseq_u8(b, k.kb[13]), k.kb[14]);
			}
		}
	}
};

#endif

// W blocks at p (2W `block`s) through one unrolled kernel: W / B whole vectors
// and, for an odd W on a two-block lane, one vector holding the last block.
template<class L, bool Dec, int W>
EMP_R256_TARGET_ATTR inline void r256_tile(block *p, const typename L::keys_t &k) {
	constexpr int full = W / L::B;
	constexpr bool part = W % L::B != 0;
	typename L::vec_t x[full + part];
	for (int t = 0; t < full; ++t) x[t] = L::load(p + 2 * L::B * t);
	if constexpr (part) x[full] = L::load_one(p + 2 * L::B * full);
	L::template crypt_n<Dec, full + part>(x, k);
	for (int t = 0; t < full; ++t) L::store(p + 2 * L::B * t, x[t]);
	if constexpr (part) L::store_one(p + 2 * L::B * full, x[full]);
}

// W blocks as whole tiles of L::max_tile and one tile of the rest.
template<class L, bool Dec, int W>
EMP_R256_TARGET_ATTR inline void r256_tiles(block *p, const typename L::keys_t &k) {
	constexpr int T = L::max_tile;
	for (int i = 0; i < W / T; ++i) r256_tile<L, Dec, T>(p + (size_t)2 * T * i, k);
	if constexpr (W % T != 0) r256_tile<L, Dec, W % T>(p + (size_t)2 * T * (W / T), k);
}

// Tile op for drain_tiles: run<W>() encrypts or decrypts the next W blocks.
template<bool Dec>
struct R256TileOp {
	block *p;
	const R256Lane::keys_t *k;
	template<int W> EMP_R256_TARGET_ATTR void run() {
		r256_tiles<R256Lane, Dec, W>(p, *k);
		p += 2 * W;
	}
};

template<bool Dec, int N>
EMP_R256_TARGET_ATTR inline void r256_ecb(block *blks, const block *rd_key) {
	if constexpr (N > 0) {
		using L = R256Lane;
		const L::keys_t k = L::template keys<Dec>(rd_key);
		r256_tiles<L, Dec, N>(blks, k);
	}
}

template<bool Dec>
EMP_R256_TARGET_ATTR inline void r256_ecb(block *blks, int64_t n, const block *rd_key) {
	if (n == 0) return;
	using L = R256Lane;
	const L::keys_t k = L::template keys<Dec>(rd_key);
	R256TileOp<Dec> op{blks, &k};
	drain_tiles(op, n);
}

// The 15 round keys as 30 halves, round key r = (k[2r], k[2r + 1]). Odd
// halves add SubWord of the previous half's last word, even halves RotWord +
// SubWord + Rcon; SubWord runs as aesenclast on that word broadcast to all four
// columns, where ShiftRows is the identity (as in AES_opt_key_schedule). The
// two halves each step reads stay in locals, so no step waits on reading back
// what the one before stored.
EMP_R256_TARGET_ATTR inline void r256_expand_key(const block *userkey, block *k) {
	block prev2 = _mm_loadu_si128((const __m128i *)userkey);         // half i - 2
	block prev1 = _mm_loadu_si128((const __m128i *)(userkey + 1));   // half i - 1
	k[0] = prev2;
	k[1] = prev1;
	const block rot_word = _mm_set1_epi32(0x0c0f0e0d), last_word = _mm_set1_epi32(0x0f0e0d0c);
	int rcon = 1;
	for (int i = 2; i < 30; ++i) {
		block t;
		if (i % 2 == 0) {
			t = _mm_aesenclast_si128(_mm_shuffle_epi8(prev1, rot_word), _mm_set1_epi32(rcon));
			rcon = ((rcon << 1) ^ ((rcon & 0x80) ? 0x1b : 0)) & 0xff;
		} else {
			t = _mm_aesenclast_si128(_mm_shuffle_epi8(prev1, last_word), _mm_setzero_si128());
		}
		block w = prev2;  // prefix XOR of its words: w_j ^= w_0 ^ .. ^ w_{j-1}
		w = _mm_xor_si128(w, _mm_slli_si128(w, 4));
		w = _mm_xor_si128(w, _mm_slli_si128(w, 8));
		w = _mm_xor_si128(w, t);
		k[i] = w;
		prev2 = prev1;
		prev1 = w;
	}
}

#if defined(__aarch64__)
// The aarch64 kernels' key layout: every round key but the last permuted.
inline void r256_permute_round_keys(block *k, const uint8_t *perm) {
	const uint8x16_t p0 = vld1q_u8(perm), p1 = vld1q_u8(perm + 16);
	for (int r = 0; r < 14; ++r) {
		const uint8x16x2_t kr = {{vreinterpretq_u8_m128i(k[2 * r]), vreinterpretq_u8_m128i(k[2 * r + 1])}};
		k[2 * r] = vreinterpretq_m128i_u8(vqtbl2q_u8(kr, p0));
		k[2 * r + 1] = vreinterpretq_m128i_u8(vqtbl2q_u8(kr, p1));
	}
}
#endif

}  // namespace detail

EMP_R256_TARGET_ATTR inline void R256_set_encrypt_key(const block *userkey, R256_KEY *key) {
	detail::r256_expand_key(userkey, key->rd_key);
#if defined(__aarch64__)
	detail::r256_permute_round_keys(key->rd_key, detail::r256_perm);
#endif
}

// Decryption round j uses K_{14-j}, through InvMixColumns for j = 1..13.
EMP_R256_TARGET_ATTR inline void R256_set_decrypt_key(const block *userkey, R256_DEC_KEY *key) {
	block e[30];
	detail::r256_expand_key(userkey, e);
	block *d = key->rd_key;
	for (int j = 0; j < 15; ++j)
		for (int h = 0; h < 2; ++h) {
			const block kh = e[2 * (14 - j) + h];
			d[2 * j + h] = (j == 0 || j == 14) ? kh : _mm_aesimc_si128(kh);
		}
#if defined(__aarch64__)
	detail::r256_permute_round_keys(d, detail::r256_dec_perm);
#endif
}

template<int N>
EMP_R256_TARGET_ATTR inline void R256_ecb_encrypt_blks(block *blks, const R256_KEY *key) {
	static_assert(N >= 0, "R256_ecb_encrypt_blks: block count must be non-negative");
	detail::r256_ecb<false, N>(blks, key->rd_key);
}

EMP_R256_TARGET_ATTR inline void R256_ecb_encrypt_blks(block *blks, int64_t n, const R256_KEY *key) {
	expecting(n >= 0, "R256_ecb_encrypt_blks: negative block count");
	expecting(n <= INT64_MAX / 32, "R256_ecb_encrypt_blks: block count overflows int64_t bytes");
	detail::r256_ecb<false>(blks, n, key->rd_key);
}

template<int N>
EMP_R256_TARGET_ATTR inline void R256_ecb_decrypt_blks(block *blks, const R256_DEC_KEY *key) {
	static_assert(N >= 0, "R256_ecb_decrypt_blks: block count must be non-negative");
	detail::r256_ecb<true, N>(blks, key->rd_key);
}

EMP_R256_TARGET_ATTR inline void R256_ecb_decrypt_blks(block *blks, int64_t n, const R256_DEC_KEY *key) {
	expecting(n >= 0, "R256_ecb_decrypt_blks: negative block count");
	expecting(n <= INT64_MAX / 32, "R256_ecb_decrypt_blks: block count overflows int64_t bytes");
	detail::r256_ecb<true>(blks, n, key->rd_key);
}

}  // namespace emp
#endif
