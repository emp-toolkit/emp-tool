#ifndef EMP_NETWORK_IO_CHANNEL_H__
#define EMP_NETWORK_IO_CHANNEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <memory>
#include <string>

#include "emp-tool/runtime/core/utils.h"   // error()
#include "emp-tool/runtime/crypto/prg.h"
#include "emp-tool/runtime/crypto/ro.h"
#include "emp-tool/runtime/io/io_channel.h"
#include "emp-tool/runtime/io/tcp_socket.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

namespace emp {

// Single-socket full-duplex NetIO. One TCP fd carries both directions.
// Send side: "wb" stdio FILE* (1 MiB stream buffer) with a 32 KiB
// user-space coalescing buffer in front of fwrite. Recv side: raw
// ::read() into its own 32 KiB staging buffer, no stdio. The two paths
// share the fd but no libc-level state.
//
// Transfers of NETWORK_DIRECT_IO_SIZE bytes or more skip the buffers, and
// every path keeps the bytes in the order they were sent. Bytes waiting in
// stdio are always older than the staged ones, so a large send first has
// stdio write out what it holds and then hands the staged bytes and the
// caller's buffer to the socket in one writev. A large receive first takes
// the bytes already staged, then reads straight into the caller's buffer,
// never more than it still asks for.
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

class NetIO : public IOChannel {
public:
	// Byte counts and flush count live on the IOChannel base.
	// NOT thread-safe (unlocked send-buffer coalescing): one thread at a
	// time per channel; threaded consumers take a sibling channel each
	// (make_sibling). Races are not detected at runtime — use TSan.

	// Whether a connecting constructor agrees on a session root with the peer
	// (see next_session_id()). coin_flip, the default, adds one round trip to
	// connection setup; none leaves the channel, and its siblings, without a
	// root. Both ends must choose the same. The listener's accept
	// acknowledgement says which it chose, so on a mismatch the connector
	// stops with an error at connect, and the listener fails at its next
	// read, or at a later write if it only sends.
	enum class SessionRoot { coin_flip, none };

	NetIO(const char *address, int port, bool quiet = false,
	      SessionRoot session_root = SessionRoot::coin_flip)
	    : NetIO(address, port, tcp::SocketOptions{}, quiet, session_root) {}

	NetIO(const char *address, int port,
	      const tcp::SocketOptions &socket_options, bool quiet = false,
	      SessionRoot session_root = SessionRoot::coin_flip)
	    : quiet(quiet), socket_options_(socket_options) {
		expecting(port >= 0 && port <= 65535,
		          "NetIO: invalid port number");

		is_server = (address == nullptr);
		addr_ = address ? address : "";
		port_ = port;
		const unsigned char ack = session_root == SessionRoot::coin_flip
		                              ? tcp::kAcceptAckSessionRoot
		                              : tcp::kAcceptAck;
		if (is_server) {
			listener = std::make_shared<tcp::ListenerHandle>(
			    tcp::open_listener(port, socket_options_));
			init_from_sock(tcp::accept_one_confirmed(listener->fd, ack));
		} else {
			unsigned char listener_ack = 0;
			const int sock = tcp::client_connect_confirmed(
			    address, port, socket_options_, &listener_ack);
			if (listener_ack != ack) {
				::close(sock);
				error(listener_ack == tcp::kAcceptAckSessionRoot
				          ? "NetIO: the listener runs the session-root coin flip, "
				            "but this end has SessionRoot::none"
				          : "NetIO: this end runs the session-root coin flip, but "
				            "the listener does not (it has SessionRoot::none, or "
				            "is an earlier emp-tool release)");
			}
			init_from_sock(sock);
		}
		if (session_root == SessionRoot::coin_flip)
			set_session_root(flip_session_root());
		if (!quiet) std::cout << "connected\n";
	}

	// Named factories owning their result (auto-freed via unique_ptr). The role
	// is explicit in the name and the signature — listen() takes no address;
	// connect() requires one — an explicit alternative to the
	// nullptr-means-server sentinel of the (const char*, int) constructor.
	static std::unique_ptr<NetIO> listen(
	    int port, bool quiet = false,
	    SessionRoot session_root = SessionRoot::coin_flip) {
		return std::make_unique<NetIO>(nullptr, port, quiet, session_root);
	}
	static std::unique_ptr<NetIO> listen(
	    int port, const tcp::SocketOptions &socket_options, bool quiet = false,
	    SessionRoot session_root = SessionRoot::coin_flip) {
		return std::make_unique<NetIO>(nullptr, port, socket_options, quiet,
		                               session_root);
	}
	static std::unique_ptr<NetIO> connect(
	    const char *address, int port, bool quiet = false,
	    SessionRoot session_root = SessionRoot::coin_flip) {
		return std::make_unique<NetIO>(address, port, quiet, session_root);
	}
	static std::unique_ptr<NetIO> connect(
	    const char *address, int port, const tcp::SocketOptions &socket_options,
	    bool quiet = false, SessionRoot session_root = SessionRoot::coin_flip) {
		return std::make_unique<NetIO>(address, port, socket_options, quiet,
		                               session_root);
	}

	// Open another channel to the same peer and port. Related server channels
	// share the listener, so make_sibling() may be called repeatedly on the
	// primary or any sibling. The client makes another connection to the same
	// port. The listener closes when the last related server NetIO is destroyed.
	// When this channel has a session root, the sibling's root is this
	// channel's next session id, so the sibling runs no coin flip of its own;
	// otherwise the sibling has no root either.
	std::unique_ptr<NetIO> make_sibling() {
		std::unique_ptr<NetIO> sibling;
		if (!is_server) {
			sibling = connect(addr_.c_str(), port_, socket_options_,
			                  /*quiet=*/true, SessionRoot::none);
		} else {
			expecting(listener != nullptr,
			          "NetIO::make_sibling requires a server listener");
			// The accepted socket inherits the listener's options. Siblings run
			// no coin flip, so the acknowledgement says none follows.
			int sibling_sock =
			    tcp::accept_one_confirmed(listener->fd, tcp::kAcceptAck);

			sibling = std::make_unique<NetIO>(sibling_sock, /*quiet=*/true);
			sibling->is_server = true;  // so make_sibling() on it accepts too
			sibling->port_ = port_;
			sibling->socket_options_ = socket_options_;
			sibling->listener = listener;
		}
		if (has_session_root())
			sibling->set_session_root(next_session_id().value());
		return sibling;
	}

	// Wrap an already-connected socket fd, for callers that run their
	// own accept loop and want to skip bind/listen/accept. The channel does
	// not know which end of the connection it is, so it has no session root.
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
		flush_unlocked();
	}

	void set_nodelay() { tcp::set_nodelay(sock); }
	void set_delay()   { tcp::set_delay(sock); }

	// The connected socket, for socket options and diagnostics. Reading or
	// writing it directly bypasses the channel's buffers and corrupts the
	// stream.
	int fd() const { return sock; }
	// A server channel's listening socket (shared with its siblings), or -1.
	int listener_fd() const { return listener ? listener->fd : -1; }

protected:
	void send_data_internal(const void *data, int64_t len) override {
		expecting(len >= 0, "NetIO::send_data: negative len");
		if (len == 0) return;
		if (len < NETWORK_DIRECT_IO_SIZE) {
			if (len <= (int64_t)(NETWORK_STAGING_BUFFER_SIZE - send_ptr)) {
				memcpy(send_buf + send_ptr, data, len);
				send_ptr += len;
			} else {
				if (send_ptr) { send_raw(send_buf, send_ptr); send_ptr = 0; }
				send_raw(data, len);
			}
		} else {
			send_direct(static_cast<const char *>(data), len);
		}
		send_dirty = true;
	}

	void recv_data_internal(void *data, int64_t len) override {
		expecting(len >= 0, "NetIO::recv_data: negative len");
		if (len == 0) return;
		// Drain pending sends before blocking on the peer's reply, else
		// any send-then-recv pattern would deadlock with our bytes still
		// staged. Raw ::read() bypasses stdio, so this has to be explicit.
		flush_unlocked();
		// A receive of NETWORK_DIRECT_IO_SIZE bytes or more reads past the
		// staging buffer for the whole call: once the staged bytes are taken,
		// every read lands in the caller's buffer and asks for no more than
		// the call still needs, so no byte past this message leaves the socket.
		const bool direct = len >= NETWORK_DIRECT_IO_SIZE;
		int64_t got = 0;
		while (got < len) {
			if (recv_ptr == recv_fill) {
				if (direct) {
					// Nothing is staged, so the next bytes on the socket are the
					// caller's: read them into its buffer, at most len - got.
					got += read_direct((char *)data + got, len - got);
					continue;
				}
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
				expecting(n > 0,
				          "net_recv_data (peer closed or read error)");
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
	int sock = -1;
	std::shared_ptr<tcp::ListenerHandle> listener;  // shared by related server channels
	bool is_server, quiet;
	// Endpoint info retained so a duplex sibling can be spawned (make_sibling).
	std::string addr_;              // peer address (empty when this is a server)
	int port_ = -1;
	tcp::SocketOptions socket_options_;

	// Send-side state (stdio "wb" stream + app-level coalescing buffer).
	FILE *stream = nullptr;
	char *stream_buf = nullptr;     // backing store for setvbuf, lifetime tied to stream
	char *send_buf = nullptr;       // 32 KiB coalescing staging
	size_t send_ptr = 0;
	bool send_dirty = false;

	// The most one direct read() or writev() takes from the caller's buffer.
	// With the staged bytes a writev also carries, a call stays below the
	// 2^31-byte limit some systems put on one read or write.
	static constexpr int64_t kMaxDirectIo = int64_t{1} << 30;

	// Recv-side state (raw read() into a 32 KiB staging buffer).
	char *recv_buf = nullptr;
	size_t recv_ptr = 0;            // next byte to deliver to the caller
	size_t recv_fill = 0;           // bytes available in recv_buf

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

	// The session root, agreed by a commit-then-reveal coin flip as soon as
	// the connection is up. Like sync(), it runs on the transport hooks, so its
	// bytes are not counted and not in the Fiat-Shamir transcript. s, r and r'
	// are fresh random blocks, and the commitment is a 32-byte digest:
	//
	//   listener -> connector   c = RO(s ‖ r)
	//   connector -> listener   r'
	//   listener -> connector   s ‖ r          (checked against c)
	//   root = RO(r ‖ r')
	//
	// The commitment follows the listener's accept acknowledgement, so the flip
	// adds one round trip to setup on both ends. The root is uniform when at
	// least one party follows the protocol: the listener is bound to r before
	// seeing r', and the connector picks r' without seeing r. The listener
	// learns the root first and can abort instead of opening, the bias any
	// two-party coin flip allows. The flip does not authenticate the peer: on
	// an unauthenticated network, a man in the middle runs one flip with each
	// party.
	block flip_session_root() {
		static constexpr const char *kCommit = "emp-tool:session-id:commit";
		block opening[2];   // s, r
		block r_prime;
		unsigned char commitment[Hash::DIGEST_SIZE];
		if (is_server) {
			PRG().random_block(opening, 2);
			RO(kCommit, zero_block).absorb(opening[0]).absorb(opening[1])
			    .squeeze_digest(commitment);
			send_data_internal(commitment, sizeof(commitment));
			recv_data_internal(&r_prime, sizeof(r_prime));
			send_data_internal(opening, sizeof(opening));
			flush_unlocked();
		} else {
			recv_data_internal(commitment, sizeof(commitment));
			PRG().random_block(&r_prime, 1);
			send_data_internal(&r_prime, sizeof(r_prime));
			recv_data_internal(opening, sizeof(opening));
			unsigned char expected[Hash::DIGEST_SIZE];
			RO(kCommit, zero_block).absorb(opening[0]).absorb(opening[1])
			    .squeeze_digest(expected);
			expecting(std::memcmp(commitment, expected, sizeof(expected)) == 0,
			          "NetIO: the listener's session-root opening does not match "
			          "its commitment");
		}
		return RO("emp-tool:session-id:root", zero_block)
		    .absorb(opening[1]).absorb(r_prime).squeeze_block();
	}

	void flush_unlocked() {
		if (!send_dirty) return;
		++flushes_count;
		if (send_ptr) { send_raw(send_buf, send_ptr); send_ptr = 0; }
		flush_stream();
		send_dirty = false;
	}

	void flush_stream() {
		expecting(::fflush(stream) == 0, [&] {
			return std::string("NetIO: fflush failed: ") + std::strerror(errno);
		});
	}

	void send_raw(const void *data, size_t len) {
		size_t sent = 0;
		while (sent < len) {
			size_t res = fwrite((const char *)data + sent, 1, len - sent, stream);
			// error() inside expecting uses _Exit, not exit(): same
			// worker-thread fatal-abort rule as recv_data_internal — do
			// not run destructors while peers are live.
			expecting(res > 0,
			          "net_send_data (peer closed or write error)");
			sent += res;
		}
	}

	// A send of NETWORK_DIRECT_IO_SIZE bytes or more goes out after every
	// byte sent before it. stdio holds the oldest of those, so it writes them
	// out first (fflush writes nothing when it holds nothing); then one writev
	// carries the staged bytes followed by the caller's. A send longer than
	// kMaxDirectIo takes one writev per kMaxDirectIo bytes; only the first
	// carries staged bytes. Out of line, and with no state of its own, so that
	// the staging path of send_data_internal, which every small send takes,
	// compiles as it would without this path.
#if defined(__GNUC__) || defined(__clang__)
	[[gnu::noinline]]
#endif
	void send_direct(const char *data, int64_t len) {
		flush_stream();
		while (len > 0) {
			const int64_t piece = len < kMaxDirectIo ? len : kMaxDirectIo;
			struct iovec iov[2];
			int cnt = 0;
			if (send_ptr) iov[cnt++] = {send_buf, send_ptr};
			iov[cnt++] = {const_cast<char *>(data), (size_t)piece};
			writev_all(iov, cnt);
			send_ptr = 0;
			data += piece;
			len -= piece;
		}
	}

	// Write every byte of iov[0..cnt), in order, resuming after a short write
	// and retrying on EINTR.
	void writev_all(struct iovec *iov, int cnt) {
		while (cnt > 0) {
			ssize_t n;
			do { n = ::writev(sock, iov, cnt); } while (n < 0 && errno == EINTR);
			// Same fatal-abort rule as send_raw.
			expecting(n > 0, "net_send_data (peer closed or write error)");
			size_t done = (size_t)n;
			while (cnt > 0 && done >= iov->iov_len) {
				done -= iov->iov_len;
				++iov;
				--cnt;
			}
			if (cnt > 0) {
				iov->iov_base = static_cast<char *>(iov->iov_base) + done;
				iov->iov_len -= done;
			}
		}
	}

	// One read() of at most `cap` bytes (and at most kMaxDirectIo) into the
	// caller's buffer; returns how many arrived. Same fatal-abort rule as the
	// staging refill in recv_data_internal.
#if defined(__GNUC__) || defined(__clang__)
	[[gnu::noinline]]
#endif
	int64_t read_direct(char *dst, int64_t cap) {
		const size_t want = (size_t)(cap < kMaxDirectIo ? cap : kMaxDirectIo);
		ssize_t n;
		do { n = ::read(sock, dst, want); } while (n < 0 && errno == EINTR);
		expecting(n > 0, "net_recv_data (peer closed or read error)");
		return (int64_t)n;
	}

};

}  // namespace emp
#endif
