#ifndef EMP_UTIL_BLOCK_HPP__
#define EMP_UTIL_BLOCK_HPP__

// Inline definitions for block.h's API. Included via block.h.

#include <assert.h>
#include <cstring>
#include <iostream>
#include <iomanip>

namespace emp {

inline bool getLSB(const block & x) {
	return (x[0] & 1) == 1;
}

#ifdef __x86_64__
__attribute__((target("sse2")))
inline block makeBlock(uint64_t high, uint64_t low) {
	return _mm_set_epi64x(high, low);
}
#elif __aarch64__
inline block makeBlock(uint64_t high, uint64_t low) {
	return (block)vcombine_u64((uint64x1_t)low, (uint64x1_t)high);
}
#endif

/* Linear orthomorphism function
 * [REF] Implementation of "Efficient and Secure Multiparty Computation from Fixed-Key Block Ciphers"
 * https://eprint.iacr.org/2019/074.pdf
 */
#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
inline block sigma(block a) {
	return _mm_shuffle_epi32(a, 78) ^ (a & makeBlock(0xFFFFFFFFFFFFFFFF, 0x00));
}

inline const block zero_block = makeBlock(0, 0);
inline const block all_one_block = makeBlock(0xFFFFFFFFFFFFFFFF,0xFFFFFFFFFFFFFFFF);
inline const block select_mask[2] = {zero_block, all_one_block};

inline block set_bit(const block & a, int i) {
	assert(i >= 0 && i < 128);
	if(i < 64)
		return makeBlock(0L, 1ULL<<i) | a;
	else
		return makeBlock(1ULL<<(i-64), 0) | a;
}

inline std::ostream& operator<<(std::ostream& out, const block& blk) {
	const auto saved_flags = out.flags();
	const auto saved_fill = out.fill();
	uint64_t* data = (uint64_t*)&blk;
	out << std::hex << std::setfill('0')
	    << std::setw(16) << data[1] << ' '
	    << std::setw(16) << data[0];
	out.flags(saved_flags);
	out.fill(saved_fill);
	return out;
}

inline void xorBlocks_arr(block* __restrict__ res, const block* __restrict__ x, const block* __restrict__ y, int nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;) {
		*(res++) = *(x++) ^ *(y++);
	}
}

inline void xorBlocksTo_arr(block* __restrict__ dst, const block* __restrict__ src, int nblocks) {
	const block * dest = nblocks+src;
	for (; src != dest;) {
		*dst = *dst ^ *(src++);
		++dst;
	}
}

inline void xorBlocks_arr(block* __restrict__ res, const block* __restrict__ x, block y, int nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;)
		*(res++) =  *(x++) ^ y;
}

#ifdef __x86_64__
__attribute__((target("sse4")))
#endif
inline bool cmpBlock(const block * x, const block * y, int nblocks) {
	__m128i acc = _mm_setzero_si128();
	const block * dest = nblocks+x;
	for (; x != dest;)
		acc = _mm_or_si128(acc, _mm_xor_si128(*(x++), *(y++)));
	return _mm_testz_si128(acc, acc);
}

// Modified from
// https://mischasan.wordpress.com/2011/10/03/the-full-sse2-bit-matrix-transpose-routine/
#define INP(x, y) inp[(x)*ncols / 8 + (y) / 8]
#define OUT(x, y) out[(y)*nrows / 8 + (x) / 8]

#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
static inline void sse_trans_16x16_byte(__m128i *m) {
  __m128i t[16];
  for (int i = 0; i < 8; ++i) {
    t[2*i]   = _mm_unpacklo_epi8(m[2*i], m[2*i+1]);
    t[2*i+1] = _mm_unpackhi_epi8(m[2*i], m[2*i+1]);
  }
  __m128i u[16];
  for (int i = 0; i < 4; ++i) {
    u[4*i]   = _mm_unpacklo_epi16(t[4*i],   t[4*i+2]);
    u[4*i+1] = _mm_unpackhi_epi16(t[4*i],   t[4*i+2]);
    u[4*i+2] = _mm_unpacklo_epi16(t[4*i+1], t[4*i+3]);
    u[4*i+3] = _mm_unpackhi_epi16(t[4*i+1], t[4*i+3]);
  }
  __m128i v[16];
  for (int i = 0; i < 2; ++i) {
    for (int k = 0; k < 4; ++k) {
      v[8*i + 2*k]     = _mm_unpacklo_epi32(u[8*i + k], u[8*i + k + 4]);
      v[8*i + 2*k + 1] = _mm_unpackhi_epi32(u[8*i + k], u[8*i + k + 4]);
    }
  }
  for (int k = 0; k < 8; ++k) {
    m[2*k]   = _mm_unpacklo_epi64(v[k], v[k+8]);
    m[2*k+1] = _mm_unpackhi_epi64(v[k], v[k+8]);
  }
}

#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
inline void sse_trans(uint8_t *out, uint8_t const *inp, uint64_t nrows,
               uint64_t ncols) {
  uint64_t rr, cc;
  int i, h;
  union {
    __m128i x;
    uint8_t b[16];
  } tmp;
  __m128i vec;
  assert(nrows % 8 == 0 && ncols % 8 == 0);

  // Fast path for nrows%16==0, ncols%128==0: 16-row in-register byte
  // transpose, avoiding the 16-way `_mm_set_epi8(INP(...))` byte-gather
  // the generic loop below would use.
  if (nrows % 16 == 0 && ncols % 128 == 0) {
    const uint64_t bpr_in  = ncols / 8;
    const uint64_t bpr_out = nrows / 8;
    for (rr = 0; rr < nrows; rr += 16) {
      for (uint64_t col_byte = 0; col_byte < bpr_in; col_byte += 16) {
        __m128i m[16];
        for (int r = 0; r < 16; ++r)
          m[r] = _mm_loadu_si128(
              (const __m128i *)(inp + (rr + r) * bpr_in + col_byte));
        sse_trans_16x16_byte(m);
        for (int j = 0; j < 16; ++j) {
          __m128i x = m[j];
          uint64_t cc8 = (col_byte + j) * 8;
          for (int b = 7; b >= 0; --b) {
            *(uint16_t *)(out + (cc8 + b) * bpr_out + rr / 8) =
                (uint16_t)_mm_movemask_epi8(x);
            x = _mm_slli_epi64(x, 1);
          }
        }
      }
    }
    return;
  }

  // Do the main body in 16x8 blocks:
  for (rr = 0; rr + 16 <= nrows; rr += 16) {
    for (cc = 0; cc < ncols; cc += 8) {
      vec = _mm_set_epi8(INP(rr + 15, cc), INP(rr + 14, cc), INP(rr + 13, cc),
                         INP(rr + 12, cc), INP(rr + 11, cc), INP(rr + 10, cc),
                         INP(rr + 9, cc), INP(rr + 8, cc), INP(rr + 7, cc),
                         INP(rr + 6, cc), INP(rr + 5, cc), INP(rr + 4, cc),
                         INP(rr + 3, cc), INP(rr + 2, cc), INP(rr + 1, cc),
                         INP(rr + 0, cc));
      for (i = 8; --i >= 0; vec = _mm_slli_epi64(vec, 1))
        *(uint16_t *)&OUT(rr, cc + i) = _mm_movemask_epi8(vec);
    }
  }
  if (rr == nrows)
    return;

  // Remainder: 8x(16n+8) bits (n may be 0), processed as pairs of 8x8.
  // The non-multiple-of-16 branch uses a scalar variant because the
  // 16-wide _mm_set_epi16 pattern below requires aligned 16-row strips.
  if ((ncols % 8 == 0 && ncols % 16 != 0) ||
      (nrows % 8 == 0 && nrows % 16 != 0)) {
    for (cc = 0; cc + 16 <= ncols; cc += 16) {
      for (i = 0; i < 8; ++i) {
        tmp.b[i] = h = *(uint16_t const *)&INP(rr + i, cc);
        tmp.b[i + 8] = h >> 8;
      }
      for (i = 8; --i >= 0; tmp.x = _mm_slli_epi64(tmp.x, 1)) {
        OUT(rr, cc + i) = h = _mm_movemask_epi8(tmp.x);
        OUT(rr, cc + i + 8) = h >> 8;
      }
    }
  } else {
    for (cc = 0; cc + 16 <= ncols; cc += 16) {
      vec = _mm_set_epi16(*(uint16_t const *)&INP(rr + 7, cc),
                          *(uint16_t const *)&INP(rr + 6, cc),
                          *(uint16_t const *)&INP(rr + 5, cc),
                          *(uint16_t const *)&INP(rr + 4, cc),
                          *(uint16_t const *)&INP(rr + 3, cc),
                          *(uint16_t const *)&INP(rr + 2, cc),
                          *(uint16_t const *)&INP(rr + 1, cc),
                          *(uint16_t const *)&INP(rr + 0, cc));
      for (i = 8; --i >= 0; vec = _mm_slli_epi64(vec, 1)) {
        OUT(rr, cc + i) = h = _mm_movemask_epi8(vec);
        OUT(rr, cc + i + 8) = h >> 8;
      }
    }
  }
  if (cc == ncols)
    return;

  //  Do the remaining 8x8 block:
  for (i = 0; i < 8; ++i)
    tmp.b[i] = INP(rr + i, cc);
  for (i = 8; --i >= 0; tmp.x = _mm_slli_epi64(tmp.x, 1))
    OUT(rr, cc + i) = _mm_movemask_epi8(tmp.x);
}
#undef INP
#undef OUT

// Specialization of sse_trans for nrows=128. Same algorithm as the generic
// sse_trans's nrows%16==0 && ncols%128==0 fast path, but with nrows=128
// constant-folded so the outer 8-iteration "rr" loop unrolls and bpr_out=16
// becomes a constant. On non-x86 the function dispatches into the generic
// sse_trans.
#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
inline void sse_trans_n128(uint8_t *out, const uint8_t *inp, uint64_t ncols) {
#ifndef __x86_64__
  sse_trans(out, inp, /*nrows=*/128, ncols);
#else
  assert((ncols % 128) == 0);
  constexpr uint64_t bpr_out = 16;          // nrows/8 with nrows=128
  const uint64_t bpr_in = ncols / 8;

  // Outer "rr" loop unrolled at compile time: 8 iterations at
  // rr = 0, 16, ..., 112. The per-strip output sub-offset rr/8 becomes
  // a compile-time constant 0, 2, 4, ..., 14 inside each unrolled body.
  #pragma GCC unroll 8
  for (int rr_idx = 0; rr_idx < 8; ++rr_idx) {
    const uint64_t rr = (uint64_t)rr_idx * 16;
    for (uint64_t col_byte = 0; col_byte < bpr_in; col_byte += 16) {
      __m128i m[16];
      for (int r = 0; r < 16; ++r) {
        m[r] = _mm_loadu_si128(
            (const __m128i *)(inp + (rr + r) * bpr_in + col_byte));
      }
      sse_trans_16x16_byte(m);
      for (int j = 0; j < 16; ++j) {
        __m128i x = m[j];
        const uint64_t cc8 = (col_byte + j) * 8;
        for (int b = 7; b >= 0; --b) {
          *(uint16_t *)(out + (cc8 + b) * bpr_out + (size_t)rr_idx * 2) =
              (uint16_t)_mm_movemask_epi8(x);
          x = _mm_slli_epi64(x, 1);
        }
      }
    }
  }
#endif
}

}  // namespace emp
#endif  // EMP_UTIL_BLOCK_HPP__
