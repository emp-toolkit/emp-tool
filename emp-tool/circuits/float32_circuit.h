#ifndef FLOAT32_H__
#define FLOAT32_H__

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/integer.h"
#include "emp-tool/circuits/swappable.h"
#include "emp-tool/circuits/number.h"
#include <math.h>
#include <array>

#define FLOAT_LEN 32
#define BIAS 127
#define SGNFC_LEN 23
#define EXPNT_LEN 8

namespace emp {

class Float32: Swappable<Float32> { public:
	std::array<Bit,32> value;
	
	Float32(Float32 && in):
		value(std::move(in.value)) {
	}

	Float32(const Float32 & in):
		value(in.value) {
	}

	Float32& operator= (Float32 rhs) {
		std::swap(value, rhs.value);
		return *this;
	}

	Float32(float input, int party = PUBLIC);

	template<typename O> 
	O reveal(int party = PUBLIC) const;

	Float32 abs() const;
	Float32 If(const Bit& select, const Float32 & d);

	Bit equal(const Float32 & rhs) const;
	Bit less_equal(const Float32 & rhs) const;
	Bit less_than(const Float32 & rhs) const;

	Float32 operator+(const Float32& rhs) const;
	Float32 operator-(const Float32& rhs) const;
	Float32 operator-() const;
	Float32 operator*(const Float32& rhs) const;
	Float32 operator/(const Float32& rhs) const;
	Float32 operator^(const Float32& rhs) const;
	Float32 operator&(const Float32& rhs) const;

	Float32 sqr() const;
	Float32 sqrt() const;
	Float32 sin() const;
	Float32 cos() const;
	Float32 exp2() const;
	Float32 exp() const;
	Float32 ln() const;
	Float32 log2() const;

	Bit& operator[](int index);
	const Bit & operator[](int index) const;
	size_t size() const {return 32;};
};

#include "emp-tool/circuits/float32_circuit.hpp"
}
#endif// DOUBLE_H__
