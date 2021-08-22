//https://github.com/samee/obliv-c/blob/obliv-c/src/ext/oblivc/obliv_bits.c#L1487
template<typename Wire>
inline void add_full(Bit_T<Wire>* dest, Bit_T<Wire> * carryOut, const Bit_T<Wire> * op1, 
	const Bit_T<Wire> * op2, const Bit_T<Wire> * carryIn, int size) {
	Bit_T<Wire> carry, bxc, axc, t;
	int skipLast; 
	int i = 0;
	if(size==0) { 
		if(carryIn && carryOut)
			*carryOut = *carryIn;
		return;
	}
	if(carryIn)
		carry = *carryIn;
	else 
		carry = false;
	// skip AND on last bit if carryOut==NULL
	skipLast = (carryOut == static_cast<Bit_T<Wire>*>(nullptr));
	while(size-->skipLast) { 
		axc = op1[i] ^ carry;
		bxc = op2[i] ^ carry;
		dest[i] = op1[i] ^ bxc;
		t = axc & bxc;
		carry =carry^t;
		++i;
	}
	if(carryOut != static_cast<Bit_T<Wire>*>(nullptr))
		*carryOut = carry;
	else
		dest[i] = carry ^ op2[i] ^ op1[i];
}

template<typename Wire>
inline void sub_full(Bit_T<Wire> * dest, Bit_T<Wire> * borrowOut, const Bit_T<Wire> * op1, 
	const Bit_T<Wire> * op2, const Bit_T<Wire> * borrowIn, int size) {
	Bit_T<Wire> borrow,bxc,bxa,t;
	int skipLast; int i = 0;
	if(size==0) { 
		if(borrowIn && borrowOut) 
			*borrowOut = *borrowIn;
		return;
	}
	if(borrowIn) 
		borrow = *borrowIn;
	else 
		borrow = false;
	// skip AND on last bit if borrowOut==NULL
	skipLast = (borrowOut == static_cast<Bit_T<Wire>*>(nullptr));
	while(size-- > skipLast) {
		bxa = op1[i] ^ op2[i];
		bxc = borrow ^ op2[i];
		dest[i] = bxa ^ borrow;
		t = bxa & bxc;
		borrow = borrow ^ t;
		++i;
	}
	if(borrowOut != static_cast<Bit_T<Wire>*>(nullptr)) {
		*borrowOut = borrow;
	}
	else
		dest[i] = op1[i] ^ op2[i] ^ borrow;
}

template<typename Wire>
inline void mul_full(Bit_T<Wire> * dest, const Bit_T<Wire> * op1, const Bit_T<Wire> * op2, int size) {
	Bit_T<Wire> * sum = new Bit_T<Wire>[size];
	Bit_T<Wire> * temp = new Bit_T<Wire>[size];
	for(int i = 0; i < size; ++i)sum[i]=false;
	for(int i=0;i<size;++i) {
		for (int k = 0; k < size-i; ++k)
			temp[k] = op1[k] & op2[i];
		add_full(sum+i, static_cast<Bit_T<Wire>*>(nullptr), sum+i, temp, static_cast<Bit_T<Wire>*>(nullptr), size-i);
	}
	memcpy(dest, sum, sizeof(Bit_T<Wire>)*size);
	delete[] sum;
	delete[] temp;
}

template<typename Wire>
inline void ifThenElse(Bit_T<Wire> * dest, const Bit_T<Wire> * tsrc, const Bit_T<Wire> * fsrc, 
		int size, Bit_T<Wire> cond) {
	Bit_T<Wire> x, a;
	int i = 0;
	while(size-- > 0) {
		x = tsrc[i] ^ fsrc[i];
		a = cond & x;
		dest[i] = a ^ fsrc[i];
		++i;
	}
}

template<typename Wire>
inline void condNeg(Bit_T<Wire> cond, Bit_T<Wire> * dest, const Bit_T<Wire> * src, int size) {
	int i;
	Bit_T<Wire> c = cond;
	for(i=0; i < size-1; ++i) {
		dest[i] = src[i] ^ cond;
		Bit_T<Wire> t  = dest[i] ^ c;
		c = c & dest[i];
		dest[i] = t;
	}
	dest[i] = cond ^ c ^ src[i];
}

template<typename Wire>
inline void div_full(Bit_T<Wire> * vquot, Bit_T<Wire> * vrem, const Bit_T<Wire> * op1, const Bit_T<Wire> * op2, 
		int size) {
	Bit_T<Wire> * overflow = new Bit_T<Wire>[size];
	Bit_T<Wire> * temp = new Bit_T<Wire>[size];
	Bit_T<Wire> * rem = new Bit_T<Wire>[size];
	Bit_T<Wire> * quot = new Bit_T<Wire>[size];
	Bit_T<Wire> b;
	memcpy(rem, op1, size*sizeof(Bit_T<Wire>));
	overflow[0] = false;
	for(int i  = 1; i < size;++i)
		overflow[i] = overflow[i-1] | op2[size-i];
	// skip AND on last bit if borrowOut==NULL
	for(int i = size-1; i >= 0; --i) {
		sub_full(temp, &b, rem+i, op2, static_cast<Bit_T<Wire>*>(nullptr), size-i);
		b = b | overflow[i];
		ifThenElse(rem+i,rem+i,temp,size-i,b);
		quot[i] = !b;
	}
	if(vrem != static_cast<Bit_T<Wire>*>(nullptr)) memcpy(vrem, rem, size*sizeof(Bit_T<Wire>));
	if(vquot != static_cast<Bit_T<Wire>*>(nullptr)) memcpy(vquot, quot, size*sizeof(Bit_T<Wire>));
	delete[] overflow;
	delete[] temp;
	delete[] rem;
	delete[] quot;
}


template<typename Wire>
inline void Integer_T<Wire>::init(bool * b, int len, int party) {
	bits.resize(len);
	backend->feed(bits.data(), party, b, len); 
}

template<typename Wire>
inline Integer_T<Wire>::Integer_T(int len, int64_t input, int party) {
	bool* b = new bool[len];
	int_to_bool<int64_t>(b, input, len);
	init(b, len, party);
	delete[] b;
}

template<typename Wire>
template<typename T>
inline Integer_T<Wire>::Integer_T(int len, T * input, int party) {
	bool* b = new bool[len];
	to_bool<T>(b, input, len);
	init(b, len, party);
	delete[] b;
}

template<typename Wire>
template<typename T>
inline Integer_T<Wire>::Integer_T(T * input, int party) {
	size_t len = 8 * sizeof(T);
	bool* b = new bool[len];
	to_bool<T>(b, input, len);
	init(b, len, party);
	delete[] b;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::select(const Bit_T<Wire> & select, const Integer_T<Wire> & a) const{
	Integer_T<Wire> res(*this);
	for(size_t i = 0; i < size(); ++i)
		res[i] = bits[i].select(select, a[i]);
	return res;
}

template<typename Wire>
inline Bit_T<Wire>& Integer_T<Wire>::operator[](size_t index) {
	return bits[min(index, size()-1)];
}

template<typename Wire>
inline const Bit_T<Wire> &Integer_T<Wire>::operator[](size_t index) const {
	return bits[min(index, size()-1)];
}

template<typename Wire>
inline void Integer_T<Wire>::revealBools(bool *bools, int party) const {
	backend->reveal(bools, party, bits.data(), size());
}

template<typename Wire>
template<typename O>
inline O Integer_T<Wire>::reveal(int party) const {
	static_assert(std::numeric_limits<O>::is_integer, "Integer reveal only support numeric types");
	O res = (O)0;	
	bool b[128];	
	backend->reveal(b, party, bits.data(), min(size(), 128UL));
	__uint128_t one = 1;
	for(size_t i = 0; i < min(sizeof(O)*8, size()); ++i) {
		if(b[i])
			res = res | (one<<i);
	}
	return res;
}

// write the bits of this integer directly into memory wherever output points. 
template<typename Wire>
template<typename T>
inline void Integer_T<Wire>::reveal(T * output, const int party) const {
	bool * b = new bool[size()];
	revealBools(b, party);
	from_bool(b, output, size());
	delete[] b;
}

template<typename Wire>
inline size_t Integer_T<Wire>::size() const {
	return bits.size();
}

//circuits
template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::abs() const {
	Integer_T<Wire> res(*this);
	for(size_t i = 0; i < size(); ++i)
		res[i] = bits[size()-1];
	return ( (*this) + res) ^ res;
}

template<typename Wire>
inline Integer_T<Wire>& Integer_T<Wire>::resize(size_t len, bool signed_extend) {
	Bit_T<Wire> MSB(false, PUBLIC); 
	if(signed_extend)
		MSB = bits[bits.size()-1];
	bits.resize(len, MSB);
	return *this;
}

//Logical operations
template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator^(const Integer_T<Wire>& rhs) const {
	Integer_T<Wire> res(*this);
	for(size_t i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] ^ rhs.bits[i];
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator^=(const Integer_T<Wire>& rhs) {
	for(size_t i = 0; i < size(); ++i)
		this->bits[i] ^= rhs.bits[i];
	return (*this);
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator|(const Integer_T<Wire>& rhs) const {
	Integer_T<Wire> res(*this);
	for(size_t i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] | rhs.bits[i];
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator&(const Integer_T<Wire>& rhs) const {
	Integer_T<Wire> res(*this);
	for(size_t i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] & rhs.bits[i];
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator<<(size_t shamt) const {
	Integer_T<Wire> res(*this);
	if(shamt > size()) {
		for(size_t i = 0; i < size(); ++i)
			res.bits[i] = false;
	}
	else {
		for(int i = size()-1; i >= shamt; --i)
			res.bits[i] = bits[i-shamt];
		for(int i = shamt-1; i>=0; --i)
			res.bits[i] = false;
	}
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator>>(size_t shamt) const {
	Integer_T<Wire> res(*this);
	if(shamt >size()) {
		for(size_t i = 0; i < size(); ++i)
			res.bits[i] = false;
	}
	else {
		for(size_t i = shamt; i < size(); ++i)
			res.bits[i-shamt] = bits[i];
		for(size_t i = size()-shamt; i < size(); ++i)
			res.bits[i] = false;
	}
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator<<(const Integer_T<Wire>& shamt) const {
	Integer_T<Wire> res(*this);
	for(size_t i = 0; i < min(size_t(ceil(log2(size()))) , shamt.size()-1); ++i)
		res = res.select(shamt[i], res<<(1<<i));
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator>>(const Integer_T<Wire>& shamt) const{
	Integer_T<Wire> res(*this);
	for(size_t i = 0; i <min(size_t(ceil(log2(size()))) , shamt.size()-1); ++i)
		res = res.select(shamt[i], res>>(1<<i));
	return res;
}

//Comparisons
template<typename Wire>
inline Bit_T<Wire> Integer_T<Wire>::geq (const Integer_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	Integer_T<Wire> thisExtend(*this), rhsExtend(rhs);
	thisExtend.resize(size()+1, true);
	rhsExtend.resize(size()+1, true);
	Integer_T<Wire> tmp = thisExtend-rhsExtend;
	return !tmp[tmp.size()-1];
}

template<typename Wire>
inline Bit_T<Wire> Integer_T<Wire>::equal(const Integer_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	Bit_T<Wire> res(true);
	for(size_t i = 0; i < size(); ++i)
		res = res & (bits[i] == rhs[i]);
	return res;
}

/* Arithmethics
 */
template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator+(const Integer_T<Wire> & rhs) const {
	assert(size() == rhs.size());
	Integer_T<Wire> res(*this);
	add_full(res.bits.data(), static_cast<Bit_T<Wire>*>(nullptr), bits.data(), rhs.bits.data(), static_cast<Bit_T<Wire>*>(nullptr), size());
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator-(const Integer_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	Integer_T<Wire> res(*this);
	sub_full(res.bits.data(), static_cast<Bit_T<Wire>*>(nullptr), bits.data(), rhs.bits.data(), static_cast<Bit_T<Wire>*>(nullptr), size());
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator*(const Integer_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	Integer_T<Wire> res(*this);
	mul_full(res.bits.data(), bits.data(), rhs.bits.data(), size());
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator/(const Integer_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	Integer_T<Wire> res(*this);
	Integer_T<Wire> i1 = abs();
	Integer_T<Wire> i2 = rhs.abs();
	Bit_T<Wire> sign = bits[size()-1] ^ rhs[size()-1];
	div_full(res.bits.data(), static_cast<Bit_T<Wire>*>(nullptr), i1.bits.data(), i2.bits.data(), size());
	condNeg(sign, res.bits.data(), res.bits.data(), size());
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator%(const Integer_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	Integer_T<Wire> res(*this);
	Integer_T<Wire> i1 = abs();
	Integer_T<Wire> i2 = rhs.abs();
	Bit_T<Wire> sign = bits[size()-1];
	div_full(static_cast<Bit_T<Wire>*>(nullptr), res.bits.data(), i1.bits.data(), i2.bits.data(), size());
	condNeg(sign, res.bits.data(), res.bits.data(), size());
	return res;
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::operator-() const {
	return Integer_T<Wire>(size(), 0, PUBLIC)-(*this);
}

//Others
template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::leading_zeros() const {
	Integer_T<Wire> res = *this;
	for(int i = size() - 2; i>=0; --i)
		res[i] = res[i+1] | res[i];

	for(size_t i = 0; i < res.size(); ++i)
		res[i] = !res[i];
	return res.hamming_weight();
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::hamming_weight() const {
	vector<Integer_T<Wire>> vec;
	for(size_t i = 0; i < size(); i++) {
		Integer_T<Wire> tmp(2, 0, PUBLIC);
		tmp[0] = bits[i];
		vec.push_back(tmp);
	}

	while(vec.size() > 1) {
		size_t j = 0;
		for(size_t i = 0; i < vec.size()-1; i+=2) {
			vec[j++] = vec[i]+vec[i+1];
		}
		if(vec.size()%2 == 1) {
			vec[j++] = vec[vec.size()-1];
		}
		for(size_t i = 0; i < j; ++i)
			vec[i].resize(vec[i].size()+1, false);
		size_t vec_size = vec.size();
		for(size_t i = j; i < vec_size; ++i)
			vec.pop_back();
	}
	return vec[0];
}

template<typename Wire>
inline Integer_T<Wire> Integer_T<Wire>::modExp(Integer_T<Wire> p, Integer_T<Wire> q) {
	// the value of q should be less than half of the MAX_INT
	Integer_T<Wire> base = *this;
	Integer_T<Wire> res(size(),1);
	for(size_t i = 0; i < p.size(); ++i) {
		Integer_T<Wire> tmp = (res * base) % q;
		res = res.select(p[i], tmp);
		base = (base*base) % q; 
	}
	return res;
}
