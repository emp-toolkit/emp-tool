#ifndef EMP_SIGNED_INT_H__
#define EMP_SIGNED_INT_H__
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include <cmath>
#include <type_traits>

namespace emp {

template<typename Wire> class Float_T;

// Signed integer (two's-complement) over `Wire`. Width is fixed at compile
// time when N > 0, runtime when N == 0 (default). Wraps mod 2^width on
// +/-/* (matches int{N}_t on x86/ARM hardware; C signed-overflow UB ignored).
// operator>> is arithmetic (sign-fill); operator<< inherited from BitVec is
// logical. Comparisons are signed. resize() sign-extends. Bit ordering is
// LSB-first.
template<typename Wire, size_t N = 0>
class SignedInt_T : public BitVec_T<Wire>,
                   public Sortable<Wire, SignedInt_T<Wire, N>> { public:
	using BitVec_T<Wire>::bits;
	using BitVec_T<Wire>::size;

	SignedInt_T() {
		if constexpr (N > 0) bits.resize(N);
	}

	// Runtime-width ctors. Body in signed_int.hpp — sign-extends `value`
	// to `width` bits when T is signed and value < 0 (vs the BitVec
	// base, which always zero-extends). `party` has NO default: that's
	// what disambiguates this overload from the fixed-width form below
	// for calls like `SignedInt_T<W, 64>(uv, PUBLIC)` on platforms
	// where size_t and uint64_t are the same type.
	template<typename T,
	         typename = std::enable_if_t<std::is_integral_v<T>
	                                  || std::is_same_v<T, __uint128_t>
	                                  || std::is_same_v<T, __int128>>>
	SignedInt_T(size_t width, T value, int party);

	SignedInt_T(size_t width, const void* data, int party)
	    : BitVec_T<Wire>(width, data, party) {}

	// Fixed-width ctors: SFINAE-gated to N > 0. Delegates to the
	// runtime-width form so the sign-extension lives in one place.
	// `party` has NO default — see BitVec_T for the rationale.
	template<typename T,
	         size_t M = N,
	         typename = std::enable_if_t<(M > 0) && (std::is_integral_v<T>
	                                  || std::is_same_v<T, __uint128_t>
	                                  || std::is_same_v<T, __int128>)>>
	SignedInt_T(T value, int party) : SignedInt_T(N, value, party) {}

	template<size_t M = N, typename = std::enable_if_t<(M > 0)>>
	SignedInt_T(const void* data, int party)
	    : BitVec_T<Wire>(N, data, party) {}

	explicit SignedInt_T(const BitVec_T<Wire>& bv) : BitVec_T<Wire>(bv) {}
	explicit SignedInt_T(BitVec_T<Wire>&& bv) : BitVec_T<Wire>(std::move(bv)) {}

	UnsignedInt_T<Wire, N> as_unsigned() const;

	// Sign-extend / truncate. Returns runtime-width (N=0).
	SignedInt_T<Wire, 0> resize(size_t new_width) const;

	UnsignedInt_T<Wire, N> abs() const;

	SignedInt_T operator+(const SignedInt_T& rhs) const;
	SignedInt_T operator-(const SignedInt_T& rhs) const;
	SignedInt_T operator-() const;
	SignedInt_T operator*(const SignedInt_T& rhs) const;
	SignedInt_T operator/(const SignedInt_T& rhs) const;
	SignedInt_T operator%(const SignedInt_T& rhs) const;

	SignedInt_T operator&(const SignedInt_T& rhs) const;
	SignedInt_T operator|(const SignedInt_T& rhs) const;
	SignedInt_T operator^(const SignedInt_T& rhs) const;
	SignedInt_T operator~() const;

	SignedInt_T operator<<(size_t shamt) const;
	SignedInt_T operator>>(size_t shamt) const;

	SignedInt_T operator<<(const UnsignedInt_T<Wire, N>& shamt) const;
	SignedInt_T operator>>(const UnsignedInt_T<Wire, N>& shamt) const;

	Bit_T<Wire>  geq(const SignedInt_T& rhs) const;
	Bit_T<Wire>  equal(const SignedInt_T& rhs) const;
	SignedInt_T  select(const Bit_T<Wire>& sel, const SignedInt_T& rhs) const;

	// Encode this signed value (interpreted as a fixed-point number with
	// `s` fractional bits) as IEEE-754 single-precision. Definition in
	// float32.hpp; requires float32.h at the call site.
	Float_T<Wire> to_float32(size_t s) const;
};

// Convenience aliases for common fixed widths.
template<typename Wire> using Int8_T  = SignedInt_T<Wire, 8>;
template<typename Wire> using Int16_T = SignedInt_T<Wire, 16>;
template<typename Wire> using Int32_T = SignedInt_T<Wire, 32>;
template<typename Wire> using Int64_T = SignedInt_T<Wire, 64>;

#include "emp-tool/circuits/signed_int.hpp"

}  // namespace emp
#endif
