#ifndef EMP_NETWORK_IO_CHANNEL_H__
#define EMP_NETWORK_IO_CHANNEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <string>

#include "emp-tool/io/io_channel.h"
using std::string;

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace emp {

// Two-socket full-duplex NetIO. The sender side coalesces small writes into a
// 32 KiB staging buffer to amortize fwrite() per-call overhead; the receiver
// reads directly from a raw fd via read() (which accepts short reads), so
// there is no application-level read alignment and therefore no wire-level
// padding.
//
// flush() drains *outbound* data only. It never affects what the peer reads,
// so calling flush() on one party has no effect on the other party — in
// contrast to the prior split-personality design in which "flush" was both an
// outgoing drain on the sender and a stale-buffer discard on the receiver,
// requiring symmetric calls to avoid wire desync.

class SenderSubChannel { public:
	int sock;
	FILE *stream = nullptr;
	char *stream_buf = nullptr;
	char *buf = nullptr;
	size_t ptr = 0;
	uint64_t counter = 0;
	uint64_t flushes = 0;
	bool dirty = false;

	SenderSubChannel(int sock) : sock(sock) {
		stream_buf = new char[NETWORK_BUFFER_SIZE];
		buf = new char[NETWORK_BUFFER_SIZE2];
		stream = fdopen(sock, "wb");
		setvbuf(stream, stream_buf, _IOFBF, NETWORK_BUFFER_SIZE);
	}
	~SenderSubChannel() {
		fclose(stream);
		delete[] stream_buf;
		delete[] buf;
	}

	void send_data(const void *data, size_t len) {
		if (len + ptr <= (size_t)NETWORK_BUFFER_SIZE2) {
			memcpy(buf + ptr, data, len);
			ptr += len;
		} else {
			if (ptr) { send_data_raw(buf, ptr); ptr = 0; }
			send_data_raw(data, len);
		}
		dirty = true;
	}

	void flush() {
		if (!dirty) return;
		flushes++;
		if (ptr) { send_data_raw(buf, ptr); ptr = 0; }
		fflush(stream);
		dirty = false;
	}

	void send_data_raw(const void *data, size_t len) {
		counter += len;
		size_t sent = 0;
		while (sent < len) {
			size_t res = fwrite((const char *)data + sent, 1, len - sent, stream);
			if (res > 0) sent += res;
			else { fprintf(stderr, "error: net_send_data\n"); exit(1); }
		}
	}
};

class RecverSubChannel { public:
	int sock;
	char *buf = nullptr;
	size_t ptr = 0;       // next byte in staging to deliver
	size_t fill = 0;      // bytes currently in staging (ptr <= fill)
	uint64_t counter = 0;
	uint64_t flushes = 0;

	RecverSubChannel(int sock) : sock(sock) {
		buf = new char[NETWORK_BUFFER_SIZE2];
	}
	~RecverSubChannel() {
		delete[] buf;
	}

	// Raw read() (not fread) so the refill accepts whatever the kernel has
	// available — without this, the receive side would block waiting for a
	// full staging buffer's worth of bytes, and the sender would have to pad
	// the wire to keep us alive (the prior design's flush-as-wire-protocol
	// footgun).
	void recv_data(void *data, size_t len) {
		counter += len;
		size_t got = 0;
		while (got < len) {
			if (ptr == fill) {
				ssize_t n = ::read(sock, buf, NETWORK_BUFFER_SIZE2);
				if (n <= 0) { fprintf(stderr, "error: net_recv_data\n"); exit(1); }
				ptr = 0;
				fill = (size_t)n;
			}
			size_t take = fill - ptr;
			if (take > len - got) take = len - got;
			memcpy((char *)data + got, buf + ptr, take);
			ptr += take;
			got += take;
		}
	}
};

class NetIO : public IOChannel { public:
	bool is_server, quiet;
	int send_sock = -1;
	int recv_sock = -1;
	string addr;
	int port;
	SenderSubChannel *schannel = nullptr;
	RecverSubChannel *rchannel = nullptr;

	// Single-port convenience: opens two sockets on `port` and `port + 1`.
	NetIO(const char *address, int port, bool quiet = false)
	    : NetIO(address, port, port + 1, quiet) {}

	// Explicit two-port constructor for firewall-constrained deployments.
	NetIO(const char *address, int send_port, int recv_port, bool quiet)
	    : quiet(quiet), port(send_port) {
		if (send_port < 0 || send_port > 65535) throw std::runtime_error("Invalid send port number!");
		if (recv_port < 0 || recv_port > 65535) throw std::runtime_error("Invalid receive port number!");

		is_server = (address == nullptr);
		if (is_server) {
			recv_sock = server_listen(send_port);
			usleep(2000);
			send_sock = server_listen(recv_port);
		} else {
			addr = string(address);
			send_sock = client_connect(address, send_port);
			recv_sock = client_connect(address, recv_port);
		}
		set_nodelay();
		schannel = new SenderSubChannel(send_sock);
		rchannel = new RecverSubChannel(recv_sock);
		if (!quiet) std::cout << "connected\n";
	}

	~NetIO() {
		if (schannel) schannel->flush();
		if (!quiet && schannel && rchannel) {
			std::cout << "Data Sent: \t" << schannel->counter << "\n";
			std::cout << "Data Received: \t" << rchannel->counter << "\n";
			std::cout << "Flushes:\t" << schannel->flushes << "\t" << rchannel->flushes << "\n";
		}
		delete schannel;
		delete rchannel;
		// schannel's fclose closed send_sock via fdopen; recv path uses raw
		// fd, so close that explicitly.
		if (recv_sock >= 0) close(recv_sock);
	}

	int server_listen(int port) {
		struct sockaddr_in dest, serv;
		socklen_t socksize = sizeof(struct sockaddr_in);
		memset(&serv, 0, sizeof(serv));
		serv.sin_family = AF_INET;
		serv.sin_addr.s_addr = htonl(INADDR_ANY);
		serv.sin_port = htons(port);
		int mysocket = socket(AF_INET, SOCK_STREAM, 0);
		int reuse = 1;
		setsockopt(mysocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
		if (bind(mysocket, (struct sockaddr *)&serv, sizeof(struct sockaddr)) < 0) { perror("error: bind"); exit(1); }
		if (listen(mysocket, 1) < 0) { perror("error: listen"); exit(1); }
		int sock = accept(mysocket, (struct sockaddr *)&dest, &socksize);
		close(mysocket);
		return sock;
	}

	int client_connect(const char *address, int port) {
		struct sockaddr_in dest;
		memset(&dest, 0, sizeof(dest));
		dest.sin_family = AF_INET;
		dest.sin_addr.s_addr = inet_addr(address);
		dest.sin_port = htons(port);
		int sock;
		while (1) {
			sock = socket(AF_INET, SOCK_STREAM, 0);
			if (connect(sock, (struct sockaddr *)&dest, sizeof(struct sockaddr)) == 0) break;
			close(sock);
			usleep(1000);
		}
		return sock;
	}

	void set_nodelay() {
		const int one = 1;
		setsockopt(send_sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		setsockopt(recv_sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	}

	void set_delay() {
		const int zero = 0;
		setsockopt(send_sock, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
		setsockopt(recv_sock, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
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

	void flush() { schannel->flush(); }

	void send_data_internal(const void *data, size_t len) override {
		schannel->send_data(data, len);
	}

	void recv_data_internal(void *data, size_t len) override {
		// Drain my outgoing before blocking on the peer's reply, otherwise
		// any request/response pattern would deadlock with our request still
		// staged.
		schannel->flush();
		rchannel->recv_data(data, len);
	}
};

}  // namespace emp
#endif
