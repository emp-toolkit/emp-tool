#ifndef EMP_HALFGATE_GEN_H__
#define EMP_HALFGATE_GEN_H__
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/mitccrh.h"
#include "emp-tool/execution/circuit_execution.h"
#include <iostream>
namespace emp {

/*
 * The half-gate garbling scheme, with improved hashing
 * [REF] Implementation of "Two Halves Make a Whole"
 * https://eprint.iacr.org/2014/756.pdf
 */
inline block halfgates_garble(block LA0, block A1, block LB0, block B1, block delta, block *table, MITCCRH<8> *mitccrh) {
	bool pa = getLSB(LA0);
	bool pb = getLSB(LB0);
	block HLA0, HA1, HLB0, HB1;
	block tmp, W0;

	block H[4];
	H[0] = LA0;
	H[1] = A1;
	H[2] = LB0;
	H[3] = B1;
	mitccrh->hash<2,2>(H);
	HLA0 = H[0];
	HA1 = H[1];
	HLB0 = H[2];
	HB1 = H[3];

	table[0] = HLA0 ^ HA1;
	table[0] = table[0] ^ (select_mask[pb] & delta);
	W0 = HLA0;
	W0 = W0 ^ (select_mask[pa] & table[0]);
	tmp = HLB0 ^ HB1;
	table[1] = tmp ^ LA0;
	W0 = W0 ^ HLB0;
	W0 = W0 ^ (select_mask[pb] & tmp);

	return W0;
}

template<typename T>
class HalfGateGen:public CircuitExecution {
public:
	block delta;
	T * io;
	block constant[2];
	MITCCRH<8> mitccrh;
	HalfGateGen(T * io) :io(io) {
		block tmp[2];
		PRG().random_block(tmp, 2);
		set_delta(tmp[0]);
		io->send_block(tmp+1, 1);
		mitccrh.setS(tmp[1]);
	}
	void set_delta(const block & _delta) {
		delta = set_bit(_delta, 0);
		PRG().random_block(constant, 2);
		io->send_block(constant, 2);
		constant[1] = constant[1] ^ delta;
	}
	block public_label(bool b) override {
		return constant[b];
	}
	block and_gate(const block& a, const block& b) override {
		block table[2];
		block res = halfgates_garble(a, a^delta, b, b^delta, delta, table, &mitccrh);
		io->send_block(table, 2);
		return res;
	}
	block xor_gate(const block&a, const block& b) override {
		return a ^ b;
	}
	block not_gate(const block&a) override {
		return xor_gate(a, public_label(true));
	}
	uint64_t num_and() override {
		return mitccrh.gid/2;
	}
};
}
#endif// HALFGATE_GEN_H__
