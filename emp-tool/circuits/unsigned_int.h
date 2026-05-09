#ifndef EMP_UNSIGNED_INT_H__
#define EMP_UNSIGNED_INT_H__
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include <cmath>
#include <type_traits>

namespace emp {

template<typename Wire, size_t N> class SignedInt_T;
template<typename Wire>           class Float_T;

// Unsigned integer over `Wire`. Width is either fixed at compile time (N > 0)
// or set at construction (N == 0, the default — runtime width). Wraps mod
// 2^width on +/-/* (matches uint{N}_t exactly). Logical shifts; comparisons
// are unsigned. resize() zero-extends. Bit ordering is LSB-first.
//
// When N > 0:
//   - default ctor produces a width-N value
//   - additional ctors `(value, party)` and `(data, party)` are unlocked
//     (no width arg needed)
//   - operators preserve width N (UnsignedInt_T<W,64> + UnsignedInt_T<W,64>
//     -> UnsignedInt_T<W,64>)
template<typename Wire, size_t N = 0>
class UnsignedInt_T : public BitVec_T<Wire>,
                     public Sortable<Wire, UnsignedInt_T<Wire, N>> { public:
	using BitVec_T<Wire>::bits;
	using BitVec_T<Wire>::size;

	UnsignedInt_T() {
		if constexpr (N > 0) bits.resize(N);
	}

	// Runtime-width ctors — usable in either mode (N==0 picks width up here;
	// N>0 ignores `width` and just fills N bits from the value).
	template<typename T,
	         typename = std::enable_if_t<std::is_integral_v<T>
	                                  || std::is_same_v<T, __uint128_t>
	                                  || std::is_same_v<T, __int128>>>
	UnsignedInt_T(size_t width, T value, int party = PUBLIC)
	    : BitVec_T<Wire>(width, value, party) {}

	UnsignedInt_T(size_t width, const void* data, int party)
	    : BitVec_T<Wire>(width, data, party) {}

	// Fixed-width ctors: only available when N > 0 (SFINAE-gated). Width is
	// implicit from the type, so callers don't pass it.
	template<typename T,
	         size_t M = N,
	         typename = std::enable_if_t<(M > 0) && (std::is_integral_v<T>
	                                  || std::is_same_v<T, __uint128_t>
	                                  || std::is_same_v<T, __int128>)>>
	UnsignedInt_T(T value, int party = PUBLIC)
	    : BitVec_T<Wire>(N, value, party) {}

	template<size_t M = N, typename = std::enable_if_t<(M > 0)>>
	UnsignedInt_T(const void* data, int party)
	    : BitVec_T<Wire>(N, data, party) {}

	explicit UnsignedInt_T(const BitVec_T<Wire>& bv) : BitVec_T<Wire>(bv) {}
	explicit UnsignedInt_T(BitVec_T<Wire>&& bv) : BitVec_T<Wire>(std::move(bv)) {}

	// Bit-cast to SignedInt with the same width and bits.
	SignedInt_T<Wire, N> as_signed() const;

	// Zero-extend (or truncate) to new_width. Always returns a runtime-width
	// UnsignedInt (N=0); width changes can't be expressed in the static type.
	UnsignedInt_T<Wire, 0> resize(size_t new_width) const;

	// Arithmetic — both operands must have equal width.
	UnsignedInt_T operator+(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator-(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator-() const;
	UnsignedInt_T operator*(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator/(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator%(const UnsignedInt_T& rhs) const;

	// Bitwise overrides preserve UnsignedInt typing.
	UnsignedInt_T operator&(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator|(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator^(const UnsignedInt_T& rhs) const;
	UnsignedInt_T operator~() const;

	UnsignedInt_T operator<<(size_t shamt) const;
	UnsignedInt_T operator>>(size_t shamt) const;

	UnsignedInt_T operator<<(const UnsignedInt_T& shamt) const;
	UnsignedInt_T operator>>(const UnsignedInt_T& shamt) const;

	// Sortable mixin hooks.
	Bit_T<Wire>      geq(const UnsignedInt_T& rhs) const;
	Bit_T<Wire>      equal(const UnsignedInt_T& rhs) const;
	UnsignedInt_T    select(const Bit_T<Wire>& sel, const UnsignedInt_T& rhs) const;

	UnsignedInt_T<Wire, 0> leading_zeros() const;
	UnsignedInt_T<Wire, 0> hamming_weight() const;
	UnsignedInt_T<Wire, 0> mod_exp(UnsignedInt_T<Wire, 0> p,
	                               UnsignedInt_T<Wire, 0> q) const;

	// Encode the unsigned magnitude (interpreted as a fixed-point value
	// with `s` fractional bits) as IEEE-754 single-precision. Definition
	// in float32.hpp; requires float32.h to be included at the call site.
	// Returns +0.0 for an all-zero input. Caller must ensure the value
	// fits in float's representable range.
	Float_T<Wire> to_float32(size_t s) const;
};

// Convenience aliases for common fixed widths.
template<typename Wire> using UInt8_T  = UnsignedInt_T<Wire, 8>;
template<typename Wire> using UInt16_T = UnsignedInt_T<Wire, 16>;
template<typename Wire> using UInt32_T = UnsignedInt_T<Wire, 32>;
template<typename Wire> using UInt64_T = UnsignedInt_T<Wire, 64>;

#include "emp-tool/circuits/unsigned_int.hpp"

}  // namespace emp
#endif
