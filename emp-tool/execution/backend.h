#ifndef EMP_CIRCUIT_EXECUTION_H__
#define EMP_CIRCUIT_EXECUTION_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/constants.h"

namespace emp {

/* Circuit Pipelining 
 * [REF] Implementation of "Faster Secure Two-Party Computation Using Garbled Circuit"
 * https://www.usenix.org/legacy/event/sec11/tech/full_papers/Huang.pdf
 */
class Backend { public:
	int party;
	Backend(int party = PUBLIC) : party (party) {}

	virtual void feed(void * lbls, int party, const bool* b, size_t nel) = 0;
	virtual void reveal(bool*out, int party, const void * lbls, size_t nel) = 0;

	virtual void and_gate(void * output, const void * left, const void * right) = 0;
	virtual void xor_gate(void * output, const void * left, const void * right) = 0;
	virtual void not_gate(void * output, const void * input) = 0;

	virtual uint64_t num_and() {
		return -1;
	}

	virtual ~Backend() {
	}
};

extern Backend * backend;

enum RTCktOpt{on, off};
}
#endif