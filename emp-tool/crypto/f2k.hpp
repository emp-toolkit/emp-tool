#ifndef EMP_F2K_HPP__
#define EMP_F2K_HPP__

// Inline definitions for f2k.h's API. Included via f2k.h; do not include
// directly. ClmulLane<N> for N ∈ {1, 2, 4} lives in simd_tier.h; only
// specializations the build can emit are defined, so the VPCLMUL
// guards inside vector_inn_prdt_sum_no_red compile out the unavailable
// tiers. ClmulLane<1> is always present on both x86_64 (PCLMUL) and
// aarch64 (NEON poly64), so it serves as the universal tail drain.

#include "emp-tool/core/simd_tier.h"
#include "emp-tool/core/utils.h"

namespace emp {

EMP_F2K_TARGET_ATTR
inline void mul128(block a, block b, block *res1, block *res2) {
	using L = detail::ClmulLane<1>;
	block p00 = L::clmul_lo_lo(a, b);
	block p11 = L::clmul_hi_hi(a, b);
	block p10 = L::clmul_hi_lo(a, b);
	block p01 = L::clmul_lo_hi(a, b);
	block mid = L::xorv(p10, p01);
	*res1 = L::xorv(p00, L::bslli_8(mid));
	*res2 = L::xorv(p11, L::bsrli_8(mid));
}

#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
inline block reduce_reflect(__m128i tmp3, __m128i tmp6) {
	__m128i tmp2, tmp4, tmp5, tmp7, tmp8, tmp9;
	tmp7 = _mm_srli_epi32(tmp3, 31);
	tmp8 = _mm_srli_epi32(tmp6, 31);
	tmp3 = _mm_slli_epi32(tmp3, 1);
	tmp6 = _mm_slli_epi32(tmp6, 1);

	tmp9 = _mm_srli_si128(tmp7, 12);
	tmp8 = _mm_slli_si128(tmp8, 4);
	tmp7 = _mm_slli_si128(tmp7, 4);
	tmp3 = _mm_or_si128(tmp3, tmp7);
	tmp6 = _mm_or_si128(tmp6, tmp8);
	tmp6 = _mm_or_si128(tmp6, tmp9);

	tmp7 = _mm_slli_epi32(tmp3, 31);
	tmp8 = _mm_slli_epi32(tmp3, 30);
	tmp9 = _mm_slli_epi32(tmp3, 25);
	tmp7 = _mm_xor_si128(tmp7, tmp8);
	tmp7 = _mm_xor_si128(tmp7, tmp9);
	tmp8 = _mm_srli_si128(tmp7, 4);
	tmp7 = _mm_slli_si128(tmp7, 12);
	tmp3 = _mm_xor_si128(tmp3, tmp7);

	tmp2 = _mm_srli_epi32(tmp3, 1);
	tmp4 = _mm_srli_epi32(tmp3, 2);
	tmp5 = _mm_srli_epi32(tmp3, 7);
	tmp2 = _mm_xor_si128(tmp2, tmp4);
	tmp2 = _mm_xor_si128(tmp2, tmp5);
	tmp2 = _mm_xor_si128(tmp2, tmp8);
	tmp3 = _mm_xor_si128(tmp3, tmp2);
	return _mm_xor_si128(tmp6, tmp3);
}

#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
inline block reduce(__m128i tmp3, __m128i tmp6) {
	__m128i tmp7, tmp8, tmp9, tmp10, tmp11, tmp12;
	__m128i XMMMASK = _mm_setr_epi32(0xffffffff, 0x0, 0x0, 0x0);
	tmp7 = _mm_srli_epi32(tmp6, 31);
	tmp8 = _mm_srli_epi32(tmp6, 30);
	tmp9 = _mm_srli_epi32(tmp6, 25);

	tmp7 = _mm_xor_si128(tmp7, tmp8);
	tmp7 = _mm_xor_si128(tmp7, tmp9);

	tmp8 = _mm_shuffle_epi32(tmp7, 147);

	tmp7 = _mm_and_si128(XMMMASK, tmp8);
	tmp8 = _mm_andnot_si128(XMMMASK, tmp8);
	tmp3 = _mm_xor_si128(tmp3, tmp8);
	tmp6 = _mm_xor_si128(tmp6, tmp7);

	tmp10 = _mm_slli_epi32(tmp6, 1);
	tmp3 = _mm_xor_si128(tmp3, tmp10);
	tmp11 = _mm_slli_epi32(tmp6, 2);
	tmp3 = _mm_xor_si128(tmp3, tmp11);
	tmp12 = _mm_slli_epi32(tmp6, 7);
	tmp3 = _mm_xor_si128(tmp3, tmp12);
	return _mm_xor_si128(tmp3, tmp6);
}

#ifdef __x86_64__
__attribute__((target("sse2,pclmul")))
#endif
inline void gfmul (__m128i a, __m128i b, __m128i *res) {
	block r1, r2;
	mul128(a, b, &r1, &r2);
	*res = reduce(r1, r2);
}

#ifdef __x86_64__
__attribute__((target("sse2,pclmul")))
#endif
inline void gfmul_reflect (__m128i a, __m128i b, __m128i *res) {
	block r1, r2;
	mul128(a, b, &r1, &r2);
	*res = reduce_reflect(r1, r2);
}

namespace detail {

// Drain L::N-block chunks from [i, sz), accumulating the unreduced
// 256-bit product into lo/hi. mid = a_hi*b_lo XOR a_lo*b_hi is folded
// into lo/hi via a 64-bit shift inside each 128-bit lane, then the
// lane-wide accumulator is folded down to a scalar xmm via halving
// XOR-reductions. ClmulLane<1> is always available, so the chain
// always terminates with a complete drain.
template <class L>
EMP_F2K_TARGET_ATTR
static inline void inn_prdt_drain_lanes(block &lo, block &hi,
                                          const block *a, const block *b,
                                          int64_t &i, int64_t sz) {
	using V = typename L::vec_t;
	V lo_v = L::zero(), hi_v = L::zero(), mid_v = L::zero();
	for (; i + L::N <= sz; i += L::N) {
		V av  = L::loadu(a + i);
		V bv  = L::loadu(b + i);
		V p00 = L::clmul_lo_lo(av, bv);
		V p11 = L::clmul_hi_hi(av, bv);
		V p10 = L::clmul_hi_lo(av, bv);
		V p01 = L::clmul_lo_hi(av, bv);
		lo_v  = L::xorv(lo_v,  p00);
		hi_v  = L::xorv(hi_v,  p11);
		mid_v = L::xor3(mid_v, p10, p01);
	}
	lo_v = L::xorv(lo_v, L::bslli_8(mid_v));
	hi_v = L::xorv(hi_v, L::bsrli_8(mid_v));
	lo = lo ^ L::fold_to_xmm(lo_v);
	hi = hi ^ L::fold_to_xmm(hi_v);
}

// One byte_off of the GF packing: gather bit-shifted contributions of
// 8 inputs at i = BYTE_OFF*8 + 0..7 into the 128-bit running sum +
// 1-byte overflow. Byte-shift happens once per byte_off in the caller.
template <int BYTE_OFF>
__attribute__((always_inline))
static inline void gf_pack_byte(const block *data, block &lo, block &hi) {
	block combined_lo = data[BYTE_OFF * 8 + 0];  // b=0: no bit shift
	block combined_hi = zero_block;
	#define EMP_GF_PACK_BIT(B) do {                              \
		block d = data[BYTE_OFF * 8 + (B)];                      \
		block s = _mm_slli_epi64(d, (B));                        \
		block c = _mm_srli_epi64(d, 64 - (B));                   \
		combined_lo ^= s ^ _mm_slli_si128(c, 8);                 \
		combined_hi ^= _mm_srli_si128(c, 8);                     \
	} while (0)
	EMP_GF_PACK_BIT(1); EMP_GF_PACK_BIT(2); EMP_GF_PACK_BIT(3);
	EMP_GF_PACK_BIT(4); EMP_GF_PACK_BIT(5); EMP_GF_PACK_BIT(6);
	EMP_GF_PACK_BIT(7);
	#undef EMP_GF_PACK_BIT

	if (BYTE_OFF == 0) {
		lo ^= combined_lo;
		hi ^= combined_hi;
	} else {
		lo ^= _mm_slli_si128(combined_lo, BYTE_OFF);
		hi ^= _mm_srli_si128(combined_lo, 16 - BYTE_OFF)
		    ^ _mm_slli_si128(combined_hi, BYTE_OFF);
	}
}

}  // namespace detail

EMP_F2K_TARGET_ATTR
inline void vector_inn_prdt_sum_no_red(block *res, const block *a, const block *b, int64_t sz) {
	block lo = zero_block, hi = zero_block;
	int64_t i = 0;
#if EMP_HAS_VPCLMUL512
	detail::inn_prdt_drain_lanes<detail::ClmulLane<4>>(lo, hi, a, b, i, sz);
#endif
#if EMP_HAS_VPCLMUL256
	detail::inn_prdt_drain_lanes<detail::ClmulLane<2>>(lo, hi, a, b, i, sz);
#endif
	// Universal tail (also the whole stream on aarch64 / no-VPCLMUL x86).
	detail::inn_prdt_drain_lanes<detail::ClmulLane<1>>(lo, hi, a, b, i, sz);
	res[0] = lo;
	res[1] = hi;
}

template<int N>
inline void vector_inn_prdt_sum_no_red(block *res, const block *a, const block *b) {
	vector_inn_prdt_sum_no_red(res, a, b, N);
}

EMP_F2K_TARGET_ATTR
inline void vector_inn_prdt_sum_red(block *res, const block *a, const block *b, int64_t sz) {
	// Defer reduction: sum unreduced 256-bit products, then reduce once.
	// GF(2^128) addition is XOR (linear), so reduce(Σ unred(a_i*b_i)) ==
	// Σ reduce(a_i*b_i). One reduce instead of sz.
	block lo_hi[2];
	vector_inn_prdt_sum_no_red(lo_hi, a, b, sz);
	*res = reduce(lo_hi[0], lo_hi[1]);
}

template<int N>
inline void vector_inn_prdt_sum_red(block *res, block const *a, const block *b) {
	vector_inn_prdt_sum_red(res, a, b, N);
}

inline void vector_inn_prdt_sum_red(block *res, const block *a, const bool *b, int64_t sz) {
	block r0 = zero_block, r1 = zero_block, r2 = zero_block, r3 = zero_block;
	int64_t i = 0;
	for (; i + 4 <= sz; i += 4) {
		r0 = r0 ^ (a[i  ] & select_mask[b[i  ]]);
		r1 = r1 ^ (a[i+1] & select_mask[b[i+1]]);
		r2 = r2 ^ (a[i+2] & select_mask[b[i+2]]);
		r3 = r3 ^ (a[i+3] & select_mask[b[i+3]]);
	}
	for (; i < sz; ++i)
		r0 = r0 ^ (a[i] & select_mask[b[i]]);
	*res = (r0 ^ r1) ^ (r2 ^ r3);
}

template<int N>
inline void vector_inn_prdt_sum_red(block *res, const block *a, const bool *b) {
	vector_inn_prdt_sum_red(res, a, b, N);
}

inline void uni_hash_coeff_gen(block* coeff, block seed, int64_t sz) {
	assert(sz > 0);
	coeff[0] = seed;
	if(sz == 1) return;

	gfmul(seed, seed, &coeff[1]);
	if(sz == 2) return;

	gfmul(coeff[1], seed, &coeff[2]);
	if(sz == 3) return;

	block multiplier;
	gfmul(coeff[2], seed, &multiplier);
	coeff[3] = multiplier;
	if(sz == 4) return;

	// Compute the rest in batches of 4.
	int64_t i = 4;
	for(; i < sz - 3; i += 4) {
		gfmul(coeff[i - 4], multiplier, &coeff[i]);
		gfmul(coeff[i - 3], multiplier, &coeff[i + 1]);
		gfmul(coeff[i - 2], multiplier, &coeff[i + 2]);
		gfmul(coeff[i - 1], multiplier, &coeff[i + 3]);
	}

	int64_t remainder = sz % 4;
	if(remainder != 0) {
		i = sz - remainder;
		for(; i < sz; ++i)
			gfmul(coeff[i - 1], seed, &coeff[i]);
	}
}

template<int N>
inline void uni_hash_coeff_gen(block* coeff, block seed) {
	uni_hash_coeff_gen(coeff, seed, N);
}

#ifdef __x86_64__
__attribute__((target("sse2,pclmul")))
#endif
inline void GaloisFieldPacking::packing(block *res, const block *data) {
	block lo = zero_block, hi = zero_block;
	detail::gf_pack_byte< 0>(data, lo, hi);
	detail::gf_pack_byte< 1>(data, lo, hi);
	detail::gf_pack_byte< 2>(data, lo, hi);
	detail::gf_pack_byte< 3>(data, lo, hi);
	detail::gf_pack_byte< 4>(data, lo, hi);
	detail::gf_pack_byte< 5>(data, lo, hi);
	detail::gf_pack_byte< 6>(data, lo, hi);
	detail::gf_pack_byte< 7>(data, lo, hi);
	detail::gf_pack_byte< 8>(data, lo, hi);
	detail::gf_pack_byte< 9>(data, lo, hi);
	detail::gf_pack_byte<10>(data, lo, hi);
	detail::gf_pack_byte<11>(data, lo, hi);
	detail::gf_pack_byte<12>(data, lo, hi);
	detail::gf_pack_byte<13>(data, lo, hi);
	detail::gf_pack_byte<14>(data, lo, hi);
	detail::gf_pack_byte<15>(data, lo, hi);
	*res = reduce(lo, hi);
}

inline void GaloisFieldPacking::packing(block *res, const bool *data) {
	bools_to_bits(res, data, 128);
}

inline void vector_self_xor(block *sum, block *data, int64_t sz) {
	block res[4];
	res[0] = zero_block;
	res[1] = zero_block;
	res[2] = zero_block;
	res[3] = zero_block;
	for(int64_t i = 0; i < (sz/4)*4; i+=4) {
		res[0] = data[i] ^ res[0];
		res[1] = data[i+1] ^ res[1];
		res[2] = data[i+2] ^ res[2];
		res[3] = data[i+3] ^ res[3];
	}
	for(int64_t i = (sz/4)*4, j = 0; i < sz; ++i, ++j)
		res[j] = data[i] ^ res[j];
	res[0] = res[0] ^ res[1];
	res[2] = res[2] ^ res[3];
	*sum = res[0] ^ res[2];
}

template<int N>
inline void vector_self_xor(block *sum, block *data) {
	vector_self_xor(sum, data, N);
}

}  // namespace emp
#endif
