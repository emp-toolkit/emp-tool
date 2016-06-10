#ifndef PRIVACY_FREE_GEN_H__
#define PRIVACY_FREE_GEN_H__
#include "io_channel.h"
#include "net_io_channel.h"
#include "block.h"
#include "utils.h"
#include "prp.h"
#include "hash.h"
#include "garble/garble_gate_privacy_free.h"
#include <iostream>

template<typename T>
bool privacy_free_gen_is_public(GarbleCircuit* gc, const block & b, int party);

template<typename T>
block privacy_free_gen_public_label(GarbleCircuit* gc, bool b);

template<typename T>
block privacy_free_gen_and(GarbleCircuit* gc, const block&a, const block&b);

template<typename T>
block privacy_free_gen_xor(GarbleCircuit*gc, const block&a, const block&b);

template<typename T>
block privacy_free_gen_not(GarbleCircuit*gc, const block&a);

template<typename T>
class PrivacyFreeGen: public GarbleCircuit{ public:
	block delta;
	PRP prp;
	T * io;
	block constant[2];
	PrivacyFreeGen(T * io) :io(io) {
		PRG tmp;
		block a;
		tmp.random_block(&a, 1);
		set_delta(a);
		is_public_ptr = &privacy_free_gen_is_public<T>;
		public_label_ptr = &privacy_free_gen_public_label<T>;
		gc_and_ptr = &privacy_free_gen_and<T>;
		gc_xor_ptr = &privacy_free_gen_xor<T>;
		gc_not_ptr = &privacy_free_gen_not<T>;
	}
	bool is_public_impl(const block & b, int party) {
		return false;
	}
	bool isDelta(const block & b) {
		__m128i neq = _mm_xor_si128(b, delta);
		return _mm_testz_si128(neq, neq);
	}
	void set_delta(const block &_delta) {
		this->delta = make_delta(_delta);
		PRG prg2(fix_key);prg2.random_block(constant, 2);
		*((char *) &constant[0]) &= 0xfe;
		*((char *) &constant[1]) |= 0x01;
		constant[1] = xorBlocks(constant[1], delta);
	}
	block public_label_impl(bool b) {
		return constant[b];
	}
	block gen_and(const block& a, const block& b) {
		block out[2], table[2];
		garble_gate_garble_privacy_free(GARBLE_GATE_AND, a, xorBlocks(a,delta), b, xorBlocks(b,delta), 
				&out[0], &out[1], delta, table, gid++, prp.aes);
		io->send_block(table, 1);
		return out[0];
	}
	block gen_xor(const block&a, const block& b) {
		return xorBlocks(a, b);
	}
	block gen_not(const block& a) {
		return gen_xor(a, public_label_impl(true));
	}
	void privacy_free_to_xor(const block* new_b0,const block * b0, const block * b1, int length){
		block h[2];
		for(int i = 0; i < length; ++i) {
			h[0] = prp.H(b0[i], i);
			h[1] = prp.H(b1[i], i);
			h[0] = xorBlocks(new_b0[i], h[0]);	
			h[1] = xorBlocks(new_b0[i], h[1]);	
			h[1] = xorBlocks(delta, h[1]);
			io->send_block(h, 2);
		}
	}
};
template<typename T>
bool privacy_free_gen_is_public(GarbleCircuit* gc, const block & b, int party) {
	return ((PrivacyFreeGen<T>*)gc)->is_public_impl(b, party);
}
template<typename T>
block privacy_free_gen_public_label(GarbleCircuit* gc, bool b) {
	return ((PrivacyFreeGen<T>*)gc)->public_label_impl(b);
}
template<typename T>
block privacy_free_gen_and(GarbleCircuit* gc, const block&a, const block&b) {
	return ((PrivacyFreeGen<T>*)gc)->gen_and(a, b);
}
template<typename T>
block privacy_free_gen_xor(GarbleCircuit*gc, const block&a, const block&b) {
	return ((PrivacyFreeGen<T>*)gc)->gen_xor(a, b);
}

template<typename T>
block privacy_free_gen_not(GarbleCircuit*gc, const block&a) {
	return ((PrivacyFreeGen<T>*)gc)->gen_not(a);
}
#endif// PRIVACY_FREE_GEN_H__
