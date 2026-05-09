#ifndef EMP_TOOL_NUMERIC_H__
#define EMP_TOOL_NUMERIC_H__
#include "emp-tool/core/constants.h"
namespace emp {

template<typename Wire>
class Bit_T;

// CRTP mixin: derived `Self` must define
//   geq(const Self&)    -> Bit_T<Wire>
//   equal(const Self&)  -> Bit_T<Wire>
//   select(const Bit_T<Wire>&, const Self&) -> Self
// Provides comparison operators. Exposes `wire_type = Wire` so the free
// functions below can recover Wire from the derived type.
template<typename Wire, typename Self>
class Sortable { public:
	using wire_type = Wire;

	Bit_T<Wire> operator>=(const Self& rhs) const {
		return static_cast<const Self*>(this)->geq(rhs);
	}
	Bit_T<Wire> operator<(const Self& rhs) const {
		return !( (*static_cast<const Self*>(this)) >= rhs );
	}
	Bit_T<Wire> operator<=(const Self& rhs) const {
		return rhs >= *static_cast<const Self*>(this);
	}
	Bit_T<Wire> operator>(const Self& rhs) const {
		return !(rhs >= *static_cast<const Self*>(this));
	}
	Bit_T<Wire> operator==(const Self& rhs) const {
		return static_cast<const Self*>(this)->equal(rhs);
	}
	Bit_T<Wire> operator!=(const Self& rhs) const {
		return !(*static_cast<const Self*>(this) == rhs);
	}
};

// Fluent builder: If(cond).Then(a).Else(b) -> cond ? a : b. Reads in source
// order; works on any Sortable T (Bit, UnsignedInt, SignedInt, Float, …).
// `Else` is capitalised to dodge the C++ keyword.
//
// Single-AND mux: `cond ? a : b = b ^ (cond & (a ^ b))`. The Else step
// delegates to T::select, which already uses this formula bit-by-bit
// (see Bit_T::select in bit.hpp), so the whole chain costs exactly one
// AND per output bit.
template<typename Wire, typename T>
class IfThenBuilder {
	Bit_T<Wire> cond_;
	T then_val_;
public:
	IfThenBuilder(const Bit_T<Wire>& c, const T& t) : cond_(c), then_val_(t) {}
	T Else(const T& else_val) const {
		// T::select(sel, rhs) returns `sel ? rhs : *this`, so calling on
		// else_val with cond and then_val produces `cond ? then_val : else_val`.
		return else_val.select(cond_, then_val_);
	}
};

template<typename Wire>
class IfBuilder {
	Bit_T<Wire> cond_;
public:
	explicit IfBuilder(const Bit_T<Wire>& c) : cond_(c) {}
	template<typename T>
	IfThenBuilder<Wire, T> Then(const T& then_val) const {
		return IfThenBuilder<Wire, T>(cond_, then_val);
	}
};

template<typename Wire>
inline IfBuilder<Wire> If(const Bit_T<Wire>& cond) {
	return IfBuilder<Wire>(cond);
}

template<typename T>
inline void swap(const Bit_T<typename T::wire_type>& swap, T& o1, T& o2) {
	T o = If(swap).Then(o1).Else(o2);
	o ^= o2;
	o1 ^= o;
	o2 ^= o;
}

template<typename T, typename D>
void cmp_swap(T* key, D* data, int i, int j, Bit_T<typename T::wire_type> acc) {
	Bit_T<typename T::wire_type> to_swap = ((key[i] > key[j]) == acc);
	swap(to_swap, key[i], key[j]);
	if (data != nullptr)
		swap(to_swap, data[i], data[j]);
}

inline int greatestPowerOfTwoLessThan(int n) {
	int k = 1;
	while (k < n)
		k = k << 1;
	return k >> 1;
}

template<typename T, typename D>
void bitonic_merge(T* key, D* data, int lo, int n,
                   Bit_T<typename T::wire_type> acc) {
	if (n > 1) {
		int m = greatestPowerOfTwoLessThan(n);
		for (int i = lo; i < lo + n - m; i++)
			cmp_swap(key, data, i, i + m, acc);
		bitonic_merge(key, data, lo, m, acc);
		bitonic_merge(key, data, lo + m, n - m, acc);
	}
}

template<typename T, typename D>
void bitonic_sort(T* key, D* data, int lo, int n,
                  Bit_T<typename T::wire_type> acc) {
	if (n > 1) {
		int m = n / 2;
		bitonic_sort(key, data, lo, m, !acc);
		bitonic_sort(key, data, lo + m, n - m, acc);
		bitonic_merge(key, data, lo, n, acc);
	}
}

template<typename T, typename D = Bit_T<typename T::wire_type>>
void sort(T* key, int size, D* data = nullptr,
          Bit_T<typename T::wire_type> acc = Bit_T<typename T::wire_type>(true, PUBLIC)) {
	bitonic_sort(key, data, 0, size, acc);
}

}
#endif// EMP_TOOL_NUMERIC_H__
