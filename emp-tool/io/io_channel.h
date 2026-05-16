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
// Fiat-Shamir support: enable_fs(send_first) turns on TWO SHA-256
// transcripts — one absorbing every byte sent (direction self→peer),
// one absorbing every byte received (peer→self). Exactly one party
// passes true.
//
// get_digest() returns the first block of H(d_AB ‖ d_BA), where d_AB
// is the A→B wire digest and d_BA is the B→A digest. Both parties
// produce the same value: the send_first side concatenates my_send
// first, the other side concatenates my_recv first, and the meanings
// swap between the two parties. Hashing per-direction (not per
// call-site) is robust against protocols that batch sends asymmetrically.
//
// Off by default.

class IOChannel {
public:
	uint64_t counter = 0;

	virtual ~IOChannel() = default;

	virtual void send_data_internal(const void *data, size_t nbyte) = 0;
	virtual void recv_data_internal(void *data, size_t nbyte) = 0;

	// Drain any outbound buffer to the underlying transport. Default
	// is a no-op for transports with nothing to flush.
	virtual void flush() {}

	// Optional wire-level handshake (e.g. 1-byte ping/pong). Default
	// no-op for transports that don't need one.
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
	// Output: H(d_AB ‖ d_BA)[0..16). Send-first side concatenates
	// my_send first, the other side concatenates my_recv first.
	block get_digest() {
		assert(fs_send_.has_value() && "get_digest: enable_fs first");
		constexpr int N = Hash::DIGEST_SIZE;        // 32
		char buf[2 * N];
		fs_send_->digest(buf + (fs_send_first_ ? 0 : N), /*reset_after=*/false);
		fs_recv_->digest(buf + (fs_send_first_ ? N : 0), /*reset_after=*/false);
		alignas(block) char out_buf[N];
		Hash::hash_once(out_buf, buf, sizeof(buf));
		block out;
		std::memcpy(&out, out_buf, sizeof(block));
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
	//
	// Security: bools_to_bits is a read-modify-write on the tail byte and
	// preserves any high padding bits that come along (positions
	// [batch%8 .. 8) in the last destination byte). With an uninitialized
	// stack `buf`, those bits would leak stack contents onto the wire and
	// make Fiat-Shamir transcripts non-deterministic for non-multiple-of-8
	// lengths. Zero the one byte that bools_to_bits leaves partially-
	// written before each pack. Whole-byte bytes get fully overwritten by
	// the SIMD/memcpy path inside bools_to_bits, so they don't need a clear.
	void send_bool(const bool *data, size_t length) {
		if (length == 0) return;
		uint8_t buf[IO_BOOL_CHUNK_SIZE / 8];
		while (length > 0) {
			size_t batch = length < IO_BOOL_CHUNK_SIZE ? length : IO_BOOL_CHUNK_SIZE;
			size_t bytes = (batch + 7) / 8;
			if (batch % 8 != 0) buf[bytes - 1] = 0;
			bools_to_bits(buf, data, batch);
			send_data(buf, bytes);
			data += batch;
			length -= batch;
		}
	}

	void recv_bool(bool *data, size_t length) {
		if (length == 0) return;
		uint8_t buf[IO_BOOL_CHUNK_SIZE / 8];
		while (length > 0) {
			size_t batch = length < IO_BOOL_CHUNK_SIZE ? length : IO_BOOL_CHUNK_SIZE;
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
