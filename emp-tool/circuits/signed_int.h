#ifndef EMP_SIGNED_INT_H__
#define EMP_SIGNED_INT_H__
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include <cmath>

namespace emp {

// Runtime-width signed integer (two's-complement). Wraps mod 2^width on
// +/-/* (matches int{N}_t on x86/ARM hardware; C signed-overflow UB is
// ignored). operator>> is arithmetic (sign-fill); operator<< inherited
// from BitVec is logical. Comparisons are signed. resize() sign-extends.
// Bit ordering is LSB-first.
template<typename Wire>
class SignedInt_T : public BitVec_T<Wire>,
                   public Sortable<Wire, SignedInt_T> { public:
	using BitVec_T<Wire>::bits;
	using BitVec_T<Wire>::size;

	SignedInt_T() = default;

	template<typename T>
	SignedInt_T(size_t width, T value, int party = PUBLIC)
	    : BitVec_T<Wire>(width, value, party) {
		static_assert(std::is_integral_v<T> || std::is_same_v<T, __int128>,
		              "SignedInt_T(width, value, party): value must be integer-shaped");
	}

	SignedInt_T(size_t width, const void* data, int party)
	    : BitVec_T<Wire>(width, data, party) {}

	explicit SignedInt_T(const BitVec_T<Wire>& bv) : BitVec_T<Wire>(bv) {}
	explicit SignedInt_T(BitVec_T<Wire>&& bv) : BitVec_T<Wire>(std::move(bv)) {}

	UnsignedInt_T<Wire> as_unsigned() const;

	// Sign-extend (or truncate) to new_width.
	SignedInt_T resize(size_t new_width) const;

	// Absolute value as an unsigned of the same width. abs(INT_MIN) wraps
	// back to INT_MIN as a bit pattern (unrepresentable in signed); the
	// returned UnsignedInt holds 2^(width-1) in that case, which is
	// faithful as an unsigned magnitude.
	UnsignedInt_T<Wire> abs() const;

	SignedInt_T operator+(const SignedInt_T& rhs) const;
	SignedInt_T operator-(const SignedInt_T& rhs) const;
	SignedInt_T operator-() const;
	SignedInt_T operator*(const SignedInt_T& rhs) const;
	SignedInt_T operator/(const SignedInt_T& rhs) const;
	SignedInt_T operator%(const SignedInt_T& rhs) const;

	// Bitwise overrides preserve SignedInt typing.
	SignedInt_T operator&(const SignedInt_T& rhs) const;
	SignedInt_T operator|(const SignedInt_T& rhs) const;
	SignedInt_T operator^(const SignedInt_T& rhs) const;
	SignedInt_T operator~() const;

	// Logical << (no sign issue on the left), arithmetic >> (sign-fill).
	SignedInt_T operator<<(size_t shamt) const;
	SignedInt_T operator>>(size_t shamt) const;

	// Dynamic shifts. shamt is treated as unsigned.
	SignedInt_T operator<<(const UnsignedInt_T<Wire>& shamt) const;
	SignedInt_T operator>>(const UnsignedInt_T<Wire>& shamt) const;

	// Sortable mixin hooks.
	Bit_T<Wire>  geq(const SignedInt_T& rhs) const;     // signed ≥
	Bit_T<Wire>  equal(const SignedInt_T& rhs) const;
	SignedInt_T  select(const Bit_T<Wire>& sel, const SignedInt_T& rhs) const;
};

#include "emp-tool/circuits/signed_int.hpp"

}  // namespace emp
#endif
