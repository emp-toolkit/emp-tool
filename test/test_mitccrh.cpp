// crypto/mitccrh.h — Multi-Instance Tweakable CCRH (Guo–Katz–Wang–Yang 2019).
// A batched key schedule + AES-encrypt-then-XOR-input construction used by
// half-gates garbling. Read example() first; the rest is verification + throughput.
//
// What's in mitccrh.h:
//   MITCCRH<BatchSize>                       AES_KEY scheduled_key[B], gid, start_point
//   setS(s)                                  reset start_point + gid
//   renew_ks() / renew_ks(gid)               schedule B keys from gid sequence
//   renew_ks(const block * tweaks)           schedule B keys from explicit tweaks
//   hash<K, H>(blks)                         consume K scheduled keys, hash K*H blocks
//   hash_cir<K, H>(blks)                     sigma() each block first, then hash<K,H>

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

	// (1) setS + hash<K, H>: feed K*H blocks, the construction XORs input back
	// over the encrypted output (TMMO).
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	MITCCRH<8> mit;
	mit.setS(S);

	alignas(16) block buf[8] = {
		makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4),
		makeBlock(0, 5), makeBlock(0, 6), makeBlock(0, 7), makeBlock(0, 8),
	};
	mit.hash<8, 1>(buf);
	cout << "  hash<8,1>(buf)[0]   = " << buf[0] << "\n";
	cout << "  hash<8,1>(buf)[7]   = " << buf[7] << "\n";

	// (2) hash_cir<K, H> applies sigma() first, then hash. Used in half-gates.
	mit.setS(S);  // reset for a fresh transcript
	alignas(16) block buf2[2] = {makeBlock(0, 0xAA), makeBlock(0, 0xBB)};
	mit.hash_cir<2, 1>(buf2);
	cout << "  hash_cir<2,1>[0]    = " << buf2[0] << "\n";

	// (3) renew_ks(tweaks): schedule from explicit tweaks instead of gid.
	mit.setS(S);
	alignas(16) block tweaks[8];
	for (int i = 0; i < 8; ++i) tweaks[i] = makeBlock(i + 100, 0);
	mit.renew_ks(tweaks);
	alignas(16) block buf3[8];
	for (int i = 0; i < 8; ++i) buf3[i] = makeBlock(i, i);
	mit.hash<8, 1>(buf3);
	cout << "  hash<8,1> w/ tweaks[0] = " << buf3[0] << "\n";
}

// ---------- correctness ----------
//
// Reference: keys[i] = S ^ makeBlock(gid0+i, 0); schedule all B with
// AES_opt_key_schedule<B>; ParaEnc<K, H> consumes scheduled_key[0..K), then
// MITCCRH XORs the ciphertext with the original input.

template <int K, int H, int B = 8>
static bool check_mitccrh_against_paraenc() {
	static_assert(K <= B && B % K == 0, "MITCCRH<B> requires K | B");
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	const uint64_t gid0 = 7;

	MITCCRH<B> mit;
	mit.setS(S);
	mit.gid = gid0;
	mit.key_used = B;  // force a fresh schedule on first hash()

	alignas(16) block in[K * H];
	PRG().random_block(in, K * H);

	alignas(16) block emp_out[K * H];
	memcpy(emp_out, in, sizeof(in));
	mit.template hash<K, H>(emp_out);

	alignas(16) block ref_keys[B];
	for (int i = 0; i < B; ++i) ref_keys[i] = S ^ makeBlock(gid0 + (uint64_t)i, 0);
	alignas(16) AES_KEY skeys[B];
	AES_opt_key_schedule<B>(ref_keys, skeys);

	alignas(16) block ref_out[K * H];
	memcpy(ref_out, in, sizeof(in));
	ParaEnc<K, H>(ref_out, skeys);
	for (int i = 0; i < K * H; ++i) ref_out[i] = ref_out[i] ^ in[i];

	bool ok = memcmp(emp_out, ref_out, sizeof(in)) == 0;
	ostringstream os;
	os << "  [MITCCRH<" << B << ">::hash<" << K << "," << H << "> = ParaEnc^in]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_hash_cir() {
	const block S = makeBlock(0x1234ULL, 0x5678ULL);
	MITCCRH<8> a, b;
	a.setS(S); b.setS(S);
	a.gid = b.gid = 0;
	a.key_used = b.key_used = 8;

	alignas(16) block in[K * H];
	PRG().random_block(in, K * H);

	alignas(16) block via_cir[K * H], via_manual[K * H];
	memcpy(via_cir, in, sizeof(in));
	a.template hash_cir<K, H>(via_cir);

	for (int i = 0; i < K * H; ++i) via_manual[i] = sigma(in[i]);
	b.template hash<K, H>(via_manual);

	bool ok = memcmp(via_cir, via_manual, sizeof(in)) == 0;
	ostringstream os;
	os << "  [hash_cir<" << K << "," << H << "> = hash(sigma(.))]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_setS_resets_gid() {
	MITCCRH<8> mit;
	mit.setS(makeBlock(1, 2));
	mit.gid = 99;
	mit.setS(makeBlock(3, 4));
	bool ok = (mit.gid == 0);
	cout << "  [setS resets gid]                              " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_setS_resets_gid();
	ok &= check_mitccrh_against_paraenc<1, 1>();
	ok &= check_mitccrh_against_paraenc<1, 4>();
	ok &= check_mitccrh_against_paraenc<2, 1>();
	ok &= check_mitccrh_against_paraenc<4, 1>();
	ok &= check_mitccrh_against_paraenc<8, 1>();
	ok &= check_mitccrh_against_paraenc<8, 4>();
	ok &= check_hash_cir<2, 1>();
	ok &= check_hash_cir<2, 2>();
	ok &= check_hash_cir<8, 1>();
	return ok;
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

static void print_vec(const string &lbl, double calls, int blocks_per_call) {
	double GiBps = calls * (double)blocks_per_call * 16.0 / (1024.0 * 1024.0 * 1024.0);
	double cy_per_blk = 3e9 / (calls * blocks_per_call);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_blk << " cy/blk @3GHz\n";
}

template <int K, int H>
static double bench_hash(double sec) {
	static_assert(K <= 8 && 8 % K == 0, "MITCCRH<8> requires K | 8");
	MITCCRH<8> mit;
	mit.setS(makeBlock(1, 2));
	mit.renew_ks(uint64_t{0});
	alignas(16) block buf[K * H];
	PRG().random_block(buf, K * H);
	return run_for(sec, [&]() { mit.template hash<K, H>(buf); }, &buf[0]);
}

template <int K, int H>
static double bench_hash_cir(double sec) {
	static_assert(K <= 8 && 8 % K == 0, "MITCCRH<8> requires K | 8");
	MITCCRH<8> mit;
	mit.setS(makeBlock(1, 2));
	mit.renew_ks(uint64_t{0});
	alignas(16) block buf[K * H];
	PRG().random_block(buf, K * H);
	return run_for(sec, [&]() { mit.template hash_cir<K, H>(buf); }, &buf[0]);
}

static void bench(double sec) {
	cout << "=== MITCCRH<8>::hash<K, H>  (renew_ks amortized over B/K = 8/K calls) ===\n";
	print_vec("hash<1,1>", bench_hash<1, 1>(sec), 1);
	print_vec("hash<1,4>", bench_hash<1, 4>(sec), 4);
	print_vec("hash<1,8>", bench_hash<1, 8>(sec), 8);
	print_vec("hash<2,1>", bench_hash<2, 1>(sec), 2);
	print_vec("hash<2,4>", bench_hash<2, 4>(sec), 8);
	print_vec("hash<4,1>", bench_hash<4, 1>(sec), 4);
	print_vec("hash<4,4>", bench_hash<4, 4>(sec), 16);
	print_vec("hash<8,1>", bench_hash<8, 1>(sec), 8);
	print_vec("hash<8,4>", bench_hash<8, 4>(sec), 32);
	print_vec("hash<8,8>", bench_hash<8, 8>(sec), 64);

	cout << "\n=== MITCCRH<8>::hash_cir<K, H>  (sigma + hash) ===\n";
	print_vec("hash_cir<1,1>", bench_hash_cir<1, 1>(sec), 1);
	print_vec("hash_cir<2,1>", bench_hash_cir<2, 1>(sec), 2);
	print_vec("hash_cir<2,2>", bench_hash_cir<2, 2>(sec), 4);
	print_vec("hash_cir<8,1>", bench_hash_cir<8, 1>(sec), 8);
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
