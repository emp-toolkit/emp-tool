#ifndef INTEGER_H__
#define INTEGER_H__

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/number.h"
#include "emp-tool/circuits/comparable.h"
#include "emp-tool/circuits/swappable.h"
#include <vector>
#include <algorithm>
#include <math.h>
#include <execinfo.h>
using std::vector;
using std::min;

template<typename T>
class Integer : public Swappable<T, Integer>, public Comparable<T, Integer> { public:
	int length = 0;
	Bit<T>* bits = nullptr;
	Integer(Integer&& in) : length(in.length) {
		bits = in.bits;
		in.bits = nullptr;
	}
	Integer(const Integer& in): length(in.length) {
		bits = new Bit<T>[length];
		memcpy(bits, in.bits, sizeof(Bit<T>)*length);
	}
	Integer& operator= (Integer rhs){
		length = rhs.length;
		std::swap(bits, rhs.bits);
		return *this;
	}
	Integer(int len, const void * b) : length(len) {
		bits = new Bit<T>[len];
		memcpy(bits, b, sizeof(Bit<T>)*len);
	}
	~Integer() {
		if (bits!=nullptr) delete[] bits;
	}

	Integer(int length, const string& str, int party = PUBLIC);
	Integer(int length, long long input, int party = PUBLIC);
	Integer() :length(0),bits(nullptr){ }

//Comparable
	Bit<T> geq(const Integer & rhs) const;
	Bit<T> equal(const Integer & rhs) const;

//Swappable
	Integer<T> select(const Bit<T> & sel, const Integer<T> & rhs) const;
	Integer<T> operator^(const Integer<T>& rhs) const;

	int size() const;
	int reveal(int party=PUBLIC) const;

	Integer<T> abs() const;
	Integer<T>& resize(int length, bool signed_extend = true);
	Integer<T> modExp(Integer p, Integer q);
	Integer<T> leading_zeros() const;
	Integer<T> hamming_weight() const;

	Integer<T> operator<<(int shamt)const;
	Integer<T> operator>>(int shamt)const;
	Integer<T> operator<<(const Integer& shamt)const;
	Integer<T> operator>>(const Integer& shamt)const;

	Integer<T> operator+(const Integer& rhs)const;
	Integer<T> operator-(const Integer& rhs)const;
	Integer<T> operator-()const;
	Integer<T> operator*(const Integer& rhs)const;
	Integer<T> operator/(const Integer& rhs)const;
	Integer<T> operator%(const Integer& rhs)const;
	Integer<T> operator&(const Integer& rhs)const;
	Integer<T> operator|(const Integer& rhs)const;

	Bit<T>& operator[](int index);
	const Bit<T> & operator[](int index) const;
	
//batcher
	template<typename... Args>
	static size_t bool_size(size_t size, Args... args) {
		return size;
	}
	static void bool_data(bool* data, size_t len, long long num) {
		bool_data(data, len, std::to_string(num));
	}
	static void bool_data(bool* data, size_t len, string str) {
		string bin = dec_to_bin(str);
		std::reverse(bin.begin(), bin.end());
//		cout << "convert " <<str<<" "<<bin<<endl;
		int l = (bin.size() > (size_t)len ? len : bin.size());
		for(int i = 0; i < l; ++i)
			data[i] = (bin[i] == '1');
		for (size_t i = l; i < len; ++i)
			data[i] = data[l-1];
	}
};
template<typename T>
void init(Bit<T> * bits, const bool* b, int length, int party = PUBLIC);
#include "emp-tool/circuits/integer.hpp"
#endif// INTEGER_H__
