// utils/ccrh.h — Circular CRH. H(x) = sigma(x) ^ PRP_K(sigma(x)). Reduces to
// CRH applied to sigma(x) = x ^ rotate_halves(x). Read example() first; the
// rest is verification + throughput.
//
// What's in ccrh.h:
//   CCRH(key=zero_block)       inherits PRP
//   block H(block)             single-block H
//   template<int n> H(out, in) batched
//   Hn(out, in, n, scratch=nullptr)  runtime-sized

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
	CCRH ccrh;  // zero key
	block in = makeBlock(0xCAFEBABEULL, 0xDEADBEEFULL);
	cout << "  H(in)               = " << ccrh.H(in) << "\n";

	alignas(16) block buf[4] = {
		makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4)};
	alignas(16) block out[4];
	ccrh.H<4>(out, buf);
	cout << "  H<4>(out, in)[0]    = " << out[0] << "\n";
}

static bool blocks_eq(block a, block b) {
	__m128i d = _mm_xor_si128(a, b);
	return _mm_testz_si128(d, d);
}

static bool check_definition() {
	// H(x) must equal sigma(x) ^ AES_K(sigma(x)).
	PRG prg;
	for (int t = 0; t < 64; ++t) {
		block key, x;
		prg.random_block(&key, 1);
		prg.random_block(&x, 1);
		CCRH ccrh(key);
		PRP prp(key);
		alignas(16) block s = sigma(x);
		alignas(16) block ref = s;
		prp.permute_block(&ref, 1);
		ref = ref ^ s;
		if (!blocks_eq(ccrh.H(x), ref)) return false;
	}
	return true;
}

template <int n>
static bool check_batched() {
	PRG prg;
	for (int t = 0; t < 8; ++t) {
		block key; prg.random_block(&key, 1);
		alignas(16) block in[n], out[n], ref[n];
		prg.random_block(in, n);
		CCRH ccrh(key);
		ccrh.template H<n>(out, in);
		PRP prp(key);
		for (int i = 0; i < n; ++i) ref[i] = sigma(in[i]);
		alignas(16) block enc[n];
		memcpy(enc, ref, sizeof(enc));
		prp.permute_block(enc, n);
		for (int i = 0; i < n; ++i) ref[i] = ref[i] ^ enc[i];
		if (memcmp(out, ref, sizeof(in)) != 0) return false;
	}
	return true;
}

static bool check_Hn(int n) {
	PRG prg;
	block key; prg.random_block(&key, 1);
	vector<block> in(n), out(n), ref(n);
	prg.random_block(in.data(), n);
	CCRH ccrh(key);
	ccrh.Hn(out.data(), in.data(), n);

	PRP prp(key);
	for (int i = 0; i < n; ++i) ref[i] = sigma(in[i]);
	vector<block> enc = ref;
	prp.permute_block(enc.data(), n);
	for (int i = 0; i < n; ++i) ref[i] = ref[i] ^ enc[i];

	return memcmp(out.data(), ref.data(), n * sizeof(block)) == 0;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	bool a = check_definition();      ok &= a;
	cout << "  H(x) = sigma(x) ^ PRP_K(sigma(x))       " << (a ? "OK" : "FAIL") << "\n";
	bool b = check_batched<1>() && check_batched<4>() && check_batched<16>() && check_batched<64>();
	ok &= b;
	cout << "  template H<n> matches reference         " << (b ? "OK" : "FAIL") << "\n";
	bool c = check_Hn(1) && check_Hn(7) && check_Hn(33) && check_Hn(1024);
	ok &= c;
	cout << "  Hn(runtime) matches reference           " << (c ? "OK" : "FAIL") << "\n";
	return ok;
}

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
	CCRH ccrh;

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

	cout << "\n=== Hn (runtime) ===\n";
	for (int n : {1, 4, 16, 64, 256, 1024, 4096, 16384}) {
		vector<block> in(n), out(n), scratch(n);
		prg.random_block(in.data(), n);
		double calls = run_for(sec, [&]() {
			ccrh.Hn(out.data(), in.data(), n, scratch.data());
		}, out.data());
		ostringstream lbl; lbl << "CCRH::Hn(N=" << n << ")";
		print_vec(lbl.str(), calls, n);
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
