#ifndef GARBLE_CIRCUIT_H__
#define GARBLE_CIRCUIT_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/utils.h"

enum RTCktOpt{on, off};
class GarbleCircuit { public:
	uint64_t gid = 0;
	//bool (*is_public_ptr)(GarbleCircuit* gc, const block & b, EmpParty party);
	//block (*public_label_ptr)(GarbleCircuit* gc, bool b);
	//block (*gc_and_ptr)(GarbleCircuit* gc, const block&a, const block&b);
	//block (*gc_xor_ptr)(GarbleCircuit*gc, const block&a, const block&b);
	//block (*gc_not_ptr)(GarbleCircuit*gc, const block&a);
	
    virtual bool is_public(const block & b, EmpParty party) = 0;
    virtual block public_label(bool b) = 0;
    virtual block gc_and(const block&a, const block&b) = 0;
    virtual block gc_xor(const block&a, const block&b) = 0;
    virtual block gc_not(const block&a) = 0;

	static block make_delta(const block & a) {
		return _mm_or_si128(makeBlock(0L, 1L), a);
	}
};
#endif// GARBLE_CIRCUIT_H__
