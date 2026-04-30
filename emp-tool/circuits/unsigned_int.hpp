// UnsignedInt_T<Wire> implementation.

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::resize(size_t new_width) const {
	UnsignedInt_T res;
	res.bits.resize(new_width);
	size_t copy = std::min(size(), new_width);
	for (size_t i = 0; i < copy; ++i) res.bits[i] = bits[i];
	Bit_T<Wire> zero(false, PUBLIC);
	for (size_t i = copy; i < new_width; ++i) res.bits[i] = zero;
	return res;
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator+(const UnsignedInt_T& rhs) const {
	assert(size() == rhs.size());
	UnsignedInt_T res(*this);
	add_full<Wire>(res.bits.data(), nullptr, bits.data(), rhs.bits.data(), nullptr, (int)size());
	return res;
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator-(const UnsignedInt_T& rhs) const {
	assert(size() == rhs.size());
	UnsignedInt_T res(*this);
	sub_full<Wire>(res.bits.data(), nullptr, bits.data(), rhs.bits.data(), nullptr, (int)size());
	return res;
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator-() const {
	UnsignedInt_T zero(size(), 0u, PUBLIC);
	return zero - *this;
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator*(const UnsignedInt_T& rhs) const {
	assert(size() == rhs.size());
	UnsignedInt_T res(*this);
	mul_full<Wire>(res.bits.data(), bits.data(), rhs.bits.data(), (int)size());
	return res;
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator/(const UnsignedInt_T& rhs) const {
	assert(size() == rhs.size());
	UnsignedInt_T res(*this);
	div_full<Wire>(res.bits.data(), nullptr, bits.data(), rhs.bits.data(), (int)size());
	return res;
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator%(const UnsignedInt_T& rhs) const {
	assert(size() == rhs.size());
	UnsignedInt_T res(*this);
	div_full<Wire>(nullptr, res.bits.data(), bits.data(), rhs.bits.data(), (int)size());
	return res;
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator&(const UnsignedInt_T& rhs) const {
	return UnsignedInt_T(BitVec_T<Wire>::operator&(rhs));
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator|(const UnsignedInt_T& rhs) const {
	return UnsignedInt_T(BitVec_T<Wire>::operator|(rhs));
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator^(const UnsignedInt_T& rhs) const {
	return UnsignedInt_T(BitVec_T<Wire>::operator^(rhs));
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator~() const {
	return UnsignedInt_T(BitVec_T<Wire>::operator~());
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator<<(size_t shamt) const {
	return UnsignedInt_T(BitVec_T<Wire>::operator<<(shamt));
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator>>(size_t shamt) const {
	return UnsignedInt_T(BitVec_T<Wire>::operator>>(shamt));
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator<<(const UnsignedInt_T& shamt) const {
	UnsignedInt_T res(*this);
	size_t bound = std::min((size_t)std::ceil(std::log2((double)size())), shamt.size() - 1);
	for (size_t i = 0; i < bound; ++i)
		res = res.select(shamt.bits[i], res << (size_t(1) << i));
	return res;
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::operator>>(const UnsignedInt_T& shamt) const {
	UnsignedInt_T res(*this);
	size_t bound = std::min((size_t)std::ceil(std::log2((double)size())), shamt.size() - 1);
	for (size_t i = 0; i < bound; ++i)
		res = res.select(shamt.bits[i], res >> (size_t(1) << i));
	return res;
}

template<typename Wire>
inline Bit_T<Wire> UnsignedInt_T<Wire>::geq(const UnsignedInt_T& rhs) const {
	// Zero-extend by 1 bit, subtract, check sign bit. If result MSB == 1,
	// we borrowed (this < rhs). Otherwise this ≥ rhs.
	assert(size() == rhs.size());
	UnsignedInt_T a = this->resize(size() + 1);
	UnsignedInt_T b = rhs.resize(size() + 1);
	UnsignedInt_T diff = a - b;
	return !diff.bits.back();
}

template<typename Wire>
inline Bit_T<Wire> UnsignedInt_T<Wire>::equal(const UnsignedInt_T& rhs) const {
	return BitVec_T<Wire>::equal(rhs);
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::select(const Bit_T<Wire>& sel,
                                                       const UnsignedInt_T& rhs) const {
	return UnsignedInt_T(BitVec_T<Wire>::select(sel, rhs));
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::leading_zeros() const {
	UnsignedInt_T sat = *this;
	for (int i = (int)size() - 2; i >= 0; --i)
		sat.bits[i] = sat.bits[i + 1] | sat.bits[i];
	for (size_t i = 0; i < size(); ++i)
		sat.bits[i] = !sat.bits[i];
	return sat.hamming_weight();
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::hamming_weight() const {
	std::vector<UnsignedInt_T> vec;
	for (size_t i = 0; i < size(); ++i) {
		UnsignedInt_T tmp(2, 0u, PUBLIC);
		tmp.bits[0] = bits[i];
		vec.push_back(tmp);
	}
	while (vec.size() > 1) {
		size_t j = 0;
		for (size_t i = 0; i + 1 < vec.size(); i += 2)
			vec[j++] = vec[i] + vec[i + 1];
		if (vec.size() % 2 == 1)
			vec[j++] = vec.back();
		for (size_t i = 0; i < j; ++i)
			vec[i] = vec[i].resize(vec[i].size() + 1);
		vec.resize(j);
	}
	return vec[0];
}

template<typename Wire>
inline UnsignedInt_T<Wire> UnsignedInt_T<Wire>::mod_exp(UnsignedInt_T p,
                                                        UnsignedInt_T q) const {
	UnsignedInt_T base = *this;
	UnsignedInt_T res(size(), 1u, PUBLIC);
	for (size_t i = 0; i < p.size(); ++i) {
		UnsignedInt_T tmp = (res * base) % q;
		res = res.select(p.bits[i], tmp);
		base = (base * base) % q;
	}
	return res;
}
