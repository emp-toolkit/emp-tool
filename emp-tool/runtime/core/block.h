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
#include <string>

namespace emp {

using block = __m128i;

// --- Basic primitives -----------------------------------------------------

inline bool   getLSB(const block& x);
inline bool   getBit(const block& x, int i);      // bit i of x (getLSB is bit 0)
inline constexpr block  makeBlock(uint64_t high, uint64_t low);
inline block  sigma(block a);                     // [eprint 2019/074]
inline block  set_bit(const block& a, int i);
// Lowercase byte-order hex of an arbitrary buffer (no separators). The single
// renderer for blocks, digests, and raw byte buffers — operator<<(block) below
// is just to_hex(&blk, sizeof(block)). Use this instead of redefining a setw(2)
// loop. Named to_hex (not hex) to avoid clashing with the std::hex manipulator.
std::string to_hex(const void* data, size_t n);
std::ostream& operator<<(std::ostream& out, const block& blk);

// zero_block, all_one_block, select_mask[2], bit0_mask, bit1_mask: defined in block.hpp.

// --- XOR / compare --------------------------------------------------------

// res[i] = x[i] ^ y[i]. Pointers must be pairwise non-overlapping.
inline void xorBlocks_arr(block* __restrict__ res,
                          const block* __restrict__ x,
                          const block* __restrict__ y, int64_t nblocks);

// In-place: dst[i] ^= src[i]. dst and src must not overlap.
inline void xorBlocksTo_arr(block* __restrict__ dst,
                            const block* __restrict__ src, int64_t nblocks);

// res[i] = x[i] ^ y (broadcast). res and x must not overlap.
inline void xorBlocks_arr(block* __restrict__ res,
                          const block* __restrict__ x,
                          block y, int64_t nblocks);

// True iff x[0..n) == y[0..n) element-wise. No early exit:
// accumulator-OR exposes ILP on the equal-case path.
inline bool cmpBlock(const block* x, const block* y, int64_t nblocks);

// --- bit-matrix transpose (kernels live in transpose.hpp) -----------------

// Transpose an nrows x ncols bit-matrix in row-major byte layout.
// nrows and ncols must each be multiples of 8.
inline void sse_trans(uint8_t* out, const uint8_t* inp, uint64_t nrows, uint64_t ncols);

// nrows=128 specialization. ncols must be a multiple of 128. Both
// buffers are block arrays (nrows=128 means each input row is a
// multiple of one block); takes block* directly to spare callers
// the reinterpret_cast. Dispatches on the widest compiled tier; see
// transpose.hpp.
inline void sse_trans_n128(block* out, const block* inp, uint64_t ncols);

// 16x16 byte transpose in place. Building block for block-loop
// transpose variants.
static inline void sse_trans_16x16_byte(__m128i* m);

}  // namespace emp

#include "emp-tool/runtime/core/block.hpp"
#include "emp-tool/runtime/core/transpose.hpp"

#endif  // EMP_UTIL_BLOCK_H__
