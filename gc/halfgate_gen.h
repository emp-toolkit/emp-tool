#ifndef HALFGATE_GEN_H__
#define HALFGATE_GEN_H__
#include "emp-tool/io/io_channel.h"
#include "emp-tool/io/net_io_channel.h"
#include "emp-tool/io/file_io_channel.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/config.h"
#include "emp-tool/utils/prp.h"
#include "emp-tool/utils/hash.h"
#include "emp-tool/garble/garble_gate_halfgates.h"
#include <iostream>

//template<typename T, RTCktOpt rt>
//bool halfgate_gen_is_public(GarbleCircuit* gc, const block & b, EmpParty party);
//
//template<typename T, RTCktOpt rt>
//block halfgate_gen_public_label(GarbleCircuit* gc, bool b);
//
//template<typename T, RTCktOpt rt>
//block halfgate_gen_and(GarbleCircuit* gc, const block&a, const block&b);
//
//template<typename T, RTCktOpt rt>
//block halfgate_gen_xor(GarbleCircuit*gc, const block&a, const block&b);
//
//template<typename T, RTCktOpt rt>
//block halfgate_gen_not(GarbleCircuit*gc, const block&a);

template<typename T, RTCktOpt rt = on>
class HalfGateGen:public GarbleCircuit{ public:
	block delta;
	PRP prp;
	block seed;
	T * io;
	Hash hash;
	bool with_file_io = false;
	HalfGateGen(T * io, const block& ss) :io(io) {
		PRG tmp(ss);
		tmp.random_block(&seed, 1);
		block a;
		tmp.random_block(&a, 1);
		set_delta(a);
		//is_public_ptr = &halfgate_gen_is_public<T, rt>;
		//public_label_ptr = &halfgate_gen_public_label<T, rt>;
		//gc_and_ptr = &halfgate_gen_and<T, rt>;
		//gc_xor_ptr = &halfgate_gen_xor<T, rt>;
		//gc_not_ptr = &halfgate_gen_not<T, rt>;
	}
	bool is_public(const block & b, EmpParty party) override {
		return isZero(&b) || isOne(&b);
	}
	void set_delta(const block &_delta) {
		this->delta = make_delta(_delta);
	}
	block public_label(bool b) override {
		return b? one_block() : zero_block();
	}
	block gc_and(const block& a, const block& b) override {
		block out[2], table[2];
		if (isZero(&a) || isZero(&b)) {
			return zero_block();
		} else if (isOne(&a)) {
			return b;
		} else if (isOne(&b)){
			return a;
		} else {
            //std::cout << oc::IoStream::lock << "gen_and " << gid << std::endl << oc::IoStream::unlock;

#ifdef INSECURE_DEBUG
            std::array<block, 3> bb{ a,b,delta };
            io->send_block(bb.data(), bb.size());
#endif
			garble_gate_garble_halfgates(GARBLE_GATE_AND, a, xorBlocks(a,delta), b, xorBlocks(b,delta), 
					&out[0], &out[1], delta, table, gid++, prp.aes);
			io->send_block(table, 2);
			return out[0];
		}
	}
	block gc_xor(const block&a, const block& b) override {
		if(isOne(&a))
			return gc_not(b);
		else if (isOne(&b))
			return gc_not(a);
        else
        {
            if (eq(a, b))
            {
                char c = 0;
                io->send_data(&c, 1);
            }
            else if (eq(a, b ^ delta))
            {
                char c = 1;
                io->send_data(&c, 1);
            }

			return xorBlocks(a, b);
        }
	}

	block gc_not(const block&a) {
		if (isZero(&a))
			return one_block();
		else if (isOne(&a))
			return zero_block();
		else
			return xorBlocks(a,delta);
	}

	void generic_to_xor(const block* new_b0,const block * b0, const block * b1, int length) {
		block h[4];
		for(int i = 0; i < length; ++i) {
			h[0] = prp.H(b0[i], 2*i);
			h[1] = prp.H(b0[i], 2*i+1);
			h[2] = prp.H(b1[i], 2*i);
			h[3] = prp.H(b1[i], 2*i+1);

			h[1] = xorBlocks(new_b0[i], h[1]);	
			h[3] = xorBlocks(new_b0[i], h[3]);	
			h[3] = xorBlocks(delta, h[3]);
			io->send_block(h, 4);
		}
	}
};
template<typename T>
class HalfGateGen<T,RTCktOpt::off>:public GarbleCircuit{ public:
	block delta;
	PRP prp;
	block seed;
	T * io;
	Hash hash;
	bool with_file_io = false;
	block constant[2];
	HalfGateGen(T * io, const block& prgSeed) :io(io) {
		PRG tmp(prgSeed);
		tmp.random_block(&seed, 1);
		block a;
		tmp.random_block(&a, 1);
		//is_public_ptr = &halfgate_gen_is_public<T, RTCktOpt::off>;
		//public_label_ptr = &halfgate_gen_public_label<T, RTCktOpt::off>;
		//gc_and_ptr = &halfgate_gen_and<T, RTCktOpt::off>;
		//gc_xor_ptr = &halfgate_gen_xor<T, RTCktOpt::off>;
		//gc_not_ptr = &halfgate_gen_not<T, RTCktOpt::off>;
		set_delta(a);
	}
	bool is_public(const block & b, EmpParty party) override {
		return false;
	}
	bool isDelta(const block & b) {
		__m128i neq = _mm_xor_si128(b, delta);
		return _mm_testz_si128(neq, neq);
	}
	void set_delta(const block &_delta) {
		this->delta = make_delta(_delta);
		PRG prg2(fix_key);prg2.random_block(constant, 2);
		constant[1] = xorBlocks(constant[1],delta);
	}
	block public_label(bool b) override {
		return constant[b];
	}
	block gc_and(const block& a, const block& b) override {
		block out[2], table[2];
		garble_gate_garble_halfgates(GARBLE_GATE_AND, a, xorBlocks(a,delta), b, xorBlocks(b,delta), 
				&out[0], &out[1], delta, table, gid++, prp.aes);
		io->send_block(table, 2);
		return out[0];
	}
	block gc_xor(const block&a, const block& b) override {
		return xorBlocks(a, b);
	}
	block gc_not(const block&a) override {
		return gc_xor(a, public_label(true));
	}
	void generic_to_xor(const block* new_b0,const block * b0, const block * b1, int length) {
		block h[4];
		for(int i = 0; i < length; ++i) {
			h[0] = prp.H(b0[i], 2*i);
			h[1] = prp.H(b0[i], 2*i+1);
			h[2] = prp.H(b1[i], 2*i);
			h[3] = prp.H(b1[i], 2*i+1);

			h[1] = xorBlocks(new_b0[i], h[1]);	
			h[3] = xorBlocks(new_b0[i], h[3]);	
			h[3] = xorBlocks(delta, h[3]);
			io->send_block(h, 4);
		}
	}
};

//template<typename T, RTCktOpt rt>
//bool halfgate_gen_is_public(GarbleCircuit* gc, const block & b, EmpParty party) {
//	return ((HalfGateGen<T, rt>*)gc)->is_public_impl(b, party);
//}
//template<typename T, RTCktOpt rt>
//block halfgate_gen_public_label(GarbleCircuit* gc, bool b) {
//	return ((HalfGateGen<T,rt>*)gc)->public_label_impl(b);
//}
//template<typename T, RTCktOpt rt>
//block halfgate_gen_and(GarbleCircuit* gc, const block&a, const block&b) {
//	return ((HalfGateGen<T,rt>*)gc)->gen_and(a, b);
//}
//template<typename T, RTCktOpt rt>
//block halfgate_gen_xor(GarbleCircuit*gc, const block&a, const block&b) {
//	return ((HalfGateGen<T, rt>*)gc)->gen_xor(a, b);
//}
//template<typename T, RTCktOpt rt>
//block halfgate_gen_not(GarbleCircuit*gc, const block&a) {
//	return ((HalfGateGen<T, rt>*)gc)->gen_not(a);
//}

#endif// HALFGATE_GEN_H__
