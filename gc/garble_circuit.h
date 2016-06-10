#ifndef GARBLE_CIRCUIT_H__
#define GARBLE_CIRCUIT_H__
#include "block.h"
#include "utils.h"

enum RTCktOpt{on, off};
class GarbleCircuit { public:
	uint64_t gid = 0;
	bool (*is_public_ptr)(GarbleCircuit* gc, const block & b, int party);
	block (*public_label_ptr)(GarbleCircuit* gc, bool b);
	block (*gc_and_ptr)(GarbleCircuit* gc, const block&a, const block&b);
	block (*gc_xor_ptr)(GarbleCircuit*gc, const block&a, const block&b);
	block (*gc_not_ptr)(GarbleCircuit*gc, const block&a);
	
	bool is_public(const block & b, int party) {
		return is_public_ptr(this, b, party);
	}
	block public_label(bool b) {
		return public_label_ptr(this, b);	
	}
	block gc_and(const block&a, const block&b) {
		return gc_and_ptr(this, a, b);
	}
	block gc_xor(const block&a, const block&b) {
		return gc_xor_ptr(this, a, b);
	}
	block gc_not(const block&a) {
		return gc_not_ptr(this, a);
	}
	static block make_delta(const block & a) {
		return _mm_or_si128(makeBlock(0L, 1L), a);
	}
};
#endif// GARBLE_CIRCUIT_H__
