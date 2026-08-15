// core/block.h — 128-bit SIMD block (__m128i alias) plus the small bit/byte
// helpers everything else in emp-tool builds on. Throughput benchmark.
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

	cout << "\n=== sse_trans_n128 (tier-dispatched, sweep ncols) ===\n";
	for (uint64_t ncols : {128ULL, 512ULL, 2048ULL, 8192ULL, 32768ULL,
	                        131072ULL, 524288ULL}) {
		size_t n = (128 * ncols) / 8;
		size_t n_blocks = n / sizeof(block);
		vector<block> in(n_blocks), out(n_blocks);
		prg.random_block(in.data(), (int64_t)n_blocks);
		double calls = run_for(sec, [&]() {
			sse_trans_n128(out.data(), in.data(), ncols);
		}, out.data());
		ostringstream lbl; lbl << "sse_trans_n128(128x" << ncols << ")";
		print_vec_bytes(lbl.str(), calls, 2 * n);
	}

	cout << "\n=== bools_to_bits / bits_to_bools (sweep N bits) ===\n";
	for (int len : {32, 128, 1024, 8192, 65536}) {
		vector<uint8_t> bools(len);
		prg.random_bool(bools.data(), len);
		vector<uint8_t> packed((len + 7) / 8);
		double calls = run_for(sec, [&]() {
			bools_to_bits(packed.data(), bools.data(), len);
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
			bits_to_bools(bools.data(), packed.data(), len);
		}, bools.data());
		ostringstream lbl; lbl << "bits_to_bools(N=" << len << ")";
		print_vec_bytes(lbl.str(), calls, (size_t)len);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
