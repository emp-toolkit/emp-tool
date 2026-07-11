#ifndef EMP_HALF_GATE_H__
#define EMP_HALF_GATE_H__

#include "emp-tool/runtime/core/utils.h"
#include "emp-tool/runtime/crypto/mitccrh.h"

namespace emp {

// Half-gate garbling [Zahur-Rosulek-Evans 2014, eprint 2014/756]: the per-AND
// garble / evaluate primitives over block labels and a shared MITCCRH. XOR and
// NOT are free (label XOR with delta), handled by the caller; only AND needs a
// ciphertext pair (`table`). Generic over the MITCCRH instantiation so the
// protocol picks BatchSize / ReuseShift. Only ReuseShift (together with the
// start_point / gid schedule) must match across parties — it determines the
// hash keys, hence the transcript. BatchSize only sets how many gids are
// keyed per schedule refill; the outputs are BatchSize-invariant, so it is a
// per-machine scheduling choice.

template <class MITC>
inline block halfgates_garble(block LA0, block A1, block LB0, block B1,
                              block delta, block* table, MITC* mitccrh) {
	bool pa = getLSB(LA0);
	bool pb = getLSB(LB0);
	block in[4] = {LA0, A1, LB0, B1};
	block H[4];
	mitccrh->template hash_cir<2, 2>(H, in);
	block HLA0 = H[0], HA1 = H[1], HLB0 = H[2], HB1 = H[3];

	table[0] = HLA0 ^ HA1 ^ (select_mask[pb] & delta);
	block W0 = HLA0 ^ (select_mask[pa] & table[0]);
	block tmp = HLB0 ^ HB1;
	table[1] = tmp ^ LA0;
	W0 ^= HLB0 ^ (select_mask[pb] & tmp);
	return W0;
}

template <class MITC>
inline block halfgates_eval(block A, block B, const block* table,
                            MITC* mitccrh) {
	int sa = getLSB(A);
	int sb = getLSB(B);
	block in[2] = {A, B};
	block H[2];
	mitccrh->template hash_cir<2, 1>(H, in);
	block W = H[0] ^ H[1];
	W ^= (select_mask[sa] & table[0]);
	W ^= (select_mask[sb] & table[1]);
	W ^= (select_mask[sb] & A);
	return W;
}

}  // namespace emp
#endif
