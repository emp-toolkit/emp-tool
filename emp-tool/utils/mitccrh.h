#ifndef MITCCRH_H__
#define MITCCRH_H__
#include "emp-tool/utils/aes_opt.h"
#include <stdio.h>

/** @addtogroup BP
  @{
 */
namespace emp {

class MITCCRH{ public:

	ROUND_KEYS key_schedule[KS_BATCH_N];
	int key_used = KS_BATCH_N;
	block start_point;

	MITCCRH() {
	}

	void setS(block sin) {
		this->start_point = sin;
	}

	void renew_ks(uint64_t gid) {
		switch(KS_BATCH_N) {
			case 2:
				AES_ks2_index(start_point, gid, key_schedule); break;
			case 4:
				AES_ks4_index(start_point, gid, key_schedule); break;
			case 8:
				AES_ks8_index(start_point, gid, key_schedule); break;
			default:
				abort();
		}
		key_used = 0;
	}

	void k2_h2(block A, block B, block *H) {
		block keys[2], masks[2];
		keys[0] = sigma(A);
		keys[1] = sigma(B);
		masks[0] = keys[0];
		masks[1] = keys[1];

		AES_ecb_ccr_ks2_enc2(keys, keys, &key_schedule[key_used]);
		key_used += 2;

		H[0] = xorBlocks(keys[0], masks[0]);
		H[1] = xorBlocks(keys[1], masks[1]);
	}

	void k2_h4(block A0, block A1, block B0, block B1, block *H) {
		block keys[4], masks[4];
		keys[0] = sigma(A0);
		keys[1] = sigma(A1);
		keys[2] = sigma(B0);
		keys[3] = sigma(B1);
		memcpy(masks, keys, sizeof keys);

		AES_ecb_ccr_ks2_enc4(keys, keys, &key_schedule[key_used]);
		key_used += 2;

		H[0] = xorBlocks(keys[0], masks[0]);
		H[1] = xorBlocks(keys[1], masks[1]);
		H[2] = xorBlocks(keys[2], masks[2]);
		H[3] = xorBlocks(keys[3], masks[3]);
	}

};
}
/**@}*/
#endif// MITCCRH_H__
