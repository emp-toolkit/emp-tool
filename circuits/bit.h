#ifndef BIT_H__
#define BIT_H__
#include "emp-tool/gc/backend.h"
#include "emp-tool/utils/utils.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/circuits/swappable.h"

class Bit : public Swappable<Bit>{ public:
	block bit;

	Bit(bool _b = false, EmpParty party = PUBLIC);
	Bit(const block& a) {
		memcpy(&bit, &a, sizeof(block));
	}

	template<typename O = bool> 
	O reveal(EmpParty party = PUBLIC) const;

	Bit operator!=(const Bit& rhs) const; 
	Bit operator==(const Bit& rhs) const;
	Bit operator &(const Bit& rhs) const;  
	Bit operator |(const Bit& rhs) const;
	Bit operator !() const; 

	//swappable
	Bit select(const Bit & select, const Bit & new_v)const ;
	Bit operator ^(const Bit& rhs) const;

	//batcher
	template<typename... Args>
	static size_t bool_size(Args&&... args) {
		return 1;
	}

	static void bool_data(bool *b, bool data) {
		b[0] = data;
	}

	Bit(size_t size, const block* a) {
		memcpy(&bit, a, sizeof(block));
	}
};
#include "bit.hpp"
#endif
