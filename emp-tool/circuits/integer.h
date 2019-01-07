#ifndef INTEGER_H__
#define INTEGER_H__

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/number.h"
#include "emp-tool/circuits/comparable.h"
#include "emp-tool/circuits/swappable.h"
#include <vector>
#include <bitset>
#include <algorithm>
#include <math.h>
#include <execinfo.h>
using std::vector;
using std::min;
namespace emp {
class Integer : public Swappable<Integer>, public Comparable<Integer> { public:
	int length = 0;
	Bit* bits = nullptr;
	Integer(Integer&& in) : length(in.length) {
		bits = in.bits;
		in.bits = nullptr;
	}
	Integer(const Integer& in): length(in.length) {
		bits = new Bit[length];
		memcpy(bits, in.bits, sizeof(Bit)*length);
	}
	Integer& operator= (Integer rhs){
		length = rhs.length;
		std::swap(bits, rhs.bits);
		return *this;
	}
	Integer(int len, const void * b) : length(len) {
		bits = new Bit[len];
		memcpy(bits, b, sizeof(Bit)*len);
	}
	~Integer() {
		if (bits!=nullptr) delete[] bits;
	}

	Integer(int length, const string& str, int party = PUBLIC);
	Integer(int length, long long input, int party = PUBLIC);
	Integer() :length(0),bits(nullptr){ }

//Comparable
	Bit geq(const Integer & rhs) const;
	Bit equal(const Integer & rhs) const;

//Swappable
	Integer select(const Bit & sel, const Integer & rhs) const;
	Integer operator^(const Integer& rhs) const;

	int size() const;
	template<typename O>
	O reveal(int party=PUBLIC) const;

	Integer abs() const;
	Integer& resize(int length, bool signed_extend = true);
	Integer modExp(Integer p, Integer q);
	Integer leading_zeros() const;
	Integer hamming_weight() const;

	Integer operator<<(int shamt)const;
	Integer operator>>(int shamt)const;
	Integer operator<<(const Integer& shamt)const;
	Integer operator>>(const Integer& shamt)const;

	Integer operator+(const Integer& rhs)const;
	Integer operator-(const Integer& rhs)const;
	Integer operator-()const;
	Integer operator*(const Integer& rhs)const;
	Integer operator/(const Integer& rhs)const;
	Integer operator%(const Integer& rhs)const;
	Integer operator&(const Integer& rhs)const;
	Integer operator|(const Integer& rhs)const;

	Bit& operator[](int index);
	const Bit & operator[](int index) const;
	
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

void init(Bit * bits, const bool* b, int length, int party = PUBLIC);
#include "emp-tool/circuits/integer.hpp"
}
#endif// INTEGER_H__
