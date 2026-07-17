#ifndef EMP_NETWORK_IO_CHANNEL_H__
#define EMP_NETWORK_IO_CHANNEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <string>

#include "emp-tool/runtime/core/utils.h"   // error()
#include "emp-tool/runtime/io/io_channel.h"
#include "emp-tool/runtime/io/tcp_socket.h"

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
	// Endpoint info retained so a duplex sibling can be spawned (make_sibling).
	std::string addr_;              // peer address (empty when this is a server)
	int port_ = -1;

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

	// Byte counts and flush count live on the IOChannel base.

#ifndef NDEBUG
	// Debug-only concurrency assertion (NetIO is not thread-safe: the
	// send_buf coalescing is unlocked). The shared touch_guard that trips on
	// a concurrent send_data/recv_data/flush lives in io_channel.h; this is
	// just its per-instance counter (dropped in release builds).
	std::atomic<int> _in_use{0};
#endif

	NetIO(const char *address, int port, bool quiet = false) : quiet(quiet) {
		expecting(port >= 0 && port <= 65535,
		          "NetIO: invalid port number");

		is_server = (address == nullptr);
		addr_ = address ? address : "";
		port_ = port;
		init_from_sock(is_server ? tcp::server_listen(port) : tcp::client_connect(address, port));
		if (!quiet) std::cout << "connected\n";
	}

	// Named factories owning their result (auto-freed via unique_ptr). The role
	// is explicit in the name and the signature — listen() takes no address;
	// connect() requires one — replacing the nullptr-means-server sentinel of the
	// (const char*, int) constructor.
	static std::unique_ptr<NetIO> listen(int port, bool quiet = false) {
		return std::make_unique<NetIO>(nullptr, port, quiet);
	}
	static std::unique_ptr<NetIO> connect(const char *address, int port, bool quiet = false) {
		return std::make_unique<NetIO>(address, port, quiet);
	}

	// Open a second channel to the same peer, on the same role and port. Safe
	// to call once this connection is established: server_listen closes its
	// listening socket after accept (and sets SO_REUSEADDR), so the port is
	// free to listen on again; the peer calls make_sibling() symmetrically and
	// the two reconnect. Ownership is the unique_ptr's.
	std::unique_ptr<NetIO> make_sibling() const {
		// Settle before reusing the same port for the duplex sibling. The
		// primary connection's accept/connect has just completed; its in-flight
		// handshake/teardown segments can otherwise collide with the sibling on
		// the same port — the server closes its first listener and re-listens,
		// and a sibling SYN landing in that gap (or a primary retransmit landing
		// on the fresh listener) can mis-pair the two channels and deadlock the
		// protocol (a low-probability race, worse at higher RTT). A 0.1 s pause
		// drains those segments so the sibling pairing is unambiguous;
		// make_sibling is called once per session, so the cost is negligible.
		usleep(100000);  // 0.1 s
		return is_server ? listen(port_, /*quiet=*/true)
		                 : connect(addr_.c_str(), port_, /*quiet=*/true);
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
		if (!quiet)
			std::cout << get_statistics_string();
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
		expecting(stream != nullptr, [&] {
			return std::string("NetIO: fdopen failed: ") + std::strerror(errno);
		});
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
		expecting(len >= 0, "NetIO::send_data: negative len");
		if (len <= (int64_t)(NETWORK_STAGING_BUFFER_SIZE - send_ptr)) {
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
		expecting(len >= 0, "NetIO::recv_data: negative len");
		// Drain pending sends before blocking on the peer's reply, else
		// any send-then-recv pattern would deadlock with our bytes still
		// staged. Raw ::read() bypasses stdio, so this has to be explicit.
		flush_unlocked();
		int64_t got = 0;
		while (got < len) {
			if (recv_ptr == recv_fill) {
				// Raw read() (not fread) so the refill accepts whatever the
				// kernel has available — fread would block waiting for a
				// full staging buffer's worth of bytes.
				ssize_t n;
				do { n = ::read(sock, recv_buf, NETWORK_STAGING_BUFFER_SIZE); }
				while (n < 0 && errno == EINTR);
				// Peer closed (n==0) or a hard read error (n<0): unrecoverable.
				// Terminate with _Exit, NOT exit(): this can run on a ThreadPool
				// worker while sibling workers are still live, and exit()'s
				// cleanup (static destructors / atexit / stdio teardown) would
				// free/flush memory those threads are mid-use on, tripping
				// glibc's heap-corruption detector ("unaligned fastbin chunk").
				// _Exit ends the process at once, running no destructors.
				if (n <= 0) { fprintf(stderr, "error: net_recv_data (peer closed or read error)\n"); _Exit(1); }
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
		size_t sent = 0;
		while (sent < len) {
			size_t res = fwrite((const char *)data + sent, 1, len - sent, stream);
			if (res > 0) sent += res;
			// _Exit, not exit(): same worker-thread fatal-abort rule as
			// recv_data_internal — do not run destructors while peers are live.
			else { fprintf(stderr, "error: net_send_data (peer closed or write error)\n"); _Exit(1); }
		}
	}

};

}  // namespace emp
#endif
