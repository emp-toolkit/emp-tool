#ifndef HALFGATE_GEN_H__
#define HALFGATE_GEN_H__
#include "emp-tool/io/net_io_channel.h"
#include "emp-tool/io/file_io_channel.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/prp.h"
#include "emp-tool/utils/mitccrh.h"
#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/garble/garble_gate_halfgates.h"
#include <iostream>
namespace emp {
template<typename T, RTCktOpt rt = on>
class HalfGateGen:public CircuitExecution { public:
	int64_t gid = 0;
	block delta;
	block start_point;
	PRP prp;
	block seed;
	T * io;
	bool with_file_io = false;
	block fix_point;
	MITCCRH mitccrh;
	HalfGateGen(T * io) :io(io) {
		PRG prg(fix_key);prg.random_block(&fix_point, 1);
		PRG tmp;
		tmp.random_block(&seed, 1);
		block a;
		tmp.random_block(&a, 1);
		set_delta(a);
		tmp.random_block(&start_point, 1);
		io->send_block(&start_point, 1);
		mitccrh.setS(start_point);
	}
	bool is_public(const block & b, int party) {
		return isZero(&b) or isOne(&b);
	}
	void set_delta(const block &_delta) {
		this->delta = make_delta(_delta);
	}
	block public_label(bool b) override {
		return b? one_block() : zero_block();
	}
	bool isDelta(const block & b) {
		__m128i neq = _mm_xor_si128(b, delta);
		return _mm_testz_si128(neq, neq);
	}

	block and_gate(const block& a, const block& b) override {
		block out[2], table[2];
		if (isZero(&a) or isZero(&b)) {
			return zero_block();
		} else if (isOne(&a)) {
			return b;
		} else if (isOne(&b)){
			return a;
		} else {
			if(mitccrh.key_used == KS_BATCH_N) {
				mitccrh.renew_ks(gid);
			}
			garble_gate_garble_halfgates(a, xorBlocks(a,delta), b, xorBlocks(b,delta), 
					&out[0], &out[1], delta, table, &mitccrh);
			gid++;
			io->send_block(table, 2);
			return out[0];
		}
	}
	block xor_gate(const block&a, const block& b) override {
		if(isOne(&a))
			return not_gate(b);
		else if (isOne(&b))
			return not_gate(a);
		else if (isZero(&a))
			return b;
		else if (isZero(&b))
			return a;
		else {
			block res = xorBlocks(a, b);
			if (isZero(&res))
				return fix_point;
			if (isDelta(res))
				return xorBlocks(fix_point, delta);
			else
				return res;//xorBlocks(a, b);
		}
	}
	block not_gate(const block&a) override {
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
class HalfGateGen<T,RTCktOpt::off>:public CircuitExecution {
public:
	int64_t gid = 0;
	block delta;
	block start_point;
	PRP prp;
	block seed;
	T * io;
	bool with_file_io = false;
	block constant[2];
	MITCCRH mitccrh;
	HalfGateGen(T * io) :io(io) {
		PRG tmp;
		tmp.random_block(&seed, 1);
		block a;
		tmp.random_block(&a, 1);
		set_delta(a);
		tmp.random_block(&start_point, 1);
		io->send_block(&start_point, 1);
		mitccrh.setS(start_point);
	}
	bool is_public(const block & b, int party) {
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
	block and_gate(const block& a, const block& b) override {
		block out[2], table[2];
		if(mitccrh.key_used == KS_BATCH_N) {
			mitccrh.renew_ks(gid);
		}
		garble_gate_garble_halfgates(a, xorBlocks(a,delta), b, xorBlocks(b,delta), 
				&out[0], &out[1], delta, table, &mitccrh);
		gid++;
		io->send_block(table, 2);
		return out[0];
	}
	block xor_gate(const block&a, const block& b) override {
		return xorBlocks(a, b);
	}
	block not_gate(const block&a) override {
		return xor_gate(a, public_label(true));
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
}
#endif// HALFGATE_GEN_H__
