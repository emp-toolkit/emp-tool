#ifndef EMP_PLAIN_CIRC_EXEC_H__
#define EMP_PLAIN_CIRC_EXEC_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/execution/circuit_execution.h"
#include <iostream>
#include <fstream>

namespace emp {
class PlainCircExec: public CircuitExecution {
public:
	const static unsigned long long P1 = 1;
	const static unsigned long long P0 = 2;
	const static unsigned long long S0 = 3;
	const static unsigned long long S1 = 4;
	int64_t gid = 0;
	bool print = false;
	block public_one, public_zero;
	uint64_t gates = 0, ands = 0;
	std::ofstream fout;

	PlainCircExec(bool print, string filename) {
		public_one = zero_block;
		public_zero = zero_block;
		this->print = print;
		uint64_t *arr = (uint64_t*) &public_one;
		arr[0] = P1;
		arr = (uint64_t*) &public_zero;
		arr[0] = P0;

		if (print) {
			fout.open(filename);
			//place holder for circuit information
			for (int i = 0; i < 200; ++i)//good for 32-bit sized circuits
				fout << " ";
			fout<<std::endl;
		}
	}

	void finalize() {
		if(print) {
			fout.clear();
			fout.close();
			fout.clear();
		}
	}
	bool is_public(const block & b, int party) {
		uint64_t *arr = (uint64_t*) &b;
		return arr[0] == P0 or arr[0] == P1;
	}
	block public_label(bool b) override {
		return b? public_one : public_zero;
	}
	block and_gate(const block& a, const block& b) override {
		uint64_t *arr_a = (uint64_t*) &a;
		uint64_t *arr_b = (uint64_t*) &b;
		if (arr_a[0] == P1) {
			return b;
		} else if (arr_b[0] == P1) {
			return a;
		} else if(arr_a[0] == P0 or arr_b[0] == P0) {
			return public_zero;
		} else {
			block res = zero_block;
			uint64_t *arr = (uint64_t*) &res;
			arr[0] = compute_and(arr_a[0], arr_b[0]);
			arr[1] = gid;
			if(print)
				fout <<"2 1 "<<arr_a[1] <<" "<<arr_b[1]<<" "<<gid<<" AND"<<std::endl;
			gid++;
			gates++;
			ands++;
			return res;
		}
	}
	block xor_gate(const block&a, const block& b) override {
		uint64_t *arr_a = (uint64_t*) &a;
		uint64_t *arr_b = (uint64_t*) &b;
		if (arr_a[0] == P1) {
			return not_gate(b);
		} else if (arr_b[0] == P1) {
			return not_gate(a);
		} else if(arr_a[0] == P0) {
			return b;
		} else if(arr_b[0] == P0){
			return a;
		} else {
			block res = zero_block;
			uint64_t *arr = (uint64_t*) &res;
			arr[0] = compute_xor(arr_a[0], arr_b[0]);
			arr[1] = gid;
			if(print)
				fout <<"2 1 "<<arr_a[1] <<" "<<arr_b[1]<<" "<<gid<<" XOR"<<std::endl;
			gates++;
			gid++;
			return res;
		}
	}
	block private_label(bool b) {
		block res = zero_block;
		uint64_t *arr = (uint64_t*) &res;
		arr[0] = b ? S1 : S0;
		arr[1] = this->gid;
		gid++;
		return res;
	}
	bool get_value(block a) {
		uint64_t *arr = (uint64_t*) &a;
		//		if(print)
		//			fout <<"1 0 "<<arr[1] <<" OUT"<<endl;
		if (arr[0] == S0 or arr[0] == P0)
			return false;
		else return true;
	}
	block not_gate(const block&a) override {
		uint64_t *arr_a = (uint64_t*) &a;
		if (arr_a[0] == P1) {
			return public_zero;
		} else if (arr_a[0] == P0) {
			return public_one;
		} else {
			block res = zero_block;
			uint64_t *arr = (uint64_t*) &res;
			if(arr_a[0] == S0) arr[0] = S1;
			else arr[0] = S0;
			arr[1] = gid;
			if(print)
				fout <<"1 1 "<<arr_a[1] <<" "<<gid<<" INV"<<std::endl;
			gid++;
			gates++;
			return res;
		}
	}
	uint64_t num_and() override {
		return ands;
	}

private:
	uint64_t compute_and(uint64_t a, uint64_t b) {
		if(a == S0 or b == S0)
			return S0;
		else return S1;
	}
	uint64_t compute_xor(uint64_t a, uint64_t b) {
		if (a == b)
			return S0;
		else return S1;
	}
};
}
#endif
