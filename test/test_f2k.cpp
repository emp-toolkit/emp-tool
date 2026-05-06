// crypto/f2k.h — GF(2^128) primitives for emp's correlated and almost-universal
// hashing. Read example() first; the rest is verification + throughput.
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
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

// ---------- example: typical usage ----------

static void example() {
	cout << "=== example ===\n";
	PRG prg;

	// (1) Single-block GF(2^128) multiply.
	block a = makeBlock(0xCAFEBABEull, 0xDEADBEEFull);
	block b = makeBlock(0x01234567ull, 0x89ABCDEFull);
	block c;
	gfmul(a, b, &c);
	cout << "  gfmul(a, b)              = " << c << "\n";

	// (2) Inner product over GF(2^128).
	constexpr int N = 4;
	block xs[N], ys[N], dot;
	prg.random_block(xs, N);
	prg.random_block(ys, N);
	vector_inn_prdt_sum_red(&dot, xs, ys, N);
	cout << "  <xs, ys> in GF(2^128)    = " << dot << "\n";

	// (3) Almost-universal hash coefficients: coeff[i] = seed^(i+1).
	block seed; prg.random_block(&seed, 1);
	block coeff[8];
	uni_hash_coeff_gen(coeff, seed, 8);
	cout << "  coeff[7] = seed^8        = " << coeff[7] << "\n";

	// (4) Pack 128 blocks into one via Σ data[i] * X^i.
	block packed_in[128], packed_out;
	prg.random_block(packed_in, 128);
	GaloisFieldPacking pkr;
	pkr.packing(&packed_out, packed_in);
	cout << "  packing(128 blocks)      = " << packed_out << "\n";

	// (5) Vector self-XOR.
	block sum_xs;
	vector_self_xor(&sum_xs, xs, N);
	cout << "  ⊕ xs[i]                  = " << sum_xs << "\n";

	// (6) Inner product when b is a bool vector (no carryless multiply).
	bool bs[N] = {true, false, true, true};
	block dot_b;
	vector_inn_prdt_sum_red(&dot_b, xs, bs, N);
	cout << "  <xs, [1,0,1,1]>          = " << dot_b << "\n";

	// (7) Pack 128 bools into one block (= LSB-first bit-packing).
	bool packed_bits[128] = {0};
	packed_bits[0] = packed_bits[7] = packed_bits[64] = 1;
	block pack_bool;
	pkr.packing(&pack_bool, packed_bits);
	cout << "  packing(bool[bit0|7|64]) = " << pack_bool << "\n";
}

// ---------- scalar reference (ground truth) ----------
//
// Bit-by-bit GF(2^128) multiplication using the same irreducible polynomial
// as f2k.h's reduce(): x^128 + x^7 + x^2 + x + 1 (GHASH polynomial).
static block ref_gfmul(block a, block b) {
	uint64_t a_lo = ((uint64_t *)&a)[0], a_hi = ((uint64_t *)&a)[1];
	uint64_t b_lo = ((uint64_t *)&b)[0], b_hi = ((uint64_t *)&b)[1];
	uint64_t z[4] = {0, 0, 0, 0};
	for (int i = 0; i < 128; ++i) {
		bool bit = i < 64 ? ((b_lo >> i) & 1) : ((b_hi >> (i - 64)) & 1);
		if (!bit) continue;
		int word = i / 64, sh = i % 64;
		if (sh == 0) {
			z[word]     ^= a_lo;
			z[word + 1] ^= a_hi;
		} else {
			z[word]     ^= a_lo << sh;
			z[word + 1] ^= (a_lo >> (64 - sh)) ^ (a_hi << sh);
			z[word + 2] ^= a_hi >> (64 - sh);
		}
	}
	static const int pp[4] = {0, 1, 2, 7};  // x^7 + x^2 + x + 1
	for (int i = 255; i >= 128; --i) {
		int word = i / 64, sh = i % 64;
		if (!((z[word] >> sh) & 1)) continue;
		z[word] ^= 1ULL << sh;
		int j = i - 128;
		for (int k = 0; k < 4; ++k) {
			int p = j + pp[k];
			z[p / 64] ^= 1ULL << (p % 64);
		}
	}
	return makeBlock(z[1], z[0]);
}

// ---------- correctness ----------

static bool blocks_eq(block a, block b) {
	__m128i d = _mm_xor_si128(a, b);
	return _mm_testz_si128(d, d);
}

static bool check_gf_identities() {
	PRG prg;
	block a, b, c, r;
	prg.random_block(&a, 1);
	prg.random_block(&b, 1);
	prg.random_block(&c, 1);

	// a * 0 = 0
	gfmul(a, makeBlock(0, 0), &r);
	if (!blocks_eq(r, makeBlock(0, 0))) return false;

	// a * 1 = a
	gfmul(a, makeBlock(0, 1), &r);
	if (!blocks_eq(r, a)) return false;

	// commutativity
	block ab, ba;
	gfmul(a, b, &ab);
	gfmul(b, a, &ba);
	if (!blocks_eq(ab, ba)) return false;

	// distributivity: a*(b ⊕ c) = a*b ⊕ a*c
	block lhs, rhs1, rhs2;
	gfmul(a, b ^ c, &lhs);
	gfmul(a, b, &rhs1);
	gfmul(a, c, &rhs2);
	if (!blocks_eq(lhs, rhs1 ^ rhs2)) return false;

	// gfmul matches scalar reference for random inputs
	for (int t = 0; t < 64; ++t) {
		prg.random_block(&a, 1);
		prg.random_block(&b, 1);
		gfmul(a, b, &r);
		if (!blocks_eq(r, ref_gfmul(a, b))) return false;
	}
	return true;
}

static bool check_vector_inn_prdt(int sz) {
	PRG prg;
	vector<block> xs(sz), ys(sz);
	prg.random_block(xs.data(), sz);
	prg.random_block(ys.data(), sz);

	// reduced
	block got, want = makeBlock(0, 0), t;
	vector_inn_prdt_sum_red(&got, xs.data(), ys.data(), sz);
	for (int i = 0; i < sz; ++i) {
		gfmul(xs[i], ys[i], &t);
		want = want ^ t;
	}
	if (!blocks_eq(got, want)) return false;

	// unreduced (256-bit accumulator)
	block got2[2], want2[2] = {makeBlock(0, 0), makeBlock(0, 0)};
	vector_inn_prdt_sum_no_red(got2, xs.data(), ys.data(), sz);
	block lo, hi;
	for (int i = 0; i < sz; ++i) {
		mul128(xs[i], ys[i], &lo, &hi);
		want2[0] = want2[0] ^ lo;
		want2[1] = want2[1] ^ hi;
	}
	return blocks_eq(got2[0], want2[0]) && blocks_eq(got2[1], want2[1]);
}

static bool check_uni_hash_coeff_gen(int sz) {
	PRG prg;
	block seed;
	prg.random_block(&seed, 1);
	vector<block> coeff(sz);
	uni_hash_coeff_gen(coeff.data(), seed, sz);
	// coeff[i] should equal seed^(i+1).
	block want = seed;
	if (!blocks_eq(coeff[0], want)) return false;
	for (int i = 1; i < sz; ++i) {
		gfmul(want, seed, &want);
		if (!blocks_eq(coeff[i], want)) return false;
	}
	return true;
}

static bool check_packing() {
	PRG prg;
	GaloisFieldPacking pkr;
	for (int t = 0; t < 16; ++t) {
		block data[128];
		prg.random_block(data, 128);
		block got;
		pkr.packing(&got, data);
		// Reference: Σ data[i] * X^i where X^i has only bit i set.
		block want = makeBlock(0, 0), prod;
		for (int i = 0; i < 128; ++i) {
			block xi = set_bit(makeBlock(0, 0), i);
			gfmul(data[i], xi, &prod);
			want = want ^ prod;
		}
		if (!blocks_eq(got, want)) return false;
	}
	return true;
}

static bool check_vector_inn_prdt_bool(int sz) {
	PRG prg;
	vector<block> xs(sz);
	prg.random_block(xs.data(), sz);
	vector<uint8_t> bs_bytes(sz);
	prg.random_bool(reinterpret_cast<bool *>(bs_bytes.data()), sz);
	const bool *bs = reinterpret_cast<const bool *>(bs_bytes.data());

	block got;
	vector_inn_prdt_sum_red(&got, xs.data(), bs, sz);

	// Reference: XOR of xs[i] for indices where bs[i] = 1.
	block want = makeBlock(0, 0);
	for (int i = 0; i < sz; ++i)
		if (bs[i]) want = want ^ xs[i];
	return blocks_eq(got, want);
}

static bool check_packing_bool() {
	PRG prg;
	GaloisFieldPacking pkr;
	for (int t = 0; t < 16; ++t) {
		uint8_t bits_bytes[128];
		prg.random_bool(reinterpret_cast<bool *>(bits_bytes), 128);
		const bool *bits = reinterpret_cast<const bool *>(bits_bytes);
		block got;
		pkr.packing(&got, bits);
		// Reference: same identity as block-version, with each X^i contributing
		// only when bits[i]=1.
		block want = makeBlock(0, 0);
		for (int i = 0; i < 128; ++i)
			if (bits[i]) want = want ^ set_bit(makeBlock(0, 0), i);
		if (!blocks_eq(got, want)) return false;
	}
	return true;
}

static bool check_vector_self_xor(int sz) {
	PRG prg;
	vector<block> xs(sz);
	prg.random_block(xs.data(), sz);
	block got, want = makeBlock(0, 0);
	vector_self_xor(&got, xs.data(), sz);
	for (int i = 0; i < sz; ++i) want = want ^ xs[i];
	return blocks_eq(got, want);
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool a = check_gf_identities();
	cout << "  GF identities + scalar ref     " << (a ? "OK" : "FAIL") << "\n";
	bool b = true;
	for (int sz : {1, 7, 64, 1024}) b &= check_vector_inn_prdt(sz);
	cout << "  vector_inn_prdt_sum_*          " << (b ? "OK" : "FAIL") << "\n";
	bool b2 = true;
	for (int sz : {1, 7, 64, 1024}) b2 &= check_vector_inn_prdt_bool(sz);
	cout << "  vector_inn_prdt_sum_red(bool)  " << (b2 ? "OK" : "FAIL") << "\n";
	bool c = true;
	for (int sz : {1, 4, 16, 1024}) c &= check_uni_hash_coeff_gen(sz);
	cout << "  uni_hash_coeff_gen             " << (c ? "OK" : "FAIL") << "\n";
	bool d = check_packing();
	cout << "  GaloisFieldPacking::packing    " << (d ? "OK" : "FAIL") << "\n";
	bool d2 = check_packing_bool();
	cout << "  GaloisFieldPacking::packing(bool) " << (d2 ? "OK" : "FAIL") << "\n";
	bool e = true;
	for (int sz : {1, 4, 17, 1024}) e &= check_vector_self_xor(sz);
	cout << "  vector_self_xor                " << (e ? "OK" : "FAIL") << "\n";
	return a && b && b2 && c && d && d2 && e;
}

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

static void print_vec(const string &lbl, double calls, int n) {
	double GiBps = calls * (double)n * 16.0 / (1024.0 * 1024.0 * 1024.0);
	double cy_per_blk = 3e9 / (calls * n);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_blk << " cy/blk @3GHz\n";
}

static const initializer_list<int> SIZES = {16, 64, 256, 1024, 4096, 16384, 65536};

static void bench(double sec) {
	PRG prg;

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

	cout << "\n=== vector_inn_prdt_sum_red (deferred reduction, 4-way unroll) ===\n";
	for (int n : SIZES) {
		vector<block> a(n), b(n);
		prg.random_block(a.data(), n);
		prg.random_block(b.data(), n);
		block r;
		double calls = run_for(sec, [&]() {
			vector_inn_prdt_sum_red(&r, a.data(), b.data(), n);
		}, &r);
		ostringstream lbl; lbl << "vec_inn_prdt_red(N=" << n << ")";
		print_vec(lbl.str(), calls, n);
	}

	cout << "\n=== vector_inn_prdt_sum_red (bool selector, branchless mask + XOR) ===\n";
	for (int n : SIZES) {
		vector<block> a(n);
		vector<uint8_t> bs(n);
		prg.random_block(a.data(), n);
		prg.random_bool(reinterpret_cast<bool *>(bs.data()), n);
		block r;
		double calls = run_for(sec, [&]() {
			vector_inn_prdt_sum_red(&r, a.data(),
			    reinterpret_cast<const bool *>(bs.data()), n);
		}, &r);
		ostringstream lbl; lbl << "vec_inn_prdt_red(bool, N=" << n << ")";
		print_vec(lbl.str(), calls, n);
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
		print_vec(lbl.str(), calls, n);
	}

	cout << "\n=== uni_hash_coeff_gen (coeff[i] = seed^(i+1)) ===\n";
	for (int n : {16, 64, 256, 1024, 4096, 16384}) {
		vector<block> coeff(n);
		block seed; prg.random_block(&seed, 1);
		double calls = run_for(sec, [&]() {
			uni_hash_coeff_gen(coeff.data(), seed, n);
		}, coeff.data());
		ostringstream lbl; lbl << "uni_hash_coeff_gen(N=" << n << ")";
		print_vec(lbl.str(), calls, n);
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
		print_vec(lbl.str(), calls, n);
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
		prg.random_bool(reinterpret_cast<bool *>(bits), 128);
		block r;
		double calls = run_for(sec, [&]() {
			pkr.packing(&r, reinterpret_cast<const bool *>(bits));
		}, &r);
		print_op("packing(bool*, 128)", calls);
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
