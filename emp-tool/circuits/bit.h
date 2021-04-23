#ifndef EMP_BIT_H__
#define EMP_BIT_H__
#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/execution/protocol_execution.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/circuits/swappable.h"

namespace emp {
class Bit : public Swappable<Bit>{ public:
	block bit;

	Bit(bool _b = false, int party = PUBLIC);
	Bit(const block& a) {
		memcpy(&bit, &a, sizeof(block));
	}

	template<typename O = bool>
	O reveal(int party = PUBLIC) const;

	Bit operator!=(const Bit& rhs) const;
	Bit operator==(const Bit& rhs) const;
	Bit operator &(const Bit& rhs) const;
	Bit operator |(const Bit& rhs) const;
	Bit operator !() const;

	//swappable
	Bit select(const Bit & select, const Bit & new_v)const ;
	Bit operator ^(const Bit& rhs) const;
	Bit operator ^=(const Bit& rhs);

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
