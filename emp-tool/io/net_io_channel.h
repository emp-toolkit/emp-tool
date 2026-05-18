#ifndef EMP_NETWORK_IO_CHANNEL_H__
#define EMP_NETWORK_IO_CHANNEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>

#include "emp-tool/io/io_channel.h"
#include "emp-tool/io/tcp_socket.h"

#include <errno.h>
#include <unistd.h>

namespace emp {

// Single-socket full-duplex NetIO. One TCP fd carries both directions.
// Send side: "wb" stdio FILE* (1 MiB stream buffer) with a 32 KiB
// user-space coalescing buffer in front of fwrite. Recv side: raw
// ::read() into its own 32 KiB staging buffer, no stdio. The two paths
// share the fd but no libc-level state.
//
// Flush contract: callers must call flush() at the end of any protocol
// step that ends in sends, before returning to the caller or blocking
// on anything other than a recv on this same NetIO. recv_data_internal
// flushes implicitly, so mixed-direction patterns drain themselves; a
// step that is purely sends strands its tail bytes otherwise. ~NetIO
// also flushes, so "send-then-destruct" patterns work.
//
// Rule of thumb: if the next thing on this NetIO isn't a recv, flush
// first.

class NetIO : public IOChannel { public:
	int sock = -1;
	bool is_server, quiet;

	// Send-side state (stdio "wb" stream + app-level coalescing buffer).
	FILE *stream = nullptr;
	char *stream_buf = nullptr;     // backing store for setvbuf, lifetime tied to stream
	char *send_buf = nullptr;       // 32 KiB coalescing staging
	size_t send_ptr = 0;
	bool send_dirty = false;

	// Recv-side state (raw read() into a 32 KiB staging buffer).
	char *recv_buf = nullptr;
	size_t recv_ptr = 0;            // next byte to deliver to the caller
	size_t recv_fill = 0;           // bytes available in recv_buf

	// Telemetry.
	uint64_t bytes_sent = 0;
	uint64_t bytes_recv = 0;
	uint64_t flushes_count = 0;

#ifndef NDEBUG
	// Debug-only concurrency assertion. NetIO is not thread-safe (the
	// send_buf coalescing is unlocked); a debug build aborts if two
	// threads enter send_data / recv_data / flush on the same instance
	// at once. See touch_guard below; release builds drop both members.
	std::atomic<int> _in_use{0};
	struct touch_guard {
		std::atomic<int> &f;
		const char *op;
		touch_guard(std::atomic<int> &x, const char *o) : f(x), op(o) {
			if (f.fetch_add(1, std::memory_order_acq_rel) != 0) {
				fprintf(stderr,
				        "NetIO race: concurrent %s on the same NetIO. "
				        "NetIO is not thread-safe — only one thread may "
				        "touch a given NetIO at a time.\n",
				        op);
				std::abort();
			}
		}
		~touch_guard() { f.fetch_sub(1, std::memory_order_release); }
	};
#endif

	NetIO(const char *address, int port, bool quiet = false) : quiet(quiet) {
		if (port < 0 || port > 65535)
			throw std::runtime_error("Invalid port number!");

		is_server = (address == nullptr);
		init_from_sock(is_server ? tcp::server_listen(port) : tcp::client_connect(address, port));
		if (!quiet) std::cout << "connected\n";
	}

	// Wrap an already-connected socket fd, for callers that run their
	// own accept loop and want to skip bind/listen/accept.
	NetIO(int existing_sock, bool quiet = true) : quiet(quiet) {
		is_server = false;
		init_from_sock(existing_sock);
	}

	// Non-copyable, non-movable: owns raw fd / FILE* / buffers that
	// would multi-free.
	NetIO(const NetIO&)             = delete;
	NetIO& operator=(const NetIO&)  = delete;
	NetIO(NetIO&&)                  = delete;
	NetIO& operator=(NetIO&&)       = delete;

	~NetIO() {
		flush();
		if (!quiet) {
			std::cout << "Data Sent: \t"     << bytes_sent     << "\n";
			std::cout << "Data Received: \t" << bytes_recv     << "\n";
			std::cout << "Flushes:\t"        << flushes_count  << "\n";
		}
		if (stream) fclose(stream);   // closes the underlying fd
		delete[] stream_buf;
		delete[] send_buf;
		delete[] recv_buf;
	}

	void flush() override {
#ifndef NDEBUG
		touch_guard _g(_in_use, "flush");
#endif
		flush_unlocked();
	}

	void init_from_sock(int new_sock) {
		sock = new_sock;
		tcp::set_nodelay(sock);
		stream_buf = new char[NETWORK_STREAM_BUFFER_SIZE];
		send_buf   = new char[NETWORK_STAGING_BUFFER_SIZE];
		recv_buf   = new char[NETWORK_STAGING_BUFFER_SIZE];
		stream = fdopen(sock, "wb");
		if (stream == nullptr) {
			// The constructor is about to throw — ~NetIO() will not run,
			// so release everything we allocated above and close the fd
			// before propagating. POSIX: on fdopen failure the fd is
			// still ours to close.
			int saved_errno = errno;
			::close(sock); sock = -1;
			delete[] stream_buf; stream_buf = nullptr;
			delete[] send_buf;   send_buf   = nullptr;
			delete[] recv_buf;   recv_buf   = nullptr;
			throw std::runtime_error(std::string("NetIO: fdopen failed: ") + std::strerror(saved_errno));
		}
		setvbuf(stream, stream_buf, _IOFBF, NETWORK_STREAM_BUFFER_SIZE);
	}

	void set_nodelay() { tcp::set_nodelay(sock); }
	void set_delay()   { tcp::set_delay(sock); }

	// 1-byte ping/pong handshake to verify both directions are alive.
	void sync() override {
		int tmp = 0;
		if (is_server) {
			send_data_internal(&tmp, 1);
			recv_data_internal(&tmp, 1);
		} else {
			recv_data_internal(&tmp, 1);
			send_data_internal(&tmp, 1);
		}
	}

	void send_data_internal(const void *data, int64_t len) override {
#ifndef NDEBUG
		touch_guard _g(_in_use, "send_data");
#endif
		if (len < 0) error("NetIO::send_data: negative len");
		if (len + (int64_t)send_ptr <= (int64_t)NETWORK_STAGING_BUFFER_SIZE) {
			memcpy(send_buf + send_ptr, data, len);
			send_ptr += len;
		} else {
			if (send_ptr) { send_raw(send_buf, send_ptr); send_ptr = 0; }
			send_raw(data, len);
		}
		send_dirty = true;
	}

	void recv_data_internal(void *data, int64_t len) override {
#ifndef NDEBUG
		touch_guard _g(_in_use, "recv_data");
#endif
		if (len < 0) error("NetIO::recv_data: negative len");
		// Drain pending sends before blocking on the peer's reply, else
		// any send-then-recv pattern would deadlock with our bytes still
		// staged. Raw ::read() bypasses stdio, so this has to be explicit.
		flush_unlocked();
		bytes_recv += len;
		int64_t got = 0;
		while (got < len) {
			if (recv_ptr == recv_fill) {
				// Raw read() (not fread) so the refill accepts whatever the
				// kernel has available — fread would block waiting for a
				// full staging buffer's worth of bytes.
				ssize_t n;
				do { n = ::read(sock, recv_buf, NETWORK_STAGING_BUFFER_SIZE); }
				while (n < 0 && errno == EINTR);
				if (n <= 0) { fprintf(stderr, "error: net_recv_data\n"); exit(1); }
				recv_ptr = 0;
				recv_fill = (size_t)n;
			}
			int64_t take = recv_fill - recv_ptr;
			if (take > len - got) take = len - got;
			memcpy((char *)data + got, recv_buf + recv_ptr, take);
			recv_ptr += take;
			got += take;
		}
	}

private:
	void flush_unlocked() {
		if (!send_dirty) return;
		++flushes_count;
		if (send_ptr) { send_raw(send_buf, send_ptr); send_ptr = 0; }
		fflush(stream);
		send_dirty = false;
	}

	void send_raw(const void *data, size_t len) {
		bytes_sent += len;
		size_t sent = 0;
		while (sent < len) {
			size_t res = fwrite((const char *)data + sent, 1, len - sent, stream);
			if (res > 0) sent += res;
			else { fprintf(stderr, "error: net_send_data\n"); exit(1); }
		}
	}

};

}  // namespace emp
#endif
