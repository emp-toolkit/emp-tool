template<typename T>
inline Bit<T>::Bit(bool b, int party) {
	if (party == PUBLIC)
		bit = T::circ_exec->public_label(b);
	else ProtocolExecution::prot_exec->feed(&bit, party, &b, 1); 
}

template<typename T>
inline Bit<T> Bit<T>::select(const Bit & select, const Bit & new_v) const{
	Bit tmp = *this;
	tmp = tmp ^ new_v;
	tmp = tmp & select;
	return *this ^ tmp;
}

template<typename T>
inline bool Bit<T>::reveal(int party) const {
	bool res;
	ProtocolExecution::prot_exec->reveal(&res, party, &bit, 1);
	return res;
}

/*template<typename T>
template<typename O = string>
inline O Bit<T>::reveal(int party) const {
	bool res;
	local_backend->Reveal(&res, party, &bit, 1);
	return res ? "true" : "false";
}*/



template<typename T>
inline Bit<T> Bit<T>::operator==(const Bit& rhs) const {
	return !(*this ^ rhs);
}

template<typename T>
inline Bit<T> Bit<T>::operator!=(const Bit& rhs) const {
	return (*this) ^ rhs;
}

template<typename T>
inline Bit<T> Bit<T>::operator &(const Bit& rhs) const{
	Bit res;
	res.bit = T::circ_exec->and_gate(bit, rhs.bit);
	return res;
}

template<typename T>
inline Bit<T> Bit<T>::operator ^(const Bit& rhs) const{
	Bit res;
	res.bit = T::circ_exec->xor_gate(bit, rhs.bit);
	return res;
}

template<typename T>
inline Bit<T> Bit<T>::operator |(const Bit& rhs) const{
	return (*this ^ rhs) ^ (*this & rhs);
}

template<typename T>
inline Bit<T> Bit<T>::operator!() const {
	return T::circ_exec->not_gate(bit);
}
