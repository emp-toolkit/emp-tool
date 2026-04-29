// utils/aes.h — AES-128 ECB primitives used as the bottom crypto layer of
// emp's PRG/PRP/CCRH chain. Read example() first; the rest is verification +
// throughput.
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

// ---------- example: typical usage ----------

static void example() {
	cout << "=== example ===\n";

	// (1) Single-key schedule + 1-block ECB encrypt.
	block key = makeBlock(0x0f0e0d0c0b0a0908ULL, 0x0706050403020100ULL);
	block msg = makeBlock(0xffeeddccbbaa9988ULL, 0x7766554433221100ULL);
	AES_KEY ek;
	AES_set_encrypt_key(key, &ek);
	block ct = msg;
	AES_ecb_encrypt_blks(&ct, 1, &ek);
	cout << "  AES_ecb_encrypt(msg)         = " << ct << "\n";

	// (2) Templated tile: encrypt 4 blocks under one key with the unrolled kernel.
	alignas(16) block tile[4] = {msg, msg, msg, msg};
	AES_ecb_encrypt_blks<4>(tile, &ek);
	cout << "  AES_ecb_encrypt_blks<4>[0]   = " << tile[0] << "\n";

	// (3) Multi-key schedule: 2 keys → 2 round-key sets, interleaved.
	alignas(16) block user_keys[2] = {key, key ^ makeBlock(0, 1)};
	alignas(16) AES_KEY eks[2];
	AES_opt_key_schedule<2>(user_keys, eks);
	alignas(16) block buf[2] = {msg, msg};
	ParaEnc<2, 1>(buf, eks);
	cout << "  ParaEnc<2,1> (buf[0])        = " << buf[0] << "\n";
	cout << "  ParaEnc<2,1> (buf[1])        = " << buf[1] << "\n";
}

// ---------- correctness ----------

static bool check_nist_vector() {
	// FIPS-197 Appendix C.1 known answer.
	uint8_t key_bytes[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	                         0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
	uint8_t pt_bytes[16]  = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	                         0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	uint8_t ct_ref[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
	                      0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};

	alignas(16) AES_KEY ek;
	block kb = bytes_to_block(key_bytes);
	AES_set_encrypt_key(kb, &ek);
	block blk = bytes_to_block(pt_bytes);
	AES_ecb_encrypt_blks(&blk, 1, &ek);
	uint8_t got[16];
	block_to_bytes(blk, got);

	bool ok = memcmp(got, ct_ref, 16) == 0;
	cout << "  [NIST FIPS-197 C.1 vector]              " << (ok ? "OK" : "FAIL")
	     << "  got=" << hex16(got) << "\n";
	return ok;
}

static bool check_against_openssl(int trials = 64, int blks_per = 33) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		uint8_t kb[16];
		prg.random_data_unaligned(kb, 16);

		vector<block> in(blks_per);
		prg.random_block(in.data(), blks_per);
		vector<block> emp_out = in;

		alignas(16) AES_KEY ek;
		block kblk = bytes_to_block(kb);
		AES_set_encrypt_key(kblk, &ek);
		AES_ecb_encrypt_blks(emp_out.data(), blks_per, &ek);

		vector<uint8_t> ref_out(16 * blks_per);
		openssl_aes128_ecb(kb, reinterpret_cast<const uint8_t *>(in.data()),
		                   ref_out.data(), blks_per);

		if (memcmp(emp_out.data(), ref_out.data(), ref_out.size()) != 0) {
			cout << "  [AES_ecb_encrypt_blks vs OpenSSL]      FAIL  trial=" << t << "\n";
			return false;
		}
	}
	cout << "  [AES_ecb_encrypt_blks vs OpenSSL]      OK   trials=" << trials
	     << " blks/trial=" << blks_per << "\n";
	return true;
}

template <int K, int N>
static bool check_paraenc_template() {
	PRG prg;
	alignas(16) block keys[K];
	alignas(16) block in[K * N];
	prg.random_block(keys, K);
	prg.random_block(in, K * N);

	alignas(16) block emp_out[K * N];
	memcpy(emp_out, in, sizeof(in));

	alignas(16) AES_KEY skeys[K];
	AES_opt_key_schedule<K>(keys, skeys);
	ParaEnc<K, N>(emp_out, skeys);

	// Reference: per-key OpenSSL AES-128-ECB. Validates AES_opt_key_schedule
	// and ParaEnc together against an external implementation.
	vector<uint8_t> ref_bytes(16 * K * N);
	for (int k = 0; k < K; ++k) {
		uint8_t kb[16];
		block_to_bytes(keys[k], kb);
		openssl_aes128_ecb(kb, reinterpret_cast<const uint8_t *>(in + k * N),
		                   ref_bytes.data() + 16 * k * N, N);
	}

	bool ok = memcmp(emp_out, ref_bytes.data(), 16 * K * N) == 0;
	ostringstream os;
	os << "  [ParaEnc<" << K << "," << N << "> vs OpenSSL]";
	cout << left << setw(42) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_paraenc_runtime(int K, int N, int trials = 4) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		vector<block> keys(K);
		prg.random_block(keys.data(), K);
		vector<block> in(K * N);
		prg.random_block(in.data(), K * N);
		vector<block> emp_out = in;

		vector<AES_KEY> skeys(K);
		for (int k = 0; k < K; ++k)
			AES_set_encrypt_key(keys[k], &skeys[k]);
		ParaEnc(emp_out.data(), skeys.data(), K, N);

		vector<uint8_t> ref_bytes(16 * K * N);
		for (int k = 0; k < K; ++k) {
			uint8_t kb[16];
			block_to_bytes(keys[k], kb);
			openssl_aes128_ecb(kb, reinterpret_cast<const uint8_t *>(&in[k * N]),
			                   ref_bytes.data() + 16 * k * N, N);
		}
		if (memcmp(emp_out.data(), ref_bytes.data(), ref_bytes.size()) != 0) {
			cout << "  [ParaEnc(K=" << K << ",N=" << N << ") runtime vs OpenSSL]  FAIL  t=" << t << "\n";
			return false;
		}
	}
	ostringstream os;
	os << "  [ParaEnc(K=" << K << ",N=" << N << ") runtime vs OpenSSL]";
	cout << left << setw(42) << os.str() << "OK\n";
	return true;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_nist_vector();
	ok &= check_against_openssl();
	ok &= check_paraenc_template<1, 1>();
	ok &= check_paraenc_template<1, 8>();
	ok &= check_paraenc_template<2, 1>();
	ok &= check_paraenc_template<4, 4>();
	ok &= check_paraenc_template<8, 1>();
	ok &= check_paraenc_template<8, 4>();
	ok &= check_paraenc_template<3, 5>();
	ok &= check_paraenc_runtime(1, 1);
	ok &= check_paraenc_runtime(1, 17);   // exercises 8+4+2+1 dispatcher
	ok &= check_paraenc_runtime(5, 11);
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

static void print_op(const string &lbl, double calls) {
	double Mops = calls / 1e6;
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << Mops << " Mops  "
	     << setw(7) << (3e9 / calls) << " cy/op @3GHz\n";
}

static void print_vec(const string &lbl, double calls, int blocks_per_call) {
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
	return run_for(sec, [&]() { ParaEnc(buf.data(), skeys.data(), K, N); }, buf.data());
}

static void bench(double sec) {
	PRG prg;

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
