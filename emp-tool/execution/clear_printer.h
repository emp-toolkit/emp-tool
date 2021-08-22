#ifndef EMP_PLAIN_CIRC_EXEC_H__
#define EMP_PLAIN_CIRC_EXEC_H__
//#include "emp-tool/utils/utils.h"
#include "emp-tool/execution/backend.h"
#include <iostream>
#include <fstream>
#include <vector>

namespace emp {
class ClearWire { public:
	uint64_t index;
	bool value;
	bool is_public;
	ClearWire(bool value = false, bool is_public = true, uint64_t index = 0):
		index(index), value(value), is_public(is_public) {

	}
};

class ClearPrinter: public Backend { public:
	typedef ClearWire wire_t;

	uint64_t ands = 0;
	//for printing circuit
	bool print = false;
	std::ofstream fout;
	string filename;
	std::vector<int64_t>output_vec;
	uint64_t gid = 0, gates = 0, n1 = 0, n2 = 0, n3 = 0;
	ClearPrinter(string filename=""): filename(filename) {
		print = filename.size()>0;
		if (print) {
			fout.open(filename);
			//place holder for circuit information
			for (int i = 0; i < 200; ++i) //good for 32-bit sized circuits
				fout << " ";
			fout<<std::endl;
		}
	}

	~ClearPrinter() {
		if(print) {
			uint64_t z_index = gid++;
			fout<<2<<" "<<1<<" "<<0<<" "<<0<<" "<<z_index<<" XOR\n";
			for (auto v : output_vec) {
				fout<<2<<" "<<1<<" "<<z_index<<" "<<v<<" "<<gid++<<" XOR\n";
			}
			gates += (1+output_vec.size());
			n3 = output_vec.size();
			fout.clear();
			fout.close();
			fout.clear();
			std::fstream fout2(filename, std::fstream::in | std::fstream::out);
			fout2<<gates<<" "<<gid<<std::endl;
			fout2<<n1<<" "<<n2<<" "<<n3<<std::endl;
			fout2.close();
		}
	}
	
	void and_gate(void * output, const void * left, const void * right) override {
		const ClearWire * l = static_cast<const ClearWire*>(left);
		const ClearWire * r = static_cast<const ClearWire*>(right);
		ClearWire * o = static_cast<ClearWire *>(output);

		if (l->is_public and l->value) {
			*o= *r;
		} else if (r->is_public and r->value) {
			*o = *l;
		} else if(l->is_public and !l->value) {
			*o = ClearWire(false, true); 
		} else if(r->is_public and !r->value) {
			*o = ClearWire(false, true);
		} else {
			*o = ClearWire(l->value and r->value, false, gid++);
			if(print)
				fout <<"2 1 "<<l->index <<" "<<r->index<<" "<<o->index<<" AND"<<std::endl;
			gates++;
			ands++;
		}
	}
	void xor_gate(void * output, const void * left, const void * right) override {
		const ClearWire * l = static_cast<const ClearWire*>(left);
		const ClearWire * r = static_cast<const ClearWire*>(right);
		ClearWire * o = static_cast<ClearWire *>(output);

		if (l->is_public and l->value) {
			not_gate(output, right);
		} else if (r->is_public and r->value) {
			not_gate(output, left);
		} else if(l->is_public and !l->value) {
			*o = *r;
		} else if(r->is_public and !r->value) {
			*o = *l;
		} else {
			*o = ClearWire(l->value xor r->value, false, gid++);
			if(print)
				fout <<"2 1 "<<l->index <<" "<<r->index<<" "<<o->index<<" XOR\n";
			gates++;
		}
	}
	void not_gate(void * output, const void * input) override {
		const ClearWire * i = static_cast<const ClearWire*>(input);
		ClearWire * o = static_cast<ClearWire *>(output);
		if (i->is_public) {
			*o = ClearWire(not i->value, true);
		} else {
			*o = ClearWire(not i->value, false, gid++);
			if(print)
				fout <<"1 1 "<<i->index <<" "<<o->index<<" INV\n";
			gates++;
		}
	}
	uint64_t num_and() override {
		return ands;
	}
	void feed(void * lbls, int party, const bool* b, size_t nel) override {
		ClearWire* out = static_cast<ClearWire *>(lbls);
		if(party == ALICE) n1+=nel;
		else if(party == BOB) n2+=nel;
		for(size_t i = 0; i < nel; ++i) {
			if(party == PUBLIC)
				out[i] = ClearWire(b[i], true);
			else
				out[i] = ClearWire(b[i], false, gid++);
		}
	}

	void reveal(bool*out, int party, const void * lbls, size_t nel) override {
		const ClearWire* in = static_cast<const ClearWire *>(lbls);
		for(size_t i = 0; i < nel; ++i) {
			out[i] = in[i].value;
			if(print)
				output_vec.push_back(in[i].index);
		}
	}
};
}
#endif
