#ifndef EMP_NETWORK_IO_BUFFERED_CHANNEL_H__
#define EMP_NETWORK_IO_BUFFERED_CHANNEL_H__

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

// NetIOBuffered — bidirectional-stdio variant of NetIO. Same public
// IOChannel interface, same flush contract (see net_io_channel.h),
// different internals: one TCP fd, one stdio FILE* opened in "wb+"
// mode, fread on the recv path. C's stdio rules require fflush between
// writes and reads on a bidirectional stream, so any recv drains pending
// sends as a side effect of fread (belt+suspenders with our explicit
// flush() at the top of recv_data_internal).
//
// Pick NetIO unless you're moving multi-MiB chunks. fread's per-call
// FILE* lock dominates at small messages; on bulk transfers it
// amortizes and pulls ahead.

class NetIOBuffered : public IOChannel { public:
	int sock = -1;
	bool is_server, quiet;

	FILE *stream = nullptr;
	char *stream_buf = nullptr;     // backing store for setvbuf, lifetime tied to stream
	// Userspace coalescing buffer in front of stdio. memcpy + ptr-bump is
	// materially faster than fwrite's per-call overhead (per-FILE* lock,
	// error path, dispatch) for high-rate small writes. Drains to fwrite
	// when it fills, when a recv triggers flush(), or at destruction.
	char *send_buf = nullptr;
	size_t send_ptr = 0;
	bool has_sent = false;          // any pending writes since last fflush?

	// Telemetry.
	uint64_t bytes_sent = 0;
	uint64_t bytes_recv = 0;
	uint64_t flushes_count = 0;

	NetIOBuffered(const char *address, int port, bool quiet = false) : quiet(quiet) {
		if (port < 0 || port > 65535)
			throw std::runtime_error("Invalid port number!");

		is_server = (address == nullptr);
		init_from_sock(is_server ? server_listen(port) : client_connect(address, port));
		if (!quiet) std::cout << "connected\n";
	}

	// Wrap an already-connected socket fd. Useful for callers that run their
	// own listener / accept loop (e.g. a multi-party mesh that dispatches
	// inbound connections by peer id) and want to hand each accepted fd to a
	// NetIOBuffered without going back through bind/listen/accept.
	NetIOBuffered(int existing_sock, bool quiet = true) : quiet(quiet) {
		is_server = false;
		init_from_sock(existing_sock);
	}

	~NetIOBuffered() {
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
		// Small writes accumulate in send_buf; oversized writes flush
		// the staging buffer first then go straight to fwrite.
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
		// Drain pending sends before blocking on the peer's reply. fread
		// on a "wb+" stream with pending writes also implicitly fflushes,
		// so this is belt+suspenders.
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
