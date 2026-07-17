// crypto/hash.h — streaming hash, templated on the algorithm (HashT<opt>:
// OpenSSL SHA-256 default, vendored BLAKE3 opt-in). Read example() first;
// the rest is verification. (EC-point KDF moved to crypto/ro.h,
// covered by test_ro.cpp.)
//
// What's in hash.h:
//   Hash::put(p, n)                           feed n bytes
//   Hash::put_block(b, n=1)                   feed n blocks
//   Hash::digest(out, reset_after=true)       finalize (or snapshot)
//   Hash::reset()                             reinit context
//   Hash::hash_once(out, p, n)                one-shot digest
//   Hash::hash_for_block(p, n)                first 16 bytes of the digest, as block
//
// `Hash` is an alias for the build's default backend (EMP_HASH_DEFAULT), so
// the generic checks below run against whichever algorithm the build selected;
// the known-answer checks pin HashT<opt> explicitly so each compiled-in
// algorithm is verified against its standard vectors regardless of the default.

#include "emp-tool/emp-tool.h"

#include <openssl/sha.h>

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

	// (1) Streaming put + digest.
	Hash h;
	h.put("abc", 3);
	uint8_t out[Hash::DIGEST_SIZE];
	h.digest(out);
	cout << "  Hash(\"abc\")    =\n    " << to_hex(out, Hash::DIGEST_SIZE) << "\n";

	// (2) put_block: feed a block-typed buffer.
	block b = makeBlock(0xDEADBEEFULL, 0xCAFEBABEULL);
	h.put_block(&b);
	h.digest(out);
	cout << "  Hash(b)        =\n    " << to_hex(out, Hash::DIGEST_SIZE) << "\n";

	// (3) Snapshot mode (reset_after = false): finalize a copy without
	// disturbing the running transcript. Used for streaming Fiat-Shamir.
	h.put("hello", 5);
	uint8_t snap1[Hash::DIGEST_SIZE], snap2[Hash::DIGEST_SIZE];
	h.digest(snap1, /*reset_after=*/false);
	h.put(" world", 6);
	h.digest(snap2);  // resets
	cout << "  snap1=" << to_hex(snap1, 8) << "...  snap2=" << to_hex(snap2, 8) << "...\n";

	// (4) One-shot helpers.
	uint8_t one[Hash::DIGEST_SIZE];
	Hash::hash_once(one, "abc", 3);
	block hb = Hash::hash_for_block("abc", 3);
	cout << "  hash_once(\"abc\")[0..8]   = " << to_hex(one, 8) << "\n";
	cout << "  hash_for_block(\"abc\")    = " << hb << "\n";
}

// ---------- correctness ----------

static bool check_sha256_known_vectors() {
	// FIPS 180-4 examples, pinned to the SHA-256 backend (compiled in every
	// build) so they hold even when the default Hash is another algorithm.
	using Sha256 = HashT<HashOption::sha256>;
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
		uint8_t out[Sha256::DIGEST_SIZE];
		Sha256::hash_once(out, v.msg, (int)v.n);
		bool ok = to_hex(out, Sha256::DIGEST_SIZE) == v.hex;
		all_ok &= ok;
	}
	cout << "  [SHA-256 FIPS 180-4 known answers]    " << (all_ok ? "OK" : "FAIL") << "\n";
	return all_ok;
}

static bool check_sha256_against_openssl(int trials = 64) {
	using Sha256 = HashT<HashOption::sha256>;
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		int n = 1 + (t * 17) % 4096;
		vector<uint8_t> buf(n);
		prg.random_data_unaligned(buf.data(), n);

		uint8_t emp_out[Sha256::DIGEST_SIZE];
		Sha256::hash_once(emp_out, buf.data(), n);

		uint8_t ref[SHA256_DIGEST_LENGTH];
		SHA256(buf.data(), n, ref);

		if (memcmp(emp_out, ref, SHA256_DIGEST_LENGTH) != 0) {
			cout << "  [SHA-256 hash_once vs OpenSSL]       FAIL  t=" << t << " n=" << n << "\n";
			return false;
		}
	}
	cout << "  [SHA-256 hash_once vs OpenSSL]        OK   trials=" << trials << "\n";
	return true;
}

#ifdef EMP_WITH_BLAKE3
static bool check_blake3_known_vectors() {
	// Official BLAKE3 test vectors (test_vectors/test_vectors.json in the
	// upstream repo): the input is the repeating byte sequence 0,1,...,250 and
	// the expected value is the first 32 bytes of the extended output. Lengths
	// past 1024 cross chunk boundaries, exercising the tree/parent logic and
	// the compiled SIMD kernels, not just the single-chunk path.
	using Blake3 = HashT<HashOption::blake3>;
	struct V { size_t n; const char *hex; };
	V vs[] = {
		{0,    "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262"},
		{3,    "e1be4d7a8ab5560aa4199eea339849ba8e293d55ca0a81006726d184519e647f"},
		{1024, "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af7"},
		{4096, "015094013f57a5277b59d8475c0501042c0b642e531b0a1c8f58d2163229e969"},
	};
	bool all_ok = true;
	for (auto &v : vs) {
		vector<uint8_t> buf(v.n);
		for (size_t i = 0; i < v.n; ++i)
			buf[i] = (uint8_t)(i % 251);
		uint8_t out[Blake3::DIGEST_SIZE];
		Blake3::hash_once(out, buf.data(), (int)v.n);
		bool ok = to_hex(out, Blake3::DIGEST_SIZE) == v.hex;
		all_ok &= ok;
	}
	cout << "  [BLAKE3 official known answers]       " << (all_ok ? "OK" : "FAIL") << "\n";
	return all_ok;
}
#endif

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

static bool check_invalid_lengths_rejected() {
	uint8_t out[Hash::DIGEST_SIZE];
	block b = zero_block;
	return dies([&] {
		Hash h;
		h.put(&b, -1);
	}) && dies([&] {
		Hash h;
		h.put_block(&b, -1);
	}) && dies([&] {
		Hash h;
		h.put_block(&b, std::numeric_limits<int64_t>::max());
	}) && dies([&] {
		Hash::hash_once(out, &b, -1);
	});
}

class NullIO final : public IOChannel {
public:
	void send_data_internal(const void *, int64_t) override {}
	void recv_data_internal(void *, int64_t) override {}
};

static bool check_fs_state_contracts_rejected() {
	return dies([] {
		NullIO io;
		(void)io.get_send_digest();
	}) && dies([] {
		NullIO io;
		(void)io.get_recv_digest();
	}) && dies([] {
		NullIO io;
		(void)io.get_digest();
	}) && dies([] {
		NullIO io;
		io.enable_fs(false);
		io.enable_fs(false);
	});
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_sha256_known_vectors();
	ok &= check_sha256_against_openssl();
#ifdef EMP_WITH_BLAKE3
	ok &= check_blake3_known_vectors();
#endif
	ok &= check_streaming_equals_oneshot();
	ok &= check_snapshot_does_not_disturb();
	bool invalid_rejected = check_invalid_lengths_rejected();
	cout << "  [negative/overflow lengths rejected]   "
	     << (invalid_rejected ? "OK" : "FAIL") << "\n";
	ok &= invalid_rejected;
	bool fs_contracts = check_fs_state_contracts_rejected();
	cout << "  [Fiat-Shamir state contracts rejected] "
	     << (fs_contracts ? "OK" : "FAIL") << "\n";
	ok &= fs_contracts;
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
