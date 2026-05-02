#ifndef EMP_NETWORK_IO_CHANNEL_H__
#define EMP_NETWORK_IO_CHANNEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

#include "emp-tool/io/io_channel.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace emp {

// Single-socket full-duplex NetIO using bidirectional stdio.
//
// One TCP fd, one stdio FILE* opened in "wb+" mode. C's stdio rules require
// fflush between writes and reads on a bidirectional stream; we exploit that
// by setting `has_sent` on every send and calling fflush at the top of every
// recv when set. This guarantees that any party blocking on a recv has first
// drained its own pending writes — including writes from earlier in the same
// session AND writes from prior protocol steps that ended in send-only
// sequences — without protocol code having to remember to flush.
//
// The previous design (split "wb" stdio + raw read()) lost this guarantee:
// the auto-flush only fired when the same NetIO did a recv, so any NetIO
// used in send-only patterns (e.g. an IKNP-receiver-role channel that only
// sends u-matrices, check_x, check_t) would strand its tail bytes in
// userspace forever. See docs/netio-flush-deadlock-investigation.md.

class NetIO : public IOChannel { public:
	int sock = -1;
	bool is_server, quiet;

	FILE *stream = nullptr;
	char *stream_buf = nullptr;     // backing store for setvbuf, lifetime tied to stream
	// Userspace coalescing buffer in front of stdio. Lots of protocol code
	// streams small writes (256-byte u-rows in IKNP, 32-byte check_x/check_t,
	// per-element packing) at high rate; coalescing them via memcpy + ptr-bump
	// is materially faster than going through fwrite's per-call overhead
	// (per-FILE* lock, error path, dispatch). We drain send_buf to fwrite when
	// it fills, when a recv triggers flush(), or when ~NetIO runs.
	char *send_buf = nullptr;
	size_t send_ptr = 0;
	bool has_sent = false;          // any pending writes since last fflush?

	// Telemetry.
	uint64_t bytes_sent = 0;
	uint64_t bytes_recv = 0;
	uint64_t flushes_count = 0;

	NetIO(const char *address, int port, bool quiet = false) : quiet(quiet) {
		if (port < 0 || port > 65535)
			throw std::runtime_error("Invalid port number!");

		is_server = (address == nullptr);
		init_from_sock(is_server ? server_listen(port) : client_connect(address, port));
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
	}

	void flush() override {
		if (!has_sent) return;
		++flushes_count;
		if (send_ptr) { send_raw(send_buf, send_ptr); send_ptr = 0; }
		fflush(stream);
		has_sent = false;
	}

	void init_from_sock(int new_sock) {
		sock = new_sock;
		set_nodelay();
		stream_buf = new char[NETWORK_BUFFER_SIZE];
		send_buf   = new char[NETWORK_BUFFER_SIZE2];
		stream = fdopen(sock, "wb+");
		setvbuf(stream, stream_buf, _IOFBF, NETWORK_BUFFER_SIZE);
	}

	void set_nodelay() {
		const int one = 1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	}
	void set_delay() {
		const int zero = 0;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
	}

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
		// Fast path: small writes accumulate in send_buf via memcpy. Drains
		// to fwrite only when the buffer would overflow, when a recv calls
		// flush(), or at NetIO destruction.
		if (len + send_ptr <= (size_t)NETWORK_BUFFER_SIZE2) {
			memcpy(send_buf + send_ptr, data, len);
			send_ptr += len;
		} else {
			if (send_ptr) { send_raw(send_buf, send_ptr); send_ptr = 0; }
			send_raw(data, len);
		}
		has_sent = true;
	}

	void recv_data_internal(void *data, size_t len) override {
		// Drain pending sends so the peer's matching read can make progress.
		// The C standard requires fflush between writes and reads on a "wb+"
		// stream anyway; this also handles any tail bytes from prior
		// send-only protocol steps on this NetIO. fread (not raw read())
		// because raw read() bypasses stdio's direction-state machine and
		// breaks correctness in some protocol patterns even when we fflush
		// explicitly.
		flush();
		bytes_recv += len;
		size_t got = 0;
		while (got < len) {
			size_t res = fread((char *)data + got, 1, len - got, stream);
			if (res > 0) got += res;
			else { fprintf(stderr, "error: net_recv_data\n"); exit(1); }
		}
	}

private:
	void send_raw(const void *data, size_t len) {
		bytes_sent += len;
		size_t sent = 0;
		while (sent < len) {
			size_t res = fwrite((const char *)data + sent, 1, len - sent, stream);
			if (res > 0) sent += res;
			else { fprintf(stderr, "error: net_send_data\n"); exit(1); }
		}
	}

	int server_listen(int port) {
		struct sockaddr_in dest, serv;
		socklen_t socksize = sizeof(struct sockaddr_in);
		memset(&serv, 0, sizeof(serv));
		serv.sin_family = AF_INET;
		serv.sin_addr.s_addr = htonl(INADDR_ANY);
		serv.sin_port = htons(port);
		int listener = socket(AF_INET, SOCK_STREAM, 0);
		int reuse = 1;
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
		if (bind(listener, (struct sockaddr *)&serv, sizeof(struct sockaddr)) < 0) { perror("error: bind"); exit(1); }
		if (listen(listener, 1) < 0) { perror("error: listen"); exit(1); }
		int s = accept(listener, (struct sockaddr *)&dest, &socksize);
		close(listener);
		return s;
	}

	int client_connect(const char *address, int port) {
		struct sockaddr_in dest;
		memset(&dest, 0, sizeof(dest));
		dest.sin_family = AF_INET;
		dest.sin_addr.s_addr = inet_addr(address);
		dest.sin_port = htons(port);
		int s;
		while (true) {
			s = socket(AF_INET, SOCK_STREAM, 0);
			if (connect(s, (struct sockaddr *)&dest, sizeof(struct sockaddr)) == 0) break;
			close(s);
			usleep(1000);
		}
		return s;
	}
};

}  // namespace emp
#endif
