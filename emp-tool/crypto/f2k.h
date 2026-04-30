#ifndef EMP_F2K_H__
#define EMP_F2K_H__
#include "emp-tool/core/block.h"

namespace emp {

// On x86_64 with VPCLMULQDQ+AVX-512, run carry-less multiply 4-wide on zmm.
// Current AVX-512 implementations deliver ~2× the lanes-per-cycle of
// scalar PCLMULQDQ, so inner-product hot paths see roughly a 2× speedup.
#if defined(__x86_64__) && defined(__VPCLMULQDQ__) && defined(__AVX512F__)
	#define EMP_F2K_TARGET_ATTR __attribute__((target("vpclmulqdq,avx512f,sse2,pclmul")))
	#define EMP_F2K_HAS_VPCLMUL512 1
#elif defined(__x86_64__)
	#define EMP_F2K_TARGET_ATTR __attribute__((target("sse2,pclmul")))
	#define EMP_F2K_HAS_VPCLMUL512 0
#else
	#define EMP_F2K_TARGET_ATTR
	#define EMP_F2K_HAS_VPCLMUL512 0
#endif
/* multiplication in galois field without reduction */
#ifdef __x86_64__
__attribute__((target("sse2,pclmul")))
inline void mul128(__m128i a, __m128i b, __m128i *res1, __m128i *res2) {
	__m128i tmp3, tmp4, tmp5, tmp6;
	tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
	tmp4 = _mm_clmulepi64_si128(a, b, 0x10);
	tmp5 = _mm_clmulepi64_si128(a, b, 0x01);
	tmp6 = _mm_clmulepi64_si128(a, b, 0x11);

	tmp4 = _mm_xor_si128(tmp4, tmp5);
	tmp5 = _mm_slli_si128(tmp4, 8);
	tmp4 = _mm_srli_si128(tmp4, 8);
	tmp3 = _mm_xor_si128(tmp3, tmp5);
	tmp6 = _mm_xor_si128(tmp6, tmp4);
	// initial mul now in tmp3, tmp6
	*res1 = tmp3;
	*res2 = tmp6;
}
#elif __aarch64__
inline void mul128(__m128i a, __m128i b, __m128i *res1, __m128i *res2) {
	__m128i tmp3, tmp4, tmp5, tmp6;
	poly64_t a_lo = (poly64_t)vget_low_u64(vreinterpretq_u64_m128i(a));
	poly64_t a_hi = (poly64_t)vget_high_u64(vreinterpretq_u64_m128i(a));
	poly64_t b_lo = (poly64_t)vget_low_u64(vreinterpretq_u64_m128i(b));
	poly64_t b_hi = (poly64_t)vget_high_u64(vreinterpretq_u64_m128i(b));
	tmp3 = (__m128i)vmull_p64(a_lo, b_lo);
	tmp4 = (__m128i)vmull_p64(a_hi, b_lo);
	tmp5 = (__m128i)vmull_p64(a_lo, b_hi);
	tmp6 = (__m128i)vmull_p64(a_hi, b_hi);

	tmp4 = _mm_xor_si128(tmp4, tmp5);
	tmp5 = _mm_slli_si128(tmp4, 8);
	tmp4 = _mm_srli_si128(tmp4, 8);
	tmp3 = _mm_xor_si128(tmp3, tmp5);
	tmp6 = _mm_xor_si128(tmp6, tmp4);
	// initial mul now in tmp3, tmp6
	*res1 = tmp3;
	*res2 = tmp6;
}
#endif

/* galois field reduction with reflection I/O*/
#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
//https://www.intel.com/content/dam/develop/public/us/en/documents/carry-less-multiplication-instruction.pdf figure 5
inline block reduce_reflect(__m128i tmp3, __m128i tmp6) {//3 is low, 6 is high
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

/* galois field reduction without reflection*/
#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
//https://www.intel.com/content/dam/develop/public/us/en/documents/carry-less-multiplication-instruction.pdf figure 7
inline block reduce(__m128i tmp3, __m128i tmp6) {//3 is low, 6 is high
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


/* inner product of two galois field vectors without reduction */
EMP_F2K_TARGET_ATTR
inline void vector_inn_prdt_sum_no_red(block *res, const block *a, const block *b, int sz) {
#if EMP_F2K_HAS_VPCLMUL512
	// VPCLMULQDQ-512: 4-lane carry-less multiply per zmm. We accumulate
	// the unreduced 256-bit product into per-lane (lo, mid, hi) zmms and
	// fold lanes at the end. mid = a_hi*b_lo XOR a_lo*b_hi, applied into
	// lo/hi as a 64-bit shift inside each 128-bit lane.
	int i = 0;
	__m512i lo_z  = _mm512_setzero_si512();
	__m512i hi_z  = _mm512_setzero_si512();
	__m512i mid_z = _mm512_setzero_si512();
	for (; i + 4 <= sz; i += 4) {
		__m512i av = _mm512_loadu_si512((const __m512i *)(a + i));
		__m512i bv = _mm512_loadu_si512((const __m512i *)(b + i));
		__m512i p00 = _mm512_clmulepi64_epi128(av, bv, 0x00);
		__m512i p11 = _mm512_clmulepi64_epi128(av, bv, 0x11);
		__m512i p10 = _mm512_clmulepi64_epi128(av, bv, 0x10);
		__m512i p01 = _mm512_clmulepi64_epi128(av, bv, 0x01);
		lo_z  = _mm512_xor_si512(lo_z,  p00);
		hi_z  = _mm512_xor_si512(hi_z,  p11);
		mid_z = _mm512_xor_si512(mid_z, _mm512_xor_si512(p10, p01));
	}
	// Apply mid into lo/hi (per 128-bit lane: shift by 64 bits = 8 bytes).
	lo_z = _mm512_xor_si512(lo_z, _mm512_bslli_epi128(mid_z, 8));
	hi_z = _mm512_xor_si512(hi_z, _mm512_bsrli_epi128(mid_z, 8));
	// Fold 4 lanes: zmm -> ymm -> xmm via halving XOR-reductions.
	__m256i lo_y = _mm256_xor_si256(_mm512_castsi512_si256(lo_z),
	                                _mm512_extracti64x4_epi64(lo_z, 1));
	__m256i hi_y = _mm256_xor_si256(_mm512_castsi512_si256(hi_z),
	                                _mm512_extracti64x4_epi64(hi_z, 1));
	block lo = _mm_xor_si128(_mm256_castsi256_si128(lo_y),
	                         _mm256_extracti128_si256(lo_y, 1));
	block hi = _mm_xor_si128(_mm256_castsi256_si128(hi_y),
	                         _mm256_extracti128_si256(hi_y, 1));
	// Tail (sz % 4 leftover, < 4 elements).
	block lo_t, hi_t;
	for (; i < sz; ++i) {
		mul128(a[i], b[i], &lo_t, &hi_t);
		lo = lo ^ lo_t;
		hi = hi ^ hi_t;
	}
	res[0] = lo;
	res[1] = hi;
#else
	// 4-way accumulator unroll: lo halves into a0..a3, hi halves into b0..b3.
	block a0 = zero_block, a1 = zero_block, a2 = zero_block, a3 = zero_block;
	block b0 = zero_block, b1 = zero_block, b2 = zero_block, b3 = zero_block;
	block lo, hi;
	int i = 0;
	for (; i + 4 <= sz; i += 4) {
		mul128(a[i],   b[i],   &lo, &hi); a0 = a0 ^ lo; b0 = b0 ^ hi;
		mul128(a[i+1], b[i+1], &lo, &hi); a1 = a1 ^ lo; b1 = b1 ^ hi;
		mul128(a[i+2], b[i+2], &lo, &hi); a2 = a2 ^ lo; b2 = b2 ^ hi;
		mul128(a[i+3], b[i+3], &lo, &hi); a3 = a3 ^ lo; b3 = b3 ^ hi;
	}
	for (; i < sz; ++i) {
		mul128(a[i], b[i], &lo, &hi);
		a0 = a0 ^ lo;
		b0 = b0 ^ hi;
	}
	res[0] = (a0 ^ a1) ^ (a2 ^ a3);
	res[1] = (b0 ^ b1) ^ (b2 ^ b3);
#endif
}

/* inner product of two galois field vectors without reduction */
template<int N>
inline void vector_inn_prdt_sum_no_red(block *res, const block *a, const block *b) {
	vector_inn_prdt_sum_no_red(res, a, b, N);
}

/* inner product of two galois field vectors with reduction */
EMP_F2K_TARGET_ATTR
inline void vector_inn_prdt_sum_red(block *res, const block *a, const block *b, int sz) {
	// Defer reduction: sum unreduced 256-bit products, then reduce once.
	// GF(2^128) addition is XOR (linear), so reduce(Σ unred(a_i*b_i)) ==
	// Σ reduce(a_i*b_i). One reduce instead of sz — ~2x on M-series.
	block lo_hi[2];
	vector_inn_prdt_sum_no_red(lo_hi, a, b, sz);
	*res = reduce(lo_hi[0], lo_hi[1]);
}

/* inner product of two galois field vectors with reduction */
template<int N>
inline void vector_inn_prdt_sum_red(block *res, block const *a, const block *b) {
	vector_inn_prdt_sum_red(res, a, b, N);
}

/* inner product Σ a[i] * b[i] in GF(2^128) where b[i] ∈ {0, 1}.
 * Each contribution is either 0 or a[i], so no carryless multiply is needed —
 * a branchless mask + XOR per element, with a 4-way unroll to break the
 * accumulator chain. Branchless via select_mask[b[i]] keeps performance
 * stable for adversarial / random b distributions. */
inline void vector_inn_prdt_sum_red(block *res, const block *a, const bool *b, int sz) {
	block r0 = zero_block, r1 = zero_block, r2 = zero_block, r3 = zero_block;
	int i = 0;
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

/* coefficients of almost universal hash function */
inline void uni_hash_coeff_gen(block* coeff, block seed, int sz) {
	assert(sz > 0);
	// Handle the case with small `sz`
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

	// Computing the rest with a batch of 4
	int i = 4;
	for(; i < sz - 3; i += 4) {
		gfmul(coeff[i - 4], multiplier, &coeff[i]);
		gfmul(coeff[i - 3], multiplier, &coeff[i + 1]);
		gfmul(coeff[i - 2], multiplier, &coeff[i + 2]);
		gfmul(coeff[i - 1], multiplier, &coeff[i + 3]);
	}

	// Cleaning up with the rest
	int remainder = sz % 4;
	if(remainder != 0) {
		i = sz - remainder;
		for(; i < sz; ++i)
			gfmul(coeff[i - 1], seed, &coeff[i]);
	}
}

/* coefficients of almost universal hash function */
template<int N>
inline void uni_hash_coeff_gen(block* coeff, block seed) {
	uni_hash_coeff_gen(coeff, seed, N);	
}

/* packing in Galois field (v[i] * X^i for v of size 128) */
namespace gf_pack_detail {
	// For one byte offset, gather the bit-shifted contributions of 8 inputs
	// at i = BYTE_OFF*8 + 0..7 into a 128-bit running sum + 1-byte overflow.
	// Byte-shift happens once per byte_off in the caller.
	template <int BYTE_OFF>
	__attribute__((always_inline))
	static inline void process_byte(const block *data, block &lo, block &hi) {
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
}  // namespace gf_pack_detail

class GaloisFieldPacking {
	public:
		// Computes res = Σ data[i] * X^i in GF(2^128).
		// Specialized: each base element X^i is a single-bit polynomial, so
		// data[i] * X^i is just data[i] left-shifted by i positions; we
		// accumulate a 256-bit running sum via shifts + XOR and reduce once.
		// Avoids the 128 PCLMULs of a generic vector_inn_prdt_sum_red.
		#ifdef __x86_64__
		__attribute__((target("sse2,pclmul")))
		#endif
		void packing(block *res, const block *data) {
			block lo = zero_block, hi = zero_block;
			gf_pack_detail::process_byte< 0>(data, lo, hi);
			gf_pack_detail::process_byte< 1>(data, lo, hi);
			gf_pack_detail::process_byte< 2>(data, lo, hi);
			gf_pack_detail::process_byte< 3>(data, lo, hi);
			gf_pack_detail::process_byte< 4>(data, lo, hi);
			gf_pack_detail::process_byte< 5>(data, lo, hi);
			gf_pack_detail::process_byte< 6>(data, lo, hi);
			gf_pack_detail::process_byte< 7>(data, lo, hi);
			gf_pack_detail::process_byte< 8>(data, lo, hi);
			gf_pack_detail::process_byte< 9>(data, lo, hi);
			gf_pack_detail::process_byte<10>(data, lo, hi);
			gf_pack_detail::process_byte<11>(data, lo, hi);
			gf_pack_detail::process_byte<12>(data, lo, hi);
			gf_pack_detail::process_byte<13>(data, lo, hi);
			gf_pack_detail::process_byte<14>(data, lo, hi);
			gf_pack_detail::process_byte<15>(data, lo, hi);
			*res = reduce(lo, hi);
		}

		// Same operation when each input v[i] is a bool (0 or 1): X^i is the
		// single-bit polynomial 1 << i, so Σ v[i] * X^i is just the block
		// whose bit i is set iff v[i]=1 — i.e., the LSB-first bit-packing
		// of the 128-bool vector. No GF arithmetic needed.
		void packing(block *res, const bool *data) {
			bools_to_bits(res, data, 128);
		}
};

/* XOR of all elements in a vector */
inline void vector_self_xor(block *sum, block *data, int sz) {
	block res[4];
	res[0] = zero_block;
	res[1] = zero_block;
	res[2] = zero_block;
	res[3] = zero_block;
	for(int i = 0; i < (sz/4)*4; i+=4) {
		res[0] = data[i] ^ res[0];
		res[1] = data[i+1] ^ res[1];
		res[2] = data[i+2] ^ res[2];
		res[3] = data[i+3] ^ res[3];
	}
	for(int i = (sz/4)*4, j = 0; i < sz; ++i, ++j)
		res[j] = data[i] ^ res[j];
	res[0] = res[0] ^ res[1];
	res[2] = res[2] ^ res[3];
	*sum = res[0] ^ res[2];
}

/* XOR of all elements in a vector */
template<int N>
inline void vector_self_xor(block *sum, block *data) {
	vector_self_xor(sum, data, N);
}
}
#endif
