// io/io_channel.h — IOChannel, the base of every channel: typed send/recv
// helpers over a subclass's raw transport. Read example() first; the rest is
// verification. Runs in one process over an in-memory channel.
//
// What's in io_channel.h (the parts tested here):
//   send_bool(data, n)   pack n bools 8 per byte, LSB-first, with the
//                        padding bits above the message in its last byte zero
//   recv_bool(data, n)   unpack them; abort if a padding bit is set
//   send_/recv_data, _block, _bool
//                        abort on a negative count, on a block count whose
//                        byte count overflows, and on a byte counter that
//                        would wrap
//
// Socket behaviour is tested in test_netio and test_tlsio; send_pt and
// recv_pt, which need an elliptic-curve group, in test_ecc.

#include "emp-tool/emp-tool.h"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace emp;
using namespace std;

// A channel in memory: sends append to out_, receives replay in_.
class MemoryIO : public IOChannel {
public:
	explicit MemoryIO(vector<unsigned char> in) : in_(std::move(in)) {}
	void send_data_internal(const void *data, int64_t nbyte) override {
		const auto *p = static_cast<const unsigned char *>(data);
		out_.insert(out_.end(), p, p + nbyte);
	}
	void recv_data_internal(void *data, int64_t nbyte) override {
		expecting(nbyte >= 0 && off_ <= in_.size() &&
		              static_cast<size_t>(nbyte) <= in_.size() - off_,
		          "MemoryIO: read past input");
		memcpy(data, in_.data() + off_, (size_t)nbyte);
		off_ += (size_t)nbyte;
	}
	vector<unsigned char> in_, out_;
	size_t off_ = 0;
};

// A transport that accepts every call unchecked, so only IOChannel's own
// checks can stop a call that breaks its contract.
class NullIO : public IOChannel {
public:
	void send_data_internal(const void *, int64_t) override {}
	void recv_data_internal(void *, int64_t) override {}
};

// Rejection paths are fatal by design, so they are checked in a child.
template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		close(STDERR_FILENO);
		f();
		_exit(0);
	}
	if (pid < 0) return false;
	int status = 0;
	if (waitpid(pid, &status, 0) != pid) return false;
	return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}

static void example() {
	cout << "=== example ===\n";

	// send_bool packs 13 bools into 2 bytes, LSB-first; the 3 bits above
	// the message in the second byte are zero.
	bool msg[13] = {1, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1};
	MemoryIO alice(vector<unsigned char>{});
	alice.send_bool(msg, 13);
	cout << "  send_bool(13 bools) on the wire  = " << hex << setfill('0');
	for (unsigned char byte : alice.out_) cout << setw(2) << (int)byte << ' ';
	cout << dec << setfill(' ') << "\n";

	// recv_bool unpacks the same bytes.
	MemoryIO bob(alice.out_);
	bool got[13];
	bob.recv_bool(got, 13);
	cout << "  recv_bool(13 bools) round trip   = "
	     << (memcmp(msg, got, sizeof(msg)) == 0 ? "equal" : "DIFFERENT") << "\n";
}

// ---------- correctness ----------

// The bytes send_bool puts on the wire for `bits`.
static vector<unsigned char> encode(const vector<uint8_t> &bits) {
	MemoryIO io(vector<unsigned char>{});
	io.send_bool(bits.data(), (int64_t)bits.size());
	return io.out_;
}

// What recv_bool returns for an n-bool message arriving as `wire`.
static vector<uint8_t> decode(vector<unsigned char> wire, int64_t n) {
	MemoryIO io(std::move(wire));
	vector<uint8_t> bits((size_t)n);
	io.recv_bool(bits.data(), n);
	return bits;
}

static vector<uint8_t> pattern(int64_t n) {
	vector<uint8_t> bits((size_t)n);
	for (int64_t i = 0; i < n; ++i) bits[(size_t)i] = i % 3 == 0;
	return bits;
}

// Messages whose last byte is partial: within one chunk, and in a second
// chunk after a full one.
static const int64_t kPartialLengths[] = {13, (int64_t)IO_BOOL_CHUNK_SIZE + 3};

// send_bool writes ceil(n/8) bytes with zero padding; recv_bool returns the
// message.
static bool check_round_trip() {
	for (int64_t n : kPartialLengths) {
		vector<uint8_t> bits = pattern(n);
		vector<unsigned char> wire = encode(bits);
		if (wire.size() != (size_t)((n + 7) / 8)) return false;
		if ((wire.back() >> (n % 8)) != 0) return false;
		if (decode(wire, n) != bits) return false;
	}
	return true;
}

// recv_bool aborts, in Release too, if any padding bit is set, so a sender
// cannot change the transcript without changing the data.
static bool check_padding_rejected() {
	for (int64_t n : kPartialLengths) {
		const vector<unsigned char> wire = encode(pattern(n));
		for (int j = (int)(n % 8); j < 8; ++j) {
			vector<unsigned char> bad = wire;
			bad.back() |= (unsigned char)(1u << j);
			if (!dies([&] { decode(bad, n); })) return false;
		}
	}
	return true;
}

// A whole-byte message has no padding, so every byte value decodes.
static bool check_whole_bytes() {
	return decode(vector<unsigned char>{0xff, 0xa5}, 16) ==
	       vector<uint8_t>{1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1};
}

// Signed public counts must fail before counter updates, multiplication,
// pointer arithmetic, or conversion to an unsigned library size, and the
// byte counters must not wrap. Valid counts go through.
static bool check_count_contracts() {
	NullIO io;
	char byte = 0;
	block blk = zero_block;
	bool bit = false;
	const int64_t max_blocks = numeric_limits<int64_t>::max();
	// recv_bool would unpack bytes this transport never writes; its valid
	// path is check_round_trip's.
	const bool valid_passes = !dies([&] {
		io.send_data(&byte, 1);
		io.recv_data(&byte, 1);
		io.send_block(&blk, 1);
		io.recv_block(&blk, 1);
		io.send_bool(&bit, 1);
	});
	return valid_passes &&
	       dies([&] { io.send_data(&byte, -1); }) &&
	       dies([&] { io.recv_data(&byte, -1); }) &&
	       dies([&] { io.send_block(&blk, -1); }) &&
	       dies([&] { io.recv_block(&blk, -1); }) &&
	       dies([&] { io.send_block(&blk, max_blocks); }) &&
	       dies([&] { io.recv_block(&blk, max_blocks); }) &&
	       dies([&] { io.send_bool(&bit, -1); }) &&
	       dies([&] { io.recv_bool(&bit, -1); }) &&
	       dies([&] {
	           io.send_counter = numeric_limits<uint64_t>::max();
	           io.send_data(&byte, 1);
	       }) &&
	       dies([&] {
	           io.recv_counter = numeric_limits<uint64_t>::max();
	           io.recv_data(&byte, 1);
	       });
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool trip = check_round_trip();
	bool pad = check_padding_rejected();
	bool whole = check_whole_bytes();
	bool counts = check_count_contracts();
	cout << "  send_bool/recv_bool round trip     " << (trip ? "OK" : "FAIL") << "\n";
	cout << "  recv_bool rejects nonzero padding  " << (pad ? "OK" : "FAIL") << "\n";
	cout << "  whole bytes decode any value       " << (whole ? "OK" : "FAIL") << "\n";
	cout << "  bad counts and wrapping rejected   " << (counts ? "OK" : "FAIL") << "\n";
	return trip && pad && whole && counts;
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
