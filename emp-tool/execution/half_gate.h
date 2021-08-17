#ifndef EMP_HALFGATE_EVA_H__
#define EMP_HALFGATE_EVA_H__
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/mitccrh.h"
#include "emp-tool/execution/backend.h"
#include <iostream>
namespace emp {

/*
 * The half-gate garbling scheme, with improved hashing
 * [REF] Implementation of "Two Halves Make a Whole"
 * https://eprint.iacr.org/2014/756.pdf
 */

template<typename T>
class HalfGate: public Backend { public:
	T * io;
	block constant[2];
	MITCCRH<8> mitccrh;
	uint64_t ands = 0;
	HalfGate(int party, T* io): Backend(party), io(io) { }
	void public_label(void * res, bool b) override {
		*((block*)res) = constant[b];
	}
	void xor_gate(void * out, const void * left, const void * right) override {
		*((block*)out) = *((block*)left) ^ *((block *) right);
	}
	void not_gate(void * out, const void * in) override {
		*((block*)out) = *((block*)in) ^ constant[1];
	}
	uint64_t num_and() override {
		return ands;
	}
	void feed(void * lbls, int party, const bool* b, size_t nel) override {
	}
	void reveal(bool*out, int party, const void * lbls, size_t nel) override {
	}
};

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
class HalfGateGen: public HalfGate<T> { public:
	block delta;
	using HalfGate<T>::io;
	using HalfGate<T>::constant;
	HalfGateGen(T * io): HalfGate<T>(ALICE, io) {
		block tmp[2];
		PRG().random_block(tmp, 2);
		set_delta(tmp[0]);
		io->send_block(tmp+1, 1);
		this->mitccrh.setS(tmp[1]);
	}
	void set_delta(const block & _delta) {
		delta = set_bit(_delta, 0);
		PRG().random_block(constant, 2);
		io->send_block(constant, 2);
		constant[1] = constant[1] ^ delta;
	}
	void and_gate(void * out, const void * left, const void * right) override {
		block table[2];
		block a = *((block *)left);
		block b = *((block *)right);
		block res = halfgates_garble(a, a^delta, b, b^delta, delta, table, &this->mitccrh);
		io->send_block(table, 2);
		*((block*)out) = res;
		this->ands++;
	}
};

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
class HalfGateEva: public HalfGate<T> { public:
	using HalfGate<T>::io;
	using HalfGate<T>::constant;
	HalfGateEva(T * io): HalfGate<T>(BOB, io) {
		set_delta();
		block tmp;
		io->recv_block(&tmp, 1);
		this->mitccrh.setS(tmp);
	}
	void set_delta() {
		io->recv_block(constant, 2);
	}
	void and_gate(void * out, const void * left, const void * right) override {
		block table[2];
		io->recv_block(table, 2);
		*((block*)out) =  halfgates_eval(*((block*)left), *((block*)right), table, &this->mitccrh);
		this->ands++;
	}
};
}
#endif// HALFGATE_H__
