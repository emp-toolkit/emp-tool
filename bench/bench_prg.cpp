// crypto/prg.h — AES-CTR pseudorandom generator. Throughput benchmark.
//
// What's in prg.h:
//   PRG(seed=nullptr, id=0)                  fixed-key seed (or urandom)
//   reseed(const block * seed, id=0)         re-key + reset counter
//   random_block(block *, n=1)               n blocks of CTR-mode AES output
//   random_data(void *, nbytes)              16-byte aligned dest
//   random_data_unaligned(void *, nbytes)    arbitrary alignment
//   random_bool(bool *, length)              packed bits expanded to 1 byte/bool
//   operator()                               UniformRandomBitGenerator (std)

#include "emp-tool/emp-tool.h"

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

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

	cout << "=== random_block (sweep N blocks) ===\n";
	for (int n : {1, 8, 64, 256, 1024, 4096, 16384}) {
		vector<block> buf(n);
		double calls = run_for(sec, [&]() { prg.random_block(buf.data(), n); }, buf.data());
		ostringstream lbl; lbl << "random_block(N=" << n << ")";
		print_vec(lbl.str(), calls, (size_t)n * 16);
	}

	cout << "\n=== random_data (16-byte-aligned dest, sweep nbytes) ===\n";
	for (int nb : {1, 15, 16, 17, 31, 32, 33, 64, 256, 1024, 4096,
	               16384, 65536}) {
		vector<block> buf((nb + 15) / 16);
		double calls = run_for(sec, [&]() {
			prg.random_data(buf.data(), nb);
		}, buf.data());
		ostringstream lbl; lbl << "random_data(N=" << nb << ")";
		print_vec(lbl.str(), calls, (size_t)nb);
	}

	cout << "\n=== random_data_unaligned (offset=1, sweep nbytes) ===\n";
	for (int nb : {1, 15, 16, 17, 31, 32, 33, 64, 256, 1024, 4096,
	               16384, 65536}) {
		vector<block> buf((nb + 30) / 16);
		uint8_t *unaligned = reinterpret_cast<uint8_t *>(buf.data()) + 1;
		double calls = run_for(sec, [&]() {
			prg.random_data_unaligned(unaligned, nb);
		}, unaligned);
		ostringstream lbl; lbl << "random_data_unaligned(N=" << nb << ")";
		print_vec(lbl.str(), calls, (size_t)nb);
	}

	cout << "\n=== random_data_unaligned (N=4096, offset sweep) ===\n";
	for (int offset : {1, 7, 15}) {
		constexpr int nb = 4096;
		vector<block> buf((nb + 30) / 16);
		uint8_t *unaligned = reinterpret_cast<uint8_t *>(buf.data()) + offset;
		double calls = run_for(sec, [&]() {
			prg.random_data_unaligned(unaligned, nb);
		}, unaligned);
		ostringstream lbl;
		lbl << "random_data_unaligned(offset=" << offset << ")";
		print_vec(lbl.str(), calls, (size_t)nb);
	}

	cout << "\n=== random_bool (sweep nbools; reports bool-bytes/sec) ===\n";
	for (int nb : {32, 128, 512, 2048, 8192, 32768, 131072}) {
		vector<uint8_t> buf(nb);
		double calls = run_for(sec, [&]() {
			prg.random_bool(buf.data(), nb);
		}, buf.data());
		ostringstream lbl; lbl << "random_bool(N=" << nb << ")";
		print_vec(lbl.str(), calls, (size_t)nb);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
