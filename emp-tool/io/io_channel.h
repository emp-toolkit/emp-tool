#ifndef EMP_IO_CHANNEL_H__
#define EMP_IO_CHANNEL_H__
#include "emp-tool/core/block.h"
#include "emp-tool/crypto/hash.h"
#include "emp-tool/crypto/prg.h"
#include "emp-tool/group/group.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>

namespace emp {

// Polymorphic transport interface. Implementations override send_data_internal
// / recv_data_internal; everything else (block / point / packed-bool helpers,
// byte counter, optional Fiat-Shamir transcript) is inherited.
//
// The previous CRTP base (IOChannel<T>) made every consumer of IO a template
// (HalfGateGen<T>, BristolFashion::compute<T>, etc.) to dodge a virtual call
// per send_data. Per-call cost in practice is dominated by stdio locks +
// syscalls, not function-call indirection, so the template tax was paying
// for nothing measurable. Switching to a virtual base costs ~1 indirect call
// per send_data and frees the rest of the toolkit to be non-templated.
//
// Fiat-Shamir support: enable_fs(send_first) turns on TWO SHA-256
// transcripts — one absorbing every byte sent (direction self→peer),
// one absorbing every byte received (peer→self). The send-first party
// passes true; the other party passes false (e.g. for NetIO, use
// is_server, or for emp-ot protocols, party == ALICE).
//
// get_digest() returns the first block of H(d_AB ‖ H(d_BA)) where
// d_AB is the A→B wire digest and d_BA is the B→A digest. Both
// parties produce the same value: the send_first side has my_send =
// d_AB and computes H(my_send ‖ H(my_recv)); the other side has
// my_recv = d_AB and computes H(my_recv ‖ H(my_send)). Both reduce
// to H(d_AB ‖ H(d_BA)). Robust against protocols that batch sends
// asymmetrically (e.g. PVW's two-round structure) because we hash
// per-direction, not per-call-site.
//
// Off by default — one predictable branch per send/recv when off,
// invisible vs syscall cost. Mirrors IKNP-malicious / ferret-malicious's
// chi-binding pattern.

class IOChannel {
public:
	uint64_t counter = 0;

	virtual ~IOChannel() = default;

	virtual void send_data_internal(const void *data, size_t nbyte) = 0;
	virtual void recv_data_internal(void *data, size_t nbyte) = 0;

	// Drain any outbound buffer to the underlying transport. NetIO needs
	// this to push staged TCP writes; MemIO and other in-memory transports
	// have nothing to flush, so the default is a no-op.
	virtual void flush() {}

	// Optional barrier — implementations that need a wire-level handshake
	// (NetIO does a 1-byte ping/pong) override; in-memory transports
	// default to no-op.
	virtual void sync() {}

	// Turn on Fiat-Shamir transcript hashing. `send_first` selects which
	// of the two H(_‖H(_)) formulas this side computes, so both parties
	// produce the same digest value — exactly one party should pass true.
	// Idempotent assert: calling twice is a bug.
	void enable_fs(bool send_first) {
		assert(!fs_send_.has_value() && "enable_fs called twice");
		fs_send_first_ = send_first;
		fs_send_.emplace();
		fs_recv_.emplace();
	}

	bool fs_enabled() const { return fs_send_.has_value(); }

	// Snapshot the running transcripts as one block (first 16 B of a
	// 32-B SHA-256 digest). Does not reset — call repeatedly across
	// protocol stages to derive sub-challenges. Asserts FS is on.
	//
	// Output: H(d_AB ‖ H(d_BA))[0..16). Send-first side computes
	// H(my_send ‖ H(my_recv)); the other side H(my_recv ‖ H(my_send)).
	block get_digest() {
		assert(fs_send_.has_value() && "get_digest: enable_fs first");
		constexpr int N = Hash::DIGEST_SIZE;        // 32
		char d_send[N], d_recv[N];
		fs_send_->digest(d_send, /*reset_after=*/false);
		fs_recv_->digest(d_recv, /*reset_after=*/false);

		const char* outer_first  = fs_send_first_ ? d_send : d_recv;
		const char* inner_input  = fs_send_first_ ? d_recv : d_send;
		char inner[N];
		Hash::hash_once(inner, inner_input, N);

		char outer_buf[2 * N];
		std::memcpy(outer_buf, outer_first, N);
		std::memcpy(outer_buf + N, inner, N);

		alignas(block) char outer[N];
		Hash::hash_once(outer, outer_buf, sizeof(outer_buf));
		block out;
		std::memcpy(&out, outer, sizeof(block));
		return out;
	}

	void send_data(const void *data, size_t nbyte) {
		counter += nbyte;
		if (fs_send_) fs_send_->put(data, nbyte);
		send_data_internal(data, nbyte);
	}

	void recv_data(void *data, size_t nbyte) {
		recv_data_internal(data, nbyte);
		if (fs_recv_) fs_recv_->put(data, nbyte);
	}

	void send_block(const block *data, size_t nblock) {
		send_data(data, nblock * sizeof(block));
	}

	void recv_block(block *data, size_t nblock) {
		recv_data(data, nblock * sizeof(block));
	}

	void send_pt(Point *A, size_t num_pts = 1) {
		for (size_t i = 0; i < num_pts; ++i) {
			size_t len = A[i].size();
			A[i].group()->resize_scratch(len);
			unsigned char *tmp = A[i].group()->scratch();
			send_data(&len, 4);
			A[i].to_bin(tmp, len);
			send_data(tmp, len);
		}
	}

	void recv_pt(Group *g, Point *A, size_t num_pts = 1) {
		size_t len = 0;
		for (size_t i = 0; i < num_pts; ++i) {
			recv_data(&len, 4);
			assert(len <= 2048);
			g->resize_scratch(len);
			unsigned char *tmp = g->scratch();
			recv_data(tmp, len);
			A[i].from_bin(g, tmp, len);
		}
	}

	// Pack 8 bools per wire byte via bools_to_bits / bits_to_bools (LSB-first
	// within each byte, see utils/block.h). Streamed in 8 KiB-of-bools chunks
	// so the staging buffer fits comfortably on the stack regardless of the
	// caller's buffer size.
	void send_bool(const bool *data, size_t length) {
		if (length == 0) return;
		constexpr size_t CHUNK_BOOLS = 8 * 1024;
		uint8_t buf[CHUNK_BOOLS / 8];
		while (length > 0) {
			size_t batch = length < CHUNK_BOOLS ? length : CHUNK_BOOLS;
			size_t bytes = (batch + 7) / 8;
			bools_to_bits(buf, data, batch);
			send_data(buf, bytes);
			data += batch;
			length -= batch;
		}
	}

	void recv_bool(bool *data, size_t length) {
		if (length == 0) return;
		constexpr size_t CHUNK_BOOLS = 8 * 1024;
		uint8_t buf[CHUNK_BOOLS / 8];
		while (length > 0) {
			size_t batch = length < CHUNK_BOOLS ? length : CHUNK_BOOLS;
			size_t bytes = (batch + 7) / 8;
			recv_data(buf, bytes);
			bits_to_bools(data, buf, batch);
			data += batch;
			length -= batch;
		}
	}

private:
	// Per-direction FS transcripts. Both nullopt = off (default).
	// fs_send_ absorbs my outgoing wire bytes, fs_recv_ my incoming
	// ones. Hash is non-copyable but std::optional only requires
	// emplace-construction. fs_send_first_ records the role-bool
	// passed to enable_fs so get_digest produces a value matching
	// the peer.
	std::optional<Hash> fs_send_;
	std::optional<Hash> fs_recv_;
	bool fs_send_first_ = false;
};

}  // namespace emp
#endif
