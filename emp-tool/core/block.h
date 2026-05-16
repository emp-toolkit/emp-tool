#ifndef EMP_UTIL_BLOCK_H__
#define EMP_UTIL_BLOCK_H__

#ifdef __x86_64__
#include <immintrin.h>
#elif __aarch64__
#include "emp-tool/third_party/sse2neon.h"
#endif

#include <cstdint>
#include <cstddef>
#include <iosfwd>

namespace emp {

using block = __m128i;

// --- Basic primitives -----------------------------------------------------

inline bool   getLSB(const block& x);
inline block  makeBlock(uint64_t high, uint64_t low);
inline block  sigma(block a);                     // [eprint 2019/074]
inline block  set_bit(const block& a, int i);
std::ostream& operator<<(std::ostream& out, const block& blk);

extern const block zero_block;
extern const block all_one_block;
extern const block select_mask[2];

// --- XOR / compare --------------------------------------------------------

// res[i] = x[i] ^ y[i]. Pointers must be pairwise non-overlapping.
inline void xorBlocks_arr(block* __restrict__ res,
                          const block* __restrict__ x,
                          const block* __restrict__ y, int nblocks);

// In-place: dst[i] ^= src[i]. dst and src must not overlap.
inline void xorBlocksTo_arr(block* __restrict__ dst,
                            const block* __restrict__ src, int nblocks);

// res[i] = x[i] ^ y (broadcast). res and x must not overlap.
inline void xorBlocks_arr(block* __restrict__ res,
                          const block* __restrict__ x,
                          block y, int nblocks);

// True iff x[0..n) == y[0..n) element-wise. No early exit:
// accumulator-OR exposes ILP on the equal-case path.
inline bool cmpBlock(const block* x, const block* y, int nblocks);

// --- SSE bit-matrix transpose --------------------------------------------

// Transpose an nrows x ncols bit-matrix in row-major byte layout.
// nrows and ncols must each be multiples of 8.
inline void sse_trans(uint8_t* out, const uint8_t* inp, uint64_t nrows, uint64_t ncols);

// nrows=128 specialization. ncols must be a multiple of 128.
inline void sse_trans_n128(uint8_t* out, const uint8_t* inp, uint64_t ncols);

// 16x16 byte transpose in place. Building block for block-loop
// transpose variants.
static inline void sse_trans_16x16_byte(__m128i* m);

}  // namespace emp

#include "emp-tool/core/block.hpp"

#endif  // EMP_UTIL_BLOCK_H__
