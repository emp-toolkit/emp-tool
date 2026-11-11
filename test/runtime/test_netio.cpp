// io/net_io_channel.h — TCP IOChannel.
//
// Public surface:
//   ctor(addr, port)                   open one TCP fd
//   ctor(existing_sock)                wrap an already-connected fd
//   send_data / recv_data              raw bytes (int64_t lengths)
//   send_block / recv_block            block-typed wrapper
//   send_bool / recv_bool              packed via bools_to_bits
//   flush()                            drain outbound only (no peer coupling)
//   sync()                             barrier: one zero byte each way
//   make_sibling()                     more connections on the same port
//   has_session_root / next_session_id session ids, from a root agreed by a
//                                      coin flip at connection setup
//   SessionRoot::none                  connect without agreeing on a root
//   tcp::SocketOptions                 pre-handshake socket buffer sizing
//   SocketOptions::for_bandwidth_and_rtt
//                                      derive buffers from a known path
//
// Test functions below are templated on the IO type so correctness and
// regression checks can be reused by IO implementations.

#include <csignal>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/wait.h>

#include "emp-tool/emp-tool.h"

using namespace std;
using namespace emp;

template <class F>
static bool dies(F &&f) {
	pid_t pid = fork();
	expecting(pid >= 0, "NetIO test: fork failed");
	if (pid == 0) {
		std::freopen("/dev/null", "w", stderr);
		f();
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

// Runs f in a child process and collects what it writes to stderr; true if
// the child exited cleanly.
template <class F>
static bool run_capturing_stderr(F &&f, std::string &err) {
	int fds[2];
	expecting(::pipe(fds) == 0, "NetIO test: pipe failed");
	pid_t pid = fork();
	expecting(pid >= 0, "NetIO test: fork failed");
	if (pid == 0) {
		::close(fds[0]);
		::dup2(fds[1], STDERR_FILENO);
		f();
		_exit(0);
	}
	::close(fds[1]);
	err.clear();
	char buf[512];
	ssize_t n;
	while ((n = ::read(fds[0], buf, sizeof(buf))) > 0) err.append(buf, (size_t)n);
	::close(fds[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int count_of(const std::string &text, const std::string &needle) {
	int count = 0;
	for (size_t at = text.find(needle); at != std::string::npos;
	     at = text.find(needle, at + needle.size()))
		++count;
	return count;
}

// -------------------------------------------------------------------------
// run_correctness(): byte stream round-trip at unaligned offsets, then bool
// packing round-trip at unaligned bool offsets. Each side asserts on the
// values it receives.
// -------------------------------------------------------------------------
template <typename IO>
static void run_correctness(IO *io, int party, const char *tag) {
	uint64_t sent_before = io->send_counter, recv_before = io->recv_counter;
	uint8_t *no_bytes = nullptr;
	io->send_bool(nullptr, 0);
	io->recv_bool(nullptr, 0);
	io->send_bool(no_bytes, 0);
	io->recv_bool(no_bytes, 0);
	expecting(io->send_counter == sent_before && io->recv_counter == recv_before,
	          "NetIO test: zero-length bool transfer changed counters");

	// Stream of unaligned-byte sends: sends `length` bytes 1000 times in
	// each direction, with `length` chosen to straddle the 32 KiB sender
	// staging buffer (NETWORK_STAGING_BUFFER_SIZE/5 + 100) so most send_data calls
	// trigger a staging-overflow path.
	{
		int length = NETWORK_STAGING_BUFFER_SIZE / 5 + 100;
		char *data = new char[length];
		char *data2 = new char[length];
		PRG prg(&zero_block);
		for (int i = 0; i < 1000; ++i) {
			if (party == ALICE) {
				prg.random_data_unaligned(data, length);
				io->send_data(data, length);
				io->send_data(data, length);
				io->recv_data(data2, length);
				expecting(memcmp(data, data2, length) == 0,
				          "NetIO test: ALICE byte round-trip mismatch");
			} else {
				prg.random_data_unaligned(data2, length);
				io->recv_data(data, length);
				io->recv_data(data, length);
				io->send_data(data2, length);
				expecting(memcmp(data, data2, length) == 0,
				          "NetIO test: BOB byte round-trip mismatch");
			}
		}
		io->flush();
		delete[] data;
		delete[] data2;
	}

	// Bool packing: 1 MiB of bools sent both aligned and at offset +7 (so
	// the implementation cannot lean on uint64_t-aligned input).
	{
		constexpr int N = 1024 * 1024;
		PRG prg(&zero_block);
		bool *data  = new bool[N];
		bool *data2 = new bool[N];
		vector<uint8_t> bytes(N), bytes2(N);
		prg.random_bool(data, N);
		for (int i = 0; i < N; ++i) bytes[i] = static_cast<uint8_t>(data[i]);
		if (party == ALICE) {
			io->send_bool(data, N);
			io->send_bool(data + 7, N - 7);
			io->send_bool(bytes.data() + 3, N - 3);
			io->send_bool(data + 5, N - 5);
		} else {
			io->recv_bool(data2, N);
			expecting(memcmp(data2, data, N) == 0,
			          "NetIO test: aligned bool round-trip mismatch");
			memset(data2, 0, N);
			io->recv_bool(data2 + 7, N - 7);
			expecting(memcmp(data2 + 7, data + 7, N - 7) == 0,
			          "NetIO test: unaligned bool round-trip mismatch");
			memset(data2, 0, N);
			io->recv_bool(data2 + 3, N - 3);
			for (int i = 3; i < N; ++i)
				expecting(data2[i] == data[i],
				          "NetIO test: byte-bool send mismatch");
			io->recv_bool(bytes2.data() + 5, N - 5);
			for (int i = 5; i < N; ++i)
				expecting((bytes2[i] != 0) == data[i],
				          "NetIO test: byte-bool receive mismatch");
		}
		delete[] data;
		delete[] data2;
	}
	// ALICE's send_bool batches push ~256 KiB into stdio's 1 MiB stream_buf
	// without any follow-up recv to trigger an auto-flush; BOB's recv_bool
	// blocks until those bytes are on the wire. An explicit flush here keeps
	// the section closed under composition — without it, if anything between
	// run_correctness and the next high-rate sender is removed, the stranded
	// bytes only escape via the stream_buf overflow path (i.e. by accident).
	io->flush();

	if (party == ALICE) cout << tag << " correctness: OK\n";
}

// -------------------------------------------------------------------------
// run_send_only_regression(): an IO that does only sends across an entire
// protocol step (no recv on the same IO to trigger an auto-flush, no
// volume large enough to overflow the 32 KiB user-space staging buffer
// or the 1 MiB stdio stream buffer) must still deliver its tail bytes to
// the peer. This is the shape of a protocol step that ends with a few KiB
// of sends and then waits on another channel: the batch is below
// NETWORK_STAGING_BUFFER_SIZE (32 KiB), so the only things that can move the
// bytes are an explicit flush() or ~IO.
// Two checks, on two short-lived IOs that don't pollute the main channel:
//   (a) explicit io.flush() drains a send-only batch.
//   (b) ~IO drains a send-only batch.
// In a regression where (a)'s drain path is broken, BOB's recv hangs on
// the bytes left in send_buf. In a regression where (b)'s drain path is
// broken, ALICE closes the socket without sending — BOB's recv hits EOF
// and exits with "error: net_recv_data".
// -------------------------------------------------------------------------
template <typename IO>
static void run_send_only_regression(int port, int party, const char *tag) {
	constexpr int N = 4096;            // well under NETWORK_STAGING_BUFFER_SIZE (32 KiB)
	char *data  = new char[N];
	char *data2 = new char[N];
	PRG prg(&zero_block);
	prg.random_data_unaligned(data, N);

	// (a) explicit flush() drains a send-only batch.
	{
		IO io(party == ALICE ? nullptr : peer_ip(), port + 1, true);
		if (party == ALICE) {
			io.send_data(data, N);
			io.flush();                // peer's recv depends on this
			char ack = 0;
			io.recv_data(&ack, 1);     // hold connection open until BOB confirms
			expecting(ack == 1, "NetIO test: explicit-flush acknowledgement mismatch");
		} else {
			io.recv_data(data2, N);
			expecting(memcmp(data, data2, N) == 0,
			          "NetIO test: explicit-flush payload mismatch");
			char ack = 1;
			io.send_data(&ack, 1);
			io.flush();
		}
	}

	// (b) destructor drain. ALICE sends N then immediately destroys the
	// IO — no flush(), no recv. BOB must still see N. ~IO is the only
	// thing that can move the bytes.
	{
		IO *io = new IO(party == ALICE ? nullptr : peer_ip(), port + 2, true);
		if (party == ALICE) {
			io->send_data(data, N);
			delete io;                 // must flush; otherwise BOB hits EOF
		} else {
			io->recv_data(data2, N);
			expecting(memcmp(data, data2, N) == 0,
			          "NetIO test: destructor-flush payload mismatch");
			delete io;
		}
	}

	delete[] data;
	delete[] data2;
	if (party == ALICE) cout << tag << " send-only regression: OK\n";
}

// Create many live siblings on the same port. Half are created from the primary;
// then the primary is destroyed and the rest are created from the first sibling.
// This verifies shared listener ownership, repeated accept/connect pairing, and
// closure only after the last related NetIO dies. A fresh session then reuses the
// same port.
static void run_sibling_regression(int port, int party) {
	{
		auto primary = party == ALICE ? NetIO::listen(port, true)
		                              : NetIO::connect(peer_ip(), port, true);
		std::vector<std::unique_ptr<NetIO>> siblings;
		siblings.reserve(16);
		for (int round = 0; round < 16; ++round) {
			NetIO *source = round < 8 ? primary.get() : siblings.front().get();
			siblings.push_back(source->make_sibling());
			siblings.back()->sync();
			if (round == 7) primary.reset();
		}
	}
	{
		auto primary = party == ALICE ? NetIO::listen(port, true)
		                              : NetIO::connect(peer_ip(), port, true);
		auto sibling = primary->make_sibling();
		sibling->sync();
	}
	if (party == ALICE) cout << "NetIO shared-listener regression: OK\n";
}

static int socket_buffer_size(int sock, int option) {
	int size = 0;
	socklen_t length = sizeof(size);
	expecting(::getsockopt(sock, SOL_SOCKET, option, &size, &length) == 0,
	          "NetIO test: getsockopt failed");
#ifdef __linux__
	size /= 2;
#endif
	return size;
}

static void expect_socket_options(const NetIO &io,
	                              const tcp::SocketOptions &options) {
	expecting(socket_buffer_size(io.fd(), SO_SNDBUF) >= options.send_buffer_size,
	          "NetIO test: send buffer option was not applied");
	expecting(socket_buffer_size(io.fd(), SO_RCVBUF) >= options.receive_buffer_size,
	          "NetIO test: receive buffer option was not applied");
}

static void run_socket_options_factory_regression() {
	int (*open_listener_legacy)(int) = tcp::open_listener;
	int (*accept_one_confirmed_legacy)(int) = tcp::accept_one_confirmed;
	int (*server_listen_legacy)(int) = tcp::server_listen;
	int (*client_connect_impl_legacy)(const char *, int, bool) =
	    tcp::client_connect_impl;
	int (*client_connect_legacy)(const char *, int) = tcp::client_connect;
	int (*client_connect_confirmed_legacy)(const char *, int) =
	    tcp::client_connect_confirmed;
	(void)open_listener_legacy;
	(void)accept_one_confirmed_legacy;
	(void)server_listen_legacy;
	(void)client_connect_impl_legacy;
	(void)client_connect_legacy;
	(void)client_connect_confirmed_legacy;

	// Short paths are left to the operating system; longer ones get the first
	// power-of-two tier above the bandwidth-delay product.
	const auto short_path = tcp::SocketOptions::for_bandwidth_and_rtt(
	    400'000'000, std::chrono::microseconds(450));
	expecting(short_path.send_buffer_size == 0 &&
	              short_path.receive_buffer_size == 0 &&
	              short_path.bandwidth_bits_per_second == 400'000'000 &&
	              short_path.round_trip_time == std::chrono::microseconds(450),
	          "NetIO test: short path was not left to the OS");

	const auto wan_path = tcp::SocketOptions::for_bandwidth_and_rtt(
	    400'000'000, std::chrono::milliseconds(100));
	expecting(wan_path.send_buffer_size == 8 * 1024 * 1024 &&
	              wan_path.receive_buffer_size == 8 * 1024 * 1024,
	          "NetIO test: WAN buffer tier mismatch");

	const auto fast_path = tcp::SocketOptions::for_bandwidth_and_rtt(
	    10'000'000'000ULL, std::chrono::milliseconds(100));
	expecting(fast_path.send_buffer_size == 128 * 1024 * 1024 &&
	              fast_path.receive_buffer_size == 128 * 1024 * 1024,
	          "NetIO test: fast-path buffer tier mismatch");

	// A request above the system limit is lowered to the limit when that is
	// more than the operating system reaches on its own, and otherwise not made
	// (the socket keeps its defaults); one warning per process says how to raise
	// the limit.
	auto over_limit = [](const tcp::SocketOptions &options) {
		int sock = ::socket(AF_INET, SOCK_STREAM, 0);
		int fresh = ::socket(AF_INET, SOCK_STREAM, 0);
		expecting(sock >= 0 && fresh >= 0, "NetIO test: socket failed");
		tcp::apply_socket_options(sock, options);
		tcp::apply_socket_options(sock, options);
		for (int option : {SO_SNDBUF, SO_RCVBUF}) {
			const std::int64_t limit = tcp::detail::socket_buffer_limit(option);
			expecting(limit >= 0, "NetIO test: socket buffer limit unreadable");
			const int expected =
			    static_cast<std::uint64_t>(limit) > tcp::SocketOptions::kOsSizedLimit
			        ? static_cast<int>(limit) : socket_buffer_size(fresh, option);
			expecting(socket_buffer_size(sock, option) == expected,
			          "NetIO test: over-limit request handled wrongly");
		}
	};
#if defined(__APPLE__)
	const std::string raise = "sudo sysctl -w kern.ipc.maxsockbuf=";
#else
	const std::string raise = "sudo sysctl -w net.core.rmem_max=";
#endif
	const std::string warning = "emp-tool: warning: ";
	std::string err;

	tcp::SocketOptions by_hand;
	by_hand.send_buffer_size = 1 << 30;
	by_hand.receive_buffer_size = 1 << 30;
	expecting(run_capturing_stderr([&] { over_limit(by_hand); }, err),
	          "NetIO test: an over-limit request was fatal");
	expecting(count_of(err, warning) == 1 &&
	              err.find("the requested 1 GiB socket buffers") != std::string::npos &&
	              err.find(raise + std::to_string(1 << 30)) != std::string::npos,
	          "NetIO test: over-limit warning for set sizes is missing or wrong");

	const auto too_fast = tcp::SocketOptions::for_bandwidth_and_rtt(
	    80'000'000'000ULL, std::chrono::milliseconds(100));
	expecting(run_capturing_stderr([&] { over_limit(too_fast); }, err),
	          "NetIO test: an over-limit request was fatal");
	expecting(count_of(err, warning) == 1 &&
	              err.find("for the 80 Gbit/s bandwidth and 100 ms round-trip "
	                       "time you gave, 1 GiB socket buffers") != std::string::npos &&
	              err.find(raise + std::to_string(1 << 30)) != std::string::npos,
	          "NetIO test: over-limit warning for a path is missing or wrong");
}

// sync() pairs any two channels, whatever their roles, such as two NetIOs
// adopted from a socketpair (both is_server == false). Its bytes stay out of
// the byte counters, and a nonzero byte (the peer is mid-message) is reported.
static void run_sync_regression() {
	int sockets[2];
	expecting(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
	          "NetIO test: socketpair failed");
	{
		NetIO a(sockets[0], true), b(sockets[1], true);
		std::thread peer([&] { b.sync(); });
		a.sync();
		peer.join();
		expecting(a.send_counter == 0 && a.recv_counter == 0 &&
		              b.send_counter == 0 && b.recv_counter == 0,
		          "NetIO test: sync() bytes were counted");
	}

	expecting(dies([] {
		int pair[2];
		expecting(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0,
		          "NetIO test: socketpair failed");
		NetIO a(pair[0], true), b(pair[1], true);
		const unsigned char stray = 1;
		a.send_data(&stray, 1);
		a.flush();
		b.sync();
	}), "NetIO test: sync() accepted a stray byte");
	cout << "NetIO sync regression: OK\n";
}

// Session roots that must not exist or must not be accepted. A channel adopted
// from a socket does not know which end of the connection it is, so it has no
// root, and asking it for a session id is an error. A connector checks the
// listener's opening: here the listener is a raw socket on a kernel-assigned
// port that sends 32 arbitrary bytes for the commitment and 32 for an opening
// that does not match it.
static void run_session_root_failure_regression() {
	int pair[2];
	expecting(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0,
	          "NetIO test: socketpair failed");
	{
		NetIO a(pair[0], true), b(pair[1], true);
		expecting(!a.has_session_root() && !b.has_session_root(),
		          "NetIO test: a channel adopted from a socket has a session root");
	}

	std::string err;
	bool clean = run_capturing_stderr([] {
		int sockets[2];
		expecting(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
		          "NetIO test: socketpair failed");
		NetIO adopted(sockets[0], true);
		(void)adopted.next_session_id();
	}, err);
	expecting(!clean && err.find("has no session root") != std::string::npos,
	          "NetIO test: next_session_id() without a session root did not fail");

	clean = run_capturing_stderr([] {
		const int listener = tcp::open_listener(0);
		sockaddr_in local{};
		socklen_t local_size = sizeof(local);
		expecting(::getsockname(listener, (sockaddr *)&local, &local_size) == 0,
		          "NetIO test: getsockname failed");
		std::thread forger([listener] {
			const int s = tcp::accept_one_confirmed(listener);
			const unsigned char forged[32] = {1};
			unsigned char r_prime[16];
			expecting(::send(s, forged, sizeof(forged), 0) == (ssize_t)sizeof(forged) &&
			              ::recv(s, r_prime, sizeof(r_prime), MSG_WAITALL) ==
			                  (ssize_t)sizeof(r_prime) &&
			              ::send(s, forged, sizeof(forged), 0) == (ssize_t)sizeof(forged),
			          "NetIO test: the forged listener's I/O failed");
		});
		NetIO connector("127.0.0.1", ntohs(local.sin_port), true);
		forger.join();
	}, err);
	expecting(!clean && err.find("does not match its commitment") != std::string::npos,
	          "NetIO test: a session-root opening that does not match its "
	          "commitment was accepted");
	cout << "NetIO session-root failure regression: OK\n";
}

static void run_flush_failure_regression() {
	expecting(dies([] {
		int sockets[2];
		expecting(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
		          "NetIO test: socketpair failed");
		::signal(SIGPIPE, SIG_IGN);
		::close(sockets[1]);
		NetIO io(sockets[0], true);
		const char byte = 1;
		io.send_data(&byte, 1);
		io.flush();
	}), "NetIO test: fflush failure was ignored");
}

static void run_socket_options_regression(int port, int party) {
	tcp::SocketOptions options;
	options.send_buffer_size = 128 * 1024;
	options.receive_buffer_size = 128 * 1024;

	auto primary = party == ALICE ? NetIO::listen(port, options, true)
	                              : NetIO::connect(peer_ip(), port, options, true);
	expect_socket_options(*primary, options);
	if (party == ALICE) {
		expecting(socket_buffer_size(primary->listener_fd(), SO_SNDBUF) >=
		              options.send_buffer_size,
		          "NetIO test: listener send buffer option was not applied");
		expecting(socket_buffer_size(primary->listener_fd(), SO_RCVBUF) >=
		              options.receive_buffer_size,
		          "NetIO test: listener receive buffer option was not applied");
	}

	auto sibling = primary->make_sibling();
	sibling->sync();
	expect_socket_options(*sibling, options);
	primary.reset();

	auto next = sibling->make_sibling();
	next->sync();
	expect_socket_options(*next, options);
	if (party == ALICE) cout << "NetIO socket-options regression: OK\n";
}

// next_session_id() between the two processes: the connection agreed on a
// root, each process drawing its own coins, and both must end with the same
// ids. Each party sends its ids to the other.
template <typename IO>
static void run_session_id(IO *io, int party, const char *tag) {
	expecting(io->has_session_root(),
	          "NetIO test: a connected channel has no session root");
	block mine[2], theirs[2];
	for (int i = 0; i < 2; ++i) mine[i] = io->next_session_id().value();
	if (party == ALICE) {
		io->send_block(mine, 2);
		io->recv_block(theirs, 2);
	} else {
		io->recv_block(theirs, 2);
		io->send_block(mine, 2);
		io->flush();
	}
	expecting(cmpBlock(mine, theirs, 2),
	          "NetIO test: the two ends derived different session ids");
	expecting(!cmpBlock(&mine[0], &mine[1], 1),
	          "NetIO test: consecutive session ids repeat");
	if (party == ALICE) cout << tag << " session ids: OK\n";
}

// Session roots across a family of channels. A sibling's root is its parent's
// next session id, so the sibling runs no flip of its own, and both ends get
// the same ids, which differ from the parent's. A channel opened with
// SessionRoot::none has no root, and neither do its siblings. The second
// family takes the next port: BOB leaves the first family first, and a
// connection reaching ALICE's listener while it closes can, on Linux, stay
// established with no peer and never be reset.
static void run_session_root_regression(int port, int party) {
	{
		auto primary = party == ALICE ? NetIO::listen(port, true)
		                              : NetIO::connect(peer_ip(), port, true);
		auto sibling = primary->make_sibling();
		block mine[2] = {primary->next_session_id().value(),
		                 sibling->next_session_id().value()};
		block theirs[2];
		if (party == ALICE) {
			sibling->send_block(mine, 2);
			sibling->recv_block(theirs, 2);
		} else {
			sibling->recv_block(theirs, 2);
			sibling->send_block(mine, 2);
			sibling->flush();
		}
		expecting(cmpBlock(mine, theirs, 2),
		          "NetIO test: the two ends of a sibling derived different session ids");
		expecting(!cmpBlock(&mine[0], &mine[1], 1),
		          "NetIO test: a sibling repeated its parent's session id");
	}
	{
		auto primary = party == ALICE
		    ? NetIO::listen(port + 1, true, NetIO::SessionRoot::none)
		    : NetIO::connect(peer_ip(), port + 1, true, NetIO::SessionRoot::none);
		auto sibling = primary->make_sibling();
		expecting(!primary->has_session_root() && !sibling->has_session_root(),
		          "NetIO test: a channel opened without a session root has one");
	}
	if (party == ALICE) cout << "NetIO session-root regression: OK\n";
}

// Zero-length send/recv are documented no-ops (docs/api_conventions.md):
// no counter, round, or flush-state mutation, no transport call, and a
// null pointer is fine at count zero. Purely local — both parties run it
// symmetrically with no wire traffic.
template <typename IO>
static void run_zero_length_noop(IO *io, int party, const char *tag) {
	const uint64_t s0 = io->send_counter, r0 = io->recv_counter;
	const uint64_t rounds0 = io->rounds, f0 = io->flushes_count;
	io->send_data(nullptr, 0);
	io->recv_data(nullptr, 0);
	expecting(io->send_counter == s0 && io->recv_counter == r0 &&
	              io->rounds == rounds0 && io->flushes_count == f0,
	          "NetIO test: zero-length send/recv mutated channel state");
	if (party == ALICE) cout << tag << " zero-length no-op: OK\n";
}

// Run the full correctness/regression suite on one IO type,
// using a contiguous block of three ports starting at port_base:
// port_base+0 = main channel, +1 / +2 = regression channels.
template <typename IO>
static void run_suite(int port_base, int party, const char *tag) {
	IO *io = new IO(party == ALICE ? nullptr : peer_ip(), port_base, true);
	run_correctness(io, party, tag);
	run_session_id(io, party, tag);
	run_zero_length_noop(io, party, tag);
	run_send_only_regression<IO>(port_base, party, tag);
	delete io;
}

int main(int argc, char **argv) {
	int port, party;
	party = parse_party(argv);
	port = peer_port();
	run_socket_options_factory_regression();
	run_flush_failure_regression();
	run_sync_regression();
	run_session_root_failure_regression();

	// Seven contiguous ports: main, two send-only cases, sibling regression,
	// socket-options regression, and two for the session-root regression.
	run_suite<NetIO>(port, party, "NetIO");
	run_sibling_regression(port + 3, party);
	run_socket_options_regression(port + 4, party);
	run_session_root_regression(port + 5, party);
}
