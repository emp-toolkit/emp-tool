// crypto/prp.h — pseudorandom permutation = fixed-key AES-128-ECB. Models
// f(x) = AES_K(x) as a random permutation in the random-permutation model.
// Read example() first; the rest is verification + throughput.
//
// What's in prp.h:
//   PRP()                          zero-key constructor
//   PRP(const char * key)          key from 16 raw bytes (loadu)
//   PRP(const block & key)         key from a block
//   aes_set_key(const block &)     re-key
//   permute_block(blks, n)         n-block in-place AES-ECB

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

// ---------- example ----------

static void example() {
	cout << "=== example ===\n";

	// (1) Zero-key PRP (default): models the fixed-key cipher in the JustGarble
	// permutation model.
	PRP prp0;
	alignas(16) block buf = makeBlock(0xCAFEBABEULL, 0xDEADBEEFULL);
	prp0.permute_block(&buf, 1);
	cout << "  PRP_0(0xcafe...beef)         = " << buf << "\n";

	// (2) Keyed PRP via block constructor.
	block key = makeBlock(0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);
	PRP prp_k(key);
	alignas(16) block buf2[2] = {makeBlock(0, 1), makeBlock(0, 2)};
	prp_k.permute_block(buf2, 2);
	cout << "  PRP_k(1)                     = " << buf2[0] << "\n";
	cout << "  PRP_k(2)                     = " << buf2[1] << "\n";

	// (3) Re-key in place.
	prp_k.aes_set_key(zero_block);
	alignas(16) block buf3 = makeBlock(0xCAFEBABEULL, 0xDEADBEEFULL);
	prp_k.permute_block(&buf3, 1);
	cout << "  re-keyed to zero, PRP(...)   = " << buf3 << "\n";
}

// ---------- correctness ----------

static bool blocks_eq(block a, block b) {
	__m128i d = _mm_xor_si128(a, b);
	return _mm_testz_si128(d, d);
}

static bool check_against_openssl(int trials = 32) {
	// permute_block chunks the input into 64-block batches internally. Sizes
	// must straddle that boundary — 1 (single block), 64 (exactly one chunk),
	// 65 (one chunk + one block), 128 (two full chunks), 257 (four chunks +
	// one block) — so a regression that fails to advance the data pointer
	// between chunks surfaces as a mismatch on the second chunk onwards.
	const int sizes[] = {1, 17, 63, 64, 65, 128, 257};
	PRG prg;
	for (int blks_per : sizes) {
		for (int t = 0; t < trials; ++t) {
			uint8_t kb[16];
			prg.random_data_unaligned(kb, 16);
			block k = bytes_to_block(kb);

			alignas(16) vector<block> in(blks_per);
			prg.random_block(in.data(), blks_per);
			alignas(16) vector<block> out = in;

			PRP prp(k);
			prp.permute_block(out.data(), blks_per);

			vector<uint8_t> ref(16 * blks_per);
			openssl_aes128_ecb(kb, reinterpret_cast<const uint8_t *>(in.data()),
			                   ref.data(), blks_per);

			if (memcmp(out.data(), ref.data(), ref.size()) != 0) {
				cout << "  FAIL permute_block vs OpenSSL  blks=" << blks_per
				     << " trial=" << t << "\n";
				return false;
			}
		}
	}
	cout << "  [permute_block vs OpenSSL AES-128-ECB]  OK   trials=" << trials
	     << " sizes={1,17,63,64,65,128,257}\n";
	return true;
}

static bool check_constructors_agree() {
	uint8_t kb[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	block kblk = bytes_to_block(kb);
	PRP via_block(kblk);
	PRP via_bytes(reinterpret_cast<const char *>(kb));

	alignas(16) block in[3] = {makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3)};
	alignas(16) block a[3], b[3];
	memcpy(a, in, sizeof(in));
	memcpy(b, in, sizeof(in));
	via_block.permute_block(a, 3);
	via_bytes.permute_block(b, 3);
	bool ok = memcmp(a, b, sizeof(in)) == 0;
	cout << "  [PRP(block) = PRP(const char*)]         " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_zero_key_matches_default_ctor() {
	PRP a;
	PRP b(zero_block);
	alignas(16) block in[4] = {makeBlock(1, 0), makeBlock(2, 0),
	                            makeBlock(3, 0), makeBlock(4, 0)};
	alignas(16) block x[4], y[4];
	memcpy(x, in, sizeof(in));
	memcpy(y, in, sizeof(in));
	a.permute_block(x, 4);
	b.permute_block(y, 4);
	bool ok = memcmp(x, y, sizeof(in)) == 0;
	cout << "  [default PRP() = PRP(zero_block)]       " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_re_key_idempotent() {
	// permute_block result must depend on the current key, not stale state.
	block k1 = makeBlock(1, 2), k2 = makeBlock(3, 4);
	PRP a(k1), b(k2);
	alignas(16) block x = makeBlock(7, 8), y = x, z = x;
	a.aes_set_key(k2);
	a.permute_block(&x, 1);
	b.permute_block(&y, 1);
	bool ok = blocks_eq(x, y);
	(void)z;
	cout << "  [aes_set_key swaps key correctly]       " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_against_openssl();
	ok &= check_constructors_agree();
	ok &= check_zero_key_matches_default_ctor();
	ok &= check_re_key_idempotent();
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
	PRP prp;

	cout << "=== permute_block (sweep N blocks) ===\n";
	for (int n : {1, 4, 8, 16, 64, 256, 1024, 4096, 16384}) {
		alignas(16) vector<block> buf(n);
		prg.random_block(buf.data(), n);
		double calls = run_for(sec, [&]() { prp.permute_block(buf.data(), n); }, buf.data());
		ostringstream lbl; lbl << "permute_block(N=" << n << ")";
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
