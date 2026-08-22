#ifndef EMP_UTIL_BLOCK_HPP__
#define EMP_UTIL_BLOCK_HPP__

// Inline definitions for block.h's API. Included via block.h.

#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>

#include "emp-tool/runtime/core/simd_tier.h"   // per-function target-attr packs

namespace emp {

inline bool getLSB(const block & x) {
	return (x[0] & 1) == 1;
}

// Bit i of x as a bool (bit 0 is getLSB). Generic accessor; any protocol meaning
// pinned to a specific bit is documented at the use site, not here.
inline bool getBit(const block & x, int i) {
	expecting(i >= 0 && i < 128, "getBit: bit index out of range [0, 128)");
	return i < 64 ? ((x[0] >> i) & 1) == 1
	              : ((x[1] >> (i - 64)) & 1) == 1;
}

// `block` is a 128-bit vector type whose 64-bit-lane aggregate-init
// is constant-evaluable on both x86 (`__m128i`) and aarch64 (sse2neon
// typedef). `{(long long)low, (long long)high}` produces the same
// byte pattern as `_mm_set_epi64x(high, low)` / `vcombine_u64(low,
// high)` — low in lane 0, high in lane 1 — and the compiler lowers
// the runtime form to the same hardware ops. constexpr lets
// file-scope `inline constexpr block` constants compile-time-evaluate
// into .rodata; see docs/static_init.md for why that matters.
inline constexpr block makeBlock(uint64_t high, uint64_t low) {
	return block{(long long)low, (long long)high};
}

/* Linear orthomorphism function
 * [REF] Implementation of "Efficient and Secure Multiparty Computation from Fixed-Key Block Ciphers"
 * https://eprint.iacr.org/2019/074.pdf
 */
#ifdef __x86_64__
__attribute__((target("sse2")))
#endif
inline block sigma(block a) {
	return _mm_shuffle_epi32(a, 78) ^ (a & makeBlock(0xFFFFFFFFFFFFFFFF, 0x00));
}

inline constexpr block zero_block    = makeBlock(0, 0);
inline constexpr block all_one_block = makeBlock(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
inline constexpr block select_mask[2] = {zero_block, all_one_block};

// Blocks with a single low bit set (only bit 0 / only bit 1) — for isolating or
// flipping a low bit. Generic constants; a protocol that reserves a specific low
// bit (e.g. for a MAC/Δ encoding) documents that convention at its own use site.
inline constexpr block bit0_mask = makeBlock(0, 1);
inline constexpr block bit1_mask = makeBlock(0, 2);

inline block set_bit(const block & a, int i) {
	expecting(i >= 0 && i < 128, "set_bit: bit index out of range [0, 128)");
	if(i < 64)
		return makeBlock(0L, 1ULL<<i) | a;
	else
		return makeBlock(1ULL<<(i-64), 0) | a;
}

inline std::string to_hex(const void* data, size_t n) {
	expecting(n <= std::string{}.max_size() / 2,
	          "to_hex: input too large");
	static const char digits[] = "0123456789abcdef";
	const unsigned char* b = static_cast<const unsigned char*>(data);
	std::string s(2 * n, '0');
	for (size_t i = 0; i < n; ++i) {
		s[2 * i]     = digits[b[i] >> 4];
		s[2 * i + 1] = digits[b[i] & 0xf];
	}
	return s;
}

// A block renders as its 16 bytes in memory order — matching how reference
// vectors / digests / the wire-trace dumps are written.
inline std::ostream& operator<<(std::ostream& out, const block& blk) {
	return out << to_hex(&blk, sizeof(block));
}

inline void xorBlocks_arr(block* __restrict__ res, const block* __restrict__ x, const block* __restrict__ y, int64_t nblocks) {
	expecting(nblocks >= 0, "xorBlocks_arr: negative block count");
	const block * dest = nblocks+x;
	for (; x != dest;) {
		*(res++) = *(x++) ^ *(y++);
	}
}

inline void xorBlocksTo_arr(block* __restrict__ dst, const block* __restrict__ src, int64_t nblocks) {
	expecting(nblocks >= 0, "xorBlocksTo_arr: negative block count");
	const block * dest = nblocks+src;
	for (; src != dest;) {
		*dst = *dst ^ *(src++);
		++dst;
	}
}

inline void xorBlocks_arr(block* __restrict__ res, const block* __restrict__ x, block y, int64_t nblocks) {
	expecting(nblocks >= 0, "xorBlocks_arr: negative block count");
	const block * dest = nblocks+x;
	for (; x != dest;)
		*(res++) =  *(x++) ^ y;
}

#ifdef __x86_64__
__attribute__((target("sse4")))
#endif
inline bool cmpBlock(const block * x, const block * y, int64_t nblocks) {
	expecting(nblocks >= 0, "cmpBlock: negative block count");
	__m128i acc = _mm_setzero_si128();
	const block * dest = nblocks+x;
	for (; x != dest;)
		acc = _mm_or_si128(acc, _mm_xor_si128(*(x++), *(y++)));
	return _mm_testz_si128(acc, acc);
}


}  // namespace emp
#endif  // EMP_UTIL_BLOCK_HPP__
