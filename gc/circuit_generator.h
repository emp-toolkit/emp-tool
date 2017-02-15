#ifndef CIRCUIT_GENEARTOR_H__
#define CIRCUIT_GENEARTOR_H__
#include <emp-tool>
#include "circuit_file_generator.h"
#include <iostream>

void gen_feed(Backend* be, int party, block * label, const bool*, int length);
void gen_reveal(Backend* be, bool* clear, int party, const block * label, int length);

class CircuitGenerator: public Backend { public:
	CircuitFileGenerator * gc;
	CircuitGenerator(CircuitFileGenerator* gc): Backend(PUBLIC) {
		this->gc = gc;	
		Feed_internal = gen_feed;
		Reveal_internal = gen_reveal;
	}
};

void gen_feed(Backend* be, int party, block * label, const bool* b, int length) {
	CircuitGenerator * backend = (CircuitGenerator*)(be);
	for(int i = 0; i < length; ++i) {
		label[i] = backend->gc->private_label(b[i]);
	}
}

void gen_reveal(Backend* be, bool* b, int party, const block * label, int length) {
	CircuitGenerator * backend = (CircuitGenerator*)(be);
	for (int i = 0; i < length; ++i) {
		b[i] = backend->gc->get_value(label[i]);
	}
}

static void setup_circuit_generator(bool print) {
		local_gc = new CircuitFileGenerator(print);
		local_backend = new CircuitGenerator((CircuitFileGenerator*)local_gc);
}
#endif //CIRCUIT_GENEARTOR_H__