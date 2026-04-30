// SignedInt_T<Wire> implementation.

template<typename Wire>
inline UnsignedInt_T<Wire> SignedInt_T<Wire>::as_unsigned() const {
	return UnsignedInt_T<Wire>(static_cast<const BitVec_T<Wire>&>(*this));
}

// Reciprocal lives on UnsignedInt_T; this fills out the back-edge so the
// .as_signed() declared on UnsignedInt_T resolves once both headers are
// included.
template<typename Wire>
inline SignedInt_T<Wire> UnsignedInt_T<Wire>::as_signed() const {
	return SignedInt_T<Wire>(static_cast<const BitVec_T<Wire>&>(*this));
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::resize(size_t new_width) const {
	SignedInt_T res;
	res.bits.resize(new_width);
	size_t copy = std::min(size(), new_width);
	for (size_t i = 0; i < copy; ++i) res.bits[i] = bits[i];
	// Sign-extend with the original MSB.
	Bit_T<Wire> msb = (size() == 0) ? Bit_T<Wire>(false, PUBLIC) : bits.back();
	for (size_t i = copy; i < new_width; ++i) res.bits[i] = msb;
	return res;
}

template<typename Wire>
inline UnsignedInt_T<Wire> SignedInt_T<Wire>::abs() const {
	// res = (*this + sign_extended_msb) ^ sign_extended_msb
	// which is the standard branchless |x| for two's complement.
	SignedInt_T smask;
	smask.bits.resize(size());
	for (size_t i = 0; i < size(); ++i) smask.bits[i] = bits.back();
	SignedInt_T res = (*this + smask);
	for (size_t i = 0; i < size(); ++i) res.bits[i] = res.bits[i] ^ smask.bits[i];
	return res.as_unsigned();
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator+(const SignedInt_T& rhs) const {
	assert(size() == rhs.size());
	SignedInt_T res(*this);
	add_full<Wire>(res.bits.data(), nullptr, bits.data(), rhs.bits.data(), nullptr, (int)size());
	return res;
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator-(const SignedInt_T& rhs) const {
	assert(size() == rhs.size());
	SignedInt_T res(*this);
	sub_full<Wire>(res.bits.data(), nullptr, bits.data(), rhs.bits.data(), nullptr, (int)size());
	return res;
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator-() const {
	SignedInt_T zero(size(), 0, PUBLIC);
	return zero - *this;
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator*(const SignedInt_T& rhs) const {
	assert(size() == rhs.size());
	SignedInt_T res(*this);
	mul_full<Wire>(res.bits.data(), bits.data(), rhs.bits.data(), (int)size());
	return res;
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator/(const SignedInt_T& rhs) const {
	assert(size() == rhs.size());
	UnsignedInt_T<Wire> a = abs();
	UnsignedInt_T<Wire> b = rhs.abs();
	SignedInt_T res(*this);
	Bit_T<Wire> sign = bits.back() ^ rhs.bits.back();
	div_full<Wire>(res.bits.data(), nullptr, a.bits.data(), b.bits.data(), (int)size());
	cond_neg<Wire>(sign, res.bits.data(), res.bits.data(), (int)size());
	return res;
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator%(const SignedInt_T& rhs) const {
	assert(size() == rhs.size());
	UnsignedInt_T<Wire> a = abs();
	UnsignedInt_T<Wire> b = rhs.abs();
	SignedInt_T res(*this);
	Bit_T<Wire> sign = bits.back();   // C99: sign of % matches dividend
	div_full<Wire>(nullptr, res.bits.data(), a.bits.data(), b.bits.data(), (int)size());
	cond_neg<Wire>(sign, res.bits.data(), res.bits.data(), (int)size());
	return res;
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator&(const SignedInt_T& rhs) const {
	return SignedInt_T(BitVec_T<Wire>::operator&(rhs));
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator|(const SignedInt_T& rhs) const {
	return SignedInt_T(BitVec_T<Wire>::operator|(rhs));
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator^(const SignedInt_T& rhs) const {
	return SignedInt_T(BitVec_T<Wire>::operator^(rhs));
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator~() const {
	return SignedInt_T(BitVec_T<Wire>::operator~());
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator<<(size_t shamt) const {
	return SignedInt_T(BitVec_T<Wire>::operator<<(shamt));
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator>>(size_t shamt) const {
	SignedInt_T res;
	res.bits.resize(size());
	Bit_T<Wire> msb = (size() == 0) ? Bit_T<Wire>(false, PUBLIC) : bits.back();
	if (shamt >= size()) {
		for (size_t i = 0; i < size(); ++i) res.bits[i] = msb;
		return res;
	}
	for (size_t i = 0; i + shamt < size(); ++i)
		res.bits[i] = bits[i + shamt];
	for (size_t i = size() - shamt; i < size(); ++i)
		res.bits[i] = msb;
	return res;
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator<<(const UnsignedInt_T<Wire>& shamt) const {
	SignedInt_T res(*this);
	size_t bound = std::min((size_t)std::ceil(std::log2((double)size())), shamt.size() - 1);
	for (size_t i = 0; i < bound; ++i)
		res = res.select(shamt.bits[i], res << (size_t(1) << i));
	return res;
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::operator>>(const UnsignedInt_T<Wire>& shamt) const {
	SignedInt_T res(*this);
	size_t bound = std::min((size_t)std::ceil(std::log2((double)size())), shamt.size() - 1);
	for (size_t i = 0; i < bound; ++i)
		res = res.select(shamt.bits[i], res >> (size_t(1) << i));
	return res;
}

template<typename Wire>
inline Bit_T<Wire> SignedInt_T<Wire>::geq(const SignedInt_T& rhs) const {
	// Sign-extend by 1, subtract, check sign of result. No overflow possible.
	assert(size() == rhs.size());
	SignedInt_T a = this->resize(size() + 1);
	SignedInt_T b = rhs.resize(size() + 1);
	SignedInt_T diff = a - b;
	return !diff.bits.back();
}

template<typename Wire>
inline Bit_T<Wire> SignedInt_T<Wire>::equal(const SignedInt_T& rhs) const {
	return BitVec_T<Wire>::equal(rhs);
}

template<typename Wire>
inline SignedInt_T<Wire> SignedInt_T<Wire>::select(const Bit_T<Wire>& sel,
                                                    const SignedInt_T& rhs) const {
	return SignedInt_T(BitVec_T<Wire>::select(sel, rhs));
}
