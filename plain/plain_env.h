#ifndef PLAIN_ENV_H__
#define PLAIN_ENV_H__
#include "plain_gc.h"
#include "backend.h"
#include <iostream>
#include <fstream>
using std::endl;

void plain_env_feed(Backend* be, int party, block * label, const bool*, int length);
void plain_env_reveal(Backend* be, bool* clear, int party, const block * label, int length);

class PlainEnv: public Backend { public:
	PlainGC * gc;
	std::ofstream fout;
	string filename;
	PlainEnv(PlainGC* gc): Backend(PUBLIC) {
		this->gc = gc;	
		Feed_internal = plain_env_feed;
		Reveal_internal = plain_env_reveal;
	}
	void finalize() {
		fout.close();
		fout.open(filename, std::ofstream::in);
		fout<<gc->gates<<" "<<gc->gid<<endl;
		fout<<n1<<" "<<n2<<" "<<n3<<endl;
		fout.close();
	}
	int n1 = 0, n2 = 0, n3 = 0;
};

void plain_env_feed(Backend* be, int party, block * label, const bool* b, int length) {
	PlainEnv * backend = (PlainEnv*)(be);
	for(int i = 0; i < length; ++i) {
		label[i] = backend->gc->private_label(b[i]);
	}
	if (party == ALICE)backend->n1+=length;
	else backend->n2+=length;
}

void plain_env_reveal(Backend* be, bool* b, int party, const block * label, int length) {
	PlainEnv * backend = (PlainEnv*)(be);
	for (int i = 0; i < length; ++i) {
		b[i] = backend->gc->get_value(label[i]);
	}
	backend->n3+=length;
}

static void setup_plain_env(bool print, string filename) {
	local_backend = (Backend *) new PlainEnv(nullptr); 
	PlainEnv * backend = (PlainEnv* )local_backend;
	if (print) {
		backend->fout.open(filename);
		//place holder for circuit information
		for (int i = 0; i < 200; ++i)//good for 32-bit sized circuits
			backend->fout << " ";
		backend->fout<<endl;
	}
	backend->filename = filename;
	local_gc = new PlainGC(print, backend->fout);
	backend->gc = (PlainGC *)local_gc;
}
static void finalize_plain_env (){
	PlainEnv * backend = (PlainEnv * )local_backend;
	backend ->finalize();
}
#endif 