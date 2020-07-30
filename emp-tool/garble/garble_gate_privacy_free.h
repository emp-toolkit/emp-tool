#ifndef LIBGARBLE_GARBLE_GATE_PRIVACY_FREE_H
#define LIBGARBLE_GARBLE_GATE_PRIVACY_FREE_H

#include "emp-tool/garble/aes.h"
#include <assert.h>
#include <string.h>
namespace emp {
static inline void garble_gate_eval_privacy_free(block A, block B,
		block *out, const block *table, uint64_t idx, const AES_KEY *key) {
	block HA, W;
	bool sa;
	block tweak;

	sa = getLSB(A);

	tweak = makeBlock(2 * idx, (uint64_t) 0);

	{
		block tmp, mask;

		tmp = xorBlocks(sigma(A), tweak);
		mask = tmp;
		AES_ecb_encrypt_blks(&tmp, 1, key);
		HA = xorBlocks(tmp, mask);
	}
	if (sa) {
		*((char *) &HA) |= 0x01;
		W = xorBlocks(HA, table[0]);
		W = xorBlocks(W, B);
	} else {
		*((char *) &HA) &= 0xfe;
		W = HA;
	}
	*out = W;
}


static inline void garble_gate_garble_privacy_free(block LA0, block A1,
		block LB0, block B1, block *out0, block *out1, block delta, block *table, 
		uint64_t idx, const AES_KEY *key) {
#ifdef DEBUG
	if ((*((char *) &LA0) & 0x01) == 1
			|| (*((char *) &LB0) & 0x01) == 1
			|| (*((char *) &A1) & 0x01) == 0
			|| (*((char *) &B1) & 0x01) == 0) {
		assert(false && "invalid lsb in block");
	}
#endif

	block tweak, tmp;
	block HLA0, HA1;

	tweak = makeBlock(2 * idx, (long) 0);

	{
		block masks[2], keys[2];

		keys[0] = xorBlocks(sigma(LA0), tweak);
		keys[1] = xorBlocks(sigma(A1), tweak);
		memcpy(masks, keys, sizeof keys);
		AES_ecb_encrypt_blks(keys, 2, key);
		HLA0 = xorBlocks(keys[0], masks[0]);
		HA1 = xorBlocks(keys[1], masks[1]);
	}
	*((char *) &HLA0) &= 0xfe;
	*((char *) &HA1) |= 0x01;
	tmp = xorBlocks(HLA0, HA1);
	table[0] = xorBlocks(tmp, LB0);
	*out0 = HLA0;
	*out1 = xorBlocks(HLA0, delta);
}
}
#endif
