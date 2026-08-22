// crypto/aes.h — AES-128 ECB primitives used as the bottom crypto layer of
// emp's PRG/PRP/CCRH chain. Read example() first; the rest is verification.
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
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

// ---------- helpers ----------

// bytes -> block: an UNALIGNED load. Kept (vs *(block*)b) because one caller
// feeds key bytes from random_data_unaligned() — deliberately misaligned to
// exercise the unaligned path — so the source cannot be assumed 16-aligned.
// The reverse (block -> bytes) needs no helper: a block is 16 contiguous bytes,
// so reinterpret_cast<const uint8_t*>(&blk) is a valid byte view.
static block bytes_to_block(const uint8_t b[16]) {
	return _mm_loadu_si128(reinterpret_cast<const __m128i *>(b));
}

template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) dup2(devnull, 2);
		f();
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
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
	block tile[4] = {msg, msg, msg, msg};
	AES_ecb_encrypt_blks<4>(tile, &ek);
	cout << "  AES_ecb_encrypt_blks<4>[0]   = " << tile[0] << "\n";

	// (3) Multi-key schedule: 2 keys → 2 round-key sets, interleaved.
	block user_keys[2] = {key, key ^ makeBlock(0, 1)};
	AES_KEY eks[2];
	AES_opt_key_schedule<2>(user_keys, eks);
	block buf[2] = {msg, msg};
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

	AES_KEY ek;
	block kb = bytes_to_block(key_bytes);
	AES_set_encrypt_key(kb, &ek);
	block blk = bytes_to_block(pt_bytes);
	AES_ecb_encrypt_blks(&blk, 1, &ek);
	const uint8_t *got = reinterpret_cast<const uint8_t *>(&blk);  // block is 16 contiguous bytes

	bool ok = memcmp(got, ct_ref, 16) == 0;
	cout << "  [NIST FIPS-197 C.1 vector]              " << (ok ? "OK" : "FAIL")
	     << "  got=" << to_hex(got, 16) << "\n";
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

		AES_KEY ek;
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
	block keys[K];
	block in[K * N];
	prg.random_block(keys, K);
	prg.random_block(in, K * N);

	block emp_out[K * N];
	memcpy(emp_out, in, sizeof(in));

	AES_KEY skeys[K];
	AES_opt_key_schedule<K>(keys, skeys);
	ParaEnc<K, N>(emp_out, skeys);

	// Reference: per-key OpenSSL AES-128-ECB. Validates AES_opt_key_schedule
	// and ParaEnc together against an external implementation.
	vector<uint8_t> ref_bytes(16 * K * N);
	for (int k = 0; k < K; ++k) {
		openssl_aes128_ecb(reinterpret_cast<const uint8_t *>(&keys[k]),
		                   reinterpret_cast<const uint8_t *>(in + k * N),
		                   ref_bytes.data() + 16 * k * N, N);
	}

	bool ok = memcmp(emp_out, ref_bytes.data(), 16 * K * N) == 0;
	ostringstream os;
	os << "  [ParaEnc<" << K << "," << N << "> vs OpenSSL]";
	cout << left << setw(42) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// Two-pointer ParaEnc<K,N>(dst, src, keys): byte-identical to the in-place
// form. Validates that the new overload doesn't perturb output and that
// callers can rely on dst[i] == in-place-encrypt(src)[i].
template <int K, int N>
static bool check_paraenc_template_out_of_place() {
	PRG prg;
	block keys[K];
	block in[K * N];
	prg.random_block(keys, K);
	prg.random_block(in, K * N);

	AES_KEY skeys[K];
	AES_opt_key_schedule<K>(keys, skeys);

	block ref[K * N];
	memcpy(ref, in, sizeof(in));
	ParaEnc<K, N>(ref, skeys);                // existing one-pointer baseline

	block oop[K * N];
	ParaEnc<K, N>(oop, in, skeys);            // new two-pointer overload

	bool ok = cmpBlock(ref, oop, K * N);
	ostringstream os;
	os << "  [ParaEnc<" << K << "," << N << ">(dst,src) vs in-place]";
	cout << left << setw(42) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_paraenc_runtime_out_of_place(int K, int N, int trials = 4) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		vector<block> keys(K);
		prg.random_block(keys.data(), K);
		vector<block> in(K * N);
		prg.random_block(in.data(), K * N);

		vector<AES_KEY> skeys(K);
		for (int k = 0; k < K; ++k)
			AES_set_encrypt_key(keys[k], &skeys[k]);

		vector<block> ref = in;
		ParaEnc(ref.data(), skeys.data(), K, N);

		vector<block> oop(K * N);
		ParaEnc(oop.data(), in.data(), skeys.data(), K, N);

		if (!cmpBlock(ref.data(), oop.data(), K * N)) {
			cout << "  [ParaEnc(K=" << K << ",N=" << N << ")(dst,src) vs in-place]  FAIL  t=" << t << "\n";
			return false;
		}
	}
	ostringstream os;
	os << "  [ParaEnc(K=" << K << ",N=" << N << ")(dst,src) vs in-place]";
	cout << left << setw(42) << os.str() << "OK\n";
	return true;
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
			openssl_aes128_ecb(reinterpret_cast<const uint8_t *>(&keys[k]),
			                   reinterpret_cast<const uint8_t *>(&in[k * N]),
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

static bool check_paraenc_runtime_contracts() {
	block blocks[2] = {makeBlock(1, 2), makeBlock(3, 4)};
	const block before[2] = {blocks[0], blocks[1]};
	AES_KEY key;
	AES_set_encrypt_key(makeBlock(5, 6), &key);

	if (!dies([&] { ParaEnc(blocks, &key, -1, 1); }) ||
	    !dies([&] { ParaEnc(blocks, &key, 1, -1); }) ||
	    !dies([&] { AES_ecb_encrypt_blks(blocks, -1, &key); }) ||
	    !dies([&] {
		    ParaEnc(blocks, &key, 2, std::numeric_limits<int64_t>::max());
	    }) ||
	    !dies([&] {
		    detail::ParaEnc_runtime_empty(int64_t{1} << 62, 4);
	    }))
		return false;

	const int64_t max = std::numeric_limits<int64_t>::max();
	if (detail::ParaEnc_runtime_empty(max, 1) ||
	    detail::ParaEnc_runtime_empty(2, max / 2) ||
	    !detail::ParaEnc_runtime_empty(0, max) ||
	    !detail::ParaEnc_runtime_empty(max, 0))
		return false;

	ParaEnc(static_cast<block *>(nullptr), static_cast<const AES_KEY *>(nullptr), 0, 7);
	ParaEnc(static_cast<block *>(nullptr), static_cast<const AES_KEY *>(nullptr), 7, 0);
	ParaEnc(static_cast<block *>(nullptr), static_cast<const block *>(nullptr),
	        static_cast<const AES_KEY *>(nullptr), 0, 7);
	ParaEnc(static_cast<block *>(nullptr), static_cast<const block *>(nullptr),
	        static_cast<const AES_KEY *>(nullptr), 7, 0);
	AES_ecb_encrypt_blks(static_cast<block *>(nullptr), 0,
	                     static_cast<const AES_KEY *>(nullptr));
	ParaEnc<0, 1>(static_cast<block *>(nullptr), static_cast<const AES_KEY *>(nullptr));
	ParaEnc<1, 0>(static_cast<block *>(nullptr), static_cast<const AES_KEY *>(nullptr));
	AES_opt_key_schedule<0>(static_cast<const block *>(nullptr),
	                        static_cast<AES_KEY *>(nullptr));
	aes_ctr_fill_dm<0>(static_cast<block *>(nullptr), 0,
	                   static_cast<const AES_KEY *>(nullptr));
	return cmpBlock(blocks, before, 2);
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
	ok &= check_paraenc_template<1, 20>();
	ok &= check_paraenc_template<1, 32>();
	ok &= check_paraenc_runtime(1, 1);
	ok &= check_paraenc_runtime(1, 17);   // exercises 8+4+2+1 dispatcher
	ok &= check_paraenc_runtime(5, 11);
	ok &= check_paraenc_runtime_contracts();

	ok &= check_paraenc_template_out_of_place<1, 1>();
	ok &= check_paraenc_template_out_of_place<1, 2>();
	ok &= check_paraenc_template_out_of_place<1, 4>();
	ok &= check_paraenc_template_out_of_place<1, 8>();
	ok &= check_paraenc_template_out_of_place<1, 16>();
	ok &= check_paraenc_template_out_of_place<2, 1>();
	ok &= check_paraenc_template_out_of_place<2, 2>();
	ok &= check_paraenc_template_out_of_place<2, 4>();
	ok &= check_paraenc_template_out_of_place<4, 1>();
	ok &= check_paraenc_template_out_of_place<4, 4>();
	ok &= check_paraenc_template_out_of_place<1, 20>();
	ok &= check_paraenc_template_out_of_place<1, 32>();
	ok &= check_paraenc_runtime_out_of_place(1, 17);
	ok &= check_paraenc_runtime_out_of_place(5, 11);
	return ok;
}

int main(int /*argc*/, char ** /*argv*/) {
	example();
	cout << "\n";
	if (!run_correctness()) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
