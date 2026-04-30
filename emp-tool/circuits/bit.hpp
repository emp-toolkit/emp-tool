template<typename Wire>
inline Bit_T<Wire>::Bit_T(bool b, int party) {
	backend->feed(&bit, party, &b, 1); 
}
template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::select(const Bit_T<Wire> & select, const Bit_T<Wire> & new_v) const{
	Bit_T<Wire> tmp = *this;
	tmp = tmp ^ new_v;
	tmp = tmp & select;
	return *this ^ tmp;
}

template<typename Wire>
template<typename O>
inline O Bit_T<Wire>::reveal(int party) const {
	bool res;
	backend->reveal(&res, party, &bit, 1);
	if constexpr (std::is_same_v<O, std::string>)
		return res ? "true" : "false";
	else
		return static_cast<O>(res);
}

template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::operator==(const Bit_T<Wire>& rhs) const {
	return !(*this ^ rhs);
}
template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::operator!=(const Bit_T<Wire>& rhs) const {
	return (*this) ^ rhs;
}

template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::operator &(const Bit_T<Wire>& rhs) const{
	Bit_T<Wire> res;
	backend->and_gate(&res.bit, &bit, &rhs.bit);
	return res;
}

template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::operator ^(const Bit_T<Wire>& rhs) const{
	Bit_T<Wire> res;
	backend->xor_gate(&res.bit, &bit, &rhs.bit);
	return res;
}

template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::operator ^=(const Bit_T<Wire>& rhs) {
	backend->xor_gate(&bit, &bit, &rhs.bit);
	return (*this);
}

template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::operator |(const Bit_T<Wire>& rhs) const{
	return (*this ^ rhs) ^ (*this & rhs);
}

template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::operator!() const {
	Bit_T<Wire> res;
	backend->not_gate(&res.bit, &bit);
	return res;
}

template<typename Wire>
inline Bit_T<Wire> Bit_T<Wire>::geq(const Bit_T & rhs) const {
	return ! ( (!this) & rhs);
}