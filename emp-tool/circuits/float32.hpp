// IEEE-754 round-trip: extract / reinject the 32 bits via uint32_t and
// std::memcpy. The previous pointer-cast form (`int *in = (int*)&input`
// and `float *fp = (float*)&out`) violates strict aliasing, and the
// `tmp << i` accumulator was a signed-shift UB when i == 31 placed a 1
// into the sign bit. Unsigned arithmetic + memcpy fixes both.
template<typename Wire>
inline Float_T<Wire>::Float_T(float input, int party) {
	uint32_t bits;
	std::memcpy(&bits, &input, sizeof(bits));
	BitVec_T<Wire> val(FLOAT_LEN, bits, party);
	for(int i = 0; i < FLOAT_LEN; ++i)
		value[i] = val.bits[i];
}

template<typename Wire>
template<typename O>
inline O Float_T<Wire>::reveal(int party) const {
	uint32_t bits = 0;
	for(int i = 0; i < FLOAT_LEN; ++i) {
		bool tmp = value[i].template reveal<bool>(party);
		if (tmp) bits |= (uint32_t(1) << i);
	}
	float fp;
	std::memcpy(&fp, &bits, sizeof(fp));
	if constexpr (std::is_same_v<O, std::string>)
		return std::to_string(fp);
	else
		return (O)fp;
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

// ---------------------------------------------------------------------------
// Float ↔ fixed-point integer conversions.
//
// IEEE-754 single-precision: (-1)^sign · 1.mantissa · 2^(exp_field - BIAS).
// A non-negative integer with `s` fractional bits represents value · 2^-s.
//
// Float → uint:
//   Treat the 24-bit "1.mantissa" as a 24-bit unsigned int M = (1 << 23) |
//   mantissa. The float's value is M · 2^(exp_field - BIAS - SGNFC_LEN).
//   Multiplying by 2^s gives M · 2^(exp_field - BIAS - SGNFC_LEN + s),
//   so we shift M by (exp_field - (BIAS + SGNFC_LEN - s)). Negative shift
//   means right-shift; past the working width means underflow to 0.
//
// Uint → float:
//   Find the leading-1 position k in the input. The value is
//   (2^k + low_bits) · 2^-s. Align so the leading 1 lands at bit SGNFC_LEN
//   of a 24-bit field (gives "1.mantissa"), then exp_field = k + BIAS - s.
//   The leading 1 sits at bit SGNFC_LEN of the aligned value, which is past
//   the 23-bit mantissa copy and is implicitly stripped — matches IEEE.
//
// Preconditions (caller's responsibility — bodies do NOT check):
//   - Float input is finite and not subnormal (NaN / Inf / subnormals
//     produce garbage; the code does not fault).
//   - For to_unsigned/to_signed: |value · 2^s| fits in the chosen N.
//   - For uint/int → float: the result fits in float's representable range.
//
// Edge cases handled:
//   - Float == 0.0 (exp_field == 0): to_unsigned returns 0.
//   - int == 0:                      to_float32 returns +0.0.
// ---------------------------------------------------------------------------

template<typename Wire>
template<int N>
inline UnsignedInt_T<Wire, N> Float_T<Wire>::to_unsigned(size_t s) const {
	// 1. Build 24-bit significand: bits[0..22] = mantissa, bit[23] = implicit 1.
	UnsignedInt_T<Wire, 0> sig(SGNFC_LEN + 1, 0u, PUBLIC);
	for (int i = 0; i < SGNFC_LEN; ++i) sig.bits[i] = value[i];
	sig.bits[SGNFC_LEN] = Bit_T<Wire>(true, PUBLIC);

	// 2. Zero-extend to N bits (N is the receiver width chosen by the caller).
	UnsignedInt_T<Wire, 0> sig_N = sig.resize(N);

	// 3. Compute the shift = exp_field - (BIAS + SGNFC_LEN - s) in 16-bit
	//    signed. 16 bits is enough headroom for any reasonable s (up to ~32k).
	constexpr size_t W = 16;
	SignedInt_T<Wire, 0> exp_s(W, 0, PUBLIC);
	for (int i = 0; i < EXPNT_LEN; ++i) exp_s.bits[i] = value[SGNFC_LEN + i];
	int64_t bias_const = (int64_t)(BIAS + SGNFC_LEN) - (int64_t)s;
	SignedInt_T<Wire, 0> shift  = exp_s - SignedInt_T<Wire, 0>(W, bias_const, PUBLIC);
	SignedInt_T<Wire, 0> nshift = -shift;

	// 4. Apply the shift. The shift operator on UnsignedInt requires shamt
	//    to share the receiver's width, so widen via resize. Both branches
	//    are computed; the unselected one's "garbage" shift amount is masked
	//    out by the If/Else select.
	UnsignedInt_T<Wire, 0> shamtL = shift.as_unsigned().resize(N);
	UnsignedInt_T<Wire, 0> shamtR = nshift.as_unsigned().resize(N);
	UnsignedInt_T<Wire, 0> shifted_left  = sig_N << shamtL;
	UnsignedInt_T<Wire, 0> shifted_right = sig_N >> shamtR;
	Bit_T<Wire> shift_is_neg = shift.bits.back();
	UnsignedInt_T<Wire, 0> shifted = If(shift_is_neg)
	                                     .Then(shifted_right)
	                                     .Else(shifted_left);

	// 5. Zero guard: if exp_field == 0 (input is ±0 or subnormal), return 0.
	Bit_T<Wire> exp_nonzero = value[SGNFC_LEN];
	for (int i = 1; i < EXPNT_LEN; ++i)
		exp_nonzero = exp_nonzero | value[SGNFC_LEN + i];
	UnsignedInt_T<Wire, 0> zeroN(N, 0u, PUBLIC);
	shifted = If(exp_nonzero).Then(shifted).Else(zeroN);

	// 6. Rebrand as fixed-N return type.
	return UnsignedInt_T<Wire, N>(static_cast<const BitVec_T<Wire>&>(shifted));
}

template<typename Wire>
template<int N>
inline SignedInt_T<Wire, N> Float_T<Wire>::to_signed(size_t s) const {
	// Magnitude path, then negate if the IEEE sign bit is set.
	SignedInt_T<Wire, N> mag = this->template to_unsigned<N>(s).as_signed();
	SignedInt_T<Wire, N> neg = -mag;
	return If(value[FLOAT_LEN - 1]).Then(neg).Else(mag);
}

template<typename Wire, size_t N>
inline Float_T<Wire> UnsignedInt_T<Wire, N>::to_float32(size_t s) const {
	using F = Float_T<Wire>;

	// Work in runtime-width so shift sizes can mismatch freely.
	UnsignedInt_T<Wire, 0> u = this->resize(this->size());

	// Detect zero (any bit set).
	Bit_T<Wire> any_set = u.bits[0];
	for (size_t i = 1; i < u.size(); ++i) any_set = any_set | u.bits[i];

	// 1. firstOneIdx = (size-1) - leading_zeros, in 8-bit signed.
	//    For input==0, lz==size and firstOneIdx wraps; the zero guard at
	//    the end discards the resulting garbage Float.
	UnsignedInt_T<Wire, 0> lz = u.leading_zeros();
	SignedInt_T<Wire, 0> firstOneIdx =
		SignedInt_T<Wire, 0>(8, (int64_t)(u.size() - 1), PUBLIC)
		- lz.resize(8).as_signed();

	// 2. Align the leading 1 to bit SGNFC_LEN (= 23):
	//      firstOneIdx > 23 → shift RIGHT by (firstOneIdx - 23) to bring
	//                         the leading 1 down to bit 23.
	//      firstOneIdx < 23 → shift LEFT  by (23 - firstOneIdx) to bring
	//                         the leading 1 up to bit 23.
	SignedInt_T<Wire, 0> twentyThree(8, (int64_t)F::SGNFC_LEN, PUBLIC);
	Bit_T<Wire> leftShift = firstOneIdx < twentyThree;
	SignedInt_T<Wire, 0> shamtL_s = twentyThree  - firstOneIdx;
	SignedInt_T<Wire, 0> shamtR_s = firstOneIdx - twentyThree;
	UnsignedInt_T<Wire, 0> shamtL = shamtL_s.as_unsigned().resize(u.size());
	UnsignedInt_T<Wire, 0> shamtR = shamtR_s.as_unsigned().resize(u.size());
	UnsignedInt_T<Wire, 0> shifted_left  = u << shamtL;
	UnsignedInt_T<Wire, 0> shifted_right = u >> shamtR;
	UnsignedInt_T<Wire, 0> shifted = If(leftShift)
	                                     .Then(shifted_left)
	                                     .Else(shifted_right);

	// 3. IEEE exponent field = firstOneIdx + (BIAS - s).
	int64_t bias_offset = (int64_t)F::BIAS - (int64_t)s;
	SignedInt_T<Wire, 0> exponent = firstOneIdx
		+ SignedInt_T<Wire, 0>(8, bias_offset, PUBLIC);

	// 4. Pack: bit FLOAT_LEN-1 = sign (0 here), bits SGNFC_LEN..FLOAT_LEN-2
	//    = exponent (low EXPNT_LEN bits), bits 0..SGNFC_LEN-1 = shifted
	//    (the leading 1 lands at bit SGNFC_LEN of `shifted` and is dropped).
	F output(0.0f, PUBLIC);
	output.value[F::FLOAT_LEN - 1] = Bit_T<Wire>(false, PUBLIC);
	for (int i = 0; i < F::EXPNT_LEN; ++i)
		output.value[F::SGNFC_LEN + i] = exponent.bits[i];
	for (int i = 0; i < F::SGNFC_LEN; ++i)
		output.value[i] = shifted.bits[i];

	// 5. Zero guard.
	F zero_float(0.0f, PUBLIC);
	return If(any_set).Then(output).Else(zero_float);
}

template<typename Wire, size_t N>
inline Float_T<Wire> SignedInt_T<Wire, N>::to_float32(size_t s) const {
	using F = Float_T<Wire>;
	UnsignedInt_T<Wire, N> mag = abs();
	F mag_f = mag.to_float32(s);
	// XOR the input's sign bit into the IEEE sign position.
	mag_f.value[F::FLOAT_LEN - 1] =
		mag_f.value[F::FLOAT_LEN - 1] ^ this->bits.back();
	return mag_f;
}

