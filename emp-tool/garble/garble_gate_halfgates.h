#ifndef LIBGARBLE_GARBLE_GATE_HALFGATES_H
#define LIBGARBLE_GARBLE_GATE_HALFGATES_H

#include "emp-tool/garble/aes.h"
#include <string.h>
namespace emp {
inline void garble_gate_eval_halfgates(block A, block B, 
		block *out, const block *table, uint64_t idx, const AES_KEY *key) {
	block HA, HB, W;
	int sa, sb;
	block tweak1, tweak2;

	sa = getLSB(A);
	sb = getLSB(B);

	tweak1 = makeBlock(2 * idx, (long) 0);
	tweak2 = makeBlock(2 * idx + 1, (long) 0);

	{
		block keys[2];
		block masks[2];

		keys[0] = xorBlocks(double_block(A), tweak1);
		keys[1] = xorBlocks(double_block(B), tweak2);
		masks[0] = keys[0];
		masks[1] = keys[1];
		AES_ecb_encrypt_blks(keys, 2, key);
		HA = xorBlocks(keys[0], masks[0]);
		HB = xorBlocks(keys[1], masks[1]);
	}

	W = xorBlocks(HA, HB);
	if (sa)
		W = xorBlocks(W, table[0]);
	if (sb) {
		W = xorBlocks(W, table[1]);
		W = xorBlocks(W, A);
	}
	*out = W;
}

inline void garble_gate_garble_halfgates(block LA0, block A1, block LB0, block B1, block *out0, block *out1, block delta, block *table, uint64_t idx, const AES_KEY *key) {
	long pa = getLSB(LA0);
	long pb = getLSB(LB0);
	block tweak1, tweak2;
	block HLA0, HA1, HLB0, HB1;
	block tmp, W0;

	tweak1 = makeBlock(2 * idx, (uint64_t) 0);
	tweak2 = makeBlock(2 * idx + 1, (uint64_t) 0);

	{
		block masks[4], keys[4];

		keys[0] = xorBlocks(double_block(LA0), tweak1);
		keys[1] = xorBlocks(double_block(A1), tweak1);
		keys[2] = xorBlocks(double_block(LB0), tweak2);
		keys[3] = xorBlocks(double_block(B1), tweak2);
		memcpy(masks, keys, sizeof keys);
		AES_ecb_encrypt_blks(keys, 4, key);
		HLA0 = xorBlocks(keys[0], masks[0]);
		HA1 = xorBlocks(keys[1], masks[1]);
		HLB0 = xorBlocks(keys[2], masks[2]);
		HB1 = xorBlocks(keys[3], masks[3]);
	}

	table[0] = xorBlocks(HLA0, HA1);
	if (pb)
		table[0] = xorBlocks(table[0], delta);
	W0 = HLA0;
	if (pa)
		W0 = xorBlocks(W0, table[0]);
	tmp = xorBlocks(HLB0, HB1);
	table[1] = xorBlocks(tmp, LA0);
	W0 = xorBlocks(W0, HLB0);
	if (pb)
		W0 = xorBlocks(W0, tmp);

	*out0 = W0;
	*out1 = xorBlocks(*out0, delta);
}
}
#endif
