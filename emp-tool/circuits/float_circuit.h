#ifndef FLOAT_H__
#define FLOAT_H__

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/integer.h"
#include "emp-tool/circuits/swappable.h"
#include "emp-tool/circuits/number.h"
#include <math.h>
namespace emp {
class Float: Swappable<Float> { public:
	Integer value;
	Integer expnt;
	Float(Float && in): 
		value(std::move(in.value)),
		expnt(std::move(in.expnt)) {
		}

	Float(const Float & in) :
		value(in.value),
		expnt(in.expnt) {
		}

	Float& operator= (Float rhs) {
		std::swap(value, rhs.value);
		std::swap(expnt, rhs.expnt);
		return *this;
	}

	Float(int value_length, int expnt_length, double input, int party = PUBLIC);

	Float If(const Bit& select, const Float & d);

	template<typename O> 
	O reveal(int party = PUBLIC) const;

	int size() const;
	Float abs() const;

	void normalize(int value_length, int to_add_to_expnt);

	string detail(int party) const;

	Bit greater(const Float & rhs) const;
	Bit equal(const Float & rhs) const;

	Float operator+(const Float& rhs) const;
	Float operator-(const Float& rhs) const;
	Float operator-() const;
	Float operator*(const Float& rhs) const;
	Float operator/(const Float& rhs) const;
	Float operator&(const Float& rhs) const;
	Float operator|(const Float& rhs) const;
	Float operator^(const Float& rhs) const;
};

#include "emp-tool/circuits/float_circuit.hpp"
}
#endif// DOUBLE_H__
