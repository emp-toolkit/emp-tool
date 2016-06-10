#ifndef HALFGATE_EVA_H__
#define HALFGATE_EVA_H__
#include "io_channel.h"
#include "net_io_channel.h"
#include "file_io_channel.h"
#include "block.h"
#include "utils.h"
#include "prp.h"
#include "hash.h"
#include "garble/garble_gate_halfgates.h"
#include <iostream>

template<typename T, RTCktOpt rt>
bool halfgate_eva_is_public(GarbleCircuit* gc, const block & b, int party);

template<typename T, RTCktOpt rt>
block halfgate_eva_public_label(GarbleCircuit* gc, bool b);

template<typename T, RTCktOpt rt>
block halfgate_eva_and(GarbleCircuit* gc, const block&a, const block&b);

template<typename T, RTCktOpt rt>
block halfgate_eva_xor(GarbleCircuit*gc, const block&a, const block&b);

template<typename T, RTCktOpt rt>
block halfgate_eva_not(GarbleCircuit*gc, const block&a);

template<typename T, RTCktOpt rt = on>
class HalfGateEva:public GarbleCircuit{ public:
	PRP prp;
	T * io;
	Hash hash;
	bool with_file_io = false;
	FileIO * fio;
	HalfGateEva(T * io) :io(io) {
		is_public_ptr = &halfgate_eva_is_public<T, rt>;
		public_label_ptr = &halfgate_eva_public_label<T, rt>;
		gc_and_ptr = &halfgate_eva_and<T, rt>;
		gc_xor_ptr = &halfgate_eva_xor<T, rt>;
		gc_not_ptr = &halfgate_eva_not<T, rt>;
	}
	void set_file_io(FileIO * fio) {
		with_file_io = true;
		this->fio = fio;
	}
	bool is_public_impl(const block & b, int party) {
		return isZero(&b) or isOne(&b);
	}
	block public_label_impl(bool b) {
		return b? one_block() : zero_block();
	}
	block and_gate(const block& a, const block& b) {
		block out, table[2];
		if (isZero(&a) or isOne(&a) or isZero(&b) or isOne(&b)) {
			return _mm_and_si128(a, b);
		} else {
			io->recv_block(table, 2);
			if(with_file_io) {
				fio->send_block(table, 2);
				return prp.H(a, gid++);
			}
			garble_gate_eval_halfgates(GARBLE_GATE_AND, a, b, &out, table, gid++, prp.aes);
			return out;
		}
	}
	block xor_gate(const block&a, const block& b) {
		if(isOne(&a))
			return not_gate(b);
		else if (isOne(&b))
			return not_gate(a);
		else
			return xorBlocks(a, b);
	}
	block not_gate(const block&a) {
		if (isZero(&a))
			return one_block();
		else if (isOne(&a))
			return zero_block();
		else
			return a;
	}
	void generic_to_xor(block* new_block, const block * old_block, int length) {
		block h[4], t;
		for(int i = 0; i < length; ++i) {
			io->recv_block(h, 4);
			t = prp.H(old_block[i], 2*i);
			if(block_cmp(&t, &h[0], 1)) {
				new_block[i] = xorBlocks(h[1], prp.H(old_block[i], 2*i+1));
			} else {
				new_block[i] = xorBlocks(h[3], prp.H(old_block[i], 2*i+1));
			}
		}
	}
};
template<typename T>
class HalfGateEva<T,RTCktOpt::off>:public GarbleCircuit{ public:
	PRP prp;
	T * io;
	Hash hash;
	bool with_file_io = false;
	FileIO * fio;
	block constant[2];
	HalfGateEva(T * io) :io(io) {
		is_public_ptr = &halfgate_eva_is_public<T, RTCktOpt::off>;
		public_label_ptr = &halfgate_eva_public_label<T, RTCktOpt::off>;
		gc_and_ptr = &halfgate_eva_and<T, RTCktOpt::off>;
		gc_xor_ptr = &halfgate_eva_xor<T, RTCktOpt::off>;
		gc_not_ptr = &halfgate_eva_not<T, RTCktOpt::off>;
		PRG prg2(fix_key);prg2.random_block(constant, 2);
	}
	void set_file_io(FileIO * fio) {
		with_file_io = true;
		this->fio = fio;
	}
	bool is_public_impl(const block & b, int party) {
		return false;
	}
	block public_label_impl(bool b) {
		return constant[b];
//		return b? one_block() : zero_block();
	}
	block and_gate(const block& a, const block& b) {
		block out, table[2];
		io->recv_block(table, 2);
		if(with_file_io) {
			fio->send_block(table, 2);
			return prp.H(a, gid++);
		}
		garble_gate_eval_halfgates(GARBLE_GATE_AND, a, b, &out, table, gid++, prp.aes);
		return out;
	}
	block xor_gate(const block& a, const block& b) {
		return xorBlocks(a,b);
	}
	block not_gate(const block&a){
		return xor_gate(a, public_label_impl(true));
	}
	void generic_to_xor(block* new_block, const block * old_block, int length) {
		block h[4], t;
		for(int i = 0; i < length; ++i) {
			io->recv_block(h, 4);
			t = prp.H(old_block[i], 2*i);
			if(block_cmp(&t, &h[0], 1)) {
				new_block[i] = xorBlocks(h[1], prp.H(old_block[i], 2*i+1));
			} else {
				new_block[i] = xorBlocks(h[3], prp.H(old_block[i], 2*i+1));
			}
		}
	}
};

template<typename T, RTCktOpt rt>
bool halfgate_eva_is_public(GarbleCircuit* gc, const block & b, int party) {
	return ((HalfGateEva<T,rt>*)gc)->is_public_impl(b, party);
}
template<typename T, RTCktOpt rt>
block halfgate_eva_public_label(GarbleCircuit* gc, bool b) {
	return ((HalfGateEva<T,rt>*)gc)->public_label_impl(b);
}
template<typename T, RTCktOpt rt>
block halfgate_eva_and(GarbleCircuit* gc, const block&a, const block&b) {
	return ((HalfGateEva<T,rt>*)gc)->and_gate(a, b);
}
template<typename T, RTCktOpt rt>
block halfgate_eva_xor(GarbleCircuit*gc, const block&a, const block&b) {
	return ((HalfGateEva<T,rt>*)gc)->xor_gate(a, b);
}
template<typename T, RTCktOpt rt>
block halfgate_eva_not(GarbleCircuit*gc, const block&a) {
	return ((HalfGateEva<T,rt>*)gc)->not_gate(a);
}

#endif// HALFGATE_EVA_H__
