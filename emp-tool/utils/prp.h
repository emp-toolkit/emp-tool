#ifndef EMP_PRP_H__
#define EMP_PRP_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/constants.h"
#include "emp-tool/utils/aes.h"

namespace emp {

/*
 * When the key is public, we usually need to model AES with this public key
 * as a random permutation.
 * [REF] "Efficient Garbling from a Fixed-Key Blockcipher"
 * https://eprint.iacr.org/2013/426.pdf
 */
class PRP { public:
	AES_KEY aes;

	PRP(const char * key = nullptr) {
		if(key == nullptr)
			aes_set_key(zero_block);
	}

	PRP(const block& key) {
		aes_set_key(key);
	}

	void aes_set_key(const block& v) {
		AES_set_encrypt_key(v, &aes);
	}

	void permute_block(block *data, int nblocks) {
		for(int i = 0; i < nblocks/AES_BATCH_SIZE; ++i) {
			AES_ecb_encrypt_blks<AES_BATCH_SIZE>(data + i*AES_BATCH_SIZE, &aes);
		}
		int remain = nblocks % AES_BATCH_SIZE;
		AES_ecb_encrypt_blks(data + nblocks - remain, remain, &aes);
	}
};
}
#endif// PRP_H__
