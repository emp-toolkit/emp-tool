#ifndef EMP_NUMBER_H__
#define EMP_NUMBER_H__
#include "emp-tool/circuits/bit.h"

namespace emp {
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
#endif
