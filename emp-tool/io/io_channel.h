#ifndef EMP_IO_CHANNEL_H__
#define EMP_IO_CHANNEL_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/prg.h"
#include "emp-tool/utils/group.h"
#include <cassert>
#include <cstdint>
#include <memory>

namespace emp {

// Polymorphic transport interface. Implementations override send_data_internal
// / recv_data_internal; everything else (block / point / packed-bool helpers,
// byte counter) is inherited.
//
// The previous CRTP base (IOChannel<T>) made every consumer of IO a template
// (HalfGateGen<T>, BristolFashion::compute<T>, etc.) to dodge a virtual call
// per send_data. Per-call cost in practice is dominated by stdio locks +
// syscalls, not function-call indirection, so the template tax was paying
// for nothing measurable. Switching to a virtual base costs ~1 indirect call
// per send_data and frees the rest of the toolkit to be non-templated.

class IOChannel {
public:
	uint64_t counter = 0;

	virtual ~IOChannel() = default;

	virtual void send_data_internal(const void *data, size_t nbyte) = 0;
	virtual void recv_data_internal(void *data, size_t nbyte) = 0;

	void send_data(const void *data, size_t nbyte) {
		counter += nbyte;
		send_data_internal(data, nbyte);
	}

	void recv_data(void *data, size_t nbyte) {
		recv_data_internal(data, nbyte);
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
			A[i].group->resize_scratch(len);
			unsigned char *tmp = A[i].group->scratch;
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
			unsigned char *tmp = g->scratch;
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
};

}  // namespace emp
#endif
