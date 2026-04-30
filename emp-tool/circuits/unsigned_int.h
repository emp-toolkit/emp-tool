#ifndef EMP_UNSIGNED_INT_H__
#define EMP_UNSIGNED_INT_H__
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include <cmath>

namespace emp {

template<typename Wire> class SignedInt_T;

// Runtime-width unsigned integer. Wraps mod 2^width on +/-/* (matches
// uint{N}_t exactly). Logical shifts; comparisons are unsigned. resize()
// zero-extends. Bit ordering is LSB-first.
template<typename Wire>
class UnsignedInt_T : public BitVec_T<Wire>,
                     public Sortable<Wire, UnsignedInt_T> { public:
	using BitVec_T<Wire>::bits;
	using BitVec_T<Wire>::size;

	UnsignedInt_T() = default;

	template<typename T>
	UnsignedInt_T(size_t width, T value, int party = PUBLIC)
	    : BitVec_T<Wire>(width, value, party) {
		static_assert(std::is_integral_v<T> || std::is_same_v<T, __uint128_t>,
		              "UnsignedInt_T(width, value, party): value must be unsigned-integer-shaped");
	}

	// Read raw bytes as the bits.
	UnsignedInt_T(size_t width, const void* data, int party)
	    : BitVec_T<Wire>(width, data, party) {}

	explicit UnsignedInt_T(const BitVec_T<Wire>& bv) : BitVec_T<Wire>(bv) {}
	explicit UnsignedInt_T(BitVec_T<Wire>&& bv) : BitVec_T<Wire>(std::move(bv)) {}

	// Bit-cast to SignedInt with the same width and bits.
	SignedInt_T<Wire> as_signed() const;

	// Zero-extend (or truncate) to new_width.
	UnsignedInt_T resize(size_t new_width) const;

	// Arithmetic — both operands must have equal width.
	UnsignedInt_T operator+(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator-(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator-() const;                       // unary, wraps (-x = ~x + 1)
	UnsignedInt_T operator*(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator/(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator%(const UnsignedInt_T& rhs) const;

	// Bitwise overrides preserve UnsignedInt typing (BitVec base would
	// strip signedness).
	UnsignedInt_T operator&(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator|(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator^(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator~() const;

	// Static-shamt logical shifts (delegate to BitVec but typed as UnsignedInt).
	UnsignedInt_T operator<<(size_t shamt) const;
	UnsignedInt_T operator>>(size_t shamt) const;

	// Dynamic shifts. shamt is treated as unsigned.
	UnsignedInt_T operator<<(const UnsignedInt_T& shamt) const;
	UnsignedInt_T operator>>(const UnsignedInt_T& shamt) const;

	// Sortable mixin hooks.
	Bit_T<Wire>      geq(const UnsignedInt_T& rhs) const;   // unsigned ≥
	Bit_T<Wire>      equal(const UnsignedInt_T& rhs) const;
	UnsignedInt_T    select(const Bit_T<Wire>& sel, const UnsignedInt_T& rhs) const;

	// Bit-vector niceties — leading_zeros and hamming_weight return an
	// UnsignedInt wide enough to hold the count without truncation.
	UnsignedInt_T leading_zeros() const;
	UnsignedInt_T hamming_weight() const;
	// modular exponentiation: (*this)^p mod q. p/q must share width.
	UnsignedInt_T mod_exp(UnsignedInt_T p, UnsignedInt_T q) const;
};

#include "emp-tool/circuits/unsigned_int.hpp"

}  // namespace emp
#endif
