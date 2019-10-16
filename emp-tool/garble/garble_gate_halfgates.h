#ifndef LIBGARBLE_GARBLE_GATE_HALFGATES_H
#define LIBGARBLE_GARBLE_GATE_HALFGATES_H

#include "emp-tool/utils/mitccrh.h"
#include <string.h>
namespace emp {
inline void garble_gate_eval_halfgates(block A, block B, block *out, const block *table, MITCCRH *mitccrh) {
	block HA, HB, W;
	int sa, sb;

	sa = getLSB(A);
	sb = getLSB(B);

	block H[2];
	mitccrh->k2_h2(A, B, H);
	HA = H[0];
	HB = H[1];

	W = xorBlocks(HA, HB);
	if (sa)
		W = xorBlocks(W, table[0]);
	if (sb) {
		W = xorBlocks(W, table[1]);
		W = xorBlocks(W, A);
	}
	*out = W;
}

inline void garble_gate_garble_halfgates(block LA0, block A1, block LB0, block B1, block *out0, block *out1, block delta, block *table, MITCCRH *mitccrh) {
	long pa = getLSB(LA0);
	long pb = getLSB(LB0);
	block HLA0, HA1, HLB0, HB1;
	block tmp, W0;

	block H[4];
	mitccrh->k2_h4(LA0, A1, LB0, B1, H);
	HLA0 = H[0];
	HA1 = H[1];
	HLB0 = H[2];
	HB1 = H[3];

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
