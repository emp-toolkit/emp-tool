#ifndef EMP_NUMBER_H__
#define EMP_NUMBER_H__
#include "emp-tool/circuits/bit.h"

namespace emp {
template<typename T, typename D>
void cmp_swap(T*key, D*data, int i, int j, Bit acc) {
	Bit to_swap = ((key[i] > key[j]) == acc);
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

template<typename T, typename D>
void bitonic_merge(T* key, D* data, int lo, int n, Bit acc) {
	if (n > 1) {
		int m = greatestPowerOfTwoLessThan(n);
		for (int i = lo; i < lo + n - m; i++)
			cmp_swap(key, data, i, i + m, acc);
		bitonic_merge(key, data, lo, m, acc);
		bitonic_merge(key, data, lo + m, n - m, acc);
	}
}

template<typename T, typename D>
void bitonic_sort(T * key, D * data, int lo, int n, Bit acc) {
	if (n > 1) {
		int m = n / 2;
		bitonic_sort(key, data, lo, m, !acc);
		bitonic_sort(key, data, lo + m, n - m, acc);
		bitonic_merge(key, data, lo, n, acc);
	}
}

template <typename T, typename D = Bit>
void sort(T * key, int size, D* data = nullptr, Bit acc = true) {
	bitonic_sort(key, data, 0, size, acc);
}
}
#endif
