#ifndef BACKEND_H__
#define BACKEND_H__
#include <pthread.h>  
#include "garble_circuit.h"
#include "block.h"
#include "config.h"

class Backend { public:
	int cur_party;
	void (*Feed_internal)(Backend* be, int, block * label, const bool*, int length);
	void (*Reveal_internal)(Backend* be, bool*, int, const block * label, int length);
	void Feed(block * lbls, int party, const bool* b, int nel) {
		this->Feed_internal(this, party, lbls, b, nel);
	}
	void Reveal(bool*out, int party, const block *lbls, int nel) {
		this->Reveal_internal(this, out, party, lbls, nel);	
	}
	Backend(int party) {
		cur_party = party;
	}
};

#ifdef THREADING
extern __thread Backend* local_backend;
extern __thread GarbleCircuit* local_gc;
#else
extern Backend* local_backend;
extern  GarbleCircuit* local_gc;
#endif

#endif// BACKEND_H__
