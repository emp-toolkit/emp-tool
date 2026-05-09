#ifndef EMP_BIT_H__
#define EMP_BIT_H__
#include <type_traits>
#include <string>
#include "emp-tool/execution/backend.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/circuits/sortable.h"
namespace emp {

template<typename Wire>
class Bit_T: public Sortable<Wire, Bit_T<Wire>> { public:
	Wire bit;

	Bit_T() {}
	// `party` has NO default: PUBLIC means "everyone holds this value",
	// so a default would silently leak a forgotten private input as
	// public. Explicit party required at every input ctor.
	Bit_T(bool _b, int party);
	Bit_T(const Wire& a) {
		bit = a;
	}

	template<typename O = bool>
	O reveal(int party = PUBLIC) const;

	Bit_T operator!=(const Bit_T& rhs) const;
	Bit_T operator==(const Bit_T& rhs) const;
	Bit_T operator &(const Bit_T& rhs) const;
	Bit_T operator |(const Bit_T& rhs) const;
	Bit_T operator !() const;

	Bit_T geq(const Bit_T& rhs) const;
	Bit_T select(const Bit_T<Wire> & select, const Bit_T & new_v) const;
	Bit_T operator ^(const Bit_T& rhs) const;
	Bit_T operator ^=(const Bit_T& rhs);

	//batcher
	template<typename... Args>
	static size_t bool_size(Args&&... args) {
		return 1;
	}

	static void bool_data(bool *b, bool data) {
		b[0] = data;
	}
};
#include "emp-tool/circuits/bit.hpp"
}
#endif
