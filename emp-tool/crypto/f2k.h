#ifndef EMP_F2K_H__
#define EMP_F2K_H__

#include "emp-tool/core/block.h"
#include <cassert>

namespace emp {

// GF(2^128) multiplication without reduction. Splits the 128-bit
// carryless product into low and high 128-bit halves (res1, res2).
inline void mul128(block a, block b, block* res1, block* res2);

// GF(2^128) reduction of the unreduced 256-bit product (low | high)
// under the standard x^128 + x^7 + x^2 + x + 1 polynomial. Reference:
// https://www.intel.com/content/dam/develop/public/us/en/documents/carry-less-multiplication-instruction.pdf
// figure 7 (reduce) / figure 5 (reduce_reflect, for reflected I/O).
inline block reduce(block lo, block hi);
inline block reduce_reflect(block lo, block hi);

inline void gfmul(block a, block b, block* res);
inline void gfmul_reflect(block a, block b, block* res);

// Σ a[i] * b[i] in GF(2^128). The *_no_red variants leave the result
// unreduced (two 128-bit halves in res[0..1]); the *_red variants
// reduce once at the end. GF addition is XOR (linear), so deferring
// reduction is identity.
inline void vector_inn_prdt_sum_no_red(block* res, const block* a, const block* b, int sz);
template<int N>
inline void vector_inn_prdt_sum_no_red(block* res, const block* a, const block* b);

inline void vector_inn_prdt_sum_red(block* res, const block* a, const block* b, int sz);
template<int N>
inline void vector_inn_prdt_sum_red(block* res, const block* a, const block* b);

// Σ a[i] * b[i] in GF(2^128) where b[i] ∈ {0, 1}. Each contribution is
// either 0 or a[i], so no carryless multiply is needed — branchless
// mask + XOR per element. Stable timing for adversarial b.
inline void vector_inn_prdt_sum_red(block* res, const block* a, const bool* b, int sz);
template<int N>
inline void vector_inn_prdt_sum_red(block* res, const block* a, const bool* b);

// Coefficients of the almost-universal hash {seed, seed^2, seed^3, ...}.
inline void uni_hash_coeff_gen(block* coeff, block seed, int sz);
template<int N>
inline void uni_hash_coeff_gen(block* coeff, block seed);

inline void vector_self_xor(block* sum, block* data, int sz);
template<int N>
inline void vector_self_xor(block* sum, block* data);

// Packing in GF(2^128): res = Σ data[i] * X^i for v of size 128. Each
// base element X^i is a single-bit polynomial, so data[i] * X^i is just
// data[i] left-shifted by i positions; the bool overload then reduces
// to LSB-first bit-packing (no GF arithmetic).
class GaloisFieldPacking {
public:
	void packing(block* res, const block* data);
	void packing(block* res, const bool* data);
};

}  // namespace emp

#include "emp-tool/crypto/f2k.hpp"

#endif
