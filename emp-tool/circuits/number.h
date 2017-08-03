#ifndef NUMBER_H__
#define NUMBER_H__
#include "emp-tool/circuits/bit.h"


template<typename T1, template<typename> class T, template<typename> class D>
void cmp_swap(T<T1>*key, D<T1>*data, int i, int j, Bit<T1> acc) {
	Bit<T1> to_swap = ((key[i] > key[j]) == acc);
	swap(to_swap, key[i], key[j]);
	if(data != nullptr)
		swap(to_swap, data[i], data[j]);
}

int greatestPowerOfTwoLessThan(int n) {
	int k = 1;
	while (k < n)
		k = k << 1;
	return k >> 1;
}

template<typename T1, template<typename> class T, template<typename> class D>
void bitonic_merge(T<T1>* key, D<T1>* data, int lo, int n, Bit<T1> acc) {
	if (n > 1) {
		int m = greatestPowerOfTwoLessThan(n);
		for (int i = lo; i < lo + n - m; i++)
			cmp_swap(key, data, i, i + m, acc);
		bitonic_merge(key, data, lo, m, acc);
		bitonic_merge(key, data, lo + m, n - m, acc);
	}
}

template<typename T1, template<typename> class T, template<typename> class D>
void bitonic_sort(T<T1> * key, D<T1> * data, int lo, int n, Bit<T1> acc) {
	if (n > 1) {
		int m = n / 2;
		bitonic_sort(key, data, lo, m, !acc);
		bitonic_sort(key, data, lo + m, n - m, acc);
		bitonic_merge(key, data, lo, n, acc);
	}
}

template<typename T1, template<typename> class T, template<typename> class D = Bit>
void sort(T<T1> * key, int size, D<T1>* data = nullptr, Bit<T1> acc = true) {
	bitonic_sort(key, data, 0, size, acc);
}

#endif
