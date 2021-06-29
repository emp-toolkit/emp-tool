#ifndef EMP_PRIVACY_FREE_EVA_H__
#define EMP_PRIVACY_FREE_EVA_H__
#include "emp-tool/io/io_channel.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/prp.h"
#include "emp-tool/execution/circuit_execution.h"
#include <iostream>
namespace emp {

inline block privacy_free_eval(block A, block B, const block &table, uint64_t idx, 
	const AES_KEY *key) {
	block HA, W;
	bool sa;
	block tweak;

	sa = getLSB(A);

	tweak = makeBlock(2 * idx, (uint64_t) 0);

	{
		block tmp, mask;

		tmp = sigma(A) ^ tweak;
		mask = tmp;
		AES_ecb_encrypt_blks(&tmp, 1, key);
		HA = tmp ^ mask;
	}
	if (sa) {
		*((char *) &HA) |= 0x01;
		W = HA ^ table[0];
		W = W ^ B;
	} else {
		*((char *) &HA) &= 0xfe;
		W = HA;
	}
	return W;
}

template<typename T>
class PrivacyFreeEva:public CircuitExecution{ public:
	PRP prp;
	T * io;
	block constant[2];
	int64_t gid = 0;
	PrivacyFreeEva(T * io) :io(io) {
		set_delta();
	}
	void set_delta() {
		io->recv_block(constant, 2);
	}
	bool is_public(const block & b, int party) {
		return false;
	}
	block public_label(bool b) {
		return constant[b];
	}
	block and_gate(const block& a, const block& b) {
		block table;
		io->recv_block(&table, 1);
		return privacy_free_eval(a, b, table, gid++, &prp.aes);
	}
	block xor_gate(const block& a, const block& b) {
		return a^b;
	}
	block not_gate(const block& a) {
		return a ^ public_label(true);
	}
	uint64_t num_and() override {
		return gid;
	}
};
}
#endif// PRIVACY_FREE_EVA_H__
