#ifndef PRIVACY_FREE_EVA_H__
#define PRIVACY_FREE_EVA_H__
#include "io_channel.h"
#include "net_io_channel.h"
#include "file_io_channel.h"
#include "block.h"
#include "utils.h"
#include "prp.h"
#include "hash.h"
#include "garble/garble_gate_privacy_free.h"
#include <iostream>

template<typename T>
bool privacy_free_eva_is_public(GarbleCircuit* gc, const block & b, int party);

template<typename T>
block privacy_free_eva_public_label(GarbleCircuit* gc, bool b);

template<typename T>
block privacy_free_eva_and(GarbleCircuit* gc, const block&a, const block&b);

template<typename T>
block privacy_free_eva_xor(GarbleCircuit*gc, const block&a, const block&b);

template<typename T>
block privacy_free_eva_not(GarbleCircuit*gc, const block&a);
template<typename T>
class PrivacyFreeEva:public GarbleCircuit{ public:
	PRP prp;
	T * io;
	block constant[2];
	PrivacyFreeEva(T * io) :io(io) {
		is_public_ptr = &privacy_free_eva_is_public<T>;
		public_label_ptr = &privacy_free_eva_public_label<T>;
		gc_and_ptr = &privacy_free_eva_and<T>;
		gc_xor_ptr = &privacy_free_eva_xor<T>;
		gc_not_ptr = &privacy_free_eva_not<T>;
		PRG prg2(fix_key);prg2.random_block(constant, 2);
		 *((char *) &constant[0]) &= 0xfe;
       *((char *) &constant[1]) |= 0x01;
	}
	bool is_public_impl(const block & b, int party) {
		return false;
	}
	block public_label_impl(bool b) {
		return constant[b];
	}
	block and_gate(const block& a, const block& b) {
		block out[2], table[1];
		io->recv_block(table, 1);
		garble_gate_eval_privacy_free(GARBLE_GATE_AND, a, b, out, table, gid++, prp.aes);
		return out[0];
	}
	block xor_gate(const block& a, const block& b) {
		return xorBlocks(a,b);
	}
	block not_gate(const block& a) {
		return xor_gate(a, public_label_impl(true));
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
template<typename T>
bool privacy_free_eva_is_public(GarbleCircuit* gc, const block & b, int party) {
	return ((PrivacyFreeEva<T>*)gc)->is_public_impl(b, party);
}
template<typename T>
block privacy_free_eva_public_label(GarbleCircuit* gc, bool b) {
	return ((PrivacyFreeEva<T>*)gc)->public_label_impl(b);
}
template<typename T>
block privacy_free_eva_and(GarbleCircuit* gc, const block&a, const block&b) {
	return ((PrivacyFreeEva<T>*)gc)->and_gate(a, b);
}
template<typename T>
block privacy_free_eva_xor(GarbleCircuit*gc, const block&a, const block&b) {
	return ((PrivacyFreeEva<T>*)gc)->xor_gate(a, b);
}

template<typename T>
block privacy_free_eva_not(GarbleCircuit*gc, const block&a) {
	return ((PrivacyFreeEva<T>*)gc)->not_gate(a);
}
#endif// PRIVACY_FREE_EVA_H__
