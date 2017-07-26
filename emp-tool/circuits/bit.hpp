inline Bit::Bit(bool b, int party) {
	if (party == PUBLIC)
		bit = local_gc->public_label(b);
	else local_backend->Feed(&bit, party, &b, 1); 
}

inline Bit Bit::select(const Bit & select, const Bit & new_v) const{
	Bit tmp = *this;
	tmp = tmp ^ new_v;
	tmp = tmp & select;
	return *this ^ tmp;
}

template<typename O>
inline O Bit::reveal(int party) const {
	O res;
	local_backend->Reveal(&res, party, &bit, 1);
	return res;
}

template<>
inline string Bit::reveal<string>(int party) const {
	bool res;
	local_backend->Reveal(&res, party, &bit, 1);
	return res ? "true" : "false";
}



inline Bit Bit::operator==(const Bit& rhs) const {
	return !(*this ^ rhs);
}

inline Bit Bit::operator!=(const Bit& rhs) const {
	return (*this) ^ rhs;
}

inline Bit Bit::operator &(const Bit& rhs) const{
	Bit res;
	res.bit = local_gc->gc_and(bit, rhs.bit);
	return res;
}
inline Bit Bit::operator ^(const Bit& rhs) const{
	Bit res;
	res.bit = local_gc->gc_xor(bit, rhs.bit);
	return res;
}

inline Bit Bit::operator |(const Bit& rhs) const{
	return (*this ^ rhs) ^ (*this & rhs);
}

inline Bit Bit::operator!() const {
	return local_gc->gc_not(bit);
}
