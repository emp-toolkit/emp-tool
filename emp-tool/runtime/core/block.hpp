#ifndef EMP_UTIL_BLOCK_HPP__
#define EMP_UTIL_BLOCK_HPP__

// Inline definitions for block.h's API. Included via block.h.

#include <assert.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>

#include "emp-tool/runtime/core/simd_tier.h"   // EMP_HAS_AVX2 / AVX512BW (block.hpp's sse_trans_n128 picks the widest available)

namespace emp {

inline bool getLSB(const block & x) {
	return (x[0] & 1) == 1;
}

// Bit i of x as a bool (bit 0 is getLSB). Generic accessor; any protocol meaning
// pinned to a specific bit is documented at the use site, not here.
inline bool getBit(const block & x, int i) {
	return i < 64 ? ((x[0] >> i) & 1) == 1
	              : ((x[1] >> (i - 64)) & 1) == 1;
}

// `block` is a 128-bit vector type whose 64-bit-lane aggregate-init
// is constant-evaluable on both x86 (`__m128i`) and aarch64 (sse2neon
// typedef). `{(long long)low, (long long)high}` produces the same
// byte pattern as `_mm_set_epi64x(high, low)` / `vcombine_u64(low,
// high)` — low in lane 0, high in lane 1 — and the compiler lowers
// the runtime form to the same hardware ops. constexpr lets
// file-scope `inline constexpr block` constants compile-time-evaluate
// into .rodata; see docs/static_init.md for why that matters.
inline constexpr block makeBlock(uint64_t high, uint64_t low) {
	return block{(long long)low, (long long)high};
}

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

inline constexpr block zero_block    = makeBlock(0, 0);
inline constexpr block all_one_block = makeBlock(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
inline constexpr block select_mask[2] = {zero_block, all_one_block};

// Blocks with a single low bit set (only bit 0 / only bit 1) — for isolating or
// flipping a low bit. Generic constants; a protocol that reserves a specific low
// bit (e.g. for a MAC/Δ encoding) documents that convention at its own use site.
inline constexpr block bit0_mask = makeBlock(0, 1);
inline constexpr block bit1_mask = makeBlock(0, 2);

inline block set_bit(const block & a, int i) {
	assert(i >= 0 && i < 128);
	if(i < 64)
		return makeBlock(0L, 1ULL<<i) | a;
	else
		return makeBlock(1ULL<<(i-64), 0) | a;
}

inline std::string to_hex(const void* data, size_t n) {
	static const char digits[] = "0123456789abcdef";
	const unsigned char* b = static_cast<const unsigned char*>(data);
	std::string s(2 * n, '0');
	for (size_t i = 0; i < n; ++i) {
		s[2 * i]     = digits[b[i] >> 4];
		s[2 * i + 1] = digits[b[i] & 0xf];
	}
	return s;
}

// A block renders as its 16 bytes in memory order — matching how reference
// vectors / digests / the wire-trace dumps are written.
inline std::ostream& operator<<(std::ostream& out, const block& blk) {
	return out << to_hex(&blk, sizeof(block));
}

inline void xorBlocks_arr(block* __restrict__ res, const block* __restrict__ x, const block* __restrict__ y, int64_t nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;) {
		*(res++) = *(x++) ^ *(y++);
	}
}

inline void xorBlocksTo_arr(block* __restrict__ dst, const block* __restrict__ src, int64_t nblocks) {
	const block * dest = nblocks+src;
	for (; src != dest;) {
		*dst = *dst ^ *(src++);
		++dst;
	}
}

inline void xorBlocks_arr(block* __restrict__ res, const block* __restrict__ x, block y, int64_t nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;)
		*(res++) =  *(x++) ^ y;
}

#ifdef __x86_64__
__attribute__((target("sse4")))
#endif
inline bool cmpBlock(const block * x, const block * y, int64_t nblocks) {
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
// becomes a constant. On x86 a runtime/compile-time tier dispatch picks the
// widest available kernel (AVX-512BW > AVX2 > SSE2); on non-x86 the function
// dispatches into the generic sse_trans.
//
// Wide variants process N consecutive 128-bit "col_blocks" (each a 16-byte
// column-slice of the output) per tile: SSE2 N=1, AVX2 N=2, AVX-512BW N=4.
// The same byte-unpack sequence transposes N independent 16x16 byte tiles
// in parallel within the wider register (unpack instructions act within
// 128-bit lanes), then one wider movepi8_mask extracts N uint16 result rows
// per shift. Any (bpr_in_blocks % N) tail col_blocks are handled by the
// scalar SSE2 inner body so all ncols%128==0 are supported.

namespace detail {

// One (rr, col_block) tile via the SSE2 inner body, writing into `out_b`
// at the output row strided by 16. Shared by every kernel for tail tiles.
#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
static inline void sse_trans_n128_one_tile_sse2(const block *inp,
                                                uint64_t bpr_in_blocks,
                                                uint64_t rr,
                                                uint64_t col_block,
                                                int rr_idx,
                                                uint8_t *out_b) {
  __m128i m[16];
  for (int r = 0; r < 16; ++r) {
    m[r] = _mm_loadu_si128(inp + (rr + r) * bpr_in_blocks + col_block);
  }
  sse_trans_16x16_byte(m);
  constexpr uint64_t bpr_out_bytes = 16;
  for (int j = 0; j < 16; ++j) {
    __m128i x = m[j];
    const uint64_t cc8 = (col_block * 16 + j) * 8;
    for (int b = 7; b >= 0; --b) {
      *(uint16_t *)(out_b + (cc8 + b) * bpr_out_bytes + (size_t)rr_idx * 2) =
          (uint16_t)_mm_movemask_epi8(x);
      x = _mm_slli_epi64(x, 1);
    }
  }
}

#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
inline void sse_trans_n128_sse2(block *out, const block *inp, uint64_t ncols) {
  const uint64_t bpr_in_blocks = ncols / 128;
  uint8_t *out_b = reinterpret_cast<uint8_t *>(out);
  #pragma GCC unroll 8
  for (int rr_idx = 0; rr_idx < 8; ++rr_idx) {
    const uint64_t rr = (uint64_t)rr_idx * 16;
    for (uint64_t col_block = 0; col_block < bpr_in_blocks; ++col_block) {
      sse_trans_n128_one_tile_sse2(inp, bpr_in_blocks, rr, col_block, rr_idx,
                                   out_b);
    }
  }
}

#if EMP_HAS_AVX2
// Two 16x16 byte transposes in parallel — 256-bit unpack instructions act
// within each 128-bit lane, so the SSE2 unpack sequence lifts unchanged.
__attribute__((target("avx2,sse2")))
static inline void avx2_trans_16x16_byte_x2(__m256i *m) {
  __m256i t[16];
  for (int i = 0; i < 8; ++i) {
    t[2*i]   = _mm256_unpacklo_epi8(m[2*i], m[2*i+1]);
    t[2*i+1] = _mm256_unpackhi_epi8(m[2*i], m[2*i+1]);
  }
  __m256i u[16];
  for (int i = 0; i < 4; ++i) {
    u[4*i]   = _mm256_unpacklo_epi16(t[4*i],   t[4*i+2]);
    u[4*i+1] = _mm256_unpackhi_epi16(t[4*i],   t[4*i+2]);
    u[4*i+2] = _mm256_unpacklo_epi16(t[4*i+1], t[4*i+3]);
    u[4*i+3] = _mm256_unpackhi_epi16(t[4*i+1], t[4*i+3]);
  }
  __m256i v[16];
  for (int i = 0; i < 2; ++i) {
    for (int k = 0; k < 4; ++k) {
      v[8*i + 2*k]     = _mm256_unpacklo_epi32(u[8*i + k], u[8*i + k + 4]);
      v[8*i + 2*k + 1] = _mm256_unpackhi_epi32(u[8*i + k], u[8*i + k + 4]);
    }
  }
  for (int k = 0; k < 8; ++k) {
    m[2*k]   = _mm256_unpacklo_epi64(v[k], v[k+8]);
    m[2*k+1] = _mm256_unpackhi_epi64(v[k], v[k+8]);
  }
}

__attribute__((target("avx2,sse2")))
inline void sse_trans_n128_avx2(block *out, const block *inp, uint64_t ncols) {
  const uint64_t bpr_in_blocks = ncols / 128;
  const uint64_t pairs = bpr_in_blocks / 2;
  const uint64_t tail_start = pairs * 2;
  constexpr uint64_t bpr_out_bytes = 16;
  uint8_t *out_b = reinterpret_cast<uint8_t *>(out);

  #pragma GCC unroll 8
  for (int rr_idx = 0; rr_idx < 8; ++rr_idx) {
    const uint64_t rr = (uint64_t)rr_idx * 16;

    // Bulk: 2 col_blocks per tile. 16 rows * 32 bytes = 512 bytes -> 16 ymm.
    for (uint64_t cb = 0; cb < pairs; ++cb) {
      const uint64_t col_block = cb * 2;
      __m256i m[16];
      for (int r = 0; r < 16; ++r) {
        m[r] = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(
            inp + (rr + r) * bpr_in_blocks + col_block));
      }
      avx2_trans_16x16_byte_x2(m);
      for (int j = 0; j < 16; ++j) {
        __m256i x = m[j];
        const uint64_t cc8_0 = (col_block * 16 + j) * 8;
        const uint64_t cc8_1 = ((col_block + 1) * 16 + j) * 8;
        for (int b = 7; b >= 0; --b) {
          const uint32_t mask = (uint32_t)_mm256_movemask_epi8(x);
          *(uint16_t *)(out_b + (cc8_0 + b) * bpr_out_bytes + (size_t)rr_idx * 2) =
              (uint16_t)mask;
          *(uint16_t *)(out_b + (cc8_1 + b) * bpr_out_bytes + (size_t)rr_idx * 2) =
              (uint16_t)(mask >> 16);
          x = _mm256_slli_epi64(x, 1);
        }
      }
    }
    // Tail (at most 1 col_block on AVX2): scalar SSE2 body.
    for (uint64_t col_block = tail_start; col_block < bpr_in_blocks; ++col_block) {
      sse_trans_n128_one_tile_sse2(inp, bpr_in_blocks, rr, col_block, rr_idx,
                                   out_b);
    }
  }
}
#endif  // EMP_HAS_AVX2

#if EMP_HAS_AVX512BW
// Four 16x16 byte transposes in parallel — 512-bit unpack acts per 128-bit
// lane, so the same byte-unpack sequence transposes 4 tiles at once. The
// shift+movepi8_mask post-step uses _mm512_movepi8_mask -> __mmask64
// holding 4 packed uint16 result rows.
__attribute__((target("avx512bw,avx512f,avx2,sse2")))
static inline void avx512_trans_16x16_byte_x4(__m512i *m) {
  __m512i t[16];
  for (int i = 0; i < 8; ++i) {
    t[2*i]   = _mm512_unpacklo_epi8(m[2*i], m[2*i+1]);
    t[2*i+1] = _mm512_unpackhi_epi8(m[2*i], m[2*i+1]);
  }
  __m512i u[16];
  for (int i = 0; i < 4; ++i) {
    u[4*i]   = _mm512_unpacklo_epi16(t[4*i],   t[4*i+2]);
    u[4*i+1] = _mm512_unpackhi_epi16(t[4*i],   t[4*i+2]);
    u[4*i+2] = _mm512_unpacklo_epi16(t[4*i+1], t[4*i+3]);
    u[4*i+3] = _mm512_unpackhi_epi16(t[4*i+1], t[4*i+3]);
  }
  __m512i v[16];
  for (int i = 0; i < 2; ++i) {
    for (int k = 0; k < 4; ++k) {
      v[8*i + 2*k]     = _mm512_unpacklo_epi32(u[8*i + k], u[8*i + k + 4]);
      v[8*i + 2*k + 1] = _mm512_unpackhi_epi32(u[8*i + k], u[8*i + k + 4]);
    }
  }
  for (int k = 0; k < 8; ++k) {
    m[2*k]   = _mm512_unpacklo_epi64(v[k], v[k+8]);
    m[2*k+1] = _mm512_unpackhi_epi64(v[k], v[k+8]);
  }
}

__attribute__((target("avx512bw,avx512f,avx2,sse2")))
inline void sse_trans_n128_avx512bw(block *out, const block *inp,
                                    uint64_t ncols) {
  const uint64_t bpr_in_blocks = ncols / 128;
  const uint64_t quads = bpr_in_blocks / 4;
  const uint64_t tail_start = quads * 4;
  constexpr uint64_t bpr_out_bytes = 16;
  uint8_t *out_b = reinterpret_cast<uint8_t *>(out);

  #pragma GCC unroll 8
  for (int rr_idx = 0; rr_idx < 8; ++rr_idx) {
    const uint64_t rr = (uint64_t)rr_idx * 16;

    // Bulk: 4 col_blocks per tile. 16 rows * 64 bytes = 1 KiB -> 16 zmm.
    for (uint64_t cb = 0; cb < quads; ++cb) {
      const uint64_t col_block = cb * 4;
      __m512i m[16];
      for (int r = 0; r < 16; ++r) {
        m[r] = _mm512_loadu_si512(reinterpret_cast<const void *>(
            inp + (rr + r) * bpr_in_blocks + col_block));
      }
      avx512_trans_16x16_byte_x4(m);
      for (int j = 0; j < 16; ++j) {
        __m512i x = m[j];
        const uint64_t cc8_0 = ((col_block + 0) * 16 + j) * 8;
        const uint64_t cc8_1 = ((col_block + 1) * 16 + j) * 8;
        const uint64_t cc8_2 = ((col_block + 2) * 16 + j) * 8;
        const uint64_t cc8_3 = ((col_block + 3) * 16 + j) * 8;
        for (int b = 7; b >= 0; --b) {
          const uint64_t mask = (uint64_t)_mm512_movepi8_mask(x);
          *(uint16_t *)(out_b + (cc8_0 + b) * bpr_out_bytes + (size_t)rr_idx * 2) =
              (uint16_t)mask;
          *(uint16_t *)(out_b + (cc8_1 + b) * bpr_out_bytes + (size_t)rr_idx * 2) =
              (uint16_t)(mask >> 16);
          *(uint16_t *)(out_b + (cc8_2 + b) * bpr_out_bytes + (size_t)rr_idx * 2) =
              (uint16_t)(mask >> 32);
          *(uint16_t *)(out_b + (cc8_3 + b) * bpr_out_bytes + (size_t)rr_idx * 2) =
              (uint16_t)(mask >> 48);
          x = _mm512_slli_epi64(x, 1);
        }
      }
    }
    // Tail (0..3 col_blocks): scalar SSE2 body.
    for (uint64_t col_block = tail_start; col_block < bpr_in_blocks; ++col_block) {
      sse_trans_n128_one_tile_sse2(inp, bpr_in_blocks, rr, col_block, rr_idx,
                                   out_b);
    }
  }
}
#endif  // EMP_HAS_AVX512BW

#if EMP_HAS_GFNI512
// GFNI tier: one full 128x128 tile per iteration, entirely in-register, with
// each output block written exactly once (contiguous zmm stores). This removes
// both costs of the unpack tiers' movemask ladder: the scattered 2-byte stores
// AND the 8 sparse read-modify-write passes over the whole output.
//
// Decomposition: bit-transpose(128x128) = byte-transpose of the 16x16 grid of
// 8x8-bit blocks + a bit-transpose inside each block.
//   stage B  per 8-row slab s, one vpermi2b gathers qword u = block(s,u) with
//            row k placed at byte (7-k)
//   stage C  vgf2p8affineqb(I, blocks, 0) with the DATA in the matrix operand
//            and I = 0x8040201008040201 broadcast as the data operand. That
//            form maps x.byte[i].bit[j] -> dst.byte[j].bit[7-i]; stage B's row
//            reversal (i = 7-k) cancels the 7-i, leaving
//            dst.byte[b].bit[k] = row_k.bit[b] -- the transposed block, no
//            fixups left over.
//   stage D  8x8 qword transpose across each 8-register group (unpack64 +
//            shuffle_i64x2 network; it lands result u in register perm(u),
//            perm = {0,2,1,3,4,6,5,7}, self-inverse, lanes in slab order),
//            then one vpermi2b per output zmm gathers the 16 per-slab bytes
//            of 4 output blocks.
// Any bpr >= 1 is handled uniformly (no tail path).
namespace gfni_trans {
struct Patterns {
  alignas(64) uint8_t patA[64];  // stage B: qwords u=0..7  (low  row bytes)
  alignas(64) uint8_t patB[64];  // stage B: qwords u=8..15 (high row bytes)
  alignas(64) uint8_t p0[64];    // stage D: output rows 8u+0..3
  alignas(64) uint8_t p1[64];    // stage D: output rows 8u+4..7
};
constexpr Patterns make_patterns() {
  Patterns P{};
  for (int u = 0; u < 8; ++u)
    for (int k = 0; k < 8; ++k) {
      P.patA[8 * u + (7 - k)] = (uint8_t)(k * 16 + u);
      P.patB[8 * u + (7 - k)] = (uint8_t)(k * 16 + 8 + u);
    }
  // out zmm byte 16*b + s = G(s,u).byte[b]; G(s,u) sits at lane s%8 (byte
  // 8*(s%8)+b) of the first vpermi2b operand for s < 8, of the second for
  // s >= 8.
  for (int b = 0; b < 4; ++b)
    for (int s = 0; s < 16; ++s) {
      P.p0[16 * b + s] = (uint8_t)((s >= 8 ? 64 : 0) + 8 * (s % 8) + b);
      P.p1[16 * b + s] = (uint8_t)((s >= 8 ? 64 : 0) + 8 * (s % 8) + b + 4);
    }
  return P;
}
inline constexpr Patterns kPat = make_patterns();
}  // namespace gfni_trans

// The stage-D 8x8 qword transpose across one 8-register group; result u is
// written back to R[perm(u)] (perm self-inverse), so on return
// R[u].lane[s] = old R[s].qword[u].
__attribute__((target("gfni,avx512vbmi,avx512bw,avx512f,avx2,sse2")))
static inline void gfni_trans_qword8(__m512i *R) {
  __m512i t0[8], t1[8], F[8];
  for (int i = 0; i < 4; ++i) {
    t0[2*i]   = _mm512_unpacklo_epi64(R[2*i], R[2*i+1]);
    t0[2*i+1] = _mm512_unpackhi_epi64(R[2*i], R[2*i+1]);
  }
  for (int i = 0; i < 2; ++i)
    for (int k = 0; k < 2; ++k) {
      t1[4*i+2*k]   = _mm512_shuffle_i64x2(t0[4*i+k], t0[4*i+k+2], 0x88);
      t1[4*i+2*k+1] = _mm512_shuffle_i64x2(t0[4*i+k], t0[4*i+k+2], 0xdd);
    }
  for (int k = 0; k < 4; ++k) {
    F[k]   = _mm512_shuffle_i64x2(t1[k], t1[k+4], 0x88);
    F[k+4] = _mm512_shuffle_i64x2(t1[k], t1[k+4], 0xdd);
  }
  constexpr int perm[8] = {0, 2, 1, 3, 4, 6, 5, 7};
  for (int r = 0; r < 8; ++r) R[perm[r]] = F[r];
}

__attribute__((target("gfni,avx512vbmi,avx512bw,avx512f,avx2,sse2")))
inline void sse_trans_n128_gfni(block *out, const block *inp, uint64_t ncols) {
  const uint64_t bpr = ncols / 128;   // input row stride in 16-byte blocks
  const uint8_t *in_b = reinterpret_cast<const uint8_t *>(inp);
  uint8_t *out_b = reinterpret_cast<uint8_t *>(out);
  const __m512i patA = _mm512_load_si512((const void *)gfni_trans::kPat.patA);
  const __m512i patB = _mm512_load_si512((const void *)gfni_trans::kPat.patB);
  const __m512i p0   = _mm512_load_si512((const void *)gfni_trans::kPat.p0);
  const __m512i p1   = _mm512_load_si512((const void *)gfni_trans::kPat.p1);
  const __m512i I    = _mm512_set1_epi64((long long)0x8040201008040201ULL);

  for (uint64_t t = 0; t < bpr; ++t) {
    __m512i A[16], Bq[16];
    for (int s = 0; s < 16; ++s) {
      const uint8_t *base = in_b + (uint64_t)(8 * s) * bpr * 16 + t * 16;
      __m512i r0 = _mm512_castsi128_si512(_mm_loadu_si128((const __m128i *)(base + 0 * bpr * 16)));
      r0 = _mm512_inserti32x4(r0, _mm_loadu_si128((const __m128i *)(base + 1 * bpr * 16)), 1);
      r0 = _mm512_inserti32x4(r0, _mm_loadu_si128((const __m128i *)(base + 2 * bpr * 16)), 2);
      r0 = _mm512_inserti32x4(r0, _mm_loadu_si128((const __m128i *)(base + 3 * bpr * 16)), 3);
      __m512i r1 = _mm512_castsi128_si512(_mm_loadu_si128((const __m128i *)(base + 4 * bpr * 16)));
      r1 = _mm512_inserti32x4(r1, _mm_loadu_si128((const __m128i *)(base + 5 * bpr * 16)), 1);
      r1 = _mm512_inserti32x4(r1, _mm_loadu_si128((const __m128i *)(base + 6 * bpr * 16)), 2);
      r1 = _mm512_inserti32x4(r1, _mm_loadu_si128((const __m128i *)(base + 7 * bpr * 16)), 3);
      A[s]  = _mm512_gf2p8affine_epi64_epi8(I, _mm512_permutex2var_epi8(r0, patA, r1), 0);
      Bq[s] = _mm512_gf2p8affine_epi64_epi8(I, _mm512_permutex2var_epi8(r0, patB, r1), 0);
    }
    gfni_trans_qword8(A);        // A[u].lane[s]   = G(s,     u)   s in 0..7
    gfni_trans_qword8(A + 8);    // A[8+u].lane[s] = G(8 + s, u)
    gfni_trans_qword8(Bq);       // Bq[u].lane[s]  = G(s,     8+u)
    gfni_trans_qword8(Bq + 8);   // Bq[8+u].lane[s]= G(8 + s, 8+u)
    uint8_t *dst = out_b + t * 128 * 16;
    for (int u = 0; u < 8; ++u) {
      _mm512_storeu_si512((void *)(dst + (uint64_t)(8*u + 0) * 16),
                          _mm512_permutex2var_epi8(A[u], p0, A[u + 8]));
      _mm512_storeu_si512((void *)(dst + (uint64_t)(8*u + 4) * 16),
                          _mm512_permutex2var_epi8(A[u], p1, A[u + 8]));
    }
    for (int u = 0; u < 8; ++u) {
      _mm512_storeu_si512((void *)(dst + (uint64_t)(8*(u + 8) + 0) * 16),
                          _mm512_permutex2var_epi8(Bq[u], p0, Bq[u + 8]));
      _mm512_storeu_si512((void *)(dst + (uint64_t)(8*(u + 8) + 4) * 16),
                          _mm512_permutex2var_epi8(Bq[u], p1, Bq[u + 8]));
    }
  }
}
#endif  // EMP_HAS_GFNI512

}  // namespace detail

inline void sse_trans_n128(block *out, const block *inp, uint64_t ncols) {
#ifndef __x86_64__
  sse_trans(reinterpret_cast<uint8_t *>(out),
            reinterpret_cast<const uint8_t *>(inp),
            /*nrows=*/128, ncols);
#else
  assert((ncols % 128) == 0);
  #if EMP_HAS_GFNI512
    detail::sse_trans_n128_gfni(out, inp, ncols);
  #elif EMP_HAS_AVX512BW
    detail::sse_trans_n128_avx512bw(out, inp, ncols);
  #elif EMP_HAS_AVX2
    detail::sse_trans_n128_avx2(out, inp, ncols);
  #else
    detail::sse_trans_n128_sse2(out, inp, ncols);
  #endif
#endif
}

}  // namespace emp
#endif  // EMP_UTIL_BLOCK_HPP__
