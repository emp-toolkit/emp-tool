#ifndef EMP_TCP_SOCKET_H__
#define EMP_TCP_SOCKET_H__


#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace emp {
namespace tcp {

// Bind, listen, accept once, return the accepted fd. Closes the
// listener after a single accept — every transport here is one-channel-
// per-port. Fail-fast on bind/listen errors.
inline int server_listen(int port) {
	struct sockaddr_in dest, serv;
	socklen_t socksize = sizeof(struct sockaddr_in);
	std::memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = htonl(INADDR_ANY);
	serv.sin_port = htons(port);
	int listener = ::socket(AF_INET, SOCK_STREAM, 0);
	int reuse = 1;
	::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
	if (::bind(listener, (struct sockaddr *)&serv, sizeof(struct sockaddr)) < 0) {
		std::perror("error: bind");
		std::exit(1);
	}
	if (::listen(listener, 1) < 0) {
		std::perror("error: listen");
		std::exit(1);
	}
	int s = ::accept(listener, (struct sockaddr *)&dest, &socksize);
	::close(listener);
	return s;
}

// Connect to address:port, retrying on failure with a 1 ms backoff so
// the server side has time to come up. Retries forever — callers are
// expected to time out at a higher level if needed.
inline int client_connect(const char *address, int port) {
	struct sockaddr_in dest;
	std::memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = ::inet_addr(address);
	dest.sin_port = htons(port);
	int s;
	while (true) {
		s = ::socket(AF_INET, SOCK_STREAM, 0);
		if (::connect(s, (struct sockaddr *)&dest, sizeof(struct sockaddr)) == 0) break;
		::close(s);
		::usleep(1000);
	}
	return s;
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
