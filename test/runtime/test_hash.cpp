// crypto/hash.h — streaming hash, templated on the algorithm (HashT<opt>:
// OpenSSL SHA-256 default, vendored BLAKE3 opt-in). Read example() first;
// the rest is verification.
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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
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

// Exposes the protected use_transcript_of for the contract checks.
class RetargetIO final : public IOChannel {
public:
	void retarget(IOChannel *inner) { use_transcript_of(inner); }
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
	}) && dies([] {
		RetargetIO io;
		io.retarget(nullptr);
	}) && dies([] {
		RetargetIO io;
		io.retarget(&io);
	}) && dies([] {
		RetargetIO io;
		NullIO inner;
		io.enable_fs(false);
		io.retarget(&inner);  // it already keeps its own transcript
	}) && dies([] {
		RetargetIO a, b;
		a.retarget(&b);
		b.retarget(&a);  // each would use the other's
	}) && dies([] {
		RetargetIO io;
		NullIO a, b;
		io.retarget(&a);
		io.retarget(&b);
	}) && dies([] {
		RetargetIO io;
		NullIO inner;
		io.retarget(&inner);
		inner.enable_fs(false);
		io.enable_fs(false);  // inner's transcript is already on
	});
}

// One end of an in-memory pipe: sends append to `out`, receives take from `in`.
// `sends` / `recvs` record the length of every transport call.
class PipeIO final : public IOChannel {
public:
	PipeIO(std::deque<uint8_t> *out, std::deque<uint8_t> *in) : out_(out), in_(in) {}
	void send_data_internal(const void *data, int64_t nbyte) override {
		sends.push_back(nbyte);
		const uint8_t *p = static_cast<const uint8_t *>(data);
		out_->insert(out_->end(), p, p + nbyte);
	}
	void recv_data_internal(void *data, int64_t nbyte) override {
		recvs.push_back(nbyte);
		expecting(static_cast<int64_t>(in_->size()) >= nbyte, "PipeIO: pipe underrun");
		std::copy(in_->begin(), in_->begin() + nbyte, static_cast<uint8_t *>(data));
		in_->erase(in_->begin(), in_->begin() + nbyte);
	}
	vector<int64_t> sends, recvs;
private:
	std::deque<uint8_t> *out_, *in_;
};

// With FS on, a transfer longer than 256 KiB reaches the transport and the
// transcript in 256 KiB pieces within its one call. Messages around and well
// past the piece size go both ways, with FS off and on. They must arrive
// intact; the transport must see each message whole with FS off, and in those
// pieces with FS on; the byte counters and rounds must count one call per
// message; and each end's transcripts must equal one-shot hashes of the bytes
// each direction carried.
static bool check_large_transfer_transcript() {
	using FsHash = HashT<EMP_FS_HASH_DEFAULT>;
	static constexpr int64_t P = 256 * 1024;
	const int64_t sizes[] = {1, P - 1, P, P + 1, 3 * P + 7, int64_t{1} << 20, 5, 2 * P};
	// The transport calls one message takes: one up to P bytes or with FS off,
	// else P-byte pieces and the remainder.
	auto pieces = [](int64_t n, bool fs, vector<int64_t> &calls) {
		if (!fs || n <= P) { calls.push_back(n); return; }
		for (; n > P; n -= P) calls.push_back(P);
		calls.push_back(n);
	};
	auto first_block = [](const char *digest) {
		block b;
		std::memcpy(&b, digest, sizeof(b));
		return b;
	};
	bool ok = true;
	for (bool fs : {false, true}) {
		std::deque<uint8_t> ab, ba;
		PipeIO a(&ab, &ba), b(&ba, &ab);
		if (fs) {
			a.enable_fs(true);
			b.enable_fs(false);
		}
		vector<uint8_t> sent_ab, sent_ba;
		vector<int64_t> calls_ab, calls_ba;
		uint64_t a_rounds = 0, b_rounds = 0;
		int last = 0;  // direction of the previous message: 1 = a to b, 2 = b to a
		PRG prg(&zero_block);
		for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
			const int64_t n = sizes[i];
			const int dir = i % 3 != 2 ? 1 : 2;
			vector<uint8_t> msg(n), got(n);
			prg.random_data_unaligned(msg.data(), n);
			if (dir == 1) {
				a.send_data(msg.data(), n);
				b.recv_data(got.data(), n);
				sent_ab.insert(sent_ab.end(), msg.begin(), msg.end());
				pieces(n, fs, calls_ab);
			} else {
				b.send_data(msg.data(), n);
				a.recv_data(got.data(), n);
				sent_ba.insert(sent_ba.end(), msg.begin(), msg.end());
				pieces(n, fs, calls_ba);
			}
			if (dir != last) {
				++a_rounds;
				++b_rounds;
				last = dir;
			}
			ok &= got == msg;
		}
		ok &= a.sends == calls_ab && b.recvs == calls_ab;
		ok &= b.sends == calls_ba && a.recvs == calls_ba;
		ok &= a.send_counter == sent_ab.size() && b.recv_counter == sent_ab.size();
		ok &= b.send_counter == sent_ba.size() && a.recv_counter == sent_ba.size();
		ok &= a.rounds == a_rounds && b.rounds == b_rounds;
		if (!fs) continue;
		char d_ab[FsHash::DIGEST_SIZE], d_ba[FsHash::DIGEST_SIZE];
		char both[2 * FsHash::DIGEST_SIZE], d_both[FsHash::DIGEST_SIZE];
		FsHash::hash_once(d_ab, sent_ab.data(), static_cast<int64_t>(sent_ab.size()));
		FsHash::hash_once(d_ba, sent_ba.data(), static_cast<int64_t>(sent_ba.size()));
		std::memcpy(both, d_ab, sizeof(d_ab));
		std::memcpy(both + sizeof(d_ab), d_ba, sizeof(d_ba));
		FsHash::hash_once(d_both, both, sizeof(both));
		const block want_ab = first_block(d_ab), want_ba = first_block(d_ba);
		const block want_both = first_block(d_both);
		const block as = a.get_send_digest(), br = b.get_recv_digest();
		const block bs = b.get_send_digest(), ar = a.get_recv_digest();
		const block da = a.get_digest(), db = b.get_digest();
		ok &= cmpBlock(&as, &want_ab, 1) && cmpBlock(&br, &want_ab, 1);
		ok &= cmpBlock(&bs, &want_ba, 1) && cmpBlock(&ar, &want_ba, 1);
		ok &= cmpBlock(&da, &want_both, 1) && cmpBlock(&db, &want_both, 1);
	}
	cout << "  [large transfers, piecewise absorb]    " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// A wrapper that sends every byte through `inner` and uses inner's transcript.
// Like a channel that buffers its own units, it sends what it has staged
// before the next message.
class StagingIO final : public IOChannel {
public:
	explicit StagingIO(IOChannel *inner) : inner_(inner) { use_transcript_of(inner); }
	void stage(uint8_t b) { staged_.push_back(b); }
	void send_data_internal(const void *data, int64_t nbyte) override {
		if (!staged_.empty()) {
			std::vector<uint8_t> unit;
			unit.swap(staged_);
			inner_->send_data(unit.data(), static_cast<int64_t>(unit.size()));
		}
		inner_->send_data(data, nbyte);
	}
	void recv_data_internal(void *data, int64_t nbyte) override {
		inner_->recv_data(data, nbyte);
	}
private:
	IOChannel *inner_;
	std::vector<uint8_t> staged_;
};

// A wrapper's transcript is its inner channel's: turning it on through the
// wrapper turns on inner's, and the staged bytes a message sends first are
// recorded before that message, as they go out, so the sending and receiving
// ends agree.
static bool check_wrapper_uses_inner_transcript() {
	std::deque<uint8_t> wire;
	PipeIO sender(&wire, nullptr), receiver(nullptr, &wire);
	StagingIO wrapper(&sender);
	wrapper.enable_fs(true);
	receiver.enable_fs(false);
	const uint8_t staged[3] = {1, 2, 3};
	for (uint8_t b : staged) wrapper.stage(b);
	const block msg = makeBlock(0x0123, 0x4567);
	wrapper.send_block(&msg, 1);

	uint8_t got_staged[3];
	block got_msg;
	receiver.recv_data(got_staged, 3);
	receiver.recv_block(&got_msg, 1);
	const block via_wrapper = wrapper.get_send_digest();
	const block via_inner = sender.get_send_digest();
	const block received = receiver.get_recv_digest();
	const block wrapper_all = wrapper.get_digest();
	const block inner_all = sender.get_digest();
	const bool ok = sender.fs_enabled() && wrapper.fs_enabled() &&
	                std::memcmp(got_staged, staged, sizeof(staged)) == 0 &&
	                cmpBlock(&got_msg, &msg, 1) &&
	                cmpBlock(&via_wrapper, &via_inner, 1) &&
	                cmpBlock(&via_inner, &received, 1) &&
	                cmpBlock(&wrapper_all, &inner_all, 1);
	cout << "  [wrapper uses its inner transcript]    " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
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
	ok &= check_wrapper_uses_inner_transcript();
	ok &= check_large_transfer_transcript();
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
