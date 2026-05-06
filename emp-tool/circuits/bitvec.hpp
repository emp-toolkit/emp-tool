// BitVec_T<Wire> implementation.

// Bit-pattern ctor: BitVec has no signedness concept, so the bytes
// past sizeof(T) are always zero-extended. UnsignedInt inherits this
// straight; SignedInt overrides the ctor to sign-extend instead so
// `Integer(64, (int)-1, …)` round-trips through the int's sign bit.
template<typename Wire>
template<typename T, typename>
inline BitVec_T<Wire>::BitVec_T(size_t width, T value, int party) {
	bits.resize(width);
	bool* tmp = new bool[width];

	constexpr size_t value_bits = sizeof(T) * 8;
	const     size_t copy_bits  = std::min(width, value_bits);

	bits_to_bools(tmp, &value, copy_bits);
	if (width > value_bits)
		std::fill(tmp + value_bits, tmp + width, false);

	backend->feed(bits.data(), party, tmp, width);
	delete[] tmp;
}

template<typename Wire>
inline BitVec_T<Wire>::BitVec_T(size_t width, const void* data, int party) {
	bits.resize(width);
	bool* tmp = new bool[width];
	bits_to_bools(tmp, data, width);
	backend->feed(bits.data(), party, tmp, width);
	delete[] tmp;
}

// Bitwise ops dispatch through Backend's bulk gate API: one virtual
// call instead of N. Backends that override the bulk variants (e.g.
// HalfGate) get the tight inner loop; others fall back to N scalar
// dispatches via the Backend default.

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::operator&(const BitVec_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	BitVec_T res(size());
	if (size() > 0)
		backend->and_gate_n(&res.bits[0].bit, &bits[0].bit, &rhs.bits[0].bit, size());
	return res;
}

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::operator|(const BitVec_T<Wire>& rhs) const {
	// (a | b) = (a ^ b) ^ (a & b) — two passes through the bulk API.
	assert(size() == rhs.size());
	BitVec_T axb(size()), aab(size()), res(size());
	if (size() > 0) {
		backend->xor_gate_n(&axb.bits[0].bit, &bits[0].bit, &rhs.bits[0].bit, size());
		backend->and_gate_n(&aab.bits[0].bit, &bits[0].bit, &rhs.bits[0].bit, size());
		backend->xor_gate_n(&res.bits[0].bit, &axb.bits[0].bit, &aab.bits[0].bit, size());
	}
	return res;
}

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::operator^(const BitVec_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	BitVec_T res(size());
	if (size() > 0)
		backend->xor_gate_n(&res.bits[0].bit, &bits[0].bit, &rhs.bits[0].bit, size());
	return res;
}

template<typename Wire>
inline BitVec_T<Wire>& BitVec_T<Wire>::operator^=(const BitVec_T<Wire>& rhs) {
	assert(size() == rhs.size());
	if (size() > 0)
		backend->xor_gate_n(&bits[0].bit, &bits[0].bit, &rhs.bits[0].bit, size());
	return *this;
}

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::operator~() const {
	BitVec_T res(size());
	if (size() > 0)
		backend->not_gate_n(&res.bits[0].bit, &bits[0].bit, size());
	return res;
}

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::operator<<(size_t shamt) const {
	BitVec_T res(size());
	if (shamt >= size()) {
		Bit_T<Wire> zero(false, PUBLIC);
		for (size_t i = 0; i < size(); ++i) res.bits[i] = zero;
		return res;
	}
	Bit_T<Wire> zero(false, PUBLIC);
	for (size_t i = 0; i < shamt; ++i)
		res.bits[i] = zero;
	for (size_t i = shamt; i < size(); ++i)
		res.bits[i] = bits[i - shamt];
	return res;
}

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::operator>>(size_t shamt) const {
	BitVec_T res(size());
	Bit_T<Wire> zero(false, PUBLIC);
	if (shamt >= size()) {
		for (size_t i = 0; i < size(); ++i) res.bits[i] = zero;
		return res;
	}
	for (size_t i = 0; i + shamt < size(); ++i)
		res.bits[i] = bits[i + shamt];
	for (size_t i = size() - shamt; i < size(); ++i)
		res.bits[i] = zero;
	return res;
}

template<typename Wire>
inline Bit_T<Wire> BitVec_T<Wire>::equal(const BitVec_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	Bit_T<Wire> acc(true, PUBLIC);
	for (size_t i = 0; i < size(); ++i)
		acc = acc & (bits[i] == rhs.bits[i]);
	return acc;
}

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::concat(const BitVec_T<Wire>& hi) const {
	BitVec_T res(size() + hi.size());
	for (size_t i = 0; i < size(); ++i)
		res.bits[i] = bits[i];
	for (size_t i = 0; i < hi.size(); ++i)
		res.bits[size() + i] = hi.bits[i];
	return res;
}

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::slice(size_t lo, size_t hi) const {
	assert(lo <= hi && hi <= size());
	BitVec_T res(hi - lo);
	for (size_t i = lo; i < hi; ++i)
		res.bits[i - lo] = bits[i];
	return res;
}

template<typename Wire>
inline BitVec_T<Wire> BitVec_T<Wire>::select(const Bit_T<Wire>& sel,
                                             const BitVec_T<Wire>& rhs) const {
	assert(size() == rhs.size());
	BitVec_T res(size());
	for (size_t i = 0; i < size(); ++i)
		res.bits[i] = bits[i].select(sel, rhs.bits[i]);
	return res;
}

template<typename Wire>
template<typename O>
inline O BitVec_T<Wire>::reveal(int party) const {
	if constexpr (std::is_same_v<O, std::string>) {
		std::string out;
		out.reserve(size());
		bool* b = new bool[size()];
		backend->reveal(b, party, bits.data(), size());
		for (size_t i = size(); i-- > 0; )
			out += b[i] ? '1' : '0';
		delete[] b;
		return out;
	} else {
		static_assert(std::is_integral_v<O> || std::is_same_v<O, __uint128_t>
		              || std::is_same_v<O, __int128>,
		              "BitVec_T::reveal<O>(): O must be integral or std::string");
		size_t n = std::min(sizeof(O) * 8, size());
		bool* b = new bool[n];
		backend->reveal(b, party, bits.data(), n);
		O res = 0;
		for (size_t i = 0; i < n; ++i)
			if (b[i]) res = res | (O(1) << i);
		delete[] b;
		return res;
	}
}

template<typename Wire>
inline void BitVec_T<Wire>::reveal(void* output, int party) const {
	bool* b = new bool[size()];
	backend->reveal(b, party, bits.data(), size());
	bools_to_bits(output, b, size());
	delete[] b;
}
