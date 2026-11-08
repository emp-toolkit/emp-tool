#ifndef EMP_CIRCUIT_FLOAT_TRAITS_H__
#define EMP_CIRCUIT_FLOAT_TRAITS_H__

// Per-width IEEE-754 parameters (FloatTraits<W>) + host <-> bit-pattern codec, and
// the on-disk fp<W>_* circuit loader (circuit::float_circuit). These are the only
// float pieces the context-bound Float_T<Ctx,W> (circuits/float.h) depends on, so
// they live here, free of any wire-bound value-class headers. Widths 16/32/64 map
// to the binary16/binary32/binary64 .empbc suites (prefix fp16/fp32/fp64). The
// host value type is `float` for fp16/fp32 and `double` for fp64; fp16 has no
// portable C++ scalar, so its conversions are done in software (RNE).

#include "emp-tool/ir/builtins.h"
#include <math.h>
#include <cstdint>
#include <cstring>

namespace emp {

inline uint16_t emp_float_to_half(float f) {
	uint32_t x;
	std::memcpy(&x, &f, sizeof(x));
	uint32_t sign = (x >> 16) & 0x8000u;
	uint32_t fexp = (x >> 23) & 0xFFu;
	uint32_t mant = x & 0x7FFFFFu;
	if (fexp == 0xFF)
		return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0u) | (mant >> 13));
	int e = (int)fexp - 127 + 15;
	if (e >= 0x1F) return (uint16_t)(sign | 0x7C00u);
	if (e <= 0) {
		if (e < -10) return (uint16_t)sign;
		mant |= 0x800000u;
		int shift = 14 - e;
		uint32_t h = mant >> shift;
		uint32_t rem = mant & ((1u << shift) - 1);
		uint32_t half = 1u << (shift - 1);
		if (rem > half || (rem == half && (h & 1u))) ++h;
		return (uint16_t)(sign | h);
	}
	uint32_t h = sign | ((uint32_t)e << 10) | (mant >> 13);
	uint32_t rem = mant & 0x1FFFu;
	if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
	return (uint16_t)h;
}

inline float emp_half_to_float(uint16_t h) {
	uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
	uint32_t exp  = (h >> 10) & 0x1Fu;
	uint32_t mant = h & 0x3FFu;
	uint32_t f;
	if (exp == 0) {
		if (mant == 0) {
			f = sign;
		} else {
			int e = 127 - 15 + 1;
			while (!(mant & 0x400u)) { mant <<= 1; --e; }
			mant &= 0x3FFu;
			f = sign | ((uint32_t)e << 23) | (mant << 13);
		}
	} else if (exp == 0x1F) {
		f = sign | 0x7F800000u | (mant << 13);
	} else {
		f = sign | ((exp - 15 + 127) << 23) | (mant << 13);
	}
	float r;
	std::memcpy(&r, &f, sizeof(r));
	return r;
}

template<int W> struct FloatTraits;

template<> struct FloatTraits<32> {
	using host_t = float;
	static constexpr int LEN = 32, BIAS = 127, SGNFC_LEN = 23, EXPNT_LEN = 8;
	static uint64_t to_bits(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }
	static host_t from_bits(uint64_t b) { uint32_t u = (uint32_t)b; float f; std::memcpy(&f, &u, 4); return f; }
};

template<> struct FloatTraits<64> {
	using host_t = double;
	static constexpr int LEN = 64, BIAS = 1023, SGNFC_LEN = 52, EXPNT_LEN = 11;
	static uint64_t to_bits(double v) { uint64_t b; std::memcpy(&b, &v, 8); return b; }
	static host_t from_bits(uint64_t b) { double d; std::memcpy(&d, &b, 8); return d; }
};

template<> struct FloatTraits<16> {
	using host_t = float;
	static constexpr int LEN = 16, BIAS = 15, SGNFC_LEN = 10, EXPNT_LEN = 5;
	static uint64_t to_bits(float v) { return emp_float_to_half(v); }
	static host_t from_bits(uint64_t b) { return emp_half_to_float((uint16_t)b); }
};

}  // namespace emp
#endif  // EMP_CIRCUIT_FLOAT_TRAITS_H__
