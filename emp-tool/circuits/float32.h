#ifndef EMP_FLOAT32_H__
#define EMP_FLOAT32_H__

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/integer.h"
#include "emp-tool/circuits/swappable.h"
#include "emp-tool/circuits/number.h"
#include "emp-tool/circuits/circuit_file.h"
#include <math.h>
#include <array>

#define FLOAT_LEN 32
#define BIAS 127
#define SGNFC_LEN 23
#define EXPNT_LEN 8

namespace emp {

class Float: Swappable<Float> { public:
	std::array<Bit,32> value;
	
	Float(Float && in):
		value(std::move(in.value)) {
	}

	Float(const Float & in):
		value(in.value) {
	}

	Float& operator= (Float rhs) {
		std::swap(value, rhs.value);
		return *this;
	}

	Float(float input = 0.0, int party = PUBLIC);

	template<typename O> 
	O reveal(int party = PUBLIC) const;

	Float abs() const;
	Float If(const Bit& select, const Float & d);

	Bit equal(const Float & rhs) const;
	Bit less_equal(const Float & rhs) const;
	Bit less_than(const Float & rhs) const;

	Float operator+(const Float& rhs) const;
	Float operator-(const Float& rhs) const;
	Float operator-() const;
	Float operator*(const Float& rhs) const;
	Float operator/(const Float& rhs) const;
	Float operator^(const Float& rhs) const;
	Float operator^=(const Float& rhs);
	Float operator&(const Float& rhs) const;

	Float sqr() const;
	Float sqrt() const;
	Float sin() const;
	Float cos() const;
	Float exp2() const;
	Float exp() const;
	Float ln() const;
	Float log2() const;

	Bit& operator[](int index);
	const Bit & operator[](int index) const;
	size_t size() const {return 32;};
};

#include "emp-tool/circuits/float32.hpp"
}
#endif// DOUBLE_H__
