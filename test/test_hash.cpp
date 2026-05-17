// crypto/hash.h — SHA-256 wrapper around OpenSSL EVP plus a fixed-size KDF
// for elliptic-curve points. Read example() first; the rest is verification +
// throughput.
//
// What's in hash.h:
//   Hash::put(p, n)                           feed n bytes
//   Hash::put_block(b, n=1)                   feed n blocks
//   Hash::digest(out, reset_after=true)       finalize (or snapshot)
//   Hash::reset()                             reinit context
//   Hash::hash_once(out, p, n)                one-shot SHA-256
//   Hash::hash_for_block(p, n)                first 16 bytes of SHA-256, as block
//   Hash::KDF(point, id=1)                    KDF for EC points

#include "emp-tool/emp-tool.h"

#include <openssl/sha.h>

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

static string hex_n(const void *p, int n) {
	const uint8_t *b = (const uint8_t *)p;
	ostringstream os;
	os << hex << setfill('0');
	for (int i = 0; i < n; ++i) os << setw(2) << (int)b[i];
	return os.str();
}

// ---------- example ----------

static void example() {
	cout << "=== example ===\n";

	// (1) Streaming put + digest.
	Hash h;
	h.put("abc", 3);
	uint8_t out[Hash::DIGEST_SIZE];
	h.digest(out);
	cout << "  SHA-256(\"abc\") =\n    " << hex_n(out, Hash::DIGEST_SIZE) << "\n";

	// (2) put_block: feed a block-typed buffer.
	block b = makeBlock(0xDEADBEEFULL, 0xCAFEBABEULL);
	h.put_block(&b);
	h.digest(out);
	cout << "  SHA-256(b)     =\n    " << hex_n(out, Hash::DIGEST_SIZE) << "\n";

	// (3) Snapshot mode (reset_after = false): finalize a copy without
	// disturbing the running transcript. Used for streaming Fiat-Shamir.
	h.put("hello", 5);
	uint8_t snap1[Hash::DIGEST_SIZE], snap2[Hash::DIGEST_SIZE];
	h.digest(snap1, /*reset_after=*/false);
	h.put(" world", 6);
	h.digest(snap2);  // resets
	cout << "  snap1=" << hex_n(snap1, 8) << "...  snap2=" << hex_n(snap2, 8) << "...\n";

	// (4) One-shot helpers.
	uint8_t one[Hash::DIGEST_SIZE];
	Hash::hash_once(one, "abc", 3);
	block hb = Hash::hash_for_block("abc", 3);
	cout << "  hash_once(\"abc\")[0..8]   = " << hex_n(one, 8) << "\n";
	cout << "  hash_for_block(\"abc\")    = " << hb << "\n";
}

// ---------- correctness ----------

static bool check_known_vectors() {
	// FIPS 180-4 examples.
	struct V { const char *msg; size_t n; const char *hex; };
	V vs[] = {
		{"abc", 3,
		 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
		{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
		 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
		{"", 0,
		 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
	};
	bool all_ok = true;
	for (auto &v : vs) {
		uint8_t out[Hash::DIGEST_SIZE];
		Hash::hash_once(out, v.msg, (int)v.n);
		bool ok = hex_n(out, Hash::DIGEST_SIZE) == v.hex;
		all_ok &= ok;
	}
	cout << "  [FIPS 180-4 known answers]            " << (all_ok ? "OK" : "FAIL") << "\n";
	return all_ok;
}

static bool check_against_openssl(int trials = 64) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		int n = 1 + (t * 17) % 4096;
		vector<uint8_t> buf(n);
		prg.random_data_unaligned(buf.data(), n);

		uint8_t emp_out[Hash::DIGEST_SIZE];
		Hash::hash_once(emp_out, buf.data(), n);

		uint8_t ref[SHA256_DIGEST_LENGTH];
		SHA256(buf.data(), n, ref);

		if (memcmp(emp_out, ref, SHA256_DIGEST_LENGTH) != 0) {
			cout << "  [Hash::hash_once vs OpenSSL SHA256]  FAIL  t=" << t << " n=" << n << "\n";
			return false;
		}
	}
	cout << "  [Hash::hash_once vs OpenSSL SHA256]   OK   trials=" << trials << "\n";
	return true;
}

static bool check_streaming_equals_oneshot(int trials = 32) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		int n = 256 + (t * 31) % 8192;
		vector<uint8_t> buf(n);
		prg.random_data_unaligned(buf.data(), n);

		uint8_t one[Hash::DIGEST_SIZE], stream[Hash::DIGEST_SIZE];
		Hash::hash_once(one, buf.data(), n);

		Hash h;
		// Feed in random-sized chunks.
		int off = 0;
		while (off < n) {
			int chunk = 1 + (off * 7 + t) % 257;
			if (chunk > n - off) chunk = n - off;
			h.put(buf.data() + off, chunk);
			off += chunk;
		}
		h.digest(stream);

		if (memcmp(one, stream, Hash::DIGEST_SIZE) != 0) {
			cout << "  [streaming = one-shot]                FAIL  t=" << t << "\n";
			return false;
		}
	}
	cout << "  [streaming put = hash_once]           OK   trials=" << trials << "\n";
	return true;
}

static bool check_snapshot_does_not_disturb() {
	// digest(_, reset_after=false) must leave the transcript intact.
	Hash a, b;
	a.put("hello", 5);
	b.put("hello", 5);
	uint8_t snap[Hash::DIGEST_SIZE];
	a.digest(snap, /*reset_after=*/false);
	a.put(" world", 6);
	b.put(" world", 6);
	uint8_t da[Hash::DIGEST_SIZE], db[Hash::DIGEST_SIZE];
	a.digest(da);
	b.digest(db);
	bool ok = memcmp(da, db, Hash::DIGEST_SIZE) == 0;
	cout << "  [snapshot leaves transcript intact]   " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_known_vectors();
	ok &= check_against_openssl();
	ok &= check_streaming_equals_oneshot();
	ok &= check_snapshot_does_not_disturb();
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

static void print_vec(const string &lbl, double calls, size_t bytes_per_call) {
	double GiBps = calls * (double)bytes_per_call / (1024.0 * 1024.0 * 1024.0);
	double cy_per_byte = 3e9 / (calls * bytes_per_call);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_byte << " cy/B @3GHz\n";
}

static void print_op(const string &lbl, double calls) {
	double Mops = calls / 1e6;
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << Mops << " Mops  "
	     << setw(7) << (3e9 / calls) << " cy/op @3GHz\n";
}

static void bench(double sec) {
	PRG prg;

	cout << "=== put + digest (throughput across input size) ===\n";
	for (size_t n : {16ULL, 64ULL, 256ULL, 1024ULL, 4096ULL, 16384ULL, 65536ULL, 262144ULL}) {
		vector<uint8_t> buf(n);
		prg.random_data_unaligned(buf.data(), (int)n);
		uint8_t dig[Hash::DIGEST_SIZE];
		Hash h;
		double calls = run_for(sec, [&]() {
			h.put(buf.data(), (int)n);
			h.digest(dig);
		}, dig);
		ostringstream lbl; lbl << "put+digest(N=" << n << ")";
		print_vec(lbl.str(), calls, n);
	}

	cout << "\n=== hash_once (one-shot) ===\n";
	for (size_t n : {16ULL, 256ULL, 4096ULL, 65536ULL}) {
		vector<uint8_t> buf(n);
		prg.random_data_unaligned(buf.data(), (int)n);
		uint8_t dig[Hash::DIGEST_SIZE];
		double calls = run_for(sec, [&]() {
			Hash::hash_once(dig, buf.data(), (int)n);
		}, dig);
		ostringstream lbl; lbl << "hash_once(N=" << n << ")";
		print_vec(lbl.str(), calls, n);
	}

	cout << "\n=== hash_for_block (single-shot, chained-dep) ===\n";
	{
		block x; prg.random_block(&x, 1);
		double calls = run_for(sec, [&]() {
			x = Hash::hash_for_block(&x, sizeof(block));
		}, &x);
		print_op("hash_for_block(16B)", calls);
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
