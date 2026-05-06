// core/block.h — 128-bit SIMD block (__m128i alias) plus the small bit/byte
// helpers everything else in emp-tool builds on. Read example() first; the
// rest is verification + throughput.
//
// What's in block.h:
//   block, makeBlock(hi, lo), zero_block, all_one_block
//   getLSB(b), set_bit(b, i)
//   sigma(b)                                  linear orthomorphism (Guo et al.)
//   xorBlocks_arr(res, x, y, n)               element-wise XOR
//   xorBlocks_arr(res, x, y_block, n)         broadcast XOR
//   xorBlocksTo_arr(dst, src, n)              in-place dst[i] ^= src[i]
//   cmpBlock(x, y, n)                         constant-ish equality
//   sse_trans(out, in, nrows, ncols)          bit-matrix transpose
//   bytes_to_bits32 / bits32_to_bytes         32 bools <-> 32 bits
//   bools_to_bits / bits_to_bools             N bools <-> N bits

#include "emp-tool/emp-tool.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

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
	PRG prg;
	for (int len : {1, 7, 8, 9, 31, 32, 33, 127, 128, 1023, 1024, 4097}) {
		vector<bool> bools_in(len);
		for (int i = 0; i < len; ++i) bools_in[i] = (i * 2654435761u) & 1;

		vector<uint8_t> packed((len + 7) / 8, 0xAA);  // sentinel byte
		// Convert to bool* (vector<bool> is bit-packed).
		vector<uint8_t> tmp(len);
		for (int i = 0; i < len; ++i) tmp[i] = bools_in[i] ? 1 : 0;
		bools_to_bits(packed.data(), reinterpret_cast<const bool *>(tmp.data()), len);

		vector<bool> bools_out_buf(len);
		vector<uint8_t> out_bytes(len);
		bits_to_bools(reinterpret_cast<bool *>(out_bytes.data()), packed.data(), len);
		for (int i = 0; i < len; ++i) {
			bool got = out_bytes[i] != 0;
			if (got != bools_in[i]) return false;
		}

		// Tail-byte preservation: bits beyond `len` in the last byte must remain 0xAA.
		if (len % 8 != 0) {
			uint8_t mask_below = (uint8_t)((1u << (len % 8)) - 1);
			uint8_t expected_tail_bits_above = 0xAA & (uint8_t)~mask_below;
			uint8_t got_tail_above = packed.back() & (uint8_t)~mask_below;
			if (got_tail_above != expected_tail_bits_above) return false;
		}
		(void)bools_out_buf;
	}
	return true;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	struct Case { const char *name; bool (*fn)(); };
	Case cases[] = {
		{"makeBlock + getLSB",          check_makeBlock_getLSB},
		{"set_bit",                     check_set_bit},
		{"sigma linear + formula",      check_sigma_linear_and_formula},
		{"xorBlocks_arr / xorBlocksTo", check_xorBlocks_arr},
		{"cmpBlock",                    check_cmpBlock},
		{"sse_trans round-trip",        check_sse_trans_roundtrip},
		{"bytes<->bits32 round-trip",   check_bits_bytes_roundtrip},
		{"bools<->bits round-trip",     check_bools_bits_roundtrip},
	};
	bool all = true;
	for (auto &c : cases) {
		bool ok = c.fn();
		all &= ok;
		cout << "  " << left << setw(36) << c.name << (ok ? "OK" : "FAIL") << "\n";
	}
	return all;
}

// ---------- throughput bench ----------

template <typename Fn>
static double run_for(double seconds, Fn &&fn, void *clob) {
	for (int i = 0; i < 32; ++i) {
		fn();
		asm volatile("" : "+m"(*(char *)clob) : : "memory");
	}
	int64_t iters = 64;
	while (true) {
		auto a = clk::now();
		for (int64_t i = 0; i < iters; ++i) {
			fn();
			asm volatile("" : "+m"(*(char *)clob) : : "memory");
		}
		double el = chrono::duration<double>(clk::now() - a).count();
		if (el >= seconds) return double(iters) / el;
		iters *= 2;
	}
}

static void print_op(const string &lbl, double calls) {
	double Mops = calls / 1e6;
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << Mops << " Mops  "
	     << setw(7) << (3e9 / calls) << " cy/op @3GHz\n";
}

static void print_vec_blocks(const string &lbl, double calls, int n) {
	double GiBps = calls * (double)n * 16.0 / (1024.0 * 1024.0 * 1024.0);
	double cy_per_blk = 3e9 / (calls * n);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_blk << " cy/blk @3GHz\n";
}

static void print_vec_bytes(const string &lbl, double calls, size_t bytes_per_call) {
	double GiBps = calls * (double)bytes_per_call / (1024.0 * 1024.0 * 1024.0);
	double cy_per_byte = 3e9 / (calls * bytes_per_call);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_byte << " cy/B @3GHz\n";
}

static const initializer_list<int> SIZES = {16, 64, 256, 1024, 4096, 16384, 65536};

static void bench(double sec) {
	PRG prg;

	cout << "=== single-shot (latency, serial-dep chain) ===\n";
	{
		block x; prg.random_block(&x, 1);
		double calls = run_for(sec, [&]() { x = sigma(x); }, &x);
		print_op("sigma", calls);
	}
	{
		// cmpBlock single-shot. Both inputs must change between iterations
		// or the compiler folds cmpBlock to a constant. Toggle the LSB of
		// `b` each call: this both forces a real compare and feeds the
		// boolean result back into the input chain.
		alignas(16) block a, b;
		prg.random_block(&a, 1); b = a;
		double calls = run_for(sec, [&]() {
			bool eq = cmpBlock(&a, &b, 1);
			((uint64_t *)&b)[0] ^= eq ? 1ULL : 2ULL;
		}, &b);
		print_op("cmpBlock(N=1)", calls);
	}

	cout << "\n=== xorBlocks_arr (3-arg, sweep N) ===\n";
	for (int n : SIZES) {
		vector<block> x(n), y(n), r(n);
		prg.random_block(x.data(), n);
		prg.random_block(y.data(), n);
		double calls = run_for(sec, [&]() {
			xorBlocks_arr(r.data(), x.data(), y.data(), n);
		}, r.data());
		ostringstream lbl; lbl << "xorBlocks_arr(N=" << n << ")";
		print_vec_blocks(lbl.str(), calls, n);
	}

	cout << "\n=== xorBlocksTo_arr (in-place, sweep N) ===\n";
	for (int n : SIZES) {
		vector<block> x(n), y(n);
		prg.random_block(x.data(), n);
		prg.random_block(y.data(), n);
		double calls = run_for(sec, [&]() {
			xorBlocksTo_arr(x.data(), y.data(), n);
		}, x.data());
		ostringstream lbl; lbl << "xorBlocksTo_arr(N=" << n << ")";
		print_vec_blocks(lbl.str(), calls, n);
	}

	cout << "\n=== sse_trans (nrows=128, sweep ncols) ===\n";
	for (uint64_t ncols : {128ULL, 512ULL, 2048ULL, 8192ULL, 32768ULL,
	                        131072ULL, 524288ULL}) {
		size_t n = (128 * ncols + 7) / 8;
		vector<uint8_t> in(n), out(n);
		prg.random_data_unaligned(in.data(), (int)n);
		double calls = run_for(sec, [&]() {
			sse_trans(out.data(), in.data(), 128, ncols);
		}, out.data());
		ostringstream lbl; lbl << "sse_trans(128x" << ncols << ")";
		// Report GiB/s of bytes touched (in + out = 2n).
		print_vec_bytes(lbl.str(), calls, 2 * n);
	}

	cout << "\n=== bools_to_bits / bits_to_bools (sweep N bits) ===\n";
	for (int len : {32, 128, 1024, 8192, 65536}) {
		vector<uint8_t> bools(len);
		prg.random_bool(reinterpret_cast<bool *>(bools.data()), len);
		vector<uint8_t> packed((len + 7) / 8);
		double calls = run_for(sec, [&]() {
			bools_to_bits(packed.data(), reinterpret_cast<const bool *>(bools.data()), len);
		}, packed.data());
		ostringstream lbl; lbl << "bools_to_bits(N=" << len << ")";
		// Bandwidth: bytes of bool input read.
		print_vec_bytes(lbl.str(), calls, (size_t)len);
	}
	for (int len : {32, 128, 1024, 8192, 65536}) {
		vector<uint8_t> packed((len + 7) / 8);
		prg.random_data_unaligned(packed.data(), (int)packed.size());
		vector<uint8_t> bools(len);
		double calls = run_for(sec, [&]() {
			bits_to_bools(reinterpret_cast<bool *>(bools.data()), packed.data(), len);
		}, bools.data());
		ostringstream lbl; lbl << "bits_to_bools(N=" << len << ")";
		print_vec_bytes(lbl.str(), calls, (size_t)len);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	example();
	cout << "\n";
	if (!run_correctness()) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	cout << "\n";
	bench(sec);
	return 0;
}
