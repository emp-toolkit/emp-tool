#ifndef EMP_TCP_SOCKET_H__
#define EMP_TCP_SOCKET_H__


#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "emp-tool/runtime/core/error.h"

namespace emp {
namespace tcp {

// Socket buffer sizes, applied before listen() / connect() so that TCP window
// scaling is negotiated for them. A size of 0 leaves the buffer to the
// operating system, which grows it with the connection (by default to 4 MiB or
// more on Linux and macOS). An explicit size fixes the buffer, and on Linux it
// also switches that growth off for the socket.
struct SocketOptions {
	int send_buffer_size = 0;
	int receive_buffer_size = 0;

	// The path the sizes were chosen for, kept so that a warning can say why;
	// set by for_bandwidth_and_rtt, 0 when the sizes are set by hand.
	std::uint64_t bandwidth_bits_per_second = 0;
	std::chrono::microseconds round_trip_time{0};

	// Bandwidth-delay products up to this size are left to the operating
	// system, which grows the buffers that far on its own.
	static constexpr std::uint64_t kOsSizedLimit = 4 * 1024 * 1024;

	// Buffers for a path of the given bandwidth and round-trip time: none
	// (sizes 0) when the bandwidth-delay product is at most kOsSizedLimit,
	// otherwise the first power of two at or above it, for both directions.
	static SocketOptions for_bandwidth_and_rtt(
	    std::uint64_t bandwidth_bits_per_second,
	    std::chrono::microseconds round_trip_time) {
		expecting(bandwidth_bits_per_second > 0,
		          "tcp: bandwidth must be positive");
		expecting(round_trip_time.count() > 0,
		          "tcp: round-trip time must be positive");

		constexpr std::uint64_t byte_scale = 8'000'000;
		constexpr std::uint64_t maximum_buffer =
		    static_cast<std::uint64_t>(std::numeric_limits<int>::max());
		const auto rtt_microseconds =
		    static_cast<std::uint64_t>(round_trip_time.count());
		const auto maximum_product = maximum_buffer * byte_scale;
		expecting(bandwidth_bits_per_second <=
		              maximum_product / rtt_microseconds,
		          "tcp: bandwidth-delay product exceeds socket buffer range");

		SocketOptions options;
		options.bandwidth_bits_per_second = bandwidth_bits_per_second;
		options.round_trip_time = round_trip_time;
		const auto product = bandwidth_bits_per_second * rtt_microseconds;
		const auto bdp_bytes = (product + byte_scale - 1) / byte_scale;
		if (bdp_bytes <= kOsSizedLimit) return options;

		auto buffer_size = 2 * kOsSizedLimit;
		while (buffer_size < bdp_bytes) {
			expecting(buffer_size <= maximum_buffer / 2,
			          "tcp: rounded bandwidth-delay product exceeds socket buffer range");
			buffer_size *= 2;
		}
		options.send_buffer_size = static_cast<int>(buffer_size);
		options.receive_buffer_size = static_cast<int>(buffer_size);
		return options;
	}
};

namespace detail {

inline std::string format_bytes(std::uint64_t n) {
	if (n >= (1u << 30) && n % (1u << 30) == 0)
		return std::to_string(n >> 30) + " GiB";
	if (n >= (1u << 20) && n % (1u << 20) == 0)
		return std::to_string(n >> 20) + " MiB";
	if (n >= (1u << 10) && n % (1u << 10) == 0)
		return std::to_string(n >> 10) + " KiB";
	return std::to_string(n) + " bytes";
}

inline std::string format_bandwidth(std::uint64_t bits_per_second) {
	if (bits_per_second % 1'000'000'000 == 0)
		return std::to_string(bits_per_second / 1'000'000'000) + " Gbit/s";
	if (bits_per_second % 1'000'000 == 0)
		return std::to_string(bits_per_second / 1'000'000) + " Mbit/s";
	return std::to_string(bits_per_second) + " bit/s";
}

inline std::string format_rtt(std::chrono::microseconds rtt) {
	if (rtt.count() % 1000 == 0)
		return std::to_string(rtt.count() / 1000) + " ms";
	return std::to_string(rtt.count()) + " us";
}

// The largest buffer the system grants a plain SO_SNDBUF / SO_RCVBUF
// request, or -1 if it cannot be read.
inline std::int64_t socket_buffer_limit(int option) {
#if defined(__linux__)
	std::FILE *file = std::fopen(option == SO_SNDBUF
	                                 ? "/proc/sys/net/core/wmem_max"
	                                 : "/proc/sys/net/core/rmem_max", "r");
	if (file == nullptr) return -1;
	long long limit = -1;
	const int read = std::fscanf(file, "%lld", &limit);
	std::fclose(file);
	return read == 1 ? limit : -1;
#elif defined(__APPLE__)
	(void)option;
	unsigned char value[sizeof(std::uint64_t)] = {};
	size_t length = sizeof(value);
	if (::sysctlbyname("kern.ipc.maxsockbuf", value, &length, nullptr, 0) != 0)
		return -1;
	if (length == sizeof(std::uint32_t)) {
		std::uint32_t limit;
		std::memcpy(&limit, value, sizeof(limit));
		return limit;
	}
	if (length == sizeof(std::uint64_t)) {
		std::uint64_t limit;
		std::memcpy(&limit, value, sizeof(limit));
		return static_cast<std::int64_t>(limit);
	}
	return -1;
#else
	(void)option;
	return -1;
#endif
}

// Says once per process that the requested buffers are above the system
// limit, what was done instead (the operating system's defaults kept, or the
// buffers lowered to the limit), and the command that raises the limit.
inline void warn_socket_buffer_limit(const SocketOptions &options,
                                     std::int64_t limit, bool lowered) {
	static std::atomic<bool> warned{false};
	if (warned.exchange(true)) return;
	const std::uint64_t wanted = static_cast<std::uint64_t>(
	    options.send_buffer_size > options.receive_buffer_size
	        ? options.send_buffer_size : options.receive_buffer_size);
	const std::string allowed = limit >= 0
	    ? format_bytes(static_cast<std::uint64_t>(limit)) : std::string();
	std::string message = "emp-tool: warning: ";
	if (options.bandwidth_bits_per_second != 0)
		message += "for the " + format_bandwidth(options.bandwidth_bits_per_second) +
		           " bandwidth and " + format_rtt(options.round_trip_time) +
		           " round-trip time you gave, " + format_bytes(wanted) +
		           " socket buffers would help throughput, but this system allows " +
		           (allowed.empty() ? std::string("less") : allowed);
	else
		message += "the requested " + format_bytes(wanted) +
		           " socket buffers are above what this system allows" +
		           (allowed.empty() ? std::string() : " (" + allowed + ")");
	message += lowered ? ", so " + allowed + " buffers are used"
	                   : ", so the operating system's defaults are kept";
	message += ". To allow " + format_bytes(wanted) + " buffers, run:\n  ";
#if defined(__APPLE__)
	message += "sudo sysctl -w kern.ipc.maxsockbuf=" + std::to_string(wanted);
#else
	message += "sudo sysctl -w net.core.rmem_max=" + std::to_string(wanted) +
	           " net.core.wmem_max=" + std::to_string(wanted);
#endif
	std::fprintf(stderr, "%s\n", message.c_str());
}

// What the kernel gives the socket for `option`.
inline int provided_socket_buffer(int sock, int option) {
	int size = 0;
	socklen_t length = sizeof(size);
	if (::getsockopt(sock, SOL_SOCKET, option, &size, &length) != 0) return -1;
#ifdef __linux__
	size /= 2;  // Linux reports twice the size it was asked for.
#endif
	return size;
}

// Sets one buffer. Both Linux and macOS cap a request above the system limit
// silently, and an explicit size also stops the operating system from growing
// the buffer. So a request above the limit is lowered to the limit when that is
// still more than the operating system reaches on its own, and otherwise not made;
// either way one warning per process says how to raise the limit.
inline void apply_socket_buffer(int sock, int option, int requested,
                                const SocketOptions &options, const char *name) {
	expecting(requested >= 0, [&] {
		return std::string("tcp: ") + name + " cannot be negative";
	});
	if (requested == 0) return;
	const std::int64_t limit = socket_buffer_limit(option);
	if (limit >= 0 && requested > limit) {
		const bool lowered =
		    static_cast<std::uint64_t>(limit) > SocketOptions::kOsSizedLimit;
		warn_socket_buffer_limit(options, limit, lowered);
		if (!lowered) return;
		requested = static_cast<int>(limit);
	}
	if (::setsockopt(sock, SOL_SOCKET, option, &requested,
	                 sizeof(requested)) != 0) {
		// Older macOS releases refuse a request above kern.ipc.maxsockbuf.
		expecting(errno == ENOBUFS, [&] {
			return std::string("tcp: setsockopt(") + name + "): " +
			       std::strerror(errno);
		});
		warn_socket_buffer_limit(options, limit, /*lowered=*/false);
		return;
	}
	const int provided = provided_socket_buffer(sock, option);
	if (provided >= 0 && provided < requested)
		warn_socket_buffer_limit(options, provided, /*lowered=*/true);
}

}  // namespace detail

inline void apply_socket_options(int sock, const SocketOptions &options) {
	detail::apply_socket_buffer(sock, SO_SNDBUF, options.send_buffer_size,
	                            options, "SO_SNDBUF");
	detail::apply_socket_buffer(sock, SO_RCVBUF, options.receive_buffer_size,
	                            options, "SO_RCVBUF");
}

// Bind and listen without accepting. Related server NetIO channels share this
// descriptor so repeated sibling connections use the same listener.
inline int open_listener(int port, const SocketOptions &options) {
	struct sockaddr_in serv;
	std::memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = htonl(INADDR_ANY);
	serv.sin_port = htons(port);
	int listener = ::socket(AF_INET, SOCK_STREAM, 0);
	expecting(listener >= 0, [&] {
		return std::string("tcp: socket: ") + std::strerror(errno);
	});
	apply_socket_options(listener, options);
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

inline int open_listener(int port) {
	return open_listener(port, SocketOptions{});
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
inline int server_listen(int port, const SocketOptions &options) {
	int listener = open_listener(port, options);
	int s = accept_one(listener);  // inherits the listener's options
	::close(listener);
	return s;
}

inline int server_listen(int port) {
	return server_listen(port, SocketOptions{});
}

// Connect to address:port, retrying on failure with a 1 ms backoff so
// the server side has time to come up. Capped at ~60 s of total retry
// time to catch a permanently-down peer instead of hanging the caller.
// When wait_for_accept is set, a connect() that landed on a listen queue
// but was not acknowledged (kAcceptAck) — because it reached a stale
// listener the server no longer accepts on — is treated as a failure and
// retried, so "connected" always means the peer's accept() ran.
inline int client_connect_impl(const char *address, int port,
                               bool wait_for_accept,
	                           const SocketOptions &options) {
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
		apply_socket_options(s, options);
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

inline int client_connect_impl(const char *address, int port,
	                           bool wait_for_accept) {
	return client_connect_impl(address, port, wait_for_accept, SocketOptions{});
}

// Plain connect: returns as soon as the TCP handshake completes (the peer may
// not have accept()ed yet). Used by transports with their own post-connect
// handshake, e.g. TLSIO.
inline int client_connect(const char *address, int port,
	                      const SocketOptions &options) {
	return client_connect_impl(address, port, /*wait_for_accept=*/false, options);
}

inline int client_connect(const char *address, int port) {
	return client_connect(address, port, SocketOptions{});
}

// Connect and wait for the server's accept acknowledgement — safe against a
// connection landing on a stale listener (see kAcceptAck). Used by NetIO.
inline int client_connect_confirmed(const char *address, int port,
	                                const SocketOptions &options) {
	return client_connect_impl(address, port, /*wait_for_accept=*/true, options);
}

inline int client_connect_confirmed(const char *address, int port) {
	return client_connect_confirmed(address, port, SocketOptions{});
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
