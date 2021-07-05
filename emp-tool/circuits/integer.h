#ifndef EMP_INTEGER_H__
#define EMP_INTEGER_H__

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/number.h"
#include "emp-tool/circuits/comparable.h"
#include "emp-tool/circuits/swappable.h"
#include <vector>
#include <bitset>
#include <algorithm>
#include <math.h>
using std::vector;
using std::min;
namespace emp {
class Integer : public Swappable<Integer>, public Comparable<Integer> { public:
	vector<Bit> bits;
	Integer(){
	}
	Integer(const vector<Bit>& bits): bits(bits) {
	}
	Integer(int length, int64_t input, int party = PUBLIC);

	template<typename T>
		Integer(int length, T * input, int party = PUBLIC);

	template<typename T>
		Integer(T * input, int party = PUBLIC);

	//Comparable
	Bit geq(const Integer & rhs) const;
	Bit equal(const Integer & rhs) const;

	//Swappable
	Integer select(const Bit & sel, const Integer & rhs) const;
	Integer operator^(const Integer& rhs) const;
	Integer operator^=(const Integer& rhs);

	size_t size() const;
	template<typename T>
		T reveal(int party=PUBLIC) const;
	template<typename T>
		void reveal(T * output, const int party=PUBLIC) const;

	Integer abs() const;
	Integer& resize(size_t length, bool signed_extend = true);
	Integer modExp(Integer p, Integer q);
	Integer leading_zeros() const;
	Integer hamming_weight() const;

	Integer operator<<(size_t shamt)const;
	Integer operator>>(size_t shamt)const;
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

	Bit& operator[](size_t index);
	const Bit & operator[](size_t index) const;	

	void init(bool * b, int len, int party);
	void revealBools(bool *bools, int party=PUBLIC) const;
};

#include "emp-tool/circuits/integer.hpp"
}
#endif// INTEGER_H__
