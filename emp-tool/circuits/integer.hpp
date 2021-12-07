//https://github.com/samee/obliv-c/blob/obliv-c/src/ext/oblivc/obliv_bits.c#L1487
inline void add_full(Bit* dest, Bit * carryOut, const Bit * op1, const Bit * op2,
		const Bit * carryIn, int size) {
	Bit carry, bxc, axc, t;
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
	skipLast = (carryOut == nullptr);
	while(size-->skipLast) { 
		axc = op1[i] ^ carry;
		bxc = op2[i] ^ carry;
		dest[i] = op1[i] ^ bxc;
		t = axc & bxc;
		carry =carry^t;
		++i;
	}
	if(carryOut != nullptr)
		*carryOut = carry;
	else
		dest[i] = carry ^ op2[i] ^ op1[i];
}
inline void sub_full(Bit * dest, Bit * borrowOut, const Bit * op1, const Bit * op2,
		const Bit * borrowIn, int size) {
	Bit borrow,bxc,bxa,t;
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
	skipLast = (borrowOut == nullptr);
	while(size-- > skipLast) {
		bxa = op1[i] ^ op2[i];
		bxc = borrow ^ op2[i];
		dest[i] = bxa ^ borrow;
		t = bxa & bxc;
		borrow = borrow ^ t;
		++i;
	}
	if(borrowOut != nullptr) {
		*borrowOut = borrow;
	}
	else
		dest[i] = op1[i] ^ op2[i] ^ borrow;
}
inline void mul_full(Bit * dest, const Bit * op1, const Bit * op2, int size) {
	Bit * sum = new Bit[size];
	Bit * temp = new Bit[size];
	for(int i = 0; i < size; ++i)sum[i]=false;
	for(int i=0;i<size;++i) {
		for (int k = 0; k < size-i; ++k)
			temp[k] = op1[k] & op2[i];
		add_full(sum+i, nullptr, sum+i, temp, nullptr, size-i);
	}
	memcpy(dest, sum, sizeof(Bit)*size);
	delete[] sum;
	delete[] temp;
}

inline void ifThenElse(Bit * dest, const Bit * tsrc, const Bit * fsrc, 
		int size, Bit cond) {
	Bit x, a;
	int i = 0;
	while(size-- > 0) {
		x = tsrc[i] ^ fsrc[i];
		a = cond & x;
		dest[i] = a ^ fsrc[i];
		++i;
	}
}
inline void condNeg(Bit cond, Bit * dest, const Bit * src, int size) {
	int i;
	Bit c = cond;
	for(i=0; i < size-1; ++i) {
		dest[i] = src[i] ^ cond;
		Bit t  = dest[i] ^ c;
		c = c & dest[i];
		dest[i] = t;
	}
	dest[i] = cond ^ c ^ src[i];
}

inline void div_full(Bit * vquot, Bit * vrem, const Bit * op1, const Bit * op2, 
		int size) {
	Bit * overflow = new Bit[size];
	Bit * temp = new Bit[size];
	Bit * rem = new Bit[size];
	Bit * quot = new Bit[size];
	Bit b;
	memcpy(rem, op1, size*sizeof(Bit));
	overflow[0] = false;
	for(int i  = 1; i < size;++i)
		overflow[i] = overflow[i-1] | op2[size-i];
	// skip AND on last bit if borrowOut==NULL
	for(int i = size-1; i >= 0; --i) {
		sub_full(temp, &b, rem+i, op2, nullptr, size-i);
		b = b | overflow[i];
		ifThenElse(rem+i,rem+i,temp,size-i,b);
		quot[i] = !b;
	}
	if(vrem != nullptr) memcpy(vrem, rem, size*sizeof(Bit));
	if(vquot != nullptr) memcpy(vquot, quot, size*sizeof(Bit));
	delete[] overflow;
	delete[] temp;
	delete[] rem;
	delete[] quot;
}


inline void Integer::init(bool * b, int len, int party) {
	bits.resize(len);
	if (party == PUBLIC) {
		block one = CircuitExecution::circ_exec->public_label(true);
		block zero = CircuitExecution::circ_exec->public_label(false);
		for(int i = 0; i < len; ++i)
			bits[i] = b[i] ? one : zero;
	}
	else {
		ProtocolExecution::prot_exec->feed((block *)bits.data(), party, b, len); 
	}
}

inline Integer::Integer(int len, int64_t input, int party) {
	bool* b = new bool[len];
	int_to_bool<int64_t>(b, input, len);
	init(b, len, party);
	delete[] b;
}

template<typename T>
inline Integer::Integer(int len, T * input, int party) {
	bool* b = new bool[len];
	to_bool<T>(b, input, len);
	init(b, len, party);
	delete[] b;
}

template<typename T>
inline Integer::Integer(T * input, int party) {
	size_t len = 8 * sizeof(T);
	bool* b = new bool[len];
	to_bool<T>(b, input, len);
	init(b, len, party);
	delete[] b;
}


inline Integer Integer::select(const Bit & select, const Integer & a) const{
	assert(size() == a.size());
	Integer res(*this);
	for(size_t i = 0; i < size(); ++i)
		res[i] = bits[i].select(select, a[i]);
	return res;
}

inline Bit& Integer::operator[](size_t index) {
	return bits[min(index, size()-1)];
}

inline const Bit &Integer::operator[](size_t index) const {
	return bits[min(index, size()-1)];
}

inline void Integer::revealBools(bool *bools, int party) const {
	ProtocolExecution::prot_exec->reveal(bools, party, (block *)bits.data(), size());
}

template<>
inline uint32_t Integer::reveal<uint32_t>(int party) const {
	std::bitset<32> bs;
	bs.reset();
	bool b[size()];
	ProtocolExecution::prot_exec->reveal(b, party, (block *)bits.data(), size());
	for (size_t i = 0; i < min(32UL, size()); ++i)
		bs.set(i, b[i]);
	return bs.to_ulong();
}

template<>
inline uint64_t Integer::reveal<uint64_t>(int party) const {
	std::bitset<64> bs;
	bs.reset();
	bool b[size()];
	ProtocolExecution::prot_exec->reveal(b, party, (block *)bits.data(), size());
	for (size_t i = 0; i < min(64UL, size()); ++i)
		bs.set(i, b[i]);
	return bs.to_ullong();
}
template<>
inline int32_t Integer::reveal<int32_t>(int party) const {
	return reveal<uint32_t>(party);
}

template<>
inline int64_t Integer::reveal<int64_t>(int party) const {
	return reveal<uint64_t>(party);
}


template<>
inline string Integer::reveal<string>(int party) const {
	bool * b = new bool[size()];
	string res = "";
	revealBools(b, party);
	for(size_t i = 0; i < size(); ++i)
		res+=(b[i]? "1" : "0");
	delete[] b;
	return res;
}

// write the bits of this integer directly into memory wherever output points. 
template<typename T>
inline void Integer::reveal(T * output, const int party) const {
	bool * b = new bool[size()];
	revealBools(b, party);
	from_bool(b, output, size());
	delete[] b;
}


inline size_t Integer::size() const {
	return bits.size();
}

//circuits
inline Integer Integer::abs() const {
	Integer res(*this);
	for(size_t i = 0; i < size(); ++i)
		res[i] = bits[size()-1];
	return ( (*this) + res) ^ res;
}

inline Integer& Integer::resize(size_t len, bool signed_extend) {
	Bit MSB(false, PUBLIC); 
	if(signed_extend)
		MSB = bits[bits.size()-1];
	bits.resize(len, MSB);
	return *this;
}

//Logical operations
inline Integer Integer::operator^(const Integer& rhs) const {
	Integer res(*this);
	for(size_t i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] ^ rhs.bits[i];
	return res;
}

inline Integer Integer::operator^=(const Integer& rhs) {
	for(size_t i = 0; i < size(); ++i)
		this->bits[i] ^= rhs.bits[i];
	return (*this);
}

inline Integer Integer::operator|(const Integer& rhs) const {
	Integer res(*this);
	for(size_t i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] | rhs.bits[i];
	return res;
}

inline Integer Integer::operator&(const Integer& rhs) const {
	Integer res(*this);
	for(size_t i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] & rhs.bits[i];
	return res;
}

inline Integer Integer::operator<<(size_t shamt) const {
	Integer res(*this);
	if(shamt > size()) {
		for(size_t i = 0; i < size(); ++i)
			res.bits[i] = false;
	}
	else {
		for(int i = size()-1; i >= (int)shamt; --i)
			res.bits[i] = bits[i-shamt];
		for(int i = shamt-1; i>=0; --i)
			res.bits[i] = false;
	}
	return res;
}

inline Integer Integer::operator>>(size_t shamt) const {
	Integer res(*this);
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

inline Integer Integer::operator<<(const Integer& shamt) const {
	Integer res(*this);
	for(size_t i = 0; i < min(size_t(ceil(log2(size()))) , shamt.size()-1); ++i)
		res = res.select(shamt[i], res<<(1<<i));
	return res;
}

inline Integer Integer::operator>>(const Integer& shamt) const{
	Integer res(*this);
	for(size_t i = 0; i <min(size_t(ceil(log2(size()))) , shamt.size()-1); ++i)
		res = res.select(shamt[i], res>>(1<<i));
	return res;
}

//Comparisons
inline Bit Integer::geq (const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer thisExtend(*this), rhsExtend(rhs);
	thisExtend.resize(size()+1, true);
	rhsExtend.resize(size()+1, true);
	Integer tmp = thisExtend-rhsExtend;
	return !tmp[tmp.size()-1];
}

inline Bit Integer::equal(const Integer& rhs) const {
	assert(size() == rhs.size());
	Bit res(true);
	for(size_t i = 0; i < size(); ++i)
		res = res & (bits[i] == rhs[i]);
	return res;
}

/* Arithmethics
 */
inline Integer Integer::operator+(const Integer & rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	add_full(res.bits.data(), nullptr, bits.data(), rhs.bits.data(), nullptr, size());
	return res;
}

inline Integer Integer::operator-(const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	sub_full(res.bits.data(), nullptr, bits.data(), rhs.bits.data(), nullptr, size());
	return res;
}

inline Integer Integer::operator*(const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	mul_full(res.bits.data(), bits.data(), rhs.bits.data(), size());
	return res;
}

inline Integer Integer::operator/(const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	Integer i1 = abs();
	Integer i2 = rhs.abs();
	Bit sign = bits[size()-1] ^ rhs[size()-1];
	div_full(res.bits.data(), nullptr, i1.bits.data(), i2.bits.data(), size());
	condNeg(sign, res.bits.data(), res.bits.data(), size());
	return res;
}
inline Integer Integer::operator%(const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	Integer i1 = abs();
	Integer i2 = rhs.abs();
	Bit sign = bits[size()-1];
	div_full(nullptr, res.bits.data(), i1.bits.data(), i2.bits.data(), size());
	condNeg(sign, res.bits.data(), res.bits.data(), size());
	return res;
}

inline Integer Integer::operator-() const {
	return Integer(size(), 0, PUBLIC)-(*this);
}

//Others
inline Integer Integer::leading_zeros() const {
	Integer res = *this;
	for(int i = size() - 2; i>=0; --i)
		res[i] = res[i+1] | res[i];

	for(size_t i = 0; i < res.size(); ++i)
		res[i] = !res[i];
	return res.hamming_weight();
}

inline Integer Integer::hamming_weight() const {
	vector<Integer> vec;
	for(size_t i = 0; i < size(); i++) {
		Integer tmp(2, 0, PUBLIC);
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
inline Integer Integer::modExp(Integer p, Integer q) {
	// the value of q should be less than half of the MAX_INT
	Integer base = *this;
	Integer res(size(),1);
	for(size_t i = 0; i < p.size(); ++i) {
		Integer tmp = (res * base) % q;
		res = res.select(p[i], tmp);
		base = (base*base) % q; 
	}
	return res;
}
