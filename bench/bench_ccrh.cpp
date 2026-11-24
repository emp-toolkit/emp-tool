// crypto/ccrh.h — Circular CRH. H(x) = sigma(x) ^ PRP_K(sigma(x)). Reduces to
// CRH applied to sigma(x) = x ^ rotate_halves(x). Throughput benchmark.
//
// What's in ccrh.h:
//   CCRH(key)                  inherits PRP; the key is required
//   block H(block)             single-block H
//   template<int n> H(out, in) batched
//   Hn(out, in, n)             runtime-sized, out-of-place (no overlap)
//   Hn(data, n)                runtime-sized, in-place

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

static void print_vec(const string &lbl, double calls, int n) {
	double GiBps = calls * (double)n * 16.0 / (1024.0 * 1024.0 * 1024.0);
	double cy_per_blk = 3e9 / (calls * n);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_blk << " cy/blk @3GHz\n";
}

static void bench(double sec) {
	PRG prg;
	block key;
	prg.random_block(&key, 1);
	CCRH ccrh(key);

	cout << "=== single-shot H (chained-dep) ===\n";
	{
		block x; prg.random_block(&x, 1);
		double calls = run_for(sec, [&]() { x = ccrh.H(x); }, &x);
		print_op("CCRH::H(block)", calls);
	}

	cout << "\n=== template H<n> (sweep n) ===\n";
	{
		alignas(16) block in[1], out[1]; prg.random_block(in, 1);
		double c = run_for(sec, [&]() { ccrh.H<1>(out, in); }, out);
		print_vec("CCRH::H<1>", c, 1);
	}
	{
		alignas(16) block in[4], out[4]; prg.random_block(in, 4);
		double c = run_for(sec, [&]() { ccrh.H<4>(out, in); }, out);
		print_vec("CCRH::H<4>", c, 4);
	}
	{
		alignas(16) block in[16], out[16]; prg.random_block(in, 16);
		double c = run_for(sec, [&]() { ccrh.H<16>(out, in); }, out);
		print_vec("CCRH::H<16>", c, 16);
	}
	{
		alignas(16) block in[64], out[64]; prg.random_block(in, 64);
		double c = run_for(sec, [&]() { ccrh.H<64>(out, in); }, out);
		print_vec("CCRH::H<64>", c, 64);
	}
	{
		alignas(16) block in[256], out[256]; prg.random_block(in, 256);
		double c = run_for(sec, [&]() { ccrh.H<256>(out, in); }, out);
		print_vec("CCRH::H<256>", c, 256);
	}

	cout << "\n=== Hn (runtime, out-of-place) ===\n";
	for (int n : {1, 4, 16, 64, 256, 1024, 4096, 16384}) {
		vector<block> in(n), out(n);
		prg.random_block(in.data(), n);
		double calls = run_for(sec, [&]() {
			ccrh.Hn(out.data(), in.data(), n);
		}, out.data());
		ostringstream lbl; lbl << "CCRH::Hn(N=" << n << ")";
		print_vec(lbl.str(), calls, n);
	}

	cout << "\n=== Hn (runtime, in-place) ===\n";
	for (int n : {1, 4, 16, 64, 256, 1024, 4096, 16384}) {
		vector<block> data(n);
		prg.random_block(data.data(), n);
		double calls = run_for(sec, [&]() {
			ccrh.Hn(data.data(), n);
		}, data.data());
		ostringstream lbl; lbl << "CCRH::Hn_inplace(N=" << n << ")";
		print_vec(lbl.str(), calls, n);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
