#ifndef PRIVACY_FREE_EVA_H__
#define PRIVACY_FREE_EVA_H__
#include "emp-tool/io/io_channel.h"
#include "emp-tool/io/net_io_channel.h"
#include "emp-tool/io/file_io_channel.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/prp.h"
#include "emp-tool/utils/hash.h"
#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/garble/garble_gate_privacy_free.h"
#include <iostream>
namespace emp {
template<typename T>
class PrivacyFreeEva:public CircuitExecution{ public:
	PRP prp;
	T * io;
	block constant[2];
	int64_t gid = 0;
	PrivacyFreeEva(T * io) :io(io) {
		PRG prg2(fix_key);prg2.random_block(constant, 2);
		 *((char *) &constant[0]) &= 0xfe;
       *((char *) &constant[1]) |= 0x01;
	}
	bool is_public(const block & b, int party) {
		return false;
	}
	block public_label(bool b) {
		return constant[b];
	}
	block and_gate(const block& a, const block& b) {
		block out[2], table[1];
		io->recv_block(table, 1);
		garble_gate_eval_privacy_free(a, b, out, table, gid++, &prp.aes);
		return out[0];
	}
	block xor_gate(const block& a, const block& b) {
		return xorBlocks(a,b);
	}
	block not_gate(const block& a) {
		return xor_gate(a, public_label(true));
	}
	void privacy_free_to_xor(block* new_block, const block * old_block, const bool* b, int length){
		block h[2];
		for(int i = 0; i < length; ++i) {
			io->recv_block(h, 2);
			if(!b[i]){
				new_block[i] = xorBlocks(h[0], prp.H(old_block[i], i));
			} else {
				new_block[i] = xorBlocks(h[1], prp.H(old_block[i], i));
			}
		}
	}
};
}
#endif// PRIVACY_FREE_EVA_H__
