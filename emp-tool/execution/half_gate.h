#ifndef EMP_HALF_GATE_H__
#define EMP_HALF_GATE_H__

#include "emp-tool/core/utils.h"
#include "emp-tool/crypto/mitccrh.h"
#include "emp-tool/execution/backend.h"

namespace emp {

// Half-gate garbling [Zahur-Rosulek-Evans 2014, eprint 2014/756].
// The base HalfGate<T> implements the protocol-agnostic gate-evaluation
// surface (xor / not / public_label) that's identical between gen and eva,
// plus a shared MITCCRH for the H-call. Concrete HalfGateGen<T> and
// HalfGateEva<T> override `and_gate` with the garbling / evaluation
// asymmetry. `feed` and `reveal` are stubbed at this layer because they
// require OT, which lives in emp-ot — the OT-aware subclass there
// overrides them.

inline block halfgates_garble(block LA0, block A1, block LB0, block B1,
                              block delta, block* table, MITCCRH<8>* mitccrh) {
	bool pa = getLSB(LA0);
	bool pb = getLSB(LB0);
	block H[4] = {LA0, A1, LB0, B1};
	mitccrh->hash_cir<2, 2>(H);
	block HLA0 = H[0], HA1 = H[1], HLB0 = H[2], HB1 = H[3];

	table[0] = HLA0 ^ HA1 ^ (select_mask[pb] & delta);
	block W0 = HLA0 ^ (select_mask[pa] & table[0]);
	block tmp = HLB0 ^ HB1;
	table[1] = tmp ^ LA0;
	W0 ^= HLB0 ^ (select_mask[pb] & tmp);
	return W0;
}

inline block halfgates_eval(block A, block B, const block* table,
                            MITCCRH<8>* mitccrh) {
	int sa = getLSB(A);
	int sb = getLSB(B);
	block H[2] = {A, B};
	mitccrh->hash_cir<2, 1>(H);
	block W = H[0] ^ H[1];
	W ^= (select_mask[sa] & table[0]);
	W ^= (select_mask[sb] & table[1]);
	W ^= (select_mask[sb] & A);
	return W;
}

template <class T>
class HalfGate : public Backend {
public:
	T* io;
	block constant[2];
	MITCCRH<8> mitccrh;

	HalfGate(int party_, T* io_) : Backend(party_), io(io_) {}

	size_t wire_bytes() const override { return sizeof(block); }

	void public_label(void* out, bool b) override {
		*static_cast<block*>(out) = constant[b];
	}

	void xor_gate(void* out, const void* l, const void* r) override {
		*static_cast<block*>(out) =
		    *static_cast<const block*>(l) ^ *static_cast<const block*>(r);
	}

	void not_gate(void* out, const void* in) override {
		*static_cast<block*>(out) =
		    *static_cast<const block*>(in) ^ constant[1];
	}

	uint64_t num_and() override { return mitccrh.gid / 2; }
};

template <class T>
class HalfGateGen : public HalfGate<T> {
public:
	using HalfGate<T>::io;
	using HalfGate<T>::constant;
	using HalfGate<T>::mitccrh;

	block delta;

	explicit HalfGateGen(T* io_) : HalfGate<T>(ALICE, io_) {
		block tmp[2];
		PRG().random_block(tmp, 2);
		delta = set_bit(tmp[0], 0);
		PRG().random_block(constant, 2);
		io->send_block(constant, 2);
		constant[1] = constant[1] ^ delta;
		io->send_block(tmp + 1, 1);
		mitccrh.setS(tmp[1]);
	}

	void and_gate(void* out, const void* l, const void* r) override {
		block table[2];
		block a = *static_cast<const block*>(l);
		block b = *static_cast<const block*>(r);
		block res = halfgates_garble(a, a ^ delta, b, b ^ delta, delta,
		                              table, &mitccrh);
		io->send_block(table, 2);
		*static_cast<block*>(out) = res;
	}
};

template <class T>
class HalfGateEva : public HalfGate<T> {
public:
	using HalfGate<T>::io;
	using HalfGate<T>::constant;
	using HalfGate<T>::mitccrh;

	explicit HalfGateEva(T* io_) : HalfGate<T>(BOB, io_) {
		io->recv_block(constant, 2);
		block tmp;
		io->recv_block(&tmp, 1);
		mitccrh.setS(tmp);
	}

	void and_gate(void* out, const void* l, const void* r) override {
		block table[2];
		io->recv_block(table, 2);
		*static_cast<block*>(out) = halfgates_eval(
		    *static_cast<const block*>(l),
		    *static_cast<const block*>(r),
		    table, &mitccrh);
	}
};

}  // namespace emp
#endif
