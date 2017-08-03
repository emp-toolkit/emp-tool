//https://github.com/samee/obliv-c/blob/obliv-c/src/ext/oblivc/obliv_bits.c#L1487
template<typename T>
inline void add_full(Bit<T>* dest, Bit<T> * carryOut, const Bit<T> * op1, const Bit<T> * op2,
		const Bit<T> * carryIn, int size) {
	Bit<T> carry, bxc, axc, t;
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
template<typename T>
inline void sub_full(Bit<T> * dest, Bit<T> * borrowOut, const Bit<T> * op1, const Bit<T> * op2,
		const Bit<T> * borrowIn, int size) {
	Bit<T> borrow,bxc,bxa,t;
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
template<typename T>
inline void mul_full(Bit<T> * dest, const Bit<T> * op1, const Bit<T> * op2, int size) {
	//	OblivBit<T> temp[MAX_BITS]={},sum[MAX_BITS]={};
	Bit<T> * sum = new Bit<T>[size];
	Bit<T> * temp = new Bit<T>[size];
	for(int i = 0; i < size; ++i)sum[i]=false;
	for(int i=0;i<size;++i) {
		for (int k = 0; k < size-i; ++k)
			temp[k] = op1[k] & op2[i];
		add_full<T>(sum+i, nullptr, sum+i, temp, nullptr, size-i);
	}
	memcpy(dest, sum, sizeof(Bit<T>)*size);
	delete[] sum;
	delete[] temp;
}

template<typename T>
inline void ifThenElse(Bit<T> * dest, const Bit<T> * tsrc, const Bit<T> * fsrc, 
		int size, Bit<T> cond) {
	Bit<T> x, a;
	int i = 0;
	while(size-- > 0) {
		x = tsrc[i] ^ fsrc[i];
		a = cond & x;
		dest[i] = a ^ fsrc[i];
		++i;
	}
}
template<typename T>
inline void condNeg(Bit<T> cond, Bit<T> * dest, const Bit<T> * src, int size) {
	int i;
	Bit<T> c = cond;
	for(i = 0; i < size-1; ++i) {
		dest[i] = src[i] ^ cond;
//		cout << dest[i].reveal(PUBLIC)<<endl;
		Bit<T> t  = dest[i] ^ c;
		c = c & dest[i];
		dest[i] = t;
	}
	dest[i] = cond ^ c ^ src[i];
}

template<typename T>
inline void div_full(Bit<T> * vquot, Bit<T> * vrem, const Bit<T> * op1, const Bit<T> * op2, 
		int size) {
	Bit<T> * overflow = new Bit<T>[size];
	Bit<T> * temp = new Bit<T>[size];
	Bit<T> * rem = new Bit<T>[size];
	Bit<T> * quot = new Bit<T>[size];
	Bit<T> b;
	memcpy(rem, op1, size*sizeof(Bit<T>));
	overflow[0] = false;
	for(int i  = 1; i < size;++i)
		overflow[i] = overflow[i-1] | op2[size-i];
	// skip AND on last bit if borrowOut==NULL
	for(int i = size-1; i >= 0; --i) {
		sub_full<T>(temp, &b, rem+i, op2, nullptr, size-i);
		b = b | overflow[i];
		ifThenElse(rem+i,rem+i,temp,size-i,b);
		quot[i] = !b;
	}
	if(vrem != nullptr) memcpy(vrem, rem, size*sizeof(Bit<T>));
	if(vquot != nullptr) memcpy(vquot, quot, size*sizeof(Bit<T>));
	delete[] overflow;
	delete[] temp;
	delete[] rem;
	delete[] quot;
}


template<typename T>
inline void init(Bit<T> * bits, const bool* b, int length, int party) {
	block * bbits = new block[length];
	if (party == PUBLIC) {
		block one = T::circ_exec->public_label(true);
		block zero = T::circ_exec->public_label(false);
		for(int i = 0; i < length; ++i)
			bbits[i] = b[i] ? one : zero;
	}
	else {
		ProtocolExecution::prot_exec->feed(bbits, party, b, length); 
	}
	for(int i = 0; i < length; ++i)
		bits[i].bit = bbits[i];
	delete[] bbits;
}

/*inline Integer<T>::Integer(const bool * b, int length, int party) {
  bits = new Bit<T>[length];
  init(bits,b,length, party);
  }*/

template<typename T>
inline int Integer<T>::reveal(int party) const {
	bool * b = new bool[length];
	ProtocolExecution::prot_exec->reveal(b, party, (block *)bits,  length);
	string bin="";
	for(int i = length-1; i >= 0; --i)
		bin += (b[i]? '1':'0');
	delete [] b;
	string s = bin_to_dec(bin);
	return stoi(s);
}

template<typename T>
inline Integer<T>::Integer(int len, const string& str, int party) : length(len) {
	bool* b = new bool[len];
	bool_data(b, len, str);
	bits = new Bit<T>[length];
	init(bits,b,length, party);
	delete[] b;
}

template<typename T>
inline Integer<T>::Integer(int len, long long input, int party)
	: Integer(len, std::to_string(input), party) {
	}

template<typename T>
inline Integer<T> Integer<T>::select(const Bit<T> & select, const Integer & a) const{
	Integer res(*this);
	for(int i = 0; i < size(); ++i)
		res[i] = bits[i].select(select, a[i]);
	return res;
}

template<typename T>
inline Bit<T>& Integer<T>::operator[](int index) {
	return bits[min(index, size()-1)];
}

template<typename T>
inline const Bit<T> &Integer<T>::operator[](int index) const {
	return bits[min(index, size()-1)];
}




template<typename T>
inline int Integer<T>::size() const {
	return length;
}

//circuits
template<typename T>
inline Integer<T> Integer<T>::abs() const {
	Integer res(*this);
	for(int i = 0; i < size(); ++i)
		res[i] = bits[size()-1];
	return ( (*this) + res) ^ res;
}

template<typename T>
inline Integer<T>& Integer<T>::resize(int len, bool signed_extend) {
	Bit<T> * old = bits;
	bits = new Bit<T>[len];
	memcpy(bits, old, min(len, size())*sizeof(Bit<T>));
	Bit<T> extended_bit = old[length-1] & signed_extend;
	for(int i = min(len, size()); i < len; ++i)
		bits[i] = extended_bit;
	this->length = len;
	delete[] old;
	return *this;
}

//Logical operations
template<typename T>
inline Integer<T> Integer<T>::operator^(const Integer& rhs) const {
	Integer res(*this);
	for(int i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] ^ rhs.bits[i];
	return res;
}

template<typename T>
inline Integer<T> Integer<T>::operator|(const Integer& rhs) const {
	Integer res(*this);
	for(int i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] | rhs.bits[i];
	return res;
}

template<typename T>
inline Integer<T> Integer<T>::operator&(const Integer& rhs) const {
	Integer res(*this);
	for(int i = 0; i < size(); ++i)
		res.bits[i] = res.bits[i] & rhs.bits[i];
	return res;
}

template<typename T>
inline Integer<T> Integer<T>::operator<<(int shamt) const {
	Integer res(*this);
	if(shamt > size()) {
		for(int i = 0; i < size(); ++i)
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

template<typename T>
inline Integer<T> Integer<T>::operator>>(int shamt) const {
	Integer res(*this);
	if(shamt >size()) {
		for(int i = 0; i < size(); ++i)
			res.bits[i] = false;
	}
	else {
		for(int i = shamt; i < size(); ++i)
			res.bits[i-shamt] = bits[i];
		for(int i = size()-shamt; i < size(); ++i)
			res.bits[i] = false;
	}
	return res;
}

template<typename T>
inline Integer<T> Integer<T>::operator<<(const Integer& shamt) const {
	Integer res(*this);
	for(int i = 0; i < min(int(ceil(log2(size()))) , shamt.size()-1); ++i)
		res = res.select(shamt[i], res<<(1<<i));
	return res;
}

template<typename T>
inline Integer<T> Integer<T>::operator>>(const Integer& shamt) const{
	Integer res(*this);
	for(int i = 0; i <min(int(ceil(log2(size()))) , shamt.size()-1); ++i)
		res = res.select(shamt[i], res>>(1<<i));
	return res;
}

template<typename T>
//Comparisons
inline Bit<T> Integer<T>::geq (const Integer& rhs) const {
	assert(size() == rhs.size());
/*	Bit<T> res(false);
	for(int i = 0; i < size(); ++i) {
		res = ((bits[i]^res)&(rhs[i]^res))^bits[i];
	} 
	return res; 
*/
	Integer tmp = (*this) - rhs;
	return !tmp[tmp.size()-1];
}

template<typename T>
inline Bit<T> Integer<T>::equal(const Integer& rhs) const {
	assert(size() == rhs.size());
	Bit<T> res(true);
	for(int i = 0; i < size(); ++i)
		res = res & (bits[i] == rhs[i]);
	return res;
}

/* Arithmethics
 */
template<typename T>
inline Integer<T> Integer<T>::operator+(const Integer & rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	add_full<T>(res.bits, nullptr, bits, rhs.bits, nullptr, size());
	return res;
}

template<typename T>
inline Integer<T> Integer<T>::operator-(const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	sub_full<T>(res.bits, nullptr, bits, rhs.bits, nullptr, size());
	return res;
}


template<typename T>
inline Integer<T> Integer<T>::operator*(const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	mul_full<T>(res.bits, bits, rhs.bits, size());
	return res;
}

template<typename T>
inline Integer<T> Integer<T>::operator/(const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	Integer res2(*this);
	Integer i1 = abs();
	Integer i2 = rhs.abs();
	Bit<T> sign = bits[size()-1] ^ rhs[size()-1];
	div_full<T>(res.bits, nullptr, i1.bits, i2.bits, size());
	condNeg<T>(sign, res2.bits, res.bits, size());
	return res2;
}
template<typename T>
inline Integer<T> Integer<T>::operator%(const Integer& rhs) const {
	assert(size() == rhs.size());
	Integer res(*this);
	Integer res2(*this);
	Integer i1 = abs();
	Integer i2 = rhs.abs();
	Bit<T> sign = bits[size()-1];
	div_full<T>(nullptr, res.bits, i1.bits, i2.bits, size());
	condNeg(sign, res2.bits, res.bits, size());
	return res2;
}

template<typename T>
inline Integer<T> Integer<T>::operator-() const {
	return Integer(size(), 0, PUBLIC)-(*this);
}

//Others
template<typename T>
inline Integer<T> Integer<T>::leading_zeros() const {
	Integer res = *this;
	for(int i = size() - 2; i>=0; --i)
		res[i] = res[i+1] | res[i];

	for(int i = 0; i < res.size(); ++i)
		res[i] = !res[i];
	return res.hamming_weight();
}

template<typename T>
inline Integer<T> Integer<T>::hamming_weight() const {
	vector<Integer> vec;
	for(int i = 0; i < size(); i++) {
		Integer tmp(2, 0, PUBLIC);
		tmp[0] = bits[i];
		vec.push_back(tmp);
	}

	while(vec.size() > 1) {
		int j = 0;
		for(size_t i = 0; i < vec.size()-1; i+=2) {
			vec[j++] = vec[i]+vec[i+1];
		}
		if(vec.size()%2 == 1) {
			vec[j++] = vec[vec.size()-1];
		}
		for(int i = 0; i < j; ++i)
			vec[i].resize(vec[i].size()+1, false);
		int vec_size = vec.size();
		for(int i = j; i < vec_size; ++i)
			vec.pop_back();
	}
	return vec[0];
}
template<typename T>
inline Integer<T> Integer<T>::modExp(Integer p, Integer q) {
	// the value of q should be less than half of the MAX_INT
	Integer base = *this;
	Integer res(1, size());
	for(int i = 0; i < p.size(); ++i) {
		Integer tmp = (res * base) % q;
		res.select(p[i], tmp);
		base = (base*base) % q; 
	}
	return res;
}
