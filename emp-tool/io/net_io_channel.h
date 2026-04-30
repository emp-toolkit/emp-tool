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

// Single-socket full-duplex NetIO. One TCP fd carries both directions; the
// kernel handles bidirectional traffic transparently.
//
// The userspace half-duplex stall in the original NetIO came from sharing one
// stdio FILE* (fdopen mode "wb+") between reads and writes — its bidir buffer
// requires fflush before each direction reversal. We sidestep that by giving
// the write side a "wb"-mode stdio FILE* (with a 1 MiB stdio buffer plus a
// 32 KiB application-level coalescing buffer) and the read side a raw read()
// path with its own 32 KiB staging. The two paths share the same fd but no
// libc-level state, so neither blocks the other.
//
// flush() drains *outbound* data only. It never affects what the peer reads,
// so calling flush() on one party has no effect on the other party — in
// contrast to the prior split-personality design where "flush" was both an
// outgoing drain and a stale-buffer discard, requiring symmetric calls to
// avoid wire desync.

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

	NetIO(const char *address, int port, bool quiet = false) : quiet(quiet) {
		if (port < 0 || port > 65535)
			throw std::runtime_error("Invalid port number!");

		is_server = (address == nullptr);
		sock = is_server ? server_listen(port) : client_connect(address, port);
		set_nodelay();

		stream_buf = new char[NETWORK_BUFFER_SIZE];
		send_buf   = new char[NETWORK_BUFFER_SIZE2];
		recv_buf   = new char[NETWORK_BUFFER_SIZE2];
		stream = fdopen(sock, "wb");
		setvbuf(stream, stream_buf, _IOFBF, NETWORK_BUFFER_SIZE);

		if (!quiet) std::cout << "connected\n";
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

	void flush() {
		if (!send_dirty) return;
		++flushes_count;
		if (send_ptr) { send_raw(send_buf, send_ptr); send_ptr = 0; }
		fflush(stream);
		send_dirty = false;
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
	void sync() {
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
		// Drain my outgoing before blocking on the peer's reply, otherwise
		// any request/response pattern would deadlock with our request still
		// staged.
		flush();
		bytes_recv += len;
		size_t got = 0;
		while (got < len) {
			if (recv_ptr == recv_fill) {
				// Raw read() (not fread) so the refill accepts whatever the
				// kernel has available — fread would block waiting for a
				// full staging buffer's worth of bytes and force the sender
				// to pad the wire to keep us alive.
				ssize_t n = ::read(sock, recv_buf, NETWORK_BUFFER_SIZE2);
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
