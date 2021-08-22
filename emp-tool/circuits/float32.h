#ifndef EMP_FLOAT32_H__
#define EMP_FLOAT32_H__
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/integer.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/circuit_file.h"
#include <math.h>
#include <array>
namespace emp {

template<typename Wire>
class Float_T: public Sortable<Wire, Float_T> { public:
	static const int FLOAT_LEN = 32;
	static const int BIAS = 127;
	static const int SGNFC_LEN = 23;
	static const int EXPNT_LEN = 8;

	std::array<Bit_T<Wire>,32> value;
	
	Float_T(Float_T && in):
		value(std::move(in.value)) {
	}

	Float_T(const Float_T & in):
		value(in.value) {
	}

	Float_T& operator= (Float_T rhs) {
		std::swap(value, rhs.value);
		return *this;
	}

	Float_T(float input, int party = PUBLIC);

	template<typename O> 
	O reveal(int party = PUBLIC) const;

	Float_T abs() const;
	Float_T select(const Bit_T<Wire> & sel, const Float_T & rhs) const;

	Bit_T<Wire> equal(const Float_T & rhs) const;
	Bit_T<Wire> less_equal(const Float_T & rhs) const;
	Bit_T<Wire> less_than(const Float_T & rhs) const;

	Float_T operator+(const Float_T& rhs) const;
	Float_T operator-(const Float_T& rhs) const;
	Float_T operator-() const;
	Float_T operator*(const Float_T& rhs) const;
	Float_T operator/(const Float_T& rhs) const;
	Float_T operator^(const Float_T& rhs) const;
	Float_T operator^=(const Float_T& rhs);
	Float_T operator&(const Float_T& rhs) const;

	Float_T sqr() const;
	Float_T sqrt() const;
	Float_T sin() const;
	Float_T cos() const;
	Float_T exp2() const;
	Float_T exp() const;
	Float_T ln() const;
	Float_T log2() const;

	Bit_T<Wire>& operator[](int index);
	const Bit_T<Wire> & operator[](int index) const;
	size_t size() const {return 32;};
};

#include "emp-tool/circuits/float32.hpp"
#include "emp-tool/circuits/float_circuits/float32_add.hpp"
#include "emp-tool/circuits/float_circuits/float32_sub.hpp"
#include "emp-tool/circuits/float_circuits/float32_mul.hpp"
#include "emp-tool/circuits/float_circuits/float32_div.hpp"
#include "emp-tool/circuits/float_circuits/float32_sq.hpp"
#include "emp-tool/circuits/float_circuits/float32_sqrt.hpp"
#include "emp-tool/circuits/float_circuits/float32_eq.hpp"
#include "emp-tool/circuits/float_circuits/float32_le.hpp"
#include "emp-tool/circuits/float_circuits/float32_leq.hpp"
#include "emp-tool/circuits/float_circuits/float32_sin.hpp"
#include "emp-tool/circuits/float_circuits/float32_cos.hpp"
#include "emp-tool/circuits/float_circuits/float32_exp.hpp"
#include "emp-tool/circuits/float_circuits/float32_exp2.hpp"
#include "emp-tool/circuits/float_circuits/float32_ln.hpp"
#include "emp-tool/circuits/float_circuits/float32_log2.hpp"
}
#endif// DOUBLE_H__
