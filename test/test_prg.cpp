// crypto/prg.h — AES-CTR pseudorandom generator. Read example() first; the
// rest is verification + throughput.
//
// What's in prg.h:
//   PRG(seed=nullptr, id=0)                  fixed-key seed (or rdseed/urandom)
//   reseed(const block * seed, id=0)         re-key + reset counter
//   random_block(block *, n=1)               n blocks of CTR-mode AES output
//   random_data(void *, nbytes)              16-byte aligned dest
//   random_data_unaligned(void *, nbytes)    arbitrary alignment
//   random_bool(bool *, length)              packed bits expanded to 1 byte/bool
//   operator()                               UniformRandomBitGenerator (std)

#include "emp-tool/emp-tool.h"

#include <chrono>
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

// ---------- example ----------

static void example() {
	cout << "=== example ===\n";

	// (1) Seeded PRG: deterministic output across runs.
	block seed = makeBlock(0xCAFEBABEULL, 0xDEADBEEFULL);
	PRG prg(&seed);
	block buf[2];
	prg.random_block(buf, 2);
	cout << "  random_block[0]    = " << buf[0] << "\n";
	cout << "  random_block[1]    = " << buf[1] << "\n";

	// (2) random_data for arbitrary sizes (16-byte aligned dest).
	prg.reseed(&seed);
	alignas(16) uint8_t bytes[40] = {0};
	prg.random_data(bytes, 40);
	cout << "  random_data(40)[..7] = " << hex << setfill('0');
	for (int i = 0; i < 8; ++i) cout << setw(2) << (int)bytes[i];
	cout << dec << setfill(' ') << "\n";

	// (3) random_bool unpacks 8 bools per random byte (1 random bit per bool).
	prg.reseed(&seed);
	bool bools[16];
	prg.random_bool(bools, 16);
	cout << "  random_bool(16)    = ";
	for (int i = 0; i < 16; ++i) cout << (int)bools[i];
	cout << "\n";

	// (4) PRG as UniformRandomBitGenerator: usable with std::distribution.
	prg.reseed(&seed);
	std::normal_distribution<> d{0.0, 1.0};
	cout << "  N(0,1) sample      = " << fixed << setprecision(4) << d(prg) << "\n";
}

// ---------- correctness ----------

static bool blocks_eq(block a, block b) {
	__m128i d = _mm_xor_si128(a, b);
	return _mm_testz_si128(d, d);
}

static bool check_seed_determinism() {
	block seed = makeBlock(7, 11);
	PRG a(&seed), b(&seed);
	for (int t = 0; t < 16; ++t) {
		block xa, xb;
		a.random_block(&xa, 1);
		b.random_block(&xb, 1);
		if (!blocks_eq(xa, xb)) return false;
	}
	return true;
}

static bool check_reseed_resets_counter() {
	block seed = makeBlock(7, 11);
	PRG p(&seed);
	block first[4], second[4];
	p.random_block(first, 4);
	p.reseed(&seed);
	p.random_block(second, 4);
	return memcmp(first, second, sizeof(first)) == 0;
}

static bool check_counter_continuity() {
	// Streaming N blocks at once must match N back-to-back single-block calls.
	block seed = makeBlock(1, 2);
	for (int n : {1, 7, 16, 17, 257}) {
		PRG a(&seed), b(&seed);
		vector<block> bulk(n), one_at_a_time(n);
		a.random_block(bulk.data(), n);
		for (int i = 0; i < n; ++i) b.random_block(&one_at_a_time[i], 1);
		if (memcmp(bulk.data(), one_at_a_time.data(), n * sizeof(block)) != 0)
			return false;
	}
	return true;
}

static bool check_random_data_matches_block() {
	// random_data(nbytes) where nbytes % 16 == 0 must agree with random_block.
	block seed = makeBlock(3, 4);
	for (int nblocks : {1, 4, 64, 1023}) {
		PRG a(&seed), b(&seed);
		alignas(16) vector<block> via_block(nblocks);
		alignas(16) vector<uint8_t> via_data(nblocks * 16);
		a.random_block(via_block.data(), nblocks);
		b.random_data(via_data.data(), nblocks * 16);
		if (memcmp(via_block.data(), via_data.data(), nblocks * 16) != 0)
			return false;
	}
	return true;
}

static bool check_random_data_unaligned() {
	block seed = makeBlock(123, 456);
	block ref[4];
	uint8_t *ref_bytes = (uint8_t *)ref;
	{
		PRG p(&seed);
		p.random_data_unaligned(ref, 4 * sizeof(block));
	}
	uint8_t buf[64];
	for (int offset = 1; offset < 16; ++offset) {
		for (int len = 1; len < 64 - offset; ++len) {
			uint8_t *unaligned = buf + offset;
			PRG p(&seed);
			p.random_data_unaligned(unaligned, len);
			if (memcmp(unaligned, ref_bytes, len) != 0) return false;
		}
	}
	return true;
}

static bool check_random_bool_is_0_or_1() {
	PRG p;
	for (int len : {1, 7, 32, 128, 1023, 4096}) {
		vector<uint8_t> buf(len);
		// random_bool writes via bool*; 1 byte per bool, value 0 or 1.
		p.random_bool(reinterpret_cast<bool *>(buf.data()), len);
		for (int i = 0; i < len; ++i)
			if (buf[i] > 1) return false;
	}
	return true;
}

static bool check_random_bool_distribution() {
	// Weak distribution check: mean bit ≈ 0.5 over a large sample.
	PRG p;
	const int N = 1 << 18;
	vector<uint8_t> buf(N);
	p.random_bool(reinterpret_cast<bool *>(buf.data()), N);
	int64_t ones = 0;
	for (int i = 0; i < N; ++i) ones += buf[i];
	double mean = (double)ones / N;
	// 5σ around 0.5 for N=2^18 is ~0.0049; allow 0.01.
	return mean > 0.49 && mean < 0.51;
}

static bool check_uniform_engine() {
	PRG p;
	uint64_t a = p(), b = p();
	// Two consecutive 64-bit draws should not collide for a working CSPRNG.
	if (a == b) return false;
	// min/max statics.
	if (PRG::min() != 0) return false;
	if (PRG::max() != 0xFFFFFFFFFFFFFFFFULL) return false;
	return true;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	struct Case { const char *name; bool (*fn)(); };
	Case cases[] = {
		{"seed determinism",                    check_seed_determinism},
		{"reseed resets counter",               check_reseed_resets_counter},
		{"counter continuity (bulk = singles)", check_counter_continuity},
		{"random_data == random_block (16B)",   check_random_data_matches_block},
		{"random_data_unaligned vs aligned ref", check_random_data_unaligned},
		{"random_bool ∈ {0,1}",                 check_random_bool_is_0_or_1},
		{"random_bool mean ~ 0.5",              check_random_bool_distribution},
		{"UniformRandomBitGenerator interface", check_uniform_engine},
	};
	bool all = true;
	for (auto &c : cases) {
		bool ok = c.fn();
		all &= ok;
		cout << "  " << left << setw(40) << c.name << (ok ? "OK" : "FAIL") << "\n";
	}
	return all;
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

static void bench(double sec) {
	PRG prg;

	cout << "=== random_block (sweep N blocks) ===\n";
	for (int n : {1, 8, 64, 256, 1024, 4096, 16384}) {
		alignas(16) vector<block> buf(n);
		double calls = run_for(sec, [&]() { prg.random_block(buf.data(), n); }, buf.data());
		ostringstream lbl; lbl << "random_block(N=" << n << ")";
		print_vec(lbl.str(), calls, (size_t)n * 16);
	}

	cout << "\n=== random_data (16-byte-aligned dest, sweep nbytes) ===\n";
	for (int nb : {16, 64, 256, 1024, 4096, 16384, 65536}) {
		alignas(16) vector<uint8_t> buf((nb + 15) & ~15);
		double calls = run_for(sec, [&]() { prg.random_data(buf.data(), nb); }, buf.data());
		ostringstream lbl; lbl << "random_data(N=" << nb << ")";
		print_vec(lbl.str(), calls, (size_t)nb);
	}

	cout << "\n=== random_data_unaligned (sweep nbytes) ===\n";
	for (int nb : {16, 64, 256, 1024, 4096, 16384}) {
		vector<uint8_t> buf(nb + 16);
		uint8_t *unaligned = buf.data() + 1;  // forced misalign
		double calls = run_for(sec, [&]() {
			prg.random_data_unaligned(unaligned, nb);
		}, unaligned);
		ostringstream lbl; lbl << "random_data_unaligned(N=" << nb << ")";
		print_vec(lbl.str(), calls, (size_t)nb);
	}

	cout << "\n=== random_bool (sweep nbools; reports bool-bytes/sec) ===\n";
	for (int nb : {32, 128, 512, 2048, 8192, 32768, 131072}) {
		vector<uint8_t> buf(nb);
		double calls = run_for(sec, [&]() {
			prg.random_bool(reinterpret_cast<bool *>(buf.data()), nb);
		}, buf.data());
		ostringstream lbl; lbl << "random_bool(N=" << nb << ")";
		print_vec(lbl.str(), calls, (size_t)nb);
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
