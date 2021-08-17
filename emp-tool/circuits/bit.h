#ifndef EMP_BIT_H__
#define EMP_BIT_H__
#include <type_traits>

#include "emp-tool/execution/backend.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/circuits/swappable.h"


namespace emp {

template<typename Wire>
class Bit_T :public Swappable<Wire, Bit_T>{ public:
	Wire bit;

	Bit_T() {}
	Bit_T(bool _b, int party = PUBLIC);
	Bit_T(const Wire& a) {
		bit = a;
	}

	bool reveal(int party = PUBLIC) const;

	Bit_T operator!=(const Bit_T& rhs) const;
	Bit_T operator==(const Bit_T& rhs) const;
	Bit_T operator &(const Bit_T& rhs) const;
	Bit_T operator |(const Bit_T& rhs) const;
	Bit_T operator !() const;

	//swappable
	Bit_T select(const Bit_T & select, const Bit_T & new_v)const ;
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
