// crypto/prg.h — AES-CTR pseudorandom generator. Read example() first; the
// rest is verification.
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

#include <openssl/evp.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

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

static bool openssl_aes128_ecb(const block& key, const block *in,
                               block *out, int nblocks) {
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (ctx == nullptr) return false;
	int outl = 0, finl = 0;
	bool ok = EVP_EncryptInit_ex(
		ctx, EVP_aes_128_ecb(), nullptr,
		reinterpret_cast<const unsigned char *>(&key), nullptr) == 1;
	ok = ok && EVP_CIPHER_CTX_set_padding(ctx, 0) == 1;
	ok = ok && EVP_EncryptUpdate(
		ctx, reinterpret_cast<unsigned char *>(out), &outl,
		reinterpret_cast<const unsigned char *>(in),
		nblocks * static_cast<int>(sizeof(block))) == 1;
	ok = ok && EVP_EncryptFinal_ex(
		ctx, reinterpret_cast<unsigned char *>(out) + outl, &finl) == 1;
	EVP_CIPHER_CTX_free(ctx);
	return ok && outl + finl == nblocks * static_cast<int>(sizeof(block));
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

static bool check_reseed_invalidates_word_buffer() {
	block old_seed = makeBlock(19, 23);
	block new_seed = makeBlock(29, 31);
	constexpr uint64_t id = 37;
	PRG reused(&old_seed);
	(void)reused();
	reused.reseed(&new_seed, id);
	PRG fresh(&new_seed, id);
	for (int i = 0; i < 64; ++i)
		if (reused() != fresh()) return false;
	return true;
}

static bool check_counter_continuity() {
	// Streaming N blocks at once must match N back-to-back single-block calls.
	block seed = makeBlock(1, 2);
	for (int n : {1, 7, 16, 17, 64, 65, 257}) {
		PRG a(&seed), b(&seed);
		vector<block> bulk(n), one_at_a_time(n);
		a.random_block(bulk.data(), n);
		for (int i = 0; i < n; ++i) b.random_block(&one_at_a_time[i], 1);
		if (memcmp(bulk.data(), one_at_a_time.data(), n * sizeof(block)) != 0)
			return false;
	}
	return true;
}

static bool check_counter_crosses_signed_boundary() {
	block seed = makeBlock(9, 10);
	constexpr int id = 37;
	constexpr uint64_t base = (uint64_t{1} << 63) - 16;
	PRG p(&seed, id);
	p.seek(base);

	block got[32];
	p.random_block(got, 32);

	block counters[32], expected[32];
	for (uint64_t i = 0; i < 32; ++i)
		counters[i] = makeBlock(0, base + i);
	const block effective_key = seed ^ makeBlock(0, id);
	return blocks_eq(p.seed(), effective_key) &&
	       openssl_aes128_ecb(effective_key, counters, expected, 32) &&
	       cmpBlock(got, expected, 32) && p.position() == base + 32;
}

static bool check_fork_at_and_seek() {
	// fork_at(c) and a fresh PRG seeked to c must both reproduce the bulk
	// stream from block c onward (one AES key, same CTR counter => same output).
	block seed = makeBlock(0xABCDEF, 0x12345);
	const int total = 200;
	PRG bulk(&seed);
	vector<block> ref(total);
	bulk.random_block(ref.data(), total);

	for (uint64_t c : {uint64_t{0}, uint64_t{1}, uint64_t{17}, uint64_t{64}, uint64_t{129}}) {
		const int avail = total - (int)c;
		// (a) fork_at(c) from a PRG at an arbitrary position.
		PRG src(&seed);
		block warm[5];
		src.random_block(warm, 5);              // advance src to a non-zero position
		PRG forked = src.fork_at(c);
		vector<block> from_fork(avail);
		forked.random_block(from_fork.data(), avail);
		if (memcmp(from_fork.data(), ref.data() + c, avail * sizeof(block)) != 0)
			return false;
		// (b) fresh PRG(seed).seek(c) matches the same slice.
		PRG fresh(&seed);
		fresh.seek(c);
		vector<block> from_seek(avail);
		fresh.random_block(from_seek.data(), avail);
		if (memcmp(from_seek.data(), ref.data() + c, avail * sizeof(block)) != 0)
			return false;
		// (c) accessors report the seeked position; seed() round-trips.
		if (fresh.position() != c + (uint64_t)avail) return false;
		if (!blocks_eq(fresh.seed(), forked.seed())) return false;
	}
	return true;
}

static bool check_random_data_matches_block() {
	// random_data(nbytes) where nbytes % 16 == 0 must agree with random_block.
	block seed = makeBlock(3, 4);
	for (int nblocks : {1, 4, 64, 1023}) {
		PRG a(&seed), b(&seed);
		vector<block> via_block(nblocks);
		vector<uint8_t> via_data(nblocks * 16);
		a.random_block(via_block.data(), nblocks);
		b.random_data(via_data.data(), nblocks * 16);
		if (memcmp(via_block.data(), via_data.data(), nblocks * 16) != 0)
			return false;
	}
	return true;
}

static bool check_random_data_unaligned() {
	block seed = makeBlock(123, 456);
	alignas(block) uint8_t ref_bytes[320];
	{
		PRG p(&seed);
		p.random_data(ref_bytes, sizeof(ref_bytes));
	}
	alignas(block) uint8_t buf[352];
	for (int offset = 1; offset < 16; ++offset) {
		for (int len = 1; len < 320; ++len) {
			memset(buf, 0xA5, sizeof(buf));
			uint8_t *unaligned = buf + offset;
			PRG p(&seed);
			p.random_data_unaligned(unaligned, len);
			if (memcmp(unaligned, ref_bytes, len) != 0) return false;
			for (int i = 0; i < offset; ++i)
				if (buf[i] != 0xA5) return false;
			for (size_t i = offset + len; i < sizeof(buf); ++i)
				if (buf[i] != 0xA5) return false;
		}
	}
	return true;
}

static bool check_random_data_unaligned_counter() {
	block seed = makeBlock(789, 101112);
	alignas(16) uint8_t aligned[320];
	alignas(block) uint8_t unaligned_storage[336];
	for (uint64_t start : {uint64_t{0}, ~uint64_t{7}}) {
		for (int len : {1, 7, 15, 16, 17, 31, 32, 33, 63, 64,
		                127, 128, 129, 255, 256, 257}) {
			PRG a(&seed), b(&seed);
			a.seek(start);
			b.seek(start);
			uint8_t *unaligned = unaligned_storage + 1;
			a.random_data(aligned, len);
			b.random_data_unaligned(unaligned, len);
			if (memcmp(aligned, unaligned, len) != 0) return false;

			block next_a, next_b;
			a.random_block(&next_a, 1);
			b.random_block(&next_b, 1);
			if (!blocks_eq(next_a, next_b)) return false;
		}
	}
	return true;
}

static bool check_random_bool_is_0_or_1() {
	PRG p;
	for (int len : {1, 7, 32, 128, 1023, 4096}) {
		vector<uint8_t> buf(len);
		p.random_bool(buf.data(), len);
		for (int i = 0; i < len; ++i)
			if (buf[i] > 1) return false;
	}
	return true;
}

static bool check_random_bool_representations_match() {
	block seed = makeBlock(0x1234, 0x5678);
	PRG empty(&seed);
	uint8_t *no_bytes = nullptr;
	empty.random_bool(nullptr, 0);
	empty.random_bool(no_bytes, 0);
	if (empty.position() != 0) return false;
	bool bools[4097];
	for (int len : {0, 1, 7, 31, 32, 33, 127, 128, 129, 2048, 2049, 4097}) {
		PRG a(&seed), b(&seed);
		vector<uint8_t> bytes(len);
		a.random_bool(bools, len);
		b.random_bool(bytes.data(), len);
		for (int i = 0; i < len; ++i)
			if (bytes[i] > 1 || (bytes[i] != 0) != bools[i]) return false;
		if (a.position() != b.position()) return false;
		block next_a, next_b;
		a.random_block(&next_a, 1);
		b.random_block(&next_b, 1);
		if (!blocks_eq(next_a, next_b)) return false;
	}
	return true;
}

static bool check_random_bool_distribution() {
	// Weak distribution check: mean bit ≈ 0.5 over a large sample.
	PRG p;
	const int N = 1 << 18;
	vector<uint8_t> buf(N);
	p.random_bool(buf.data(), N);
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

static bool check_negative_lengths_rejected() {
	PRG p;
	alignas(block) block b = zero_block;
	bool bit = false;
	return dies([&] { p.random_data(&b, -1); })
	    && dies([&] { p.random_data_unaligned(&b, -1); })
	    && dies([&] { p.random_bool(&bit, -1); })
	    && dies([&] { p.random_block(&b, -1); });
}

static bool check_zero_lengths_noop() {
	block seed = makeBlock(13, 14);
	PRG p(&seed);
	p.seek(123);
	p.random_data(nullptr, 0);
	p.random_data_unaligned(nullptr, 0);
	p.random_bool(nullptr, 0);
	p.random_block(nullptr, 0);
	return p.position() == 123;
}

static bool check_aligned_apis_reject_unaligned_data() {
	PRG p;
	alignas(block) uint8_t storage[sizeof(block) + 1] = {};
	void *unaligned = storage + 1;
	return dies([&] { p.random_data(unaligned, 1); })
	    && dies([&] { p.random_block(reinterpret_cast<block *>(unaligned), 1); });
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	struct Case { const char *name; bool (*fn)(); };
	Case cases[] = {
		{"seed determinism",                    check_seed_determinism},
		{"reseed resets counter",               check_reseed_resets_counter},
		{"reseed invalidates buffered words",    check_reseed_invalidates_word_buffer},
		{"counter continuity (bulk = singles)", check_counter_continuity},
		{"counter crosses signed boundary",      check_counter_crosses_signed_boundary},
		{"fork_at / seek reproduce bulk slice",  check_fork_at_and_seek},
		{"random_data == random_block (16B)",   check_random_data_matches_block},
		{"random_data_unaligned vs aligned ref", check_random_data_unaligned},
		{"random_data_unaligned counter",       check_random_data_unaligned_counter},
		{"random_bool ∈ {0,1}",                 check_random_bool_is_0_or_1},
		{"random_bool bool/byte parity",         check_random_bool_representations_match},
		{"random_bool mean ~ 0.5",              check_random_bool_distribution},
		{"UniformRandomBitGenerator interface", check_uniform_engine},
		{"negative lengths rejected",           check_negative_lengths_rejected},
		{"zero lengths are no-ops",              check_zero_lengths_noop},
		{"aligned APIs reject unaligned data",  check_aligned_apis_reject_unaligned_data},
	};
	bool all = true;
	for (auto &c : cases) {
		bool ok = c.fn();
		all &= ok;
		cout << "  " << left << setw(40) << c.name << (ok ? "OK" : "FAIL") << "\n";
	}
	return all;
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
