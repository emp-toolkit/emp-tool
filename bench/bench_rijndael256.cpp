// crypto/rijndael256.h — Rijndael-256 ECB throughput, with AES-128 over the
// same bytes on the same harness for reference.
//
// Rows:
//   R256_set_{en,de}crypt_key            chained through the last round key
//   R256_ecb_{en,de}crypt_blks(N=1)      one 256-bit block, output fed back in
//   R256_ecb_encrypt_blks<N>             compile-time widths
//   R256_ecb_{en,de}crypt_blks(blks, n)  runtime sweep
//   AES_ecb_encrypt_blks(blks, 2n)       AES-128 on the same bytes

#include "emp-tool/emp-tool.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
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
	cout << "  " << left << setw(40) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << calls / 1e6 << " Mops  "
	     << setw(7) << (3e9 / calls) << " cy/op @3GHz\n";
}

static void print_bytes(const string &lbl, double calls, int64_t bytes_per_call) {
	const double bytes = calls * (double)bytes_per_call;
	cout << "  " << left << setw(40) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << bytes / (1024.0 * 1024.0 * 1024.0) << " GiB/s "
	     << setw(7) << setprecision(3) << (3e9 / bytes) << " cy/B @3GHz\n";
}

template <int N>
static double bench_template(const R256_KEY &ek, double sec) {
	vector<block> buf(2 * N);
	PRG().random_block(buf.data(), 2 * N);
	return run_for(sec, [&]() { R256_ecb_encrypt_blks<N>(buf.data(), &ek); }, buf.data());
}

// Either schedule: R256_ecb_encrypt_blks or R256_ecb_decrypt_blks by type.
template <class Key>
static double bench_runtime(const Key &key, int64_t n, double sec) {
	vector<block> buf(2 * n);
	PRG().random_block(buf.data(), 2 * n);
	return run_for(sec, [&]() {
		int64_t call_n = n;  // keep the runtime width opaque to the optimizer
		asm volatile("" : "+r"(call_n));
		if constexpr (is_same_v<Key, R256_KEY>) R256_ecb_encrypt_blks(buf.data(), call_n, &key);
		else                                    R256_ecb_decrypt_blks(buf.data(), call_n, &key);
	}, buf.data());
}

static double bench_aes_runtime(const AES_KEY &ek, int64_t nblks, double sec) {
	vector<block> buf(nblks);
	PRG().random_block(buf.data(), nblks);
	return run_for(sec, [&]() {
		int64_t call_n = nblks;
		asm volatile("" : "+r"(call_n));
		AES_ecb_encrypt_blks(buf.data(), call_n, &ek);
	}, buf.data());
}

static void bench(double sec) {
	PRG prg;
#if EMP_HAS_R256_VPERMB512
	cout << "Rijndael-256 tier: VAES-512 + VBMI (vpermb, 2 blocks per zmm)\n\n";
#elif EMP_HAS_VAES256
	cout << "Rijndael-256 tier: VAES-256 (split halves, 2 blocks per ymm pair)\n\n";
#elif defined(__aarch64__)
	cout << "Rijndael-256 tier: ARMv8 Crypto (TBL, 1 block per register pair)\n\n";
#else
	cout << "Rijndael-256 tier: AES-NI + SSE4.1 (split halves, 1 block per xmm pair)\n\n";
#endif

	block uk[2];
	prg.random_block(uk, 2);
	R256_KEY ek;
	R256_set_encrypt_key(uk, &ek);
	R256_DEC_KEY dk;
	R256_set_decrypt_key(uk, &dk);
	AES_KEY aes;
	AES_set_encrypt_key(uk[0], &aes);

	cout << "=== single-shot (latency, serial-dep chain) ===\n";
	{
		// Chained through the last round key so the schedule cannot be hoisted.
		block k[2] = {uk[0], uk[1]};
		R256_KEY sk;
		print_op("R256_set_encrypt_key", run_for(sec, [&]() {
			R256_set_encrypt_key(k, &sk);
			k[0] = sk.rd_key[28];
			k[1] = sk.rd_key[29];
		}, &sk));
		R256_DEC_KEY sd;
		print_op("R256_set_decrypt_key", run_for(sec, [&]() {
			R256_set_decrypt_key(k, &sd);
			k[0] = sd.rd_key[0];  // the first decryption round key
			k[1] = sd.rd_key[1];
		}, &sd));
	}
	{
		block x[2];
		prg.random_block(x, 2);
		print_op("R256_ecb_encrypt_blks(N=1)", run_for(sec, [&]() { R256_ecb_encrypt_blks(x, 1, &ek); }, x));
		print_op("R256_ecb_decrypt_blks(N=1)", run_for(sec, [&]() { R256_ecb_decrypt_blks(x, 1, &dk); }, x));
	}
	{
		block x;
		prg.random_block(&x, 1);
		print_op("AES_ecb_encrypt_blks(N=1)  [AES-128]", run_for(sec, [&]() { AES_ecb_encrypt_blks(&x, 1, &aes); }, &x));
	}

	cout << "\n=== R256_ecb_encrypt_blks<N>  (compile-time width) ===\n";
	print_bytes("R256_ecb_encrypt_blks<1>", bench_template<1>(ek, sec), 32);
	print_bytes("R256_ecb_encrypt_blks<2>", bench_template<2>(ek, sec), 64);
	print_bytes("R256_ecb_encrypt_blks<4>", bench_template<4>(ek, sec), 128);
	print_bytes("R256_ecb_encrypt_blks<8>", bench_template<8>(ek, sec), 256);
	print_bytes("R256_ecb_encrypt_blks<16>", bench_template<16>(ek, sec), 512);

	cout << "\n=== R256_ecb_{en,de}crypt_blks(blks, n)  vs  AES_ecb_encrypt_blks(blks, 2n) ===\n";
	for (int64_t n : {1, 4, 16, 64, 256, 1024}) {
		ostringstream r, d, a;
		r << "R256_ecb_encrypt_blks(n=" << n << ")";
		d << "R256_ecb_decrypt_blks(n=" << n << ")";
		a << "AES_ecb_encrypt_blks(n=" << 2 * n << ")  [AES-128]";
		print_bytes(r.str(), bench_runtime(ek, n, sec), 32 * n);
		print_bytes(d.str(), bench_runtime(dk, n, sec), 32 * n);
		print_bytes(a.str(), bench_aes_runtime(aes, 2 * n, sec), 32 * n);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
