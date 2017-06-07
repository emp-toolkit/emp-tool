#ifndef BACKEND_H__
#define BACKEND_H__

#ifdef USE_PTHREADS
#include <pthread.h>  
#else
#include <thread>
#endif
#include "garble_circuit.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/config.h"

class Backend { public:
	int cur_party;
	void (*Feed_internal)(Backend* be, EmpParty party, block * label, const bool*, int length);
	void (*Reveal_internal)(Backend* be, bool*, EmpParty party, const block * label, int length);
	void Feed(block * lbls, EmpParty party, const bool* b, int nel) {
		this->Feed_internal(this, party, lbls, b, nel);
	}
	void Reveal(bool*out, EmpParty party, const block *lbls, int nel) {
		this->Reveal_internal(this, out, party, lbls, nel);	
	}
	Backend(EmpParty party) {
		cur_party = party;
	}
};

#ifdef THREADING
extern thread_local Backend* local_backend;
extern thread_local GarbleCircuit* local_gc;
#else
extern Backend* local_backend;
extern  GarbleCircuit* local_gc;
#endif

#endif// BACKEND_H__
