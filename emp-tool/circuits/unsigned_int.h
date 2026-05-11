#ifndef EMP_UNSIGNED_INT_H__
#define EMP_UNSIGNED_INT_H__
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

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

	// Runtime-width ctors — usable in either mode (N==0 picks width up
	// here; N>0 still requires width==N — the fixed-width invariant on
	// the underlying BitVec must hold). `party` has NO default: that's
	// what disambiguates this overload from the fixed-width form below
	// for calls like `UnsignedInt_T<W, 64>(uv, PUBLIC)`. Without this,
	// on platforms where size_t and uint64_t are the same type both
	// ctors are identity-rank matches and the call is ambiguous.
	template<typename T,
	         typename = std::enable_if_t<std::is_integral_v<T>
	                                  || std::is_same_v<T, __uint128_t>
	                                  || std::is_same_v<T, __int128>>>
	UnsignedInt_T(size_t width, T value, int party)
	    : BitVec_T<Wire>(resolved_runtime_width(width), value, party) {}

	UnsignedInt_T(size_t width, const void* data, int party)
	    : BitVec_T<Wire>(enforced_data_width(width), data, party) {}

	// Fixed-width ctors: only available when N > 0 (SFINAE-gated). Width is
	// implicit from the type, so callers don't pass it. `party` has NO
	// default — see BitVec_T for the rationale.
	template<typename T,
	         size_t M = N,
	         typename = std::enable_if_t<(M > 0) && (std::is_integral_v<T>
	                                  || std::is_same_v<T, __uint128_t>
	                                  || std::is_same_v<T, __int128>)>>
	UnsignedInt_T(T value, int party)
	    : BitVec_T<Wire>(N, value, party) {}

	template<size_t M = N, typename = std::enable_if_t<(M > 0)>>
	UnsignedInt_T(const void* data, int party)
	    : BitVec_T<Wire>(N, data, party) {}

	explicit UnsignedInt_T(const BitVec_T<Wire>& bv)
	    : BitVec_T<Wire>(enforce_bv_width(bv)) {}
	explicit UnsignedInt_T(BitVec_T<Wire>&& bv)
	    : BitVec_T<Wire>(std::move(enforce_bv_width(bv))) {}

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

private:
	// Fixed-width-N invariant guard for the runtime-width *value* ctor
	// (T value, not pointer). When N > 0 the base BitVec must be exactly
	// N bits; passing any other width through to BitVec_T(width, …)
	// silently produces a value whose static type lies about its runtime
	// size — the call site keeps thinking UInt32, the bits vector is 16
	// long, downstream width-equality asserts (e.g. operator+) fire much
	// later. Debug: abort. Release: normalize to N so production stays
	// consistent with the type. The value already lives in a T-typed
	// local, so widening costs nothing.
	static size_t resolved_runtime_width(size_t width) {
		if constexpr (N > 0) {
			assert(width == N &&
			       "UnsignedInt_T<N>: runtime ctor width must equal N");
			return N;
		}
		return width;
	}

	// Strict variant for the (size_t, const void*, int) ctor. We can't
	// silently normalize to N here: doing so would either read past the
	// caller's buffer (width < N) or drop bits the caller meant to feed
	// (width > N). Either way, the right answer is to surface the
	// programmer error. Fatal in all build types.
	static size_t enforced_data_width(size_t width) {
		if constexpr (N > 0) {
			if (width != N)
				throw std::runtime_error(
				    "UnsignedInt_T<N>(width, data, party): width must equal N");
		}
		return width;
	}

	// Strict variant for the (BitVec_T) converting ctors. A 16-bit
	// BitVec slotted into a UInt32-typed wrapper would propagate a
	// width-lying value to every operator that asserts size equality,
	// so reject it at the conversion point. Forwarding overload so the
	// rvalue ctor still moves.
	template<typename Bv>
	static Bv&& enforce_bv_width(Bv&& bv) {
		if constexpr (N > 0) {
			if (bv.size() != N)
				throw std::runtime_error(
				    "UnsignedInt_T<N>(BitVec): bv.size() must equal N");
		}
		return std::forward<Bv>(bv);
	}
};

// Convenience aliases for common fixed widths.
template<typename Wire> using UInt8_T  = UnsignedInt_T<Wire, 8>;
template<typename Wire> using UInt16_T = UnsignedInt_T<Wire, 16>;
template<typename Wire> using UInt32_T = UnsignedInt_T<Wire, 32>;
template<typename Wire> using UInt64_T = UnsignedInt_T<Wire, 64>;

#include "emp-tool/circuits/unsigned_int.hpp"

}  // namespace emp
#endif
