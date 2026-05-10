#ifndef EMP_NETWORK_IO_CHANNEL_H__
#define EMP_NETWORK_IO_CHANNEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <iostream>

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
// A bidirectional-stdio variant (NetIOBuffered, in
// net_io_buffered_channel.h) implements the same IOChannel contract.
// Prefer NetIO for small-message workloads (per-recv FILE* lock matters);
// NetIOBuffered for multi-MiB bulk transfers (one big stdio copy
// amortizes the lock).
//
// Flush contract:
//
// Callers MUST call flush() at the end of any protocol step that ends
// in sends, before returning to the caller or blocking on anything
// other than a recv on this same NetIO. recv_data_internal calls
// flush() at the top automatically, so mixed-direction patterns drain
// themselves — but a step that is purely sends strands its tail bytes
// in send_buf otherwise.
//
// ~NetIO also flushes, so pure "send-then-destruct" patterns work. A
// long-lived NetIO that does a mid-life send-only step does not: the
// destructor only fires at NetIO end-of-life, which doesn't happen
// until the protocol completes, which can't complete because the peer
// is blocked on bytes stuck in send_buf. Circular-wait deadlock, not
// "data eventually arrives slowly".
//
// Rule of thumb: if the next thing you do on this NetIO isn't a recv,
// flush first. test/test_netio.cpp encodes the contract.

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

	// Wrap an already-connected socket fd. Useful for callers that run their
	// own listener / accept loop (e.g. a multi-party mesh that dispatches
	// inbound connections by peer id) and want to hand each accepted fd to a
	// NetIO without going back through bind/listen/accept.
	NetIO(int existing_sock, bool quiet = true) : quiet(quiet) {
		is_server = false;
		init_from_sock(existing_sock);
	}

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
		stream_buf = new char[NETWORK_BUFFER_SIZE];
		send_buf   = new char[NETWORK_BUFFER_SIZE2];
		recv_buf   = new char[NETWORK_BUFFER_SIZE2];
		stream = fdopen(sock, "wb");
		setvbuf(stream, stream_buf, _IOFBF, NETWORK_BUFFER_SIZE);
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

	void send_data_internal(const void *data, size_t len) override {
#ifndef NDEBUG
		touch_guard _g(_in_use, "send_data");
#endif
		if (len + send_ptr <= (size_t)NETWORK_BUFFER_SIZE2) {
			memcpy(send_buf + send_ptr, data, len);
			send_ptr += len;
		} else {
			if (send_ptr) { send_raw(send_buf, send_ptr); send_ptr = 0; }
			send_raw(data, len);
		}
		send_dirty = true;
	}

	void recv_data_internal(void *data, size_t len) override {
#ifndef NDEBUG
		touch_guard _g(_in_use, "recv_data");
#endif
		// Drain pending sends before blocking on the peer's reply, else
		// any send-then-recv pattern would deadlock with our bytes still
		// staged. Raw ::read() bypasses stdio, so this has to be explicit.
		flush_unlocked();
		bytes_recv += len;
		size_t got = 0;
		while (got < len) {
			if (recv_ptr == recv_fill) {
				// Raw read() (not fread) so the refill accepts whatever the
				// kernel has available — fread would block waiting for a
				// full staging buffer's worth of bytes.
				ssize_t n;
				do { n = ::read(sock, recv_buf, NETWORK_BUFFER_SIZE2); }
				while (n < 0 && errno == EINTR);
				if (n <= 0) { fprintf(stderr, "error: net_recv_data\n"); exit(1); }
				recv_ptr = 0;
				recv_fill = (size_t)n;
			}
			size_t take = recv_fill - recv_ptr;
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
