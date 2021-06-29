#ifndef EMP_HALFGATE_EVA_H__
#define EMP_HALFGATE_EVA_H__
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/mitccrh.h"
#include "emp-tool/execution/circuit_execution.h"
#include <iostream>
namespace emp {

inline block halfgates_eval(block A, block B, const block *table, MITCCRH<8> *mitccrh) {
	block HA, HB, W;
	int sa, sb;

	sa = getLSB(A);
	sb = getLSB(B);

	block H[2];
	H[0] = A;
	H[1] = B;
	mitccrh->hash<2,1>(H);
	HA = H[0];
	HB = H[1];

	W = HA ^ HB;
	W = W ^ (select_mask[sa] & table[0]);
	W = W ^ (select_mask[sb] & table[1]);
	W = W ^ (select_mask[sb] & A);
	return W;
}


template<typename T>
class HalfGateEva:public CircuitExecution {
public:
	T * io;
	block constant[2];
	MITCCRH<8> mitccrh;
	HalfGateEva(T * io) :io(io) {
		set_delta();
		block tmp;
		io->recv_block(&tmp, 1);
		mitccrh.setS(tmp);
	}
	void set_delta() {
		io->recv_block(constant, 2);
	}
	block public_label(bool b) override {
		return constant[b];
	}
	block and_gate(const block& a, const block& b) override {
		block table[2];
		io->recv_block(table, 2);
		return halfgates_eval(a, b, table, &mitccrh);
	}
	block xor_gate(const block& a, const block& b) override {
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
#endif// HALFGATE_EVA_H__
