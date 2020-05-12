#ifndef EMP_PRP_H__
#define EMP_PRP_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/constants.h"
#include "emp-tool/utils/aes.h"

namespace emp {

class PRP { public:
	AES_KEY aes;

	PRP(const char * seed = fix_key) {
		aes_set_key(seed);
	}

	PRP(const block& seed): PRP((const char *)&seed) {
	}

	void aes_set_key(const char * key) {
		aes_set_key(_mm_loadu_si128((block*)key));
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
