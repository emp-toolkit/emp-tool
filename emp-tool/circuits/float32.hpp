template<typename Wire>
inline Float_T<Wire>::Float_T(float input, int party) {
	int *in = (int*)(&input);
	BitVec_T<Wire> val(FLOAT_LEN, *in, party);
	for(int i = 0; i < FLOAT_LEN; ++i)
		value[i] = val.bits[i];
}

template<typename Wire>
template<typename O>
inline O Float_T<Wire>::reveal(int party) const {
	int out = 0;
	for(int i = 0; i < FLOAT_LEN; ++i) {
		int tmp = value[i].template reveal<bool>(party);
		out += (tmp << i);
	}
	float *fp = (float*)(&out);
	if constexpr (std::is_same_v<O, std::string>)
		return std::to_string(*fp);
	else
		return (O)(*fp);
}

template<typename Wire>
inline Float_T<Wire> Float_T<Wire>::abs() const {
	Float_T<Wire> res(*this);
	res[FLOAT_LEN-1] = Bit_T<Wire>(false, PUBLIC);
	return res;
}

template<typename Wire>
inline Float_T<Wire> Float_T<Wire>::select(const Bit_T<Wire>& select, const Float_T<Wire> & d) const {
	Float_T<Wire> res(*this);
	for(int i = 0; i < 32; ++i)
		res.value[i] = value[i].select(select, d.value[i]);
	return res;
}

template<typename Wire>
inline Bit_T<Wire>& Float_T<Wire>::operator[](int index) {
	return value[std::min(index, FLOAT_LEN-1)];
}

template<typename Wire>
inline const Bit_T<Wire> &Float_T<Wire>::operator[](int index) const {
	return value[std::min(index, FLOAT_LEN-1)];
}

template<typename Wire>
inline Float_T<Wire> Float_T<Wire>::operator-() const {
	Float_T<Wire> res(*this);
	res[31] = !res[31];
	return res;
}

template<typename Wire>
inline Float_T<Wire> Float_T<Wire>::operator^(const Float_T<Wire>& rhs) const {
	Float_T<Wire> res(*this);
	for(int i = 0; i < 32; ++i)
		res[i] = res[i] ^ rhs[i];
	return res;
}

template<typename Wire>
inline Float_T<Wire> Float_T<Wire>::operator^=(const Float_T<Wire>& rhs) {
	for(int i = 0; i < 32; ++i)
		value[i] ^= rhs[i];
	return (*this);
}

template<typename Wire>
inline Float_T<Wire> Float_T<Wire>::operator&(const Float_T<Wire>& rhs) const {
	Float_T<Wire> res(*this);
	for(int i = 0; i < 32; ++i)
		res[i] = res[i] & rhs[i];
	return res;
}

