#ifndef EMP_AES_H
#define EMP_AES_H

#include "emp-tool/core/block.h"
#include <cassert>
#include <cstdint>

namespace emp {

typedef struct { block rd_key[11]; } AES_KEY;

#define EMP_AES_ASSERT_ALIGNED(p) \
	assert((reinterpret_cast<uintptr_t>(p) & (alignof(block) - 1)) == 0 \
	       && "pointer must be 16-byte (block) aligned")

// Schedule NumKeys AES-128 round keys, one per element of `user_key`,
// into `keys`. Source: "Fast Garbling of Circuits Under Standard
// Assumptions" — https://eprint.iacr.org/2015/751.pdf
template<int NumKeys>
inline void AES_opt_key_schedule(const block* user_key, AES_KEY* keys);

// AES-ECB on K * N blocks: keys[k] encrypts blks[k*N..k*N+N) in place
// (or, for the out-of-place form, dst[k*N..k*N+N) = AES(src[k*N..]).
// All K*N blocks and K*11 round keys are expected to live in SIMD
// registers simultaneously for the fully-unrolled kernel; sweet spot is
// K*N <= 16. For larger K*N, use the runtime form below.
template<int numKeys, int numEncs>
inline void ParaEnc(block* blks, const AES_KEY* keys);

template<int numKeys, int numEncs>
inline void ParaEnc(block* __restrict__ dst,
                    const block* __restrict__ src,
                    const AES_KEY* keys);

// Runtime-sized companion. Tiles each per-key stream into compile-time
// tiles {8, 4, 2, 1} so each tile still goes through the unrolled,
// register-resident templated kernel. Cross-key ILP is not exploited
// here — preserving K-major layout while tiling on N would require
// interleaved layouts. Hot callers that know (K, N) at compile time
// should use ParaEnc<K, N> directly.
inline void ParaEnc(block* blks, const AES_KEY* keys, int64_t K, int64_t N);

inline void ParaEnc(block* __restrict__ dst,
                    const block* __restrict__ src,
                    const AES_KEY* keys, int64_t K, int64_t N);

// Single-key convenience wrappers.
inline void AES_set_encrypt_key(const block& userkey, AES_KEY* key);

template<int N>
inline void AES_ecb_encrypt_blks(block* blks, const AES_KEY* key);

inline void AES_ecb_encrypt_blks(block* blks, int64_t nblks, const AES_KEY* key);

}  // namespace emp

#include "emp-tool/crypto/aes.hpp"

#endif
