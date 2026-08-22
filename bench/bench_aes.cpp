// crypto/aes.h — AES-128 ECB primitives used as the bottom crypto layer of
// emp's PRG/PRP/CCRH chain. Throughput benchmark.
//
// What's in aes.h:
//   AES_KEY                                         11-round-key schedule
//   AES_opt_key_schedule<K>(user_keys, ek)          interleaved K-key schedule
//   ParaEnc<K, N>(blks, keys)                       K-major templated tile
//   ParaEnc(blks, keys, K, N)                       runtime, dispatches to tiles
//   AES_set_encrypt_key(uk, &ek)                    single-key wrapper
//   AES_ecb_encrypt_blks<N>(blks, &ek)              single-key templated wrapper
//   AES_ecb_encrypt_blks(blks, n, &ek)              single-key runtime wrapper

#include "emp-tool/emp-tool.h"

#include <openssl/evp.h>

#include <chrono>
#include <cstdlib>
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

// ---------- helpers ----------

static block bytes_to_block(const uint8_t b[16]) {
	return _mm_loadu_si128(reinterpret_cast<const __m128i *>(b));
}
static void block_to_bytes(block x, uint8_t b[16]) {
	_mm_storeu_si128(reinterpret_cast<__m128i *>(b), x);
}
static string hex16(const uint8_t b[16]) {
	ostringstream os;
	os << hex << setfill('0');
	for (int i = 0; i < 16; ++i) os << setw(2) << (int)b[i];
	return os.str();
}

static void openssl_aes128_ecb(const uint8_t key[16], const uint8_t *in,
                               uint8_t *out, int nblks) {
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr);
	EVP_CIPHER_CTX_set_padding(ctx, 0);
	int outl = 0, finl = 0;
	EVP_EncryptUpdate(ctx, out, &outl, in, 16 * nblks);
	EVP_EncryptFinal_ex(ctx, out + outl, &finl);
	EVP_CIPHER_CTX_free(ctx);
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

static void print_vec(const string &lbl, double calls, int64_t blocks_per_call) {
	double GiBps = calls * (double)blocks_per_call * 16.0 / (1024.0 * 1024.0 * 1024.0);
	double cy_per_blk = 3e9 / (calls * blocks_per_call);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_blk << " cy/blk @3GHz\n";
}

template <int K, int N>
static double bench_paraenc_template(double sec) {
	alignas(16) block keys[K];
	PRG().random_block(keys, K);
	alignas(16) AES_KEY skeys[K];
	AES_opt_key_schedule<K>(keys, skeys);
	alignas(16) block buf[K * N];
	PRG().random_block(buf, K * N);
	return run_for(sec, [&]() { ParaEnc<K, N>(buf, skeys); }, &buf[0]);
}

static double bench_paraenc_runtime(int K, int N, double sec) {
	vector<block> keys(K);
	PRG().random_block(keys.data(), K);
	vector<AES_KEY> skeys(K);
	for (int k = 0; k < K; ++k) AES_set_encrypt_key(keys[k], &skeys[k]);
	vector<block> buf(K * N);
	PRG().random_block(buf.data(), K * N);
	return run_for(sec, [&]() {
		// Cross the barrier on every invocation so loop-invariant motion cannot
		// turn this runtime-dispatch row into a one-time shape check.
		int call_K = K, call_N = N;
		asm volatile("" : "+r"(call_K), "+r"(call_N));
		ParaEnc(buf.data(), skeys.data(), call_K, call_N);
	}, buf.data());
}

static void bench(double sec) {
	PRG prg;

#if EMP_HAS_VAES512
	cout << "AES SIMD tier: VAES-512 (4 blocks/vector)\n\n";
#elif EMP_HAS_VAES256
	cout << "AES SIMD tier: VAES-256 (2 blocks/vector)\n\n";
#elif defined(__aarch64__)
	cout << "AES SIMD tier: ARMv8 Crypto (1 block/vector)\n\n";
#else
	cout << "AES SIMD tier: AES-NI (1 block/vector)\n\n";
#endif

	cout << "=== single-shot (latency, serial-dep chain) ===\n";
	{
		// AES_set_encrypt_key chained through the LAST round key. Chaining
		// through rd_key[0] would be a no-op (rd_key[0] == userkey by
		// construction) and the compiler hoists the schedule.
		block uk; prg.random_block(&uk, 1);
		alignas(16) AES_KEY ek;
		double calls = run_for(sec, [&]() {
			AES_set_encrypt_key(uk, &ek);
			uk = ek.rd_key[10];
		}, &ek);
		print_op("AES_set_encrypt_key", calls);
	}
	{
		// Single-block encrypt with output-as-input chain.
		block uk; prg.random_block(&uk, 1);
		alignas(16) AES_KEY ek;
		AES_set_encrypt_key(uk, &ek);
		alignas(16) block x; prg.random_block(&x, 1);
		double calls = run_for(sec, [&]() {
			AES_ecb_encrypt_blks(&x, 1, &ek);
		}, &x);
		print_op("AES_ecb_encrypt_blks(N=1)", calls);
	}

	cout << "\n=== ParaEnc<K, N>  (templated, N blocks under K keys) ===\n";
	print_vec("ParaEnc<1,1>",  bench_paraenc_template<1, 1>(sec),  1);
	print_vec("ParaEnc<1,2>",  bench_paraenc_template<1, 2>(sec),  2);
	print_vec("ParaEnc<1,4>",  bench_paraenc_template<1, 4>(sec),  4);
	print_vec("ParaEnc<1,8>",  bench_paraenc_template<1, 8>(sec),  8);
	print_vec("ParaEnc<1,16>", bench_paraenc_template<1, 16>(sec), 16);
	print_vec("ParaEnc<1,32>", bench_paraenc_template<1, 32>(sec), 32);
	print_vec("ParaEnc<2,1>",  bench_paraenc_template<2, 1>(sec),  2);
	print_vec("ParaEnc<2,4>",  bench_paraenc_template<2, 4>(sec),  8);
	print_vec("ParaEnc<4,1>",  bench_paraenc_template<4, 1>(sec),  4);
	print_vec("ParaEnc<4,4>",  bench_paraenc_template<4, 4>(sec),  16);
	print_vec("ParaEnc<8,1>",  bench_paraenc_template<8, 1>(sec),  8);
	print_vec("ParaEnc<8,2>",  bench_paraenc_template<8, 2>(sec),  16);
	print_vec("ParaEnc<8,4>",  bench_paraenc_template<8, 4>(sec),  32);
	print_vec("ParaEnc<8,8>",  bench_paraenc_template<8, 8>(sec),  64);

	cout << "\n=== ParaEnc(blks, keys, K, N)  (runtime dispatcher) ===\n";
	for (int n : {1, 2, 4, 8, 16, 64, 256, 1024}) {
		ostringstream lbl; lbl << "ParaEnc(K=1, N=" << n << ")";
		print_vec(lbl.str(), bench_paraenc_runtime(1, n, sec), n);
	}
	for (int n : {1, 4, 16, 64}) {
		ostringstream lbl; lbl << "ParaEnc(K=8, N=" << n << ")";
		print_vec(lbl.str(), bench_paraenc_runtime(8, n, sec), 8 * n);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
