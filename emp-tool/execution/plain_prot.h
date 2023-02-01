#ifndef EMP_PLAIN_ENV_H__
#define EMP_PLAIN_ENV_H__
#include "emp-tool/emp-tool.h"
#include <iostream>
#include <fstream>
using std::endl;
using std::fstream;

namespace emp {
class PlainProt: public ProtocolExecution { 
public:
	int64_t n1 = 0, n2 = 0, n3 = 0;

	bool print;
	string filename;
	PlainCircExec * cast_circ_exec;
	std::vector<int64_t>output_vec;
	PlainProt(bool _print, string _filename) : print(_print), 
	filename(_filename) {
	 cast_circ_exec = static_cast<PlainCircExec *> (CircuitExecution::circ_exec);
	}

	void finalize() override {
		if(print) {
			fstream fout(filename, std::fstream::in | std::fstream::out);
			fout<<cast_circ_exec->gates<<" "<<cast_circ_exec->gid<<endl;
			fout<<n1<<" "<<n2<<" "<<n3<<endl;
			fout.close();
		}
	}

	void feed(block * label, int party, const bool* b, int length) override {
                if (party == PUBLIC) {
                  for(int i = 0; i < length; ++i)
                    label[i] = cast_circ_exec->public_label(b[i]);
                  return;
                }
                
                for(int i = 0; i < length; ++i)
			label[i] = cast_circ_exec->private_label(b[i]);

		if (party == ALICE) n1+=length;
		else n2+=length;
	}

	void reveal(bool* b, int party, const block * label, int length) override {
		for (int i = 0; i < length; ++i) {
			uint64_t *arr = (uint64_t*) (&label[i]);
			output_vec.push_back(arr[1]);
			b[i] = cast_circ_exec->get_value(label[i]);
		}
		n3+=length;
	}
};

inline void setup_plain_prot(bool print, string filename) {
	CircuitExecution::circ_exec = new PlainCircExec(print, filename);
	ProtocolExecution::prot_exec = new PlainProt(print, filename);
}

inline void finalize_plain_prot () {
	PlainCircExec * cast_circ_exec = static_cast<PlainCircExec *> (CircuitExecution::circ_exec);
	PlainProt * cast_prot_exec = static_cast<PlainProt*> (ProtocolExecution::prot_exec);
	int64_t z_index = cast_circ_exec->gid++;
	cast_circ_exec->fout<<2<<" "<<1<<" "<<0<<" "<<0<<" "<<z_index<<" XOR"<<endl;
	for (auto v : cast_prot_exec->output_vec) {
		cast_circ_exec->fout<<2<<" "<<1<<" "<<z_index<<" "<<v<<" "<<cast_circ_exec->gid++<<" XOR"<<endl;
	}
	cast_circ_exec->gates += (1+cast_prot_exec->output_vec.size());
	cast_circ_exec->finalize();

	ProtocolExecution::prot_exec->finalize();
	delete PlainCircExec::circ_exec;
	delete ProtocolExecution::prot_exec;
}
}
#endif 
