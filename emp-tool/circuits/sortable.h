#ifndef EMP_TOOL_NUMERIC_H__
#define EMP_TOOL_NUMERIC_H__
namespace emp {

template<typename Wire>
class Bit_T;

template<typename Wire, template<typename> class T>
class Sortable { public:
	T<Wire> If(const Bit_T<Wire> & sel, const T<Wire>& rhs) const {
		return static_cast<const T<Wire>*>(this)->select(sel, rhs);
	}
	Bit_T<Wire> operator>=(const T<Wire>&rhs) const {
		return static_cast<const T<Wire>*>(this)->geq(rhs);
	}
	Bit_T<Wire> operator<(const T<Wire>& rhs) const {
		return !( (*static_cast<const T<Wire>*>(this))>= rhs );
	}

	Bit_T<Wire> operator<=(const T<Wire>& rhs) const {
		return rhs >= *static_cast<const T<Wire>*>(this);
	}

	Bit_T<Wire> operator>(const T<Wire>& rhs) const {
		return !(rhs >= *static_cast<const T<Wire>*>(this));
	}
	
	Bit_T<Wire> operator==(const T<Wire>& rhs) const {
		return static_cast<const T<Wire>*>(this)->equal(rhs);
	}
	Bit_T<Wire> operator!=(const T<Wire>& rhs) const {
		return !(*static_cast<const T<Wire>*>(this) == rhs);
	}
};

template<typename Wire, template<typename> class T>
inline T<Wire> If(const Bit_T<Wire> & select, const T<Wire> & o1, const T<Wire> & o2) {
	T<Wire> res = o2;
	return res.If(select, o1);
}

template<typename Wire, template<typename> class T>
inline void swap(const Bit_T<Wire> & swap, T<Wire> & o1, T<Wire> & o2) {
	T<Wire> o = If(swap, o1, o2);
	o ^= o2;
	o1 ^= o;
	o2 ^= o;
}

template<typename Wire, template<typename> class T, template<typename> class D>
void cmp_swap(T<Wire>*key, D<Wire>*data, int i, int j, Bit_T<Wire> acc) {
	Bit_T<Wire> to_swap = ((key[i] > key[j]) == acc);
	swap(to_swap, key[i], key[j]);
	if(data != nullptr)
		swap(to_swap, data[i], data[j]);
}

inline int greatestPowerOfTwoLessThan(int n) {
	int k = 1;
	while (k < n)
		k = k << 1;
	return k >> 1;
}

template<typename Wire, template<typename> class T, template<typename> class D>
void bitonic_merge(T<Wire>* key, D<Wire>* data, int lo, int n, Bit_T<Wire> acc) {
	if (n > 1) {
		int m = greatestPowerOfTwoLessThan(n);
		for (int i = lo; i < lo + n - m; i++)
			cmp_swap(key, data, i, i + m, acc);
		bitonic_merge(key, data, lo, m, acc);
		bitonic_merge(key, data, lo + m, n - m, acc);
	}
}

template<typename Wire, template<typename> class T, template<typename> class D>
void bitonic_sort(T<Wire> * key, D<Wire> * data, int lo, int n, Bit_T<Wire> acc) {
	if (n > 1) {
		int m = n / 2;
		bitonic_sort(key, data, lo, m, !acc);
		bitonic_sort(key, data, lo + m, n - m, acc);
		bitonic_merge(key, data, lo, n, acc);
	}
}

template<typename Wire, template<typename> class T, template<typename> class D = Bit_T>
void sort(T<Wire> * key, int size, D<Wire>* data = nullptr, Bit_T<Wire> acc = true) {
	bitonic_sort(key, data, 0, size, acc);
}

}
#endif// EMP_TOOL_NUMERIC_H__