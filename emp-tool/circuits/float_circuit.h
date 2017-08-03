#ifndef FLOAT_H__
#define FLOAT_H__

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/integer.h"
#include "emp-tool/circuits/swappable.h"
#include "emp-tool/circuits/number.h"
#include <math.h>

template<typename T>
class Float: Swappable<T, Float> { public:
	Integer<T> value;
	Integer<T> expnt;
	Float(Float && in): 
		value(std::move(in.value)),
		expnt(std::move(in.expnt)) {
		}

	Float(const Float & in) :
		value(in.value),
		expnt(in.expnt) {
		}

	Float<T>& operator= (Float rhs) {
		std::swap(value, rhs.value);
		std::swap(expnt, rhs.expnt);
		return *this;
	}

	Float(int value_length, int expnt_length, double input, int party = PUBLIC);

	Float<T> If(const Bit<T>& select, const Float & d);

	double reveal(int party = PUBLIC) const;

	int size() const;
	Float<T> abs() const;

	void normalize(int value_length, int to_add_to_expnt);

	string detail(int party) const;

	Bit<T> greater(const Float & rhs) const;
	Bit<T> equal(const Float & rhs) const;

	Float<T> operator+(const Float& rhs) const;
	Float<T> operator-(const Float& rhs) const;
	Float<T> operator-() const;
	Float<T> operator*(const Float& rhs) const;
	Float<T> operator/(const Float& rhs) const;
	Float<T> operator&(const Float& rhs) const;
	Float<T> operator|(const Float& rhs) const;
	Float<T> operator^(const Float& rhs) const;
};

#include "emp-tool/circuits/float_circuit.hpp"
#endif// DOUBLE_H__
