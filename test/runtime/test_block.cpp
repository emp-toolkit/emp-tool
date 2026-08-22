// core/block.h — 128-bit SIMD block (__m128i alias) plus the small bit/byte
// helpers everything else in emp-tool builds on. Read example() first; the
// rest is verification.
//
// What's in block.h:
//   block, makeBlock(hi, lo), zero_block, all_one_block
//   getLSB(b), set_bit(b, i)
//   sigma(b)                                  linear orthomorphism (Guo et al.)
//   to_hex(data, n)                           lowercase byte-order hex
//   xorBlocks_arr(res, x, y, n)               element-wise XOR
//   xorBlocks_arr(res, x, y_block, n)         broadcast XOR
//   xorBlocksTo_arr(dst, src, n)              in-place dst[i] ^= src[i]
//   cmpBlock(x, y, n)                         constant-ish equality
//   sse_trans(out, in, nrows, ncols)          bit-matrix transpose
//   bytes_to_bits32 / bits32_to_bytes         32 bools <-> 32 bits
//   bools_to_bits / bits_to_bools             N bools <-> N bits

#include "emp-tool/runtime/core/simd_tier.h"
#include "emp-tool/emp-tool.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

static bool check_direct_simd_tier_include() {
#if EMP_HAS_AVX2
	(void)&emp::detail::sse_trans_n128_avx2;
#endif
#if EMP_HAS_AVX512BW
	(void)&emp::detail::sse_trans_n128_avx512bw;
#endif
#if EMP_HAS_GFNI256 && !EMP_HAS_GFNI512
	(void)&emp::detail::sse_trans_n128_gfni256;
#endif
#if EMP_HAS_GFNI512
	(void)&emp::detail::sse_trans_n128_gfni;
#endif
	return true;
}

template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) dup2(devnull, 2);
		f();
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

// ---------- example ----------

static void example() {
	cout << "=== example ===\n";

	// (1) Build a block, set a bit, read its LSB.
	block a = makeBlock(0xCAFEBABEULL, 0xDEADBEEFULL);
	cout << "  a                  = " << a << "\n";
	cout << "  getLSB(a)          = " << getLSB(a) << "\n";
	cout << "  set_bit(zero, 65)  = " << set_bit(zero_block, 65) << "\n";

	// (2) sigma is the linear orthomorphism σ((H,L)) = (H^L, H) used in the
	// JustGarble construction. Linear: σ(x ^ y) = σ(x) ^ σ(y).
	cout << "  sigma(a)           = " << sigma(a) << "\n";

	// (3) XOR helpers.
	block xs[3] = {a, a, a}, ys[3] = {sigma(a), sigma(a), sigma(a)}, res[3];
	xorBlocks_arr(res, xs, ys, 3);
	cout << "  xorBlocks_arr[0]   = " << res[0] << "\n";
	xorBlocksTo_arr(res, ys, 3);              // res ^= ys, leaving res = xs
	cout << "  after ^= ys, res[0] = " << res[0] << "  (= a)\n";

	// (4) bool[] <-> packed bits.
	bool bools[16] = {1,0,1,1, 0,0,1,0, 1,1,1,0, 0,1,0,1};
	uint8_t packed[2] = {0, 0};
	bools_to_bits(packed, bools, 16);
	cout << "  bools_to_bits      = 0x" << hex << setw(2) << setfill('0')
	     << (int)packed[0] << (int)packed[1] << dec << setfill(' ') << "\n";

	// (5) sse_trans: transpose a 16x16 bit matrix; transposing twice = identity.
	uint8_t in[32], mid[32], out[32];
	for (int i = 0; i < 32; ++i) in[i] = (uint8_t)(i * 17 + 3);
	sse_trans(mid, in, 16, 16);
	sse_trans(out, mid, 16, 16);
	cout << "  sse_trans round-trip in[0..3] = "
	     << hex << setfill('0')
	     << setw(2) << (int)in[0] << setw(2) << (int)in[1]
	     << setw(2) << (int)in[2] << setw(2) << (int)in[3]
	     << dec << setfill(' ') << "  out[0..3] = "
	     << hex << setfill('0')
	     << setw(2) << (int)out[0] << setw(2) << (int)out[1]
	     << setw(2) << (int)out[2] << setw(2) << (int)out[3]
	     << dec << setfill(' ') << "\n";
}

// ---------- correctness ----------

static bool blocks_eq(block a, block b) {
	__m128i d = _mm_xor_si128(a, b);
	return _mm_testz_si128(d, d);
}

static bool check_makeBlock_getLSB() {
	for (int t = 0; t < 64; ++t) {
		uint64_t lo = ((uint64_t)t * 0x9E3779B97F4A7C15ULL);
		uint64_t hi = ((uint64_t)t * 0xBF58476D1CE4E5B9ULL);
		block b = makeBlock(hi, lo);
		uint64_t got_lo, got_hi;
		memcpy(&got_lo, (const char *)&b + 0, 8);
		memcpy(&got_hi, (const char *)&b + 8, 8);
		if (got_lo != lo || got_hi != hi) return false;
		if (getLSB(b) != (bool)(lo & 1)) return false;
	}
	return true;
}

static bool check_set_bit() {
	PRG prg;
	for (int t = 0; t < 32; ++t) {
		block b; prg.random_block(&b, 1);
		for (int i = 0; i < 128; ++i) {
			block c = set_bit(b, i);
			// c == b OR'd with single-bit mask.
			uint64_t mlo = (i < 64) ? (1ULL << i) : 0;
			uint64_t mhi = (i >= 64) ? (1ULL << (i - 64)) : 0;
			block mask = makeBlock(mhi, mlo);
			if (!blocks_eq(c, b | mask)) return false;
			// And bit i is set in c.
			uint64_t got_lo, got_hi;
			memcpy(&got_lo, (const char *)&c + 0, 8);
			memcpy(&got_hi, (const char *)&c + 8, 8);
			bool bit = i < 64 ? ((got_lo >> i) & 1) : ((got_hi >> (i - 64)) & 1);
			if (!bit) return false;
		}
	}
	return true;
}

static bool check_sigma_linear_and_formula() {
	// sigma((H, L)) = (H ^ L, H). Verify the formula directly, then linearity.
	PRG prg;
	for (int t = 0; t < 256; ++t) {
		block a; prg.random_block(&a, 1);
		uint64_t lo, hi;
		memcpy(&lo, (const char *)&a + 0, 8);
		memcpy(&hi, (const char *)&a + 8, 8);
		block want = makeBlock(hi ^ lo, hi);
		if (!blocks_eq(sigma(a), want)) return false;

		block b; prg.random_block(&b, 1);
		if (!blocks_eq(sigma(a ^ b), sigma(a) ^ sigma(b))) return false;
	}
	return true;
}

static bool check_xorBlocks_arr() {
	PRG prg;
	for (int sz : {1, 7, 33, 1024}) {
		vector<block> x(sz), y(sz), res(sz), want(sz);
		prg.random_block(x.data(), sz);
		prg.random_block(y.data(), sz);
		xorBlocks_arr(res.data(), x.data(), y.data(), sz);
		for (int i = 0; i < sz; ++i) want[i] = x[i] ^ y[i];
		if (memcmp(res.data(), want.data(), sz * sizeof(block)) != 0) return false;

		// In-place version: dst ^= src.
		vector<block> dst = x;
		xorBlocksTo_arr(dst.data(), y.data(), sz);
		if (memcmp(dst.data(), want.data(), sz * sizeof(block)) != 0) return false;

		// Broadcast version: res = x ^ y_const.
		block yc; prg.random_block(&yc, 1);
		xorBlocks_arr(res.data(), x.data(), yc, sz);
		for (int i = 0; i < sz; ++i) want[i] = x[i] ^ yc;
		if (memcmp(res.data(), want.data(), sz * sizeof(block)) != 0) return false;
	}
	return true;
}

static bool check_zero_length_helpers() {
	block* out = nullptr;
	const block* in = nullptr;
	xorBlocks_arr(out, in, in, 0);
	xorBlocksTo_arr(out, in, 0);
	xorBlocks_arr(out, in, zero_block, 0);
	return cmpBlock(in, in, 0) && to_hex(nullptr, 0).empty();
}

static bool check_cmpBlock() {
	PRG prg;
	for (int sz : {1, 4, 33, 257}) {
		vector<block> a(sz), b(sz);
		prg.random_block(a.data(), sz);
		memcpy(b.data(), a.data(), sz * sizeof(block));
		if (!cmpBlock(a.data(), b.data(), sz)) return false;
		// Flip one bit somewhere; must report not-equal.
		((uint64_t *)b.data())[sz / 2] ^= 1ULL << ((sz * 7) % 64);
		if (cmpBlock(a.data(), b.data(), sz)) return false;
	}
	return true;
}

static bool check_invalid_ranges_rejected() {
	block a = zero_block, b = zero_block, out = zero_block;
	bool bits[1] = {false};
	uint8_t packed[1] = {0};
	return dies([&] { (void)getBit(a, -1); })
	    && dies([&] { (void)getBit(a, 128); })
	    && dies([&] { (void)set_bit(a, -1); })
	    && dies([&] { (void)set_bit(a, 128); })
	    && dies([&] { xorBlocks_arr(&out, &a, &b, -1); })
	    && dies([&] { xorBlocksTo_arr(&out, &a, -1); })
	    && dies([&] { xorBlocks_arr(&out, &a, b, -1); })
	    && dies([&] { (void)cmpBlock(&a, &b, -1); })
	    && dies([&] {
		    (void)to_hex(nullptr, std::string{}.max_size() / 2 + 1);
	    })
	    && dies([&] { bools_to_bits(packed, bits, -1); })
	    && dies([&] { bits_to_bools(bits, packed, -1); })
	    && dies([&] { sse_trans(packed, packed, 7, 8); })
	    && dies([&] { sse_trans_n128(&out, &a, 64); });
}

static bool check_sse_trans_roundtrip() {
	PRG prg;
	struct Shape { uint64_t r, c; };
	for (Shape s : (Shape[]){{16, 16}, {16, 32}, {32, 16}, {128, 128},
	                          {128, 256}, {64, 128}, {8, 16}, {24, 32}}) {
		size_t n = (s.r * s.c + 7) / 8;
		vector<uint8_t> in(n), mid(n), out(n);
		prg.random_data_unaligned(in.data(), (int)n);
		sse_trans(mid.data(), in.data(),  s.r, s.c);
		sse_trans(out.data(), mid.data(), s.c, s.r);
		if (memcmp(in.data(), out.data(), n) != 0) {
			cout << "    sse_trans round-trip FAIL at " << s.r << "x" << s.c << "\n";
			return false;
		}
	}
	return true;
}

static bool check_sse_trans_scalar_reference() {
	PRG prg;
	for (uint64_t nrows = 8; nrows <= 128; nrows += 8) {
		for (uint64_t ncols = 8; ncols <= 256; ncols += 8) {
			const size_t nbytes = (size_t)(nrows * ncols / 8);
			const size_t in_stride = (size_t)(ncols / 8);
			const size_t out_stride = (size_t)(nrows / 8);
			vector<uint8_t> in(nbytes), got(nbytes, 0xa5), want(nbytes, 0);
			prg.random_data_unaligned(in.data(), (int64_t)nbytes);

			for (uint64_t row = 0; row < nrows; ++row) {
				for (uint64_t col = 0; col < ncols; ++col) {
					const uint8_t bit =
					    (in[(size_t)row * in_stride + col / 8] >> (col % 8)) & 1;
					want[(size_t)col * out_stride + row / 8] |= bit << (row % 8);
				}
			}

			sse_trans(got.data(), in.data(), nrows, ncols);
			if (got != want) {
				cout << "    sse_trans scalar-reference FAIL at "
				     << nrows << "x" << ncols << "\n";
				return false;
			}
		}
	}
	return true;
}

// Parity: the tier-dispatched sse_trans_n128 must match the generic
// (SSE2-only) sse_trans byte-for-byte. Exercises every variant the build
// instantiates (SSE2 / AVX2 / AVX-512BW): on x86 the dispatcher picks the
// widest; on aarch64 it falls through to sse_trans. Sweep covers AVX-512BW
// bulk multiples (mult of 512 cols = 4 col_blocks), AVX2 multiples (256 cols
// = 2 col_blocks), and tail sizes 1/2/3 col_blocks past those.
static bool check_sse_trans_n128_parity() {
	PRG prg;
	const uint64_t ncols_list[] = {
		128, 256, 384, 512, 640, 768, 896, 1024,    // 1..8 col_blocks
		1280, 1536, 1664,                            // odd boundaries
		2048, 4096, 8192, 16384,                     // typical SoftSpoken sizes
		131072,                                      // SoftSpoken k=8 at kChunkBlocks=1024
		74240                                        // Ferret bootstrap (kChunkBlocks=580)
	};
	for (uint64_t ncols : ncols_list) {
		const size_t n_bytes = (128 * ncols) / 8;
		const size_t n_blocks = n_bytes / sizeof(block);
		vector<block> in(n_blocks), out_ref(n_blocks), out_n128(n_blocks);
		prg.random_block(in.data(), (int64_t)n_blocks);
		sse_trans(reinterpret_cast<uint8_t *>(out_ref.data()),
		          reinterpret_cast<const uint8_t *>(in.data()), 128, ncols);
		sse_trans_n128(out_n128.data(), in.data(), ncols);
		if (memcmp(out_ref.data(), out_n128.data(), n_bytes) != 0) {
			cout << "    sse_trans_n128 PARITY FAIL at ncols=" << ncols << "\n";
			return false;
		}
		// Also verify the round-trip: transpose, transpose back via the generic
		// reverse direction; output must equal input.
		vector<block> back_blocks(n_blocks);
		sse_trans(reinterpret_cast<uint8_t *>(back_blocks.data()),
		          reinterpret_cast<const uint8_t *>(out_n128.data()),
		          ncols, 128);
		if (memcmp(in.data(), back_blocks.data(), n_bytes) != 0) {
			cout << "    sse_trans_n128 ROUNDTRIP FAIL at ncols=" << ncols << "\n";
			return false;
		}
	}
	return true;
}

static bool check_bits_bytes_roundtrip() {
	PRG prg;
	for (int t = 0; t < 256; ++t) {
		bool a[32], b[32];
		prg.random_bool(a, 32);
		uint32_t bits = bytes_to_bits32(a);
		bits32_to_bytes(bits, b);
		for (int i = 0; i < 32; ++i) if (a[i] != b[i]) return false;
	}
	for (int t = 0; t < 256; ++t) {
		uint32_t x;
		prg.random_data_unaligned(&x, 4);
		bool b[32];
		bits32_to_bytes(x, b);
		uint32_t y = bytes_to_bits32(b);
		if (x != y) return false;
	}
	return true;
}

static bool check_bools_bits_roundtrip() {
	uint8_t *no_bytes = nullptr;
	bools_to_bits(nullptr, nullptr, 0);
	bits_to_bools(nullptr, nullptr, 0);
	bools_to_bits(nullptr, no_bytes, 0);
	bits_to_bools(no_bytes, nullptr, 0);
	bool bools_in[4097], bools_out[4097];
	for (int len : {1, 7, 8, 9, 31, 32, 33, 127, 128, 1023, 1024, 4097}) {
		vector<uint8_t> bytes_in(len);
		for (int i = 0; i < len; ++i) {
			bytes_in[i] = ((i * 2654435761u) & 1) ? 0xA5 : 0;
			bools_in[i] = bytes_in[i] != 0;
		}

		vector<uint8_t> packed_bytes((len + 7) / 8, 0xAA);
		vector<uint8_t> packed_bools((len + 7) / 8, 0xAA);
		bools_to_bits(packed_bytes.data(), bytes_in.data(), len);
		bools_to_bits(packed_bools.data(), bools_in, len);
		if (packed_bytes != packed_bools) return false;

		vector<uint8_t> bytes_out(len);
		bits_to_bools(bytes_out.data(), packed_bytes.data(), len);
		bits_to_bools(bools_out, packed_bytes.data(), len);
		for (int i = 0; i < len; ++i) {
			if (bytes_out[i] > 1 || (bytes_out[i] != 0) != bools_in[i]) return false;
			if (bools_out[i] != bools_in[i]) return false;
		}

		// Tail-byte preservation: bits beyond `len` in the last byte must remain 0xAA.
		if (len % 8 != 0) {
			uint8_t mask_below = (uint8_t)((1u << (len % 8)) - 1);
			uint8_t expected_tail_bits_above = 0xAA & (uint8_t)~mask_below;
			uint8_t got_tail_above = packed_bytes.back() & (uint8_t)~mask_below;
			if (got_tail_above != expected_tail_bits_above) return false;
		}
	}
	return true;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	struct Case { const char *name; bool (*fn)(); };
	Case cases[] = {
		{"direct SIMD-tier include",      check_direct_simd_tier_include},
		{"makeBlock + getLSB",          check_makeBlock_getLSB},
		{"set_bit",                     check_set_bit},
		{"sigma linear + formula",      check_sigma_linear_and_formula},
		{"xorBlocks_arr / xorBlocksTo", check_xorBlocks_arr},
		{"zero-length block helpers",    check_zero_length_helpers},
		{"cmpBlock",                    check_cmpBlock},
		{"invalid ranges rejected",     check_invalid_ranges_rejected},
		{"sse_trans round-trip",        check_sse_trans_roundtrip},
		{"sse_trans scalar reference",  check_sse_trans_scalar_reference},
		{"sse_trans_n128 parity",       check_sse_trans_n128_parity},
		{"bytes<->bits32 round-trip",   check_bits_bytes_roundtrip},
		{"bool/byte-bools<->bits parity", check_bools_bits_roundtrip},
	};
	bool all = true;
	for (auto &c : cases) {
		bool ok = c.fn();
		all &= ok;
		cout << "  " << left << setw(36) << c.name << (ok ? "OK" : "FAIL") << "\n";
	}
	return all;
}

int main(int /*argc*/, char ** /*argv*/) {
	example();
	cout << "\n";
	if (!run_correctness()) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
