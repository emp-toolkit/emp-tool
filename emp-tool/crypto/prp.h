#ifndef EMP_PRP_H__
#define EMP_PRP_H__
#include "emp-tool/core/block.h"
#include "emp-tool/core/constants.h"
#include "emp-tool/crypto/aes.h"

namespace emp {

/*
 * When the key is public, we usually need to model AES with this public key
 * as a random permutation.
 * [REF] "Efficient Garbling from a Fixed-Key Blockcipher"
 * https://eprint.iacr.org/2013/426.pdf
 */
class PRP { public:
	AES_KEY aes;

	// `key`, if non-null, must point to at least sizeof(block) (16) bytes.
	PRP(const char * key = nullptr) {
		AES_set_encrypt_key(
			key == nullptr ? zero_block : _mm_loadu_si128((const __m128i *)key),
			&aes);
	}

	PRP(const block& key) {
		AES_set_encrypt_key(key, &aes);
	}

	void permute_block(block *data, int64_t nblocks) {
		assert(((uintptr_t)data & (alignof(block) - 1)) == 0 &&
		       "random_block requires 16-byte aligned data");
		while (nblocks > 0) {
			int64_t n = nblocks < AES_BATCH_SIZE ? nblocks : AES_BATCH_SIZE;
			ParaEnc(data, &aes, 1, n);
			data    += n;
			nblocks -= n;
		}
	}
};
}
#endif// PRP_H__
