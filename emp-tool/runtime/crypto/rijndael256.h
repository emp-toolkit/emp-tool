#ifndef EMP_RIJNDAEL256_H
#define EMP_RIJNDAEL256_H

#include "emp-tool/runtime/core/block.h"
#include <cstdint>

namespace emp {

// Rijndael-256: Rijndael with a 256-bit block and a 256-bit key (Nb = Nk = 8,
// 14 rounds). AES is the 128-bit-block member of the same family; this one
// runs on the same AES round instructions, with one byte permutation of the
// state per round (see rijndael256.hpp).
//
// A Rijndael-256 block is two consecutive `block`s: block i of a buffer is
// blks[2i] (bytes 0..15) followed by blks[2i + 1] (bytes 16..31), in Rijndael
// byte order (byte j is state row j % 4, column j / 4). The key is two blocks
// the same way.

// The 15 round keys of each direction, as 30 blocks, in their own types so an
// encryption schedule cannot be passed to decryption or the reverse. The
// layout is the kernels' own and differs between x86 and aarch64; schedule
// them with R256_set_encrypt_key / R256_set_decrypt_key.
struct R256_KEY { block rd_key[30]; };
struct R256_DEC_KEY { block rd_key[30]; };

// Schedule the round keys of the 256-bit key userkey[0..1].
inline void R256_set_encrypt_key(const block *userkey, R256_KEY *key);
inline void R256_set_decrypt_key(const block *userkey, R256_DEC_KEY *key);

// Encrypt (decrypt) N Rijndael-256 blocks (2N blocks) in place, through
// kernels unrolled at compile time.
template<int N>
inline void R256_ecb_encrypt_blks(block *blks, const R256_KEY *key);
template<int N>
inline void R256_ecb_decrypt_blks(block *blks, const R256_DEC_KEY *key);

// Runtime-sized companions: n Rijndael-256 blocks (2n blocks) in place, in
// the same compile-time tiles.
inline void R256_ecb_encrypt_blks(block *blks, int64_t n, const R256_KEY *key);
inline void R256_ecb_decrypt_blks(block *blks, int64_t n, const R256_DEC_KEY *key);

}  // namespace emp

#include "emp-tool/runtime/crypto/rijndael256.hpp"

#endif
