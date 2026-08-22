// crypto/f2k.h — GF(2^128) primitives for emp's correlated and almost-universal
// hashing. Throughput benchmark.
//
// What's in f2k.h:
//   mul128(a, b, &lo, &hi)           carry-less 128x128 -> 256, no reduction
//   reduce(lo, hi)                   GF(2^128) reduction
//   gfmul(a, b, &r)                  full GF(2^128) multiply (mul128 + reduce)
//   gfmul_reflect(a, b, &r)          same, with bit-reflected I/O (GHASH form)
//   vector_inn_prdt_sum_no_red(...)  Σ a[i]*b[i] left unreduced (256-bit out)
//   vector_inn_prdt_sum_red(...)     Σ a[i]*b[i] reduced once at the end
//   vector_inn_prdt_sum_red(...,bool*) same when b[i] ∈ {0,1} (no PCLMUL)
//   uni_hash_coeff_gen(coeff, s, n)  coeff[i] = s^(i+1)
//   GaloisFieldPacking().packing(&r, vec128)   Σ vec[i] * X^i in GF(2^128)
//   GaloisFieldPacking().packing(&r, bool128)  same for bool inputs (= packed bits)
//   vector_self_xor(&sum, xs, n)               sum = ⊕ xs[i]

#include "emp-tool/emp-tool.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

// ---------- throughput bench ----------
//
// Single-shot ops chain output → input so each call has a real serial dep on
// the previous (otherwise the compiler hoists the constant call). Vector ops
// sweep size to show where each routine becomes bandwidth-bound vs
// compute-bound.

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

static void print_vec(const string &lbl, double calls, int n,
                      int bytes_per_element) {
	double GiBps = calls * (double)n * bytes_per_element /
	               (1024.0 * 1024.0 * 1024.0);
	double cy_per_element = 3e9 / (calls * n);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_element << " cy/elem @3GHz\n";
}

static const initializer_list<int> SIZES = {
	1, 4, 8, 16, 64, 256, 1024, 4096, 16384, 65536
};

static void bench(double sec) {
	PRG prg;

#if EMP_HAS_VPCLMUL512
	cout << "F2K multiply tier: VPCLMUL-512 (4 products/vector)\n";
#elif EMP_HAS_VPCLMUL256
	cout << "F2K multiply tier: VPCLMUL-256 (2 products/vector)\n";
#elif defined(__aarch64__)
	cout << "F2K multiply tier: ARMv8 PMULL (1 product/vector)\n";
#else
	cout << "F2K multiply tier: PCLMULQDQ (1 product/vector)\n";
#endif
	cout << "Vector GiB/s counts logical input bytes (output bytes for generators).\n\n";

	cout << "=== single-shot (latency, serial-dep chain) ===\n";
	{
		block lo, hi, b;
		prg.random_block(&lo, 1); prg.random_block(&b, 1); hi = lo;
		double calls = run_for(sec, [&]() { mul128(lo, b, &lo, &hi); }, &lo);
		print_op("mul128", calls);
	}
	{
		block a, b;
		prg.random_block(&a, 1); prg.random_block(&b, 1);
		double calls = run_for(sec, [&]() { gfmul(a, b, &a); }, &a);
		print_op("gfmul", calls);
	}
	{
		block a, b;
		prg.random_block(&a, 1); prg.random_block(&b, 1);
		double calls = run_for(sec, [&]() { gfmul_reflect(a, b, &a); }, &a);
		print_op("gfmul_reflect", calls);
	}
	{
		block lo, hi;
		prg.random_block(&lo, 1); prg.random_block(&hi, 1);
		double calls = run_for(sec, [&]() { lo = reduce(lo, hi); }, &lo);
		print_op("reduce", calls);
	}
	{
		constexpr int n = 256;
		vector<block> a(n), b(n), out(n);
		prg.random_block(a.data(), n);
		prg.random_block(b.data(), n);
		double calls = run_for(sec, [&]() {
			for (int i = 0; i < n; ++i) gfmul(a[i], b[i], &out[i]);
		}, out.data());
		print_vec("gfmul independent(N=256)", calls, n, 32);
	}

	cout << "\n=== vector_inn_prdt_sum_red (deferred reduction) ===\n";
	for (int n : SIZES) {
		vector<block> a(n), b(n);
		prg.random_block(a.data(), n);
		prg.random_block(b.data(), n);
		block r;
		double calls = run_for(sec, [&]() {
			vector_inn_prdt_sum_red(&r, a.data(), b.data(), n);
		}, &r);
		ostringstream lbl; lbl << "vec_inn_prdt_red(N=" << n << ")";
		print_vec(lbl.str(), calls, n, 32);
	}

	cout << "\n=== vector_inn_prdt_sum_red (bool selector, branchless mask + XOR) ===\n";
	for (int n : SIZES) {
		vector<block> a(n);
		vector<uint8_t> bs(n);
		prg.random_block(a.data(), n);
		prg.random_bool(bs.data(), n);
		block r;
		double calls = run_for(sec, [&]() {
			vector_inn_prdt_sum_red(&r, a.data(), bs.data(), n);
		}, &r);
		ostringstream lbl; lbl << "vec_inn_prdt_red(byte-bool, N=" << n << ")";
		print_vec(lbl.str(), calls, n, 17);
	}

	cout << "\n=== vector_inn_prdt_sum_no_red (256-bit accumulator, no reduce) ===\n";
	for (int n : SIZES) {
		vector<block> a(n), b(n);
		prg.random_block(a.data(), n);
		prg.random_block(b.data(), n);
		block r[2];
		double calls = run_for(sec, [&]() {
			vector_inn_prdt_sum_no_red(r, a.data(), b.data(), n);
		}, r);
		ostringstream lbl; lbl << "vec_inn_prdt_no_red(N=" << n << ")";
		print_vec(lbl.str(), calls, n, 32);
	}

	cout << "\n=== uni_hash_coeff_gen (coeff[i] = seed^(i+1)) ===\n";
	for (int n : {16, 64, 256, 1024, 4096, 16384}) {
		vector<block> coeff(n);
		block seed; prg.random_block(&seed, 1);
		double calls = run_for(sec, [&]() {
			uni_hash_coeff_gen(coeff.data(), seed, n);
		}, coeff.data());
		ostringstream lbl; lbl << "uni_hash_coeff_gen(N=" << n << ")";
		print_vec(lbl.str(), calls, n, 16);
	}

	cout << "\n=== vector_self_xor (⊕ xs[i]) ===\n";
	for (int n : SIZES) {
		vector<block> a(n);
		prg.random_block(a.data(), n);
		block r;
		double calls = run_for(sec, [&]() {
			vector_self_xor(&r, a.data(), n);
		}, &r);
		ostringstream lbl; lbl << "vector_self_xor(N=" << n << ")";
		print_vec(lbl.str(), calls, n, 16);
	}

	cout << "\n=== GaloisFieldPacking::packing (fixed N=128) ===\n";
	{
		GaloisFieldPacking pkr;
		block data[128]; prg.random_block(data, 128);
		block r;
		double calls = run_for(sec, [&]() { pkr.packing(&r, data); }, &r);
		print_op("packing(block*, 128)", calls);
	}
	{
		GaloisFieldPacking pkr;
		uint8_t bits[128];
		prg.random_bool(bits, 128);
		block r;
		double calls = run_for(sec, [&]() {
			pkr.packing(&r, bits);
		}, &r);
		print_op("packing(byte-bool*, 128)", calls);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
