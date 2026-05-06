// core/utils.h, core/utils.hpp — small free-standing helpers. Read example()
// first; the rest is verification + throughput.
//
// What's in utils.h/utils.hpp (the parts worth testing):
//   bool_to_int<T>(const bool *)   pack 8*sizeof(T) bools (LSB-first) into T
//   bool_to_block(const bool *)    pack 128 bools into a block

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

static void example() {
	cout << "=== example ===\n";

	// (1) bool_to_int<T>: 8*sizeof(T) bools (LSB-first) -> T.
	bool b8[8] = {1,0,1,1, 0,0,1,0};
	uint8_t v8 = bool_to_int<uint8_t>(b8);
	cout << "  bool_to_int<u8>([1,0,1,1, 0,0,1,0]) = 0x"
	     << hex << setw(2) << setfill('0') << (int)v8 << dec << setfill(' ') << "\n";

	bool b16[16] = {1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1};
	uint16_t v16 = bool_to_int<uint16_t>(b16);
	cout << "  bool_to_int<u16>(..)                = 0x"
	     << hex << setw(4) << setfill('0') << v16 << dec << setfill(' ') << "\n";

	// (2) bool_to_block: 128 bools -> block.
	bool b128[128] = {0};
	b128[0] = 1; b128[7] = 1; b128[64] = 1;
	cout << "  bool_to_block(bit0|bit7|bit64)      = " << bool_to_block(b128) << "\n";
}

// ---------- correctness ----------

template <typename T>
static bool check_bool_to_int_random(int trials) {
	PRG prg;
	const int W = sizeof(T) * 8;
	for (int t = 0; t < trials; ++t) {
		uint64_t x = 0;
		prg.random_data_unaligned(&x, sizeof(T));
		bool bits[64];
		for (int i = 0; i < W; ++i) bits[i] = (x >> i) & 1;
		T got = bool_to_int<T>(bits);
		T want = (T)x;
		if (got != want) return false;
	}
	return true;
}

static bool check_bool_to_int_known() {
	bool all1[64];
	for (int i = 0; i < 64; ++i) all1[i] = 1;
	if (bool_to_int<uint8_t>(all1)  != (uint8_t)~0)  return false;
	if (bool_to_int<uint16_t>(all1) != (uint16_t)~0) return false;
	if (bool_to_int<uint32_t>(all1) != (uint32_t)~0) return false;
	if (bool_to_int<uint64_t>(all1) != (uint64_t)~0) return false;

	bool all0[64] = {0};
	if (bool_to_int<uint64_t>(all0) != 0) return false;
	return true;
}

static bool check_bool_to_block_random(int trials) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		alignas(16) block want;
		prg.random_block(&want, 1);
		bool bits[128];
		uint8_t bytes[16];
		_mm_storeu_si128(reinterpret_cast<__m128i *>(bytes), want);
		for (int i = 0; i < 128; ++i) bits[i] = (bytes[i / 8] >> (i % 8)) & 1;
		block got = bool_to_block(bits);
		__m128i d = _mm_xor_si128(got, want);
		if (!_mm_testz_si128(d, d)) return false;
	}
	return true;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	struct Case { const char *name; bool (*fn)(); };
	auto u8  = []{ return check_bool_to_int_random<uint8_t>(64); };
	auto u16 = []{ return check_bool_to_int_random<uint16_t>(64); };
	auto u32 = []{ return check_bool_to_int_random<uint32_t>(64); };
	auto u64 = []{ return check_bool_to_int_random<uint64_t>(64); };
	auto blk = []{ return check_bool_to_block_random(64); };
	auto kn  = []{ return check_bool_to_int_known(); };
	bool a = u8();    cout << "  bool_to_int<uint8_t>  random          " << (a ? "OK" : "FAIL") << "\n";
	bool b = u16();   cout << "  bool_to_int<uint16_t> random          " << (b ? "OK" : "FAIL") << "\n";
	bool c = u32();   cout << "  bool_to_int<uint32_t> random          " << (c ? "OK" : "FAIL") << "\n";
	bool d = u64();   cout << "  bool_to_int<uint64_t> random          " << (d ? "OK" : "FAIL") << "\n";
	bool e = blk();   cout << "  bool_to_block         random          " << (e ? "OK" : "FAIL") << "\n";
	bool f = kn();    cout << "  bool_to_int<*>        known answers   " << (f ? "OK" : "FAIL") << "\n";
	return a && b && c && d && e && f;
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

static void bench(double sec) {
	PRG prg;
	cout << "=== single-shot (chained-dep) ===\n";
	{
		bool bits[64];
		prg.random_bool(bits, 64);
		uint64_t sink = 0;
		double calls = run_for(sec, [&]() {
			uint64_t v = bool_to_int<uint64_t>(bits);
			sink ^= v;
			bits[v & 63] ^= 1;  // serial dep on output
		}, &sink);
		print_op("bool_to_int<uint64_t>", calls);
	}
	{
		alignas(16) bool bits[128];
		prg.random_bool(bits, 128);
		alignas(16) block sink = zero_block;
		double calls = run_for(sec, [&]() {
			block r = bool_to_block(bits);
			sink = sink ^ r;
			// chain: tweak one input bit using output's LSB
			bits[0] ^= getLSB(sink);
		}, &sink);
		print_op("bool_to_block", calls);
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
