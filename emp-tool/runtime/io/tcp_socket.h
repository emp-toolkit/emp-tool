#ifndef EMP_TCP_SOCKET_H__
#define EMP_TCP_SOCKET_H__


#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "emp-tool/runtime/core/error.h"

namespace emp {
namespace tcp {

// Bind and listen without accepting. Related server NetIO channels share this
// descriptor so repeated sibling connections use the same listener.
inline int open_listener(int port) {
	struct sockaddr_in serv;
	std::memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = htonl(INADDR_ANY);
	serv.sin_port = htons(port);
	int listener = ::socket(AF_INET, SOCK_STREAM, 0);
	expecting(listener >= 0, [&] {
		return std::string("tcp: socket: ") + std::strerror(errno);
	});
	int reuse = 1;
	::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
	expecting(::bind(listener, (struct sockaddr *)&serv,
	                 sizeof(struct sockaddr)) >= 0, [&] {
		return std::string("tcp: bind: ") + std::strerror(errno);
	});
	// Backlog must hold every not-yet-accepted connection: a NetIO listener
	// is shared across sibling channels (make_sibling), so under load the
	// server can fall behind while several clients connect. A backlog of 1
	// overflows there, and an overflowed connection is dropped/reset — the
	// client sees a completed connect while the server never accepts it,
	// crossing the accept/connect pairing (peer-closed or deadlock).
	expecting(::listen(listener, SOMAXCONN) >= 0, [&] {
		return std::string("tcp: listen: ") + std::strerror(errno);
	});
	return listener;
}

// Shared RAII owner for a listening socket. NetIO siblings share one handle so
// any related channel can accept another sibling and the listener closes when
// the last related server channel is destroyed.
class ListenerHandle {
public:
	explicit ListenerHandle(int fd) : fd(fd) {}
	~ListenerHandle() { if (fd >= 0) ::close(fd); }

	ListenerHandle(const ListenerHandle&) = delete;
	ListenerHandle& operator=(const ListenerHandle&) = delete;

	int fd;
};

// Application-level "the peer's accept() actually ran" marker. A completed
// TCP connect() only proves the connection reached A listen queue — possibly
// a stale listener the server has stopped accepting on (e.g. a fresh session
// reusing the same port). The server sends this byte right after accept(); the
// client waits for it before treating the connection as established.
inline constexpr unsigned char kAcceptAck = 0xa5;

// Accept one connection from an existing listener, retrying an interrupted
// system call. The caller decides whether to keep or close the listener.
inline int accept_one(int listener) {
	struct sockaddr_in peer;
	socklen_t peer_size = sizeof(peer);
	int s;
	do {
		s = ::accept(listener, (struct sockaddr *)&peer, &peer_size);
	} while (s < 0 && errno == EINTR);
	expecting(s >= 0, [&] {
		return std::string("tcp: accept: ") + std::strerror(errno);
	});
	return s;
}

// accept_one plus a one-byte application acknowledgement, so the client can
// confirm this accept() actually ran (see kAcceptAck). Raw ::send, so it does
// not touch IOChannel byte counters or any Fiat-Shamir transcript.
inline int accept_one_confirmed(int listener) {
	int s = accept_one(listener);
	ssize_t n;
	do {
		n = ::send(s, &kAcceptAck, 1, 0);
	} while (n < 0 && errno == EINTR);
	expecting(n == 1, "tcp: failed to acknowledge accepted connection");
	return s;
}

// One-shot compatibility helper used by transports that need only one
// connection, such as TLSIO.
inline int server_listen(int port) {
	int listener = open_listener(port);
	int s = accept_one(listener);
	::close(listener);
	return s;
}

// Connect to address:port, retrying on failure with a 1 ms backoff so
// the server side has time to come up. Capped at ~60 s of total retry
// time to catch a permanently-down peer instead of hanging the caller.
// When wait_for_accept is set, a connect() that landed on a listen queue
// but was not acknowledged (kAcceptAck) — because it reached a stale
// listener the server no longer accepts on — is treated as a failure and
// retried, so "connected" always means the peer's accept() ran.
inline int client_connect_impl(const char *address, int port,
                               bool wait_for_accept) {
	struct sockaddr_in dest;
	std::memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = ::inet_addr(address);
	dest.sin_port = htons(port);
	const int max_retries = 60000;  // 60 s at 1 ms backoff
	for (int attempt = 0; attempt < max_retries; ++attempt) {
		int s = ::socket(AF_INET, SOCK_STREAM, 0);
		expecting(s >= 0, [&] {
			return std::string("tcp: socket: ") + std::strerror(errno);
		});
		if (::connect(s, (struct sockaddr *)&dest, sizeof(struct sockaddr)) == 0) {
			if (!wait_for_accept)
				return s;
			// Raw ::recv, so the ack is not counted by any IOChannel.
			unsigned char ack = 0;
			ssize_t n;
			do {
				n = ::recv(s, &ack, 1, 0);
			} while (n < 0 && errno == EINTR);
			if (n == 1 && ack == kAcceptAck)
				return s;
		}
		::close(s);
		::usleep(1000);
	}
	const std::string msg = "client_connect: " + std::string(address) + ":" +
	                        std::to_string(port) + " unreachable after " +
	                        std::to_string(max_retries) + " attempts";
	error(msg.c_str());
}

// Plain connect: returns as soon as the TCP handshake completes (the peer may
// not have accept()ed yet). Used by transports with their own post-connect
// handshake, e.g. TLSIO.
inline int client_connect(const char *address, int port) {
	return client_connect_impl(address, port, /*wait_for_accept=*/false);
}

// Connect and wait for the server's accept acknowledgement — safe against a
// connection landing on a stale listener (see kAcceptAck). Used by NetIO.
inline int client_connect_confirmed(const char *address, int port) {
	return client_connect_impl(address, port, /*wait_for_accept=*/true);
}

inline void set_nodelay(int sock) {
	const int one = 1;
	::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

inline void set_delay(int sock) {
	const int zero = 0;
	::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
}

}  // namespace tcp
}  // namespace emp

#endif
