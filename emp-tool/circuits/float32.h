#ifndef EMP_FLOAT32_H__
#define EMP_FLOAT32_H__
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/circuit_file.h"
#include <math.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>
namespace emp {

template<typename Wire>
class Float_T: public Sortable<Wire, Float_T<Wire>> { public:
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

	// No-arg ctor: leaves `value` default-initialized (Bit_T<Wire>{}).
	// Useful for vector<Float> and similar default-constructed slots.
	Float_T() = default;
	// Input ctor: feeds `input`'s 32-bit pattern as `party`'s input.
	// `party` has NO default — see Bit_T/BitVec_T for the rationale.
	Float_T(float input, int party);

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

	// Decode this float as a non-negative N-bit fixed-point integer with
	// `s` fractional bits. The IEEE sign bit is ignored — caller's job to
	// reject negative inputs if that matters. Truncates toward zero.
	// Returns 0 when the input is zero. NaN, Inf, and subnormals are
	// preconditions: undefined behavior. Definition in float32.hpp.
	template<int N> UnsignedInt_T<Wire, N> to_unsigned(size_t s) const;

	// Decode this float as a signed N-bit fixed-point integer with `s`
	// fractional bits. Composes to_unsigned with the IEEE sign bit.
	template<int N> SignedInt_T<Wire, N>   to_signed(size_t s) const;
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
