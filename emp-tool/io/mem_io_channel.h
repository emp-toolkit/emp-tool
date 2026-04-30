#ifndef EMP_MEM_IO_CHANNEL_H__
#define EMP_MEM_IO_CHANNEL_H__

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "emp-tool/io/io_channel.h"

namespace emp {

// In-memory IOChannel backed by std::vector<uint8_t>. send_data appends;
// recv_data reads sequentially from the start. Use clear() to reset for a
// new write cycle, or rewind() to re-read from the beginning. To prime the
// buffer from disk, callers should slurp the file with std::ifstream and
// pass the bytes via load(span, size).
class MemIO : public IOChannel { public:
	std::vector<uint8_t> buffer;
	size_t read_pos = 0;

	MemIO() = default;
	explicit MemIO(size_t reserve_bytes) {
		buffer.reserve(reserve_bytes);
	}

	void clear() {
		buffer.clear();
		read_pos = 0;
	}

	void rewind() { read_pos = 0; }

	// Replace the buffer with externally-supplied bytes (e.g. slurped from
	// a file via std::ifstream::read).
	void load(const void *data, size_t len) {
		buffer.assign((const uint8_t *)data, (const uint8_t *)data + len);
		read_pos = 0;
	}

	void send_data_internal(const void *data, size_t len) override {
		const uint8_t *p = static_cast<const uint8_t *>(data);
		buffer.insert(buffer.end(), p, p + len);
	}

	void recv_data_internal(void *data, size_t len) override {
		if (read_pos + len > buffer.size()) {
			fprintf(stderr, "error: mem_recv_data underflow (need %zu, have %zu)\n",
			        len, buffer.size() - read_pos);
			throw std::out_of_range("MemIO::recv_data underflow");
		}
		std::memcpy(data, buffer.data() + read_pos, len);
		read_pos += len;
	}
};

}  // namespace emp
#endif
