// utils/crh.h — Correlation-robust hash. H(x) = x ^ PRP_K(x). Read example()
// first; the rest is verification + throughput.
//
// What's in crh.h:
//   CRH(key=zero_block)        inherits PRP
//   block H(block)             single-block H
//   template<int n> H(out, in) batched H, may run in-place (out == in OK)
//   Hn(out, in, n, scratch=nullptr)  runtime-sized batched H

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
	CRH crh;  // zero key
	block in = makeBlock(0xCAFEBABEULL, 0xDEADBEEFULL);
	cout << "  H(in)               = " << crh.H(in) << "\n";

	alignas(16) block buf[4] = {
		makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4)};
	alignas(16) block out[4];
	crh.H<4>(out, buf);
	cout << "  H<4>(out, in)[0]    = " << out[0] << "\n";

	// In-place: out and in overlap; CRH dispatches to xorBlocksTo_arr.
	crh.H<4>(buf, buf);
	cout << "  in-place H<4>[0]    = " << buf[0] << "\n";
}

static bool blocks_eq(block a, block b) {
	__m128i d = _mm_xor_si128(a, b);
	return _mm_testz_si128(d, d);
}

static bool check_definition() {
	// H(x) must equal x ^ AES_K(x). Build the reference via PRP.
	PRG prg;
	for (int t = 0; t < 64; ++t) {
		block key, x;
		prg.random_block(&key, 1);
		prg.random_block(&x, 1);
		CRH crh(key);
		PRP prp(key);
		alignas(16) block ref = x;
		prp.permute_block(&ref, 1);
		ref = ref ^ x;
		if (!blocks_eq(crh.H(x), ref)) return false;
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
		CRH crh(key);
		crh.template H<n>(out, in);
		PRP prp(key);
		memcpy(ref, in, sizeof(in));
		prp.permute_block(ref, n);
		for (int i = 0; i < n; ++i) ref[i] = ref[i] ^ in[i];
		if (memcmp(out, ref, sizeof(in)) != 0) return false;

		// In-place: pass the same buffer for in and out.
		alignas(16) block inout[n];
		memcpy(inout, in, sizeof(in));
		crh.template H<n>(inout, inout);
		if (memcmp(inout, ref, sizeof(in)) != 0) return false;
	}
	return true;
}

static bool check_Hn(int n) {
	PRG prg;
	block key; prg.random_block(&key, 1);
	vector<block> in(n), out(n), ref(n);
	prg.random_block(in.data(), n);
	CRH crh(key);
	crh.Hn(out.data(), in.data(), n);
	PRP prp(key);
	memcpy(ref.data(), in.data(), n * sizeof(block));
	prp.permute_block(ref.data(), n);
	for (int i = 0; i < n; ++i) ref[i] = ref[i] ^ in[i];
	return memcmp(out.data(), ref.data(), n * sizeof(block)) == 0;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	bool a = check_definition();      ok &= a;
	cout << "  H(x) = x ^ PRP_K(x)                     " << (a ? "OK" : "FAIL") << "\n";
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
	CRH crh;

	cout << "=== single-shot H (chained-dep) ===\n";
	{
		block x; prg.random_block(&x, 1);
		double calls = run_for(sec, [&]() { x = crh.H(x); }, &x);
		print_op("CRH::H(block)", calls);
	}

	cout << "\n=== template H<n> (sweep n) ===\n";
	for (int n : {1, 4, 16, 64, 256, 1024}) {
		alignas(16) vector<block> in(n), out(n);
		prg.random_block(in.data(), n);
		double calls;
		switch (n) {
#define CASE(N) case N: calls = run_for(sec, [&]() { crh.H<N>(out.data(), in.data()); }, out.data()); break
		CASE(1); CASE(4); CASE(16); CASE(64); CASE(256); CASE(1024);
#undef CASE
		default: calls = 0; break;
		}
		ostringstream lbl; lbl << "CRH::H<" << n << ">";
		print_vec(lbl.str(), calls, n);
	}

	cout << "\n=== Hn (runtime) ===\n";
	for (int n : {1, 4, 16, 64, 256, 1024, 4096, 16384}) {
		vector<block> in(n), out(n), scratch(n);
		prg.random_block(in.data(), n);
		double calls = run_for(sec, [&]() {
			crh.Hn(out.data(), in.data(), n, scratch.data());
		}, out.data());
		ostringstream lbl; lbl << "CRH::Hn(N=" << n << ")";
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
