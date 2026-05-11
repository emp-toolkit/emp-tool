#ifndef EMP_NUMERIC_KERNELS_H__
#define EMP_NUMERIC_KERNELS_H__
#include "emp-tool/circuits/bit.h"
#include <cstring>
#include <vector>

// Bit-array-level arithmetic kernels shared by UnsignedInt_T and SignedInt_T.
// All operate on Bit_T<Wire>* bit arrays of the caller's chosen length.
// Sign semantics live one level up: signed division is unsigned div with
// pre/post condNeg, signed comparison is sign-extended subtraction, etc.
//
// Bit ordering everywhere is LSB-first.

namespace emp {

// dest <- op1 + op2 + carryIn (mod 2^size). carryOut may be null.
// Reference: obliv-c / oblivious-IR add_full.
template<typename Wire>
inline void add_full(Bit_T<Wire>* dest, Bit_T<Wire>* carryOut,
                     const Bit_T<Wire>* op1, const Bit_T<Wire>* op2,
                     const Bit_T<Wire>* carryIn, int size) {
	Bit_T<Wire> carry, bxc, axc, t;
	int i = 0;
	if (size == 0) {
		if (carryIn && carryOut) *carryOut = *carryIn;
		return;
	}
	carry = carryIn ? *carryIn : Bit_T<Wire>(false, PUBLIC);
	int skipLast = (carryOut == nullptr);
	while (size-- > skipLast) {
		axc = op1[i] ^ carry;
		bxc = op2[i] ^ carry;
		dest[i] = op1[i] ^ bxc;
		t = axc & bxc;
		carry = carry ^ t;
		++i;
	}
	if (carryOut) *carryOut = carry;
	else dest[i] = carry ^ op2[i] ^ op1[i];
}

// dest <- op1 - op2 - borrowIn (mod 2^size). borrowOut may be null.
template<typename Wire>
inline void sub_full(Bit_T<Wire>* dest, Bit_T<Wire>* borrowOut,
                     const Bit_T<Wire>* op1, const Bit_T<Wire>* op2,
                     const Bit_T<Wire>* borrowIn, int size) {
	Bit_T<Wire> borrow, bxc, bxa, t;
	int i = 0;
	if (size == 0) {
		if (borrowIn && borrowOut) *borrowOut = *borrowIn;
		return;
	}
	borrow = borrowIn ? *borrowIn : Bit_T<Wire>(false, PUBLIC);
	int skipLast = (borrowOut == nullptr);
	while (size-- > skipLast) {
		bxa = op1[i] ^ op2[i];
		bxc = borrow ^ op2[i];
		dest[i] = bxa ^ borrow;
		t = bxa & bxc;
		borrow = borrow ^ t;
		++i;
	}
	if (borrowOut) *borrowOut = borrow;
	else dest[i] = op1[i] ^ op2[i] ^ borrow;
}

// dest <- low `size` bits of op1 * op2 (signed/unsigned identical at the
// low N bits of an N×N multiply, so this is shared).
//
// `sum`/`tmp` are short-lived scratch (size = caller's int width, so 8..64
// in practice). Heap allocation is in the noise vs the O(size^2) gate
// work that follows; use std::vector for RAII (no leaks on a future
// exception, no manual delete[] pair to keep in sync).
template<typename Wire>
inline void mul_full(Bit_T<Wire>* dest, const Bit_T<Wire>* op1,
                     const Bit_T<Wire>* op2, int size) {
	std::vector<Bit_T<Wire>> sum(size, Bit_T<Wire>(false, PUBLIC));
	std::vector<Bit_T<Wire>> tmp(size);
	for (int i = 0; i < size; ++i) {
		for (int k = 0; k < size - i; ++k)
			tmp[k] = op1[k] & op2[i];
		add_full<Wire>(sum.data() + i, nullptr,
		               sum.data() + i, tmp.data(), nullptr, size - i);
	}
	std::memcpy(dest, sum.data(), sizeof(Bit_T<Wire>) * size);
}

// dest[i] <- cond ? tsrc[i] : fsrc[i].
template<typename Wire>
inline void if_then_else(Bit_T<Wire>* dest, const Bit_T<Wire>* tsrc,
                         const Bit_T<Wire>* fsrc, int size,
                         const Bit_T<Wire>& cond) {
	Bit_T<Wire> x, a;
	int i = 0;
	while (size-- > 0) {
		x = tsrc[i] ^ fsrc[i];
		a = cond & x;
		dest[i] = a ^ fsrc[i];
		++i;
	}
}

// dest <- cond ? -src : src. Two's-complement negation of src, gated.
template<typename Wire>
inline void cond_neg(const Bit_T<Wire>& cond, Bit_T<Wire>* dest,
                     const Bit_T<Wire>* src, int size) {
	int i;
	Bit_T<Wire> c = cond;
	for (i = 0; i < size - 1; ++i) {
		dest[i] = src[i] ^ cond;
		Bit_T<Wire> t = dest[i] ^ c;
		c = c & dest[i];
		dest[i] = t;
	}
	dest[i] = cond ^ c ^ src[i];
}

// vquot, vrem <- op1 / op2, op1 % op2 (both unsigned). Either out may be null.
// Behavior on op2 == 0 mirrors the original obliv-c circuit (saturates),
// not C semantics.
//
// Same RAII story as mul_full above: short-lived scratch, four vectors
// instead of four hand-paired new[]/delete[] sites.
template<typename Wire>
inline void div_full(Bit_T<Wire>* vquot, Bit_T<Wire>* vrem,
                     const Bit_T<Wire>* op1, const Bit_T<Wire>* op2, int size) {
	std::vector<Bit_T<Wire>> overflow(size);
	std::vector<Bit_T<Wire>> tmp(size);
	std::vector<Bit_T<Wire>> rem(size);
	std::vector<Bit_T<Wire>> quot(size);
	Bit_T<Wire> b;
	std::memcpy(rem.data(), op1, size * sizeof(Bit_T<Wire>));
	overflow[0] = Bit_T<Wire>(false, PUBLIC);
	for (int i = 1; i < size; ++i)
		overflow[i] = overflow[i - 1] | op2[size - i];
	for (int i = size - 1; i >= 0; --i) {
		sub_full<Wire>(tmp.data(), &b, rem.data() + i, op2, nullptr, size - i);
		b = b | overflow[i];
		if_then_else<Wire>(rem.data() + i, rem.data() + i, tmp.data(), size - i, b);
		quot[i] = !b;
	}
	if (vrem)  std::memcpy(vrem,  rem.data(),  size * sizeof(Bit_T<Wire>));
	if (vquot) std::memcpy(vquot, quot.data(), size * sizeof(Bit_T<Wire>));
}

}  // namespace emp
#endif
