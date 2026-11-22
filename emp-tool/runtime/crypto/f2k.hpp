#ifndef EMP_F2K_HPP__
#define EMP_F2K_HPP__

// Inline definitions for f2k.h's API. Included via f2k.h; do not include
// directly. ClmulLane<N> for N ∈ {1, 2, 4} lives in simd_tier.h; only
// specializations the build can emit are defined, so the VPCLMUL
// guards inside vector_inn_prdt_sum_no_red and vector_inn_prdt_sum_red_x4
// compile out the unavailable tiers. ClmulLane<1> is always present on both x86_64 (PCLMUL) and
// aarch64 (NEON poly64), so it serves as the universal tail drain.

#include "emp-tool/runtime/core/simd_tier.h"
#include "emp-tool/runtime/core/utils.h"

#include <utility>

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
// XOR-reductions. With fewer than L::N blocks left the tier returns at
// once: its loop would not run, so zeroing and folding its accumulators
// would only add zero. ClmulLane<1> is always available, so the chain
// always terminates with a complete drain.
template <class L>
EMP_F2K_TARGET_ATTR
static inline void inn_prdt_drain_lanes(block &lo, block &hi,
                                          const block *a, const block *b,
                                          int64_t &i, int64_t sz) {
	if (sz - i < L::N) return;
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

}  // namespace detail

EMP_F2K_TARGET_ATTR
inline void vector_inn_prdt_sum_no_red(block *res, const block *a, const block *b, int64_t sz) {
	expecting(sz >= 0, "vector_inn_prdt_sum_no_red: negative size");
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
	static_assert(N >= 0, "vector_inn_prdt_sum_no_red: size must be non-negative");
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
	static_assert(N >= 0, "vector_inn_prdt_sum_red: size must be non-negative");
	vector_inn_prdt_sum_red(res, a, b, N);
}

namespace detail {

// reduce() on every 128-bit lane: each lane holds an unreduced 256-bit value
// (lo, hi), and the result is lo + hi·x^128 mod x^128 + x^7 + x^2 + x + 1.
// With g = x^7 + x^2 + x + 1 ≡ x^128 and hi = h0 + h1·x^64,
//   hi·x^128 ≡ h0·g + t1·x^64, with t1 = h1·g of at most 71 bits.
// The low 64 bits of t1 land below x^128; its top 7 bits wrap once more as
// (t1 >> 64)·g, of at most 14 bits. The remainder is unique, so this agrees
// with reduce() bit for bit. Only the same-half selectors appear, since they
// mean the same thing on every target.
template <class L>
EMP_F2K_TARGET_ATTR
static inline typename L::vec_t reduce_lanes(typename L::vec_t lo,
                                             typename L::vec_t hi) {
	using V = typename L::vec_t;
	const V g  = L::broadcast(makeBlock(0x87, 0x87));
	const V t0 = L::clmul_lo_lo(hi, g);   // h0·g
	const V t1 = L::clmul_hi_hi(hi, g);   // h1·g
	const V t2 = L::clmul_hi_hi(t1, g);   // (t1 >> 64)·g
	return L::xorv(L::xor3(lo, t0, L::bslli_8(t1)), t2);
}

// Four n-term inner products laid out term-major (see
// vector_inn_prdt_sum_red_x4). Lane j of the accumulators belongs to sum j,
// so 4 / L::N vectors carry the four sums and no lanes are folded together.
template <class L>
EMP_F2K_TARGET_ATTR
static inline void inn_prdt_x4(block *res, const block *a, const block *b,
                               int64_t n) {
	using V = typename L::vec_t;
	constexpr int kVecs = 4 / L::N;
	V lo[kVecs], hi[kVecs], mid[kVecs];
	for (int v = 0; v < kVecs; ++v) lo[v] = hi[v] = mid[v] = L::zero();
	for (int64_t l = 0; l < n; ++l) {
		for (int v = 0; v < kVecs; ++v) {
			const V av = L::loadu(a + 4 * l + v * L::N);
			const V bv = L::loadu(b + 4 * l + v * L::N);
			lo[v]  = L::xorv(lo[v], L::clmul_lo_lo(av, bv));
			hi[v]  = L::xorv(hi[v], L::clmul_hi_hi(av, bv));
			mid[v] = L::xor3(mid[v], L::clmul_hi_lo(av, bv), L::clmul_lo_hi(av, bv));
		}
	}
	for (int v = 0; v < kVecs; ++v) {
		const V lo2 = L::xorv(lo[v], L::bslli_8(mid[v]));
		const V hi2 = L::xorv(hi[v], L::bsrli_8(mid[v]));
		L::storeu(res + v * L::N, reduce_lanes<L>(lo2, hi2));
	}
}

}  // namespace detail

EMP_F2K_TARGET_ATTR
inline void vector_inn_prdt_sum_red_x4(block *res, const block *a, const block *b, int64_t n) {
	expecting(n >= 0, "vector_inn_prdt_sum_red_x4: negative size");
#if EMP_HAS_VPCLMUL512
	detail::inn_prdt_x4<detail::ClmulLane<4>>(res, a, b, n);
#elif EMP_HAS_VPCLMUL256
	detail::inn_prdt_x4<detail::ClmulLane<2>>(res, a, b, n);
#else
	detail::inn_prdt_x4<detail::ClmulLane<1>>(res, a, b, n);
#endif
}

template<int N>
inline void vector_inn_prdt_sum_red_x4(block *res, const block *a, const block *b) {
	static_assert(N >= 0, "vector_inn_prdt_sum_red_x4: size must be non-negative");
	vector_inn_prdt_sum_red_x4(res, a, b, N);
}

namespace detail {

template <typename T>
inline void vector_inn_prdt_sum_red_bits(block *res, const block *a, const T *b,
                                         int64_t sz) {
	expecting(sz >= 0, "vector_inn_prdt_sum_red: negative size");
	block r0 = zero_block, r1 = zero_block, r2 = zero_block, r3 = zero_block;
	int64_t i = 0;
	for (; i + 4 <= sz; i += 4) {
		r0 = r0 ^ (a[i  ] & select_mask[b[i  ] != 0]);
		r1 = r1 ^ (a[i+1] & select_mask[b[i+1] != 0]);
		r2 = r2 ^ (a[i+2] & select_mask[b[i+2] != 0]);
		r3 = r3 ^ (a[i+3] & select_mask[b[i+3] != 0]);
	}
	for (; i < sz; ++i)
		r0 = r0 ^ (a[i] & select_mask[b[i] != 0]);
	*res = (r0 ^ r1) ^ (r2 ^ r3);
}

}  // namespace detail

inline void vector_inn_prdt_sum_red(block *res, const block *a, const bool *b,
                                    int64_t sz) {
	detail::vector_inn_prdt_sum_red_bits(res, a, b, sz);
}

template<int N>
inline void vector_inn_prdt_sum_red(block *res, const block *a, const bool *b) {
	static_assert(N >= 0, "vector_inn_prdt_sum_red: size must be non-negative");
	vector_inn_prdt_sum_red(res, a, b, N);
}

template<typename T>
requires std::is_same_v<T, uint8_t>
inline void vector_inn_prdt_sum_red(block *res, const block *a, const T *b,
                                    int64_t sz) {
	detail::vector_inn_prdt_sum_red_bits(res, a, b, sz);
}

template<int N, typename T>
requires std::is_same_v<T, uint8_t>
inline void vector_inn_prdt_sum_red(block *res, const block *a, const T *b) {
	vector_inn_prdt_sum_red(res, a, b, N);
}

namespace detail {

// a^2 in GF(2^128). The two cross terms of a·a are equal and cancel in
// characteristic 2, so the unreduced square is (a_lo^2, a_hi^2): two
// carry-less multiplies instead of mul128's four, then gfmul's reduce().
EMP_F2K_TARGET_ATTR
static inline block gf_square(block a) {
	using L = ClmulLane<1>;
	return reduce(L::clmul_lo_lo(a, a), L::clmul_hi_hi(a, a));
}

}  // namespace detail

// coeff[i] = seed^(i+1). Past the first three, the pair seed^k = coeff[k-1]
// and seed^(k+1) = coeff[k] gives the next two powers:
//   coeff[2k-1] = seed^(2k)   = (seed^k)^2
//   coeff[2k]   = seed^(2k+1) = seed^k · seed^(k+1)
// For k >= 2 both reads lie below both writes (k < 2k-1), in slots filled
// before this step, and every slot is written once. An odd sz ends on a full
// step; an even sz leaves coeff[sz-1] = seed^sz, the square of
// coeff[sz/2 - 1]. Every power is a reduced field element, so the bits do
// not depend on the order of the products.
EMP_F2K_TARGET_ATTR
inline void uni_hash_coeff_gen(block* coeff, block seed, int64_t sz) {
	expecting(sz > 0, "uni_hash_coeff_gen: size must be positive");
	coeff[0] = seed;
	if (sz == 1) return;
	coeff[1] = detail::gf_square(seed);
	if (sz == 2) return;
	gfmul(seed, coeff[1], &coeff[2]);
	for (int64_t k = 2; 2 * k < sz; ++k) {
		const block a = coeff[k - 1], b = coeff[k];
		coeff[2 * k - 1] = detail::gf_square(a);
		gfmul(a, b, &coeff[2 * k]);
	}
	if (sz % 2 == 0)
		coeff[sz - 1] = detail::gf_square(coeff[sz / 2 - 1]);
}

template<int N>
inline void uni_hash_coeff_gen(block* coeff, block seed) {
	static_assert(N > 0, "uni_hash_coeff_gen: size must be positive");
	uni_hash_coeff_gen(coeff, seed, N);
}

// GaloisFieldPacking::packing(block*): res = Σ_{i<128} data[i]·X^i in
// GF(2^128). Write i = 8k + b, with byte offset k < 16 and bit offset b < 8:
//   S_b = Σ_k data[8k+b]·X^(8k),    Σ_i data[i]·X^i = Σ_b S_b·X^b.
// Each term of S_b is a whole-byte shift, and S_b has degree < 248, so it is
// a 256-bit (lo, hi) pair built from byte shuffles alone. Bits move only in
// the seven S_b·X^b with b > 0, each carrying across 64-bit words. Every tier
// computes this unreduced sum (degree < 255) exactly, so all of them hand the
// same (lo, hi) to the one reduce() in packing().
#if EMP_HAS_AVX512BW
  #define EMP_GF_PACK_TARGET_ATTR __attribute__((target("avx512bw,avx512f,avx2,sse2,pclmul")))
#elif EMP_HAS_AVX2
  #define EMP_GF_PACK_TARGET_ATTR __attribute__((target("avx2,sse2,pclmul")))
#elif defined(__x86_64__)
  #define EMP_GF_PACK_TARGET_ATTR __attribute__((target("ssse3,sse2,pclmul")))
#else
  #define EMP_GF_PACK_TARGET_ATTR
#endif

namespace detail {

#ifdef __x86_64__
// x86 tiers: S_b by Horner's rule from the top byte offset down,
// S_b <- S_b·X^8 + data[8k+b], each step one byte shift, one alignr and one
// XOR. GfPackLane<N> carries N chains per vector (1 = xmm with SSSE3,
// 2 = ymm with AVX2, 4 = zmm with AVX-512BW), one per 128-bit lane, and each
// byte shuffle stays inside its lane. shl_acc<B> adds the chains' pairs to
// the running sum (tlo, thi), lane j shifted by B + j bits; u and v are the
// top bits of each 64-bit word, which move into the word above. S_b has no
// bits at degree 248 or higher, so no bit leaves the pair.
template <int N> struct GfPackLane;

template <>
struct GfPackLane<1> {
	static constexpr int N = 1;
	using vec_t = __m128i;
	EMP_GF_PACK_TARGET_ATTR static vec_t zero()                        { return _mm_setzero_si128(); }
	EMP_GF_PACK_TARGET_ATTR static vec_t loadu(const block *p)         { return _mm_loadu_si128((const __m128i *)p); }
	EMP_GF_PACK_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)        { return _mm_xor_si128(a, b); }
	// The pair (lo, hi) times X^8: both halves move up one byte, and lo's
	// top byte enters hi from below.
	EMP_GF_PACK_TARGET_ATTR static vec_t x8_lo(vec_t lo)               { return _mm_slli_si128(lo, 1); }
	EMP_GF_PACK_TARGET_ATTR static vec_t x8_hi(vec_t lo, vec_t hi)     { return _mm_alignr_epi8(hi, lo, 15); }
	template <int B>
	EMP_GF_PACK_TARGET_ATTR static void shl_acc(vec_t lo, vec_t hi, vec_t &tlo, vec_t &thi) {
		if constexpr (B == 0) {
			tlo = _mm_xor_si128(tlo, lo);
			thi = _mm_xor_si128(thi, hi);
		} else {
			const vec_t u = _mm_srli_epi64(lo, 64 - B), v = _mm_srli_epi64(hi, 64 - B);
			tlo = _mm_xor_si128(tlo, _mm_xor_si128(_mm_slli_epi64(lo, B), _mm_slli_si128(u, 8)));
			thi = _mm_xor_si128(thi, _mm_xor_si128(_mm_slli_epi64(hi, B), _mm_alignr_epi8(v, u, 8)));
		}
	}
	EMP_GF_PACK_TARGET_ATTR static block fold_to_xmm(vec_t v)          { return v; }
};

#if EMP_HAS_AVX2
template <>
struct GfPackLane<2> {
	static constexpr int N = 2;
	using vec_t = __m256i;
	EMP_GF_PACK_TARGET_ATTR static vec_t zero()                        { return _mm256_setzero_si256(); }
	EMP_GF_PACK_TARGET_ATTR static vec_t loadu(const block *p)         { return _mm256_loadu_si256((const __m256i *)p); }
	EMP_GF_PACK_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)        { return _mm256_xor_si256(a, b); }
	EMP_GF_PACK_TARGET_ATTR static vec_t x8_lo(vec_t lo)               { return _mm256_bslli_epi128(lo, 1); }
	EMP_GF_PACK_TARGET_ATTR static vec_t x8_hi(vec_t lo, vec_t hi)     { return _mm256_alignr_epi8(hi, lo, 15); }
	// Per-word shift counts. A count of 64 (lane 0 of chain 0) makes
	// vpsrlvq return 0, so that lane carries nothing.
	template <int B>
	EMP_GF_PACK_TARGET_ATTR static void shl_acc(vec_t lo, vec_t hi, vec_t &tlo, vec_t &thi) {
		const vec_t c = _mm256_set_epi64x(B + 1, B + 1, B, B);
		const vec_t r = _mm256_set_epi64x(63 - B, 63 - B, 64 - B, 64 - B);
		const vec_t u = _mm256_srlv_epi64(lo, r), v = _mm256_srlv_epi64(hi, r);
		tlo = _mm256_xor_si256(tlo, _mm256_xor_si256(_mm256_sllv_epi64(lo, c), _mm256_bslli_epi128(u, 8)));
		thi = _mm256_xor_si256(thi, _mm256_xor_si256(_mm256_sllv_epi64(hi, c), _mm256_alignr_epi8(v, u, 8)));
	}
	EMP_GF_PACK_TARGET_ATTR static block fold_to_xmm(vec_t v) {
		return _mm_xor_si128(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
	}
};
#endif

#if EMP_HAS_AVX512BW
template <>
struct GfPackLane<4> {
	static constexpr int N = 4;
	using vec_t = __m512i;
	EMP_GF_PACK_TARGET_ATTR static vec_t zero()                        { return _mm512_setzero_si512(); }
	EMP_GF_PACK_TARGET_ATTR static vec_t loadu(const block *p)         { return _mm512_loadu_si512((const void *)p); }
	EMP_GF_PACK_TARGET_ATTR static vec_t xorv(vec_t a, vec_t b)        { return _mm512_xor_si512(a, b); }
	EMP_GF_PACK_TARGET_ATTR static vec_t x8_lo(vec_t lo)               { return _mm512_bslli_epi128(lo, 1); }
	EMP_GF_PACK_TARGET_ATTR static vec_t x8_hi(vec_t lo, vec_t hi)     { return _mm512_alignr_epi8(hi, lo, 15); }
	// As GfPackLane<2>; vpternlogq with truth table 0x96 is a three-way XOR.
	template <int B>
	EMP_GF_PACK_TARGET_ATTR static void shl_acc(vec_t lo, vec_t hi, vec_t &tlo, vec_t &thi) {
		const vec_t c = _mm512_set_epi64(B + 3, B + 3, B + 2, B + 2, B + 1, B + 1, B, B);
		const vec_t r = _mm512_set_epi64(61 - B, 61 - B, 62 - B, 62 - B,
		                                 63 - B, 63 - B, 64 - B, 64 - B);
		const vec_t u = _mm512_srlv_epi64(lo, r), v = _mm512_srlv_epi64(hi, r);
		tlo = _mm512_ternarylogic_epi64(tlo, _mm512_sllv_epi64(lo, c), _mm512_bslli_epi128(u, 8), 0x96);
		thi = _mm512_ternarylogic_epi64(thi, _mm512_sllv_epi64(hi, c), _mm512_alignr_epi8(v, u, 8), 0x96);
	}
	EMP_GF_PACK_TARGET_ATTR static block fold_to_xmm(vec_t v) {
		const __m256i y = _mm256_xor_si256(_mm512_castsi512_si256(v), _mm512_extracti64x4_epi64(v, 1));
		return _mm_xor_si128(_mm256_castsi256_si128(y), _mm256_extracti128_si256(y, 1));
	}
};
#endif

// One pass over chains m = Base .. Base + sizeof...(C) - 1: lane j of chain m
// builds S_b for b = m·N + j, from its k = 15 element down.
template <class L, int Base, int... C>
EMP_GF_PACK_TARGET_ATTR
static inline void gf_pack_pass(const block *data, typename L::vec_t &tlo,
                                typename L::vec_t &thi,
                                std::integer_sequence<int, C...>) {
	using V = typename L::vec_t;
	constexpr int kChains = sizeof...(C);
	V lo[kChains], hi[kChains];
	for (int c = 0; c < kChains; ++c) {
		lo[c] = L::loadu(data + 8 * 15 + (Base + c) * L::N);
		hi[c] = L::zero();
	}
	for (int k = 14; k >= 0; --k)
		for (int c = 0; c < kChains; ++c) {
			hi[c] = L::x8_hi(lo[c], hi[c]);
			lo[c] = L::xorv(L::x8_lo(lo[c]), L::loadu(data + 8 * k + (Base + c) * L::N));
		}
	(L::template shl_acc<(Base + C) * L::N>(lo[C], hi[C], tlo, thi), ...);
}

// The 8 / N chains run in passes of at most four, so the eight accumulators
// of a pass stay in registers next to its loads (16 xmm registers without
// AVX-512).
template <class L>
EMP_GF_PACK_TARGET_ATTR
static inline void gf_pack_lanes(const block *data, block &lo, block &hi) {
	constexpr int kChains = 8 / L::N, kPass = kChains < 4 ? kChains : 4;
	static_assert(kChains <= 2 * kPass, "gf_pack_lanes runs at most two passes");
	typename L::vec_t tlo = L::zero(), thi = L::zero();
	gf_pack_pass<L, 0>(data, tlo, thi, std::make_integer_sequence<int, kPass>{});
	if constexpr (kChains > kPass)
		gf_pack_pass<L, kPass>(data, tlo, thi, std::make_integer_sequence<int, kPass>{});
	lo = L::fold_to_xmm(tlo);
	hi = L::fold_to_xmm(thi);
}
#endif  // __x86_64__

#ifdef __aarch64__
// NEON tier: no Horner chain. Element 8k+b enters S_b directly as
// data[8k+b]·X^(8k), whose low and high halves are two byte extracts, and the
// elements of byte offsets k and k+1 fold together before reaching the
// accumulators, so the 128 elements form independent short chains.
// EOR3 needs the SHA3 extension; without it the three-way XOR is two EORs.
static inline uint8x16_t gf_pack_neon_xor3(uint8x16_t a, uint8x16_t b, uint8x16_t c) {
#if defined(__ARM_FEATURE_SHA3)
	return veor3q_u8(a, b, c);
#else
	return veorq_u8(a, veorq_u8(b, c));
#endif
}
template <int K>
static inline uint8x16_t gf_pack_neon_lo(uint8x16_t d) {   // low half of d·X^(8K)
	if constexpr (K == 0) return d;
	else return vextq_u8(vdupq_n_u8(0), d, 16 - K);
}
template <int K>
static inline uint8x16_t gf_pack_neon_hi(uint8x16_t d) {   // high half of d·X^(8K)
	static_assert(K > 0, "d·X^0 has no high half");
	return vextq_u8(d, vdupq_n_u8(0), 16 - K);
}
template <int K>
static inline void gf_pack_neon_pair(const block *data, uint8x16_t lo[8], uint8x16_t hi[8]) {
	for (int b = 0; b < 8; ++b) {
		const uint8x16_t d0 = vreinterpretq_u8_s64(data[8 * K + b]);
		const uint8x16_t d1 = vreinterpretq_u8_s64(data[8 * (K + 1) + b]);
		lo[b] = gf_pack_neon_xor3(lo[b], gf_pack_neon_lo<K>(d0), gf_pack_neon_lo<K + 1>(d1));
		if constexpr (K == 0)
			hi[b] = veorq_u8(hi[b], gf_pack_neon_hi<1>(d1));
		else
			hi[b] = gf_pack_neon_xor3(hi[b], gf_pack_neon_hi<K>(d0), gf_pack_neon_hi<K + 1>(d1));
	}
}
// (tlo, thi) += (lo, hi)·X^B for 0 < B < 8, as GfPackLane<1>::shl_acc.
template <int B>
static inline void gf_pack_neon_shl_acc(uint8x16_t lo8, uint8x16_t hi8,
                                        uint64x2_t &tlo, uint64x2_t &thi) {
	const uint64x2_t lo = vreinterpretq_u64_u8(lo8), hi = vreinterpretq_u64_u8(hi8);
	const uint64x2_t u = vshrq_n_u64(lo, 64 - B), v = vshrq_n_u64(hi, 64 - B);
	tlo = veorq_u64(tlo, veorq_u64(vshlq_n_u64(lo, B), vextq_u64(vdupq_n_u64(0), u, 1)));
	thi = veorq_u64(thi, veorq_u64(vshlq_n_u64(hi, B), vextq_u64(u, v, 1)));
}
static inline void gf_pack_neon(const block *data, block &lo_out, block &hi_out) {
	uint8x16_t lo[8], hi[8];
	for (int b = 0; b < 8; ++b) lo[b] = hi[b] = vdupq_n_u8(0);
	gf_pack_neon_pair< 0>(data, lo, hi);
	gf_pack_neon_pair< 2>(data, lo, hi);
	gf_pack_neon_pair< 4>(data, lo, hi);
	gf_pack_neon_pair< 6>(data, lo, hi);
	gf_pack_neon_pair< 8>(data, lo, hi);
	gf_pack_neon_pair<10>(data, lo, hi);
	gf_pack_neon_pair<12>(data, lo, hi);
	gf_pack_neon_pair<14>(data, lo, hi);
	uint64x2_t tlo = vreinterpretq_u64_u8(lo[0]), thi = vreinterpretq_u64_u8(hi[0]);
	gf_pack_neon_shl_acc<1>(lo[1], hi[1], tlo, thi);
	gf_pack_neon_shl_acc<2>(lo[2], hi[2], tlo, thi);
	gf_pack_neon_shl_acc<3>(lo[3], hi[3], tlo, thi);
	gf_pack_neon_shl_acc<4>(lo[4], hi[4], tlo, thi);
	gf_pack_neon_shl_acc<5>(lo[5], hi[5], tlo, thi);
	gf_pack_neon_shl_acc<6>(lo[6], hi[6], tlo, thi);
	gf_pack_neon_shl_acc<7>(lo[7], hi[7], tlo, thi);
	lo_out = vreinterpretq_s64_u64(tlo);
	hi_out = vreinterpretq_s64_u64(thi);
}
#endif  // __aarch64__

}  // namespace detail

EMP_GF_PACK_TARGET_ATTR
inline void GaloisFieldPacking::packing(block *res, const block *data) {
	block lo, hi;
#if EMP_HAS_AVX512BW
	detail::gf_pack_lanes<detail::GfPackLane<4>>(data, lo, hi);
#elif EMP_HAS_AVX2
	detail::gf_pack_lanes<detail::GfPackLane<2>>(data, lo, hi);
#elif defined(__x86_64__)
	detail::gf_pack_lanes<detail::GfPackLane<1>>(data, lo, hi);
#else
	detail::gf_pack_neon(data, lo, hi);
#endif
	*res = reduce(lo, hi);
}
#undef EMP_GF_PACK_TARGET_ATTR

inline void GaloisFieldPacking::packing(block *res, const bool *data) {
	bools_to_bits(res, data, 128);
}

template<typename T>
requires std::is_same_v<T, uint8_t>
inline void GaloisFieldPacking::packing(block *res, const T *data) {
	bools_to_bits(res, data, 128);
}

inline void vector_self_xor(block *sum, block *data, int64_t sz) {
	expecting(sz >= 0, "vector_self_xor: negative size");
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
	static_assert(N >= 0, "vector_self_xor: size must be non-negative");
	vector_self_xor(sum, data, N);
}

}  // namespace emp
#endif
