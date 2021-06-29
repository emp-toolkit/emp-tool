#ifndef EMP_PRIVACY_FREE_GEN_H__
#define EMP_PRIVACY_FREE_GEN_H__
#include "emp-tool/io/io_channel.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/prp.h"
#include "emp-tool/execution/circuit_execution.h"
#include <iostream>
namespace emp {

inline block privacy_free_garble(block LA0, block A1,
		block LB0, block B1, block delta, block *table, 
		uint64_t idx, const AES_KEY *key) {

	block tweak, tmp;
	block HLA0, HA1;

	tweak = makeBlock(2 * idx, (long) 0);

	{
		block masks[2], keys[2];

		keys[0] = sigma(LA0) ^ tweak;
		keys[1] = sigma(A1) ^ tweak;
		memcpy(masks, keys, sizeof keys);
		AES_ecb_encrypt_blks(keys, 2, key);
		HLA0 = keys[0] ^ masks[0];
		HA1 = keys[1] ^ masks[1];
	}
	*((char *) &HLA0) &= 0xfe;
	*((char *) &HA1) |= 0x01;
	tmp = HLA0 ^ HA1;
	table[0] = tmp ^ LB0;
	return HLA0;
}

template<typename T>
class PrivacyFreeGen: public CircuitExecution{ 
public:
	block delta;
	PRP prp;
	T * io;
	block constant[2];
	int64_t gid = 0;
	PrivacyFreeGen(T * io) :io(io) {
		PRG tmp;
		block a;
		tmp.random_block(&a, 1);
		set_delta(a);
	}
	bool is_public(const block & b, int party) {
		return false;
	}
	bool isDelta(const block & b) {
		__m128i neq = _mm_xor_si128(b, delta);
		return _mm_testz_si128(neq, neq);
	}
	void set_delta(const block &_delta) {
		this->delta = set_bit(_delta, 0);
		PRG().random_block(constant, 2);
		*((char *) &constant[0]) &= 0xfe;
		*((char *) &constant[1]) |= 0x01;
		io->send_block(constant, 2);
		constant[1] = constant[1] ^ delta;
	}
	block public_label(bool b) {
		return constant[b];
	}
	block and_gate(const block& a, const block& b) {
		block table[1];
		block res = privacy_free_garble(a, a ^ delta, b, b ^ delta, 
				delta, table, gid++, &prp.aes);
		io->send_block(table, 1);
		return res;
	}
	block xor_gate(const block&a, const block& b) {
		return a ^ b;
	}
	block not_gate(const block& a) {
		return a ^ public_label(true);
	}
	uint64_t num_and() override {
		return gid;
	}
};
}
#endif// PRIVACY_FREE_GEN_H__
