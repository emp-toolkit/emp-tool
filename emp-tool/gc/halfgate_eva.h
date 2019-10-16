#ifndef HALFGATE_EVA_H__
#define HALFGATE_EVA_H__
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
class HalfGateEva:public CircuitExecution{ public:
	int64_t gid = 0;
	block start_point;
	PRP prp;
	T * io;
	bool with_file_io = false;
	FileIO * fio;
	block fix_point;
	MITCCRH mitccrh;
	HalfGateEva(T * io) :io(io) {
		PRG prg(fix_key);prg.random_block(&fix_point, 1);
		io->recv_block(&start_point, 1);
		mitccrh.setS(start_point);
	}
	void set_file_io(FileIO * fio) {
		with_file_io = true;
		this->fio = fio;
	}
	bool is_public(const block & b, int party) {
		return isZero(&b) or isOne(&b);
	}
	block public_label(bool b) override {
		return b? one_block() : zero_block();
	}
	block and_gate(const block& a, const block& b) override {
		block out, table[2];
		if (isZero(&a) or isOne(&a) or isZero(&b) or isOne(&b)) {
			return _mm_and_si128(a, b);
		} else {
			io->recv_block(table, 2);
			if(with_file_io) {
				fio->send_block(table, 2);
				return prp.H(a, gid++);
			}
			if(mitccrh.key_used == KS_BATCH_N) {
				mitccrh.renew_ks(gid);
			}
			garble_gate_eval_halfgates(a, b, &out, table, &mitccrh);
			gid++;
			return out;
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
			else return res;
//			return xorBlocks(a, b);
		}
	}
	block not_gate(const block&a) override {
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
class HalfGateEva<T,RTCktOpt::off>:public CircuitExecution {
public:
	int64_t gid = 0;
	block start_point;
	PRP prp;
	T * io;
	bool with_file_io = false;
	FileIO * fio;
	block constant[2];
	MITCCRH mitccrh;
	HalfGateEva(T * io) :io(io) {
		PRG prg2(fix_key);prg2.random_block(constant, 2);
		prg2.random_block(&start_point, 1);
		mitccrh.start_point = start_point;
		io->recv_block(&start_point, 1);
		mitccrh.setS(start_point);
	}
	void set_file_io(FileIO * fio) {
		with_file_io = true;
		this->fio = fio;
	}
	bool is_public(const block & b, int party) {
		return false;
	}
	block public_label(bool b) override {
		return constant[b];
//		return b? one_block() : zero_block();
	}
	block and_gate(const block& a, const block& b) override {
		block out, table[2];
		io->recv_block(table, 2);
		if(with_file_io) {
			fio->send_block(table, 2);
			return prp.H(a, gid++);
		}
		if(mitccrh.key_used == KS_BATCH_N) {
			mitccrh.renew_ks(gid);
		}
		garble_gate_eval_halfgates(a, b, &out, table, &mitccrh);
		return out;
	}
	block xor_gate(const block& a, const block& b) override {
		return xorBlocks(a,b);
	}
	block not_gate(const block&a) override {
		return xor_gate(a, public_label(true));
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
}
#endif// HALFGATE_EVA_H__
