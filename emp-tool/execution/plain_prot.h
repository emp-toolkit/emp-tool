#ifndef PLAIN_ENV_H__
#define PLAIN_ENV_H__
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
	PlainProt(bool _print, string _filename) : print(_print), 
	filename(_filename) {
	 cast_circ_exec = static_cast<PlainCircExec *> (CircuitExecution::circ_exec);
	}

	void finalize() {
		if(print) {
			fstream fout(filename, std::fstream::in | std::fstream::out);
			fout<<cast_circ_exec->gates<<" "<<cast_circ_exec->gid<<endl;
			fout<<n1<<" "<<n2<<" "<<n3<<endl;
			fout.close();
		}
	}

	void feed(block * label, int party, const bool* b, int length) override {
		for(int i = 0; i < length; ++i)
			label[i] = cast_circ_exec->private_label(b[i]);

		if (party == ALICE) n1+=length;
		else n2+=length;
	}

	void reveal(bool* b, int party, const block * label, int length) override {
		for (int i = 0; i < length; ++i)
			b[i] = cast_circ_exec->get_value(label[i]);
		n3+=length;
	}
};

inline void setup_plain_prot(bool print, string filename) {
	CircuitExecution::circ_exec = new PlainCircExec(print, filename);
	ProtocolExecution::prot_exec = new PlainProt(print, filename);
}

inline void finalize_plain_prot () {
	PlainCircExec * cast_circ_exec = static_cast<PlainCircExec *> (CircuitExecution::circ_exec);
	cast_circ_exec->finalize();
	ProtocolExecution::prot_exec->finalize();
	delete PlainCircExec::circ_exec;
	delete ProtocolExecution::prot_exec;
}
}
#endif 
