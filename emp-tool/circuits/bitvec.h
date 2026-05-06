#ifndef EMP_BITVEC_H__
#define EMP_BITVEC_H__
#include <algorithm>
#include <type_traits>
#include "emp-tool/circuits/bit.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/core/block.h"
#include <vector>
#include <string>
#include <type_traits>
#include <cstring>
#include <algorithm>

namespace emp {

// Runtime-width vector of wires. Carries no arithmetic semantics — for
// numeric work use UnsignedInt_T / SignedInt_T which inherit from this.
//
// Bit ordering is LSB-first: bits[0] is bit 0, bits[size()-1] is the MSB.
template<typename Wire>
class BitVec_T { public:
	std::vector<Bit_T<Wire>> bits;

	BitVec_T() = default;

	explicit BitVec_T(size_t width) : bits(width) {}

	BitVec_T(const std::vector<Bit_T<Wire>>& bs) : bits(bs) {}
	BitVec_T(std::vector<Bit_T<Wire>>&& bs) : bits(std::move(bs)) {}

	// Feed `width` bits of an integral `value` (LSB-first), interpreted by
	// `party` as their input. PUBLIC means everyone agrees on the value.
	// SFINAE-restricted to integral T so that pointer arguments resolve
	// to the (size_t, const void*, int) overload below.
	template<typename T,
	         typename = std::enable_if_t<std::is_integral_v<T>
	                                  || std::is_same_v<T, __uint128_t>
	                                  || std::is_same_v<T, __int128>>>
	BitVec_T(size_t width, T value, int party = PUBLIC);

	// Feed `width` bits read from raw memory (LSB-first within each byte).
	BitVec_T(size_t width, const void* data, int party);

	size_t size() const { return bits.size(); }

	Bit_T<Wire>&       operator[](size_t i)       { return bits[i]; }
	const Bit_T<Wire>& operator[](size_t i) const { return bits[i]; }

	// Bitwise — both operands must have equal width (asserted).
	BitVec_T operator&(const BitVec_T& rhs) const;
	BitVec_T operator|(const BitVec_T& rhs) const;
	BitVec_T operator^(const BitVec_T& rhs) const;
	BitVec_T operator~() const;
	BitVec_T& operator^=(const BitVec_T& rhs);

	// Logical shifts (zero-fill on both sides). Shift amounts ≥ width
	// produce zero. SignedInt_T overrides operator>> with arithmetic shift.
	BitVec_T operator<<(size_t shamt) const;
	BitVec_T operator>>(size_t shamt) const;

	// Bitwise equality (equal in *every* bit). Returns a single Bit.
	Bit_T<Wire> equal(const BitVec_T& rhs) const;

	// Concat: result = this | (hi << this->size()). low at low indices.
	BitVec_T concat(const BitVec_T& hi) const;

	// Slice [lo, hi). hi may be size() (open upper). Returns a width
	// (hi - lo) BitVec, sharing wire references via copy of the Bit_T.
	BitVec_T slice(size_t lo, size_t hi) const;

	// Bit-wise oblivious select: result[i] = sel ? rhs[i] : (*this)[i].
	BitVec_T select(const Bit_T<Wire>& sel, const BitVec_T& rhs) const;

	// Read out the value. If O is std::string, returns the bit string
	// (MSB-first). If O is integral, packs LSB-first into O (excess high
	// bits are zeroed; bits beyond sizeof(O)*8 are dropped).
	template<typename O = std::string>
	O reveal(int party = PUBLIC) const;

	// Read out the bits to raw memory (LSB-first within each byte). Output
	// must have at least ⌈size()/8⌉ bytes.
	void reveal(void* output, int party = PUBLIC) const;
};

#include "emp-tool/circuits/bitvec.hpp"

}  // namespace emp
#endif
