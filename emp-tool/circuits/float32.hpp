inline Float::Float(float input, int party) {
	int *in = (int*)(&input);
	Integer val = Integer(FLOAT_LEN, *in, party);
	for(int i = 0; i < FLOAT_LEN; ++i)
		value[i] = val.bits[i];
}

template<>
inline string Float::reveal<string>(int party) const {
	int out = 0;
	for(int i = FLOAT_LEN-1; i >= 0; --i) {
		out <<= 1;
		out += value[i].reveal<bool>(party);
	}
	float *fp = (float*)(&out);
	return std::to_string(*fp);
}

template<>
inline double Float::reveal<double>(int party) const {
	int out = 0;
	for(int i = 0; i < FLOAT_LEN; ++i) {	
		int tmp = value[i].reveal<bool>(party);
		out += (tmp << i);
	}
	float *fp = (float*)(&out);
	return (double)*fp;
}

inline Float Float::abs() const {
	Float res(*this);
	res[FLOAT_LEN-1] = Bit(false, PUBLIC);
	return res;
}

inline Float Float::If(const Bit& select, const Float & d) {
	Float res(*this);
	for(int i = 0; i < 32; ++i)
		res.value[i] = value[i].If(select, d.value[i]);
	return res;
}

inline Bit& Float::operator[](int index) {
	return value[min(index, FLOAT_LEN-1)];
}

inline const Bit &Float::operator[](int index) const {
	return value[min(index, FLOAT_LEN-1)];
}




inline Float Float::operator-() const {
	Float res(*this);
	res[31] = !res[31];
	return res;
}

inline Float Float::operator^(const Float& rhs) const {
	Float res(*this);
	for(int i = 0; i < 32; ++i)
		res[i] = res[i] ^ rhs[i];
	return res;
}

inline Float Float::operator^=(const Float& rhs) {
	for(int i = 0; i < 32; ++i)
		value[i] ^= rhs[i];
	return (*this);
}

inline Float Float::operator&(const Float& rhs) const {
	Float res(*this);
	for(int i = 0; i < 32; ++i)
		res[i] = res[i] & rhs[i];
	return res;
}

