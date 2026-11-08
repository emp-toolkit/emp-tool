// crypto/hash.h — streaming hash, templated on the algorithm (HashT<opt>:
// OpenSSL SHA-256 default, vendored BLAKE3 opt-in). Throughput benchmark of
// `Hash`, the build's default backend.
//
// What's in hash.h:
//   Hash::put(p, n)                           feed n bytes
//   Hash::put_block(b, n=1)                   feed n blocks
//   Hash::digest(out, reset_after=true)       finalize (or snapshot)
//   Hash::reset()                             reinit context
//   Hash::hash_once(out, p, n)                one-shot digest
//   Hash::hash_for_block(p, n)                first 16 bytes of the digest, as block

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
	bench(sec);
	return 0;
}
