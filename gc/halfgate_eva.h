#ifndef HALFGATE_EVA_H__
#define HALFGATE_EVA_H__
#include "emp-tool/io/io_channel.h"
#include "emp-tool/io/net_io_channel.h"
#include "emp-tool/io/file_io_channel.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/prp.h"
#include "emp-tool/utils/hash.h"
#include "emp-tool/garble/garble_gate_halfgates.h"
#include <iostream>

//template<typename T, RTCktOpt rt>
//bool halfgate_eva_is_public(GarbleCircuit* gc, const block & b, EmpParty party);
//
//template<typename T, RTCktOpt rt>
//block halfgate_eva_public_label(GarbleCircuit* gc, bool b);
//
//template<typename T, RTCktOpt rt>
//block halfgate_eva_and(GarbleCircuit* gc, const block&a, const block&b);
//
//template<typename T, RTCktOpt rt>
//block halfgate_eva_xor(GarbleCircuit*gc, const block&a, const block&b);
//
//template<typename T, RTCktOpt rt>
//block halfgate_eva_not(GarbleCircuit*gc, const block&a);

template<typename T, RTCktOpt rt = on>
class HalfGateEva:public GarbleCircuit{ public:
	PRP prp;
	T * io;
	Hash hash;
	bool with_file_io = false;
	FileIO * fio;
	HalfGateEva(T * io) :io(io) {
		//is_public_ptr = &halfgate_eva_is_public<T, rt>;
		//public_label_ptr = &halfgate_eva_public_label<T, rt>;
		//gc_and_ptr = &halfgate_eva_and<T, rt>;
		//gc_xor_ptr = &halfgate_eva_xor<T, rt>;
		//gc_not_ptr = &halfgate_eva_not<T, rt>;
	}
	void set_file_io(FileIO * fio) {
		with_file_io = true;
		this->fio = fio;
	}
	bool is_public(const block & b, EmpParty party) override {
		return isZero(&b) || isOne(&b);
	}
	block public_label(bool b) override {
		return b? one_block() : zero_block();
	}
	block gc_and(const block& a, const block& b) override {
		block out, table[2];
		if (isZero(&a) || isOne(&a) || isZero(&b) || isOne(&b)) {
			return _mm_and_si128(a, b);
		} else {
#ifdef INSECURE_DEBUG
            std::array<block, 3> buff;
            io->recv_block(buff.data(), buff.size());
            if (neq(a, buff[0]) && neq(a, buff[0] ^ buff[2]))
            {
                std::cout << "gid " << gid << " a = " << a << "!\in { " << buff[0] << ", " << (buff[0] ^ buff[2]) << "} " << std::endl;
                throw std::runtime_error("bad 'a' var " LOCATION);
            }
            if (neq(b, buff[1]) && neq(b, buff[1] ^ buff[2]))
            {
                std::cout << "gid " << gid << " b = " << a << "!\in { " << buff[0] << ", " << (buff[0] ^ buff[2]) << "} " << std::endl;
                throw std::runtime_error("bad 'b' var " LOCATION);
            }
#endif

			io->recv_block(table, 2);
			if(with_file_io) {
				fio->send_block(table, 2);
				return prp.H(a, gid++);
			}
            //std::cout << oc::IoStream::lock << "eva_and " << gid << std::endl << oc::IoStream::unlock;

			garble_gate_eval_halfgates(GARBLE_GATE_AND, a, b, &out, table, gid++, prp.aes);
			return out;
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
                char c;
                io->recv_data(&c, 1);
                if (c) return one_block();
                else return zero_block();
            }
            else
			    return xorBlocks(a, b);
        }
	}
	block gc_not(const block&a) override {
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
		//is_public_ptr = &halfgate_eva_is_public<T, RTCktOpt::off>;
		//public_label_ptr = &halfgate_eva_public_label<T, RTCktOpt::off>;
		//gc_and_ptr = &halfgate_eva_and<T, RTCktOpt::off>;
		//gc_xor_ptr = &halfgate_eva_xor<T, RTCktOpt::off>;
		//gc_not_ptr = &halfgate_eva_not<T, RTCktOpt::off>;
		PRG prg2(fix_key);prg2.random_block(constant, 2);
	}
	void set_file_io(FileIO * fio) {
		with_file_io = true;
		this->fio = fio;
	}
	bool is_public(const block & b, EmpParty party) override {
		return false;
	}
	block public_label(bool b) override {
		return constant[b];
//		return b? one_block() : zero_block();
	}
	block gc_and(const block& a, const block& b) override {
		block out, table[2];
		io->recv_block(table, 2);
		if(with_file_io) {
			fio->send_block(table, 2);
			return prp.H(a, gid++);
		}

		garble_gate_eval_halfgates(GARBLE_GATE_AND, a, b, &out, table, gid++, prp.aes);
		return out;
	}
	block gc_xor(const block& a, const block& b) override {
		return xorBlocks(a,b);
	}
	block gc_not(const block&a) override {
		return gc_xor(a, public_label(true));
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

//template<typename T, RTCktOpt rt>
//bool halfgate_eva_is_public(GarbleCircuit* gc, const block & b, EmpParty party) {
//	return ((HalfGateEva<T,rt>*)gc)->is_public_impl(b, party);
//}
//template<typename T, RTCktOpt rt>
//block halfgate_eva_public_label(GarbleCircuit* gc, bool b) {
//	return ((HalfGateEva<T,rt>*)gc)->public_label_impl(b);
//}
//template<typename T, RTCktOpt rt>
//block halfgate_eva_and(GarbleCircuit* gc, const block&a, const block&b) {
//	return ((HalfGateEva<T,rt>*)gc)->and_gate(a, b);
//}
//template<typename T, RTCktOpt rt>
//block halfgate_eva_xor(GarbleCircuit*gc, const block&a, const block&b) {
//	return ((HalfGateEva<T,rt>*)gc)->xor_gate(a, b);
//}
//template<typename T, RTCktOpt rt>
//block halfgate_eva_not(GarbleCircuit*gc, const block&a) {
//	return ((HalfGateEva<T,rt>*)gc)->not_gate(a);
//}

#endif// HALFGATE_EVA_H__
