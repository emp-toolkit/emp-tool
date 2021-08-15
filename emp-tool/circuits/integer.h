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
template<typename Wire>
class Integer_T : public Swappable<Wire, Integer_T>, public Comparable<Wire, Integer_T> { public:
	vector<Bit_T<Wire>> bits;
	Integer_T(){
	}
	Integer_T(const vector<Bit_T<Wire>>& bits): bits(bits) {
	}
	Integer_T(int length, int64_t input, int party = PUBLIC);

	template<typename T>
	Integer_T(int length, T * input, int party = PUBLIC);

	template<typename T>
	Integer_T(T * input, int party = PUBLIC);

	//Comparable
	Bit_T<Wire> geq(const Integer_T & rhs) const;
	Bit_T<Wire> equal(const Integer_T & rhs) const;

	//Swappable
	Integer_T select(const Bit_T<Wire> & sel, const Integer_T & rhs) const;
	Integer_T operator^(const Integer_T& rhs) const;
	Integer_T operator^=(const Integer_T& rhs);

	size_t size() const;
	template<typename T>
	T reveal(int party=PUBLIC) const;
	template<typename T>
	void reveal(T * output, const int party=PUBLIC) const;

	Integer_T abs() const;
	Integer_T& resize(size_t length, bool signed_extend = true);
	Integer_T modExp(Integer_T p, Integer_T q);
	Integer_T leading_zeros() const;
	Integer_T hamming_weight() const;

	Integer_T operator<<(size_t shamt)const;
	Integer_T operator>>(size_t shamt)const;
	Integer_T operator<<(const Integer_T& shamt)const;
	Integer_T operator>>(const Integer_T& shamt)const;

	Integer_T operator+(const Integer_T& rhs)const;
	Integer_T operator-(const Integer_T& rhs)const;
	Integer_T operator-()const;
	Integer_T operator*(const Integer_T& rhs)const;
	Integer_T operator/(const Integer_T& rhs)const;
	Integer_T operator%(const Integer_T& rhs)const;
	Integer_T operator&(const Integer_T& rhs)const;
	Integer_T operator|(const Integer_T& rhs)const;

	Bit_T<Wire>& operator[](size_t index);
	const Bit_T<Wire> & operator[](size_t index) const;	

	void init(bool * b, int len, int party);
	void revealBools(bool *bools, int party=PUBLIC) const;
};

#include "emp-tool/circuits/integer.hpp"
}
#endif// INTEGER_H__
