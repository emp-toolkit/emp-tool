#ifndef EMP_MITCCRH_H__
#define EMP_MITCCRH_H__
#include "emp-tool/utils/aes_opt.h"
#include <stdio.h>

namespace emp {

/*
 * [REF] Implementation of "Better Concrete Security for Half-Gates Garbling (in the Multi-Instance Setting)"
 * https://eprint.iacr.org/2019/1168.pdf
 */

template<int BatchSize = 8>
class MITCCRH { public:
	AES_KEY scheduled_key[BatchSize];
	block keys[BatchSize];
	int key_used = BatchSize;
	block start_point;
	uint64_t gid = 0;

	void setS(block sin) {
		this->start_point = sin;
	}

	void renew_ks(uint64_t gid) {
		this->gid = gid;
		renew_ks();
	}

	void renew_ks() {
		for(int i = 0; i < BatchSize; ++i)
			keys[i] = start_point ^ makeBlock(gid++, 0);
		AES_opt_key_schedule<BatchSize>(keys, scheduled_key);
		key_used = 0;
	}

	template<int K, int H>
	void hash_cir(block * blks) {
		for(int i = 0; i < K*H; ++i)
			blks[i] = sigma(blks[i]);
		hash<K, H>(blks);
	}

	template<int K, int H>
	void hash(block * blks) {
		assert(K <= BatchSize);
		assert(BatchSize % K == 0);
		if(key_used == BatchSize) renew_ks();

		block tmp[K*H];
		for(int i = 0; i < K*H; ++i)
			tmp[i] = blks[i];
		
		ParaEnc<K,H>(tmp, scheduled_key+key_used);
		key_used += K;
		
		for(int i = 0; i < K*H; ++i)
			blks[i] = blks[i] ^ tmp[i];
	}

};
}
#endif// MITCCRH_H__
