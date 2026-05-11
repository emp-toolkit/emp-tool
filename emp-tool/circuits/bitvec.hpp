// BitVec_T<Wire> implementation.

// Bit-pattern ctor: BitVec has no signedness concept, so the bytes
// past sizeof(T) are always zero-extended. UnsignedInt inherits this
// straight; SignedInt overrides the ctor to sign-extend instead so
// `Integer(64, (int)-1, …)` round-trips through the int's sign bit.
//
// `tmp` is a per-bit unpack of `value` handed to backend->feed. RAII
// via std::unique_ptr<bool[]> so the (rare) exception path doesn't leak.
template<typename Wire>
template<typename T, typename>
inline BitVec_T<Wire>::BitVec_T(size_t width, T value, int party) {
	bits.resize(width);
	auto tmp = std::make_unique<bool[]>(width);

	constexpr size_t value_bits = sizeof(T) * 8;
	const     size_t copy_bits  = std::min(width, value_bits);

	bits_to_bools(tmp.get(), &value, copy_bits);
	if (width > value_bits)
		std::fill(tmp.get() + value_bits, tmp.get() + width, false);

	backend->feed(bits.data(), party, tmp.get(), width);
}

template<typename Wire>
inline BitVec_T<Wire>::BitVec_T(size_t width, const void* data, int party) {
	bits.resize(width);
	auto tmp = std::make_unique<bool[]>(width);
	bits_to_bools(tmp.get(), data, width);
	backend->feed(bits.data(), party, tmp.get(), width);
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

// `b` is the unpacked-bit scratch handed back from backend->reveal. RAII
// via std::unique_ptr<bool[]> so error paths (e.g. reveal throws on a
// disconnected channel) can't leak it.
//
// Sign-bit shift safety: `O(1) << (sizeof(O)*8 - 1)` is UB in C++17 when
// O is signed (the shift lands a 1 in the sign bit). Common in practice:
// reveal<int32_t>() on a value with bit 31 set, e.g. negative ints or
// INT32_MIN. Accumulate into the matching unsigned type instead and
// memcpy back — same bit pattern, no UB on either path.
template<typename Wire>
template<typename O>
inline O BitVec_T<Wire>::reveal(int party) const {
	if constexpr (std::is_same_v<O, std::string>) {
		std::string out;
		out.reserve(size());
		auto b = std::make_unique<bool[]>(size());
		backend->reveal(b.get(), party, bits.data(), size());
		for (size_t i = size(); i-- > 0; )
			out += b[i] ? '1' : '0';
		return out;
	} else {
		static_assert(std::is_integral_v<O> || std::is_same_v<O, __uint128_t>
		              || std::is_same_v<O, __int128>,
		              "BitVec_T::reveal<O>(): O must be integral or std::string");
		size_t n = std::min(sizeof(O) * 8, size());
		auto b = std::make_unique<bool[]>(n);
		backend->reveal(b.get(), party, bits.data(), n);
		if constexpr (std::is_unsigned_v<O> || std::is_same_v<O, __uint128_t>) {
			// Unsigned arithmetic doesn't have the sign-bit-shift UB.
			O res = 0;
			for (size_t i = 0; i < n; ++i)
				if (b[i]) res = res | (O(1) << i);
			return res;
		} else {
			// Signed O: accumulate into the matching unsigned type so we
			// never shift a 1 into the sign bit. __int128 isn't covered by
			// std::make_unsigned in the standard, so spell that case manually.
			using U = std::conditional_t<std::is_same_v<O, __int128>,
			                             __uint128_t,
			                             std::make_unsigned_t<O>>;
			U acc = 0;
			for (size_t i = 0; i < n; ++i)
				if (b[i]) acc = acc | (U(1) << i);
			O res;
			std::memcpy(&res, &acc, sizeof(O));
			return res;
		}
	}
}

template<typename Wire>
inline void BitVec_T<Wire>::reveal(void* output, int party) const {
	auto b = std::make_unique<bool[]>(size());
	backend->reveal(b.get(), party, bits.data(), size());
	bools_to_bits(output, b.get(), size());
}
