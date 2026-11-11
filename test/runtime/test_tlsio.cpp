// io/tls_io_channel.h — TLS-1.3 IOChannel over a single TCP fd.
//
// Public surface:
//   ctor(addr, port, cfg)              open one TCP fd + complete TLS handshake
//   ctor(existing_sock, role, cfg)     wrap an already-connected fd
//   send_data / recv_data              raw bytes
//   send_block / recv_block            block-typed wrapper (inherited)
//   send_bool / recv_bool              packed via bools_to_bits (inherited)
//   flush()                            drain outbound coalescing buffer
//   sync()                             barrier (inherited from IOChannel)
//   has_session_root / next_session_id session ids, from a root taken from
//                                      the TLS exporter
//   TLSConfig keys                     own private key + the peer's pinned
//                                      public key; a peer without the
//                                      matching private key is rejected,
//                                      whatever certificates it presents
//   TLSConfig::socket_options          pre-handshake socket buffer sizing
//
// Same flush contract and thread-safety rules as NetIO. The test
// mirrors test_netio.cpp's correctness + send-only regression suite,
// exercising the same paths against TLSIO.
//
// Keys are laid out the way a deployment holds them: each party has its own
// private key and the other party's public key, and nothing else (no CA, no
// certificates):
//
//   alice_key.pem     SK_alice, ALICE's private key (RSA)
//   alice_pub.pem     PK_alice, which BOB pins
//   bob_key.pem       SK_bob, BOB's private key (Ed25519)
//   bob_pub.pem       PK_bob, which ALICE pins
//   mallory_pub.pem   a third public key, pinned by the rejection checks
//
// ALICE generates all of them and BOB polls for mallory_pub.pem, written last.
// In a deployment each party generates its own key pair and gives the other
// only its public key. The two key types differ to cover both ways TLSIO signs
// its throwaway certificate (with a digest for RSA, without one for Ed25519).

#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "emp-tool/emp-tool.h"

using namespace std;
using namespace emp;

template <class F>
static bool dies(F &&f) {
	pid_t pid = fork();
	expecting(pid >= 0, "TLSIO test: fork failed");
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
	expecting(::pipe(fds) == 0, "TLSIO test: pipe failed");
	pid_t pid = fork();
	expecting(pid >= 0, "TLSIO test: fork failed");
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

// The key files live under the test's port, so runs on other ports never
// share them.
static int g_port = 0;
static std::string pem_path(const char *name) {
	return "/tmp/emp_tlsio_test_" + std::to_string(g_port) + "_" + name + ".pem";
}

// Bail with the OpenSSL error queue dumped, on any non-success path.
// Used in lieu of assert() because the test runs Release-by-default and
// assert() is a no-op there — most of these calls have side effects.
[[noreturn]] static void key_die(const char *what) {
	fprintf(stderr, "test_tlsio key gen: %s failed\n", what);
	ERR_print_errors_fp(stderr);
	exit(1);
}

// 0600 perms from creation (not chmod'd after) so we never publish a
// world-readable private key, even briefly.
static void write_private_key(EVP_PKEY *key, const std::string &path) {
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) key_die("open key");
	FILE *f = fdopen(fd, "wb");
	if (!f) key_die("fdopen key");
	if (PEM_write_PrivateKey(f, key, nullptr, nullptr, 0, nullptr, nullptr) != 1)
		key_die("PEM_write_PrivateKey");
	fclose(f);
}

// 0644 from creation: anyone may read a public key, but no one else may
// replace the one a party pins.
static void write_public_key(EVP_PKEY *key, const std::string &path) {
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) key_die("open public key");
	FILE *f = fdopen(fd, "wb");
	if (!f) key_die("fdopen public key");
	if (PEM_write_PUBKEY(f, key) != 1) key_die("PEM_write_PUBKEY");
	fclose(f);
}

// Make the key files listed at the top. mallory_pub.pem is renamed into place
// last, so its presence tells BOB the others are complete.
static void generate_keys() {
	EVP_PKEY *alice = EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", (size_t)2048);
	EVP_PKEY *bob = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
	EVP_PKEY *mallory = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
	if (!alice || !bob || !mallory) key_die("EVP_PKEY_Q_keygen");
	write_private_key(alice, pem_path("alice_key"));
	write_public_key(alice, pem_path("alice_pub"));
	write_private_key(bob, pem_path("bob_key"));
	write_public_key(bob, pem_path("bob_pub"));
	const std::string ready = pem_path("mallory_pub");
	write_public_key(mallory, ready + ".tmp");
	if (::rename((ready + ".tmp").c_str(), ready.c_str()) != 0)
		key_die("rename mallory_pub");
	EVP_PKEY_free(alice);
	EVP_PKEY_free(bob);
	EVP_PKEY_free(mallory);
}

// Block until mallory_pub.pem exists, with a 5 s cap so a missed handoff
// surfaces as a test failure rather than a hang.
static void wait_for_keys() {
	const std::string ready = pem_path("mallory_pub");
	struct stat sb;
	for (int i = 0; i < 5000; ++i) {
		if (::stat(ready.c_str(), &sb) == 0) return;
		usleep(1000);
	}
	fprintf(stderr, "test_tlsio: timed out waiting for the key files\n");
	exit(1);
}

static TLSConfig make_cfg(int party) {
	TLSConfig cfg;
	// Mirror test_netio.cpp: ALICE runs the TCP listener, and is the TLS
	// server too. Each party gives its own private key and pins the other's
	// public key.
	cfg.is_tls_server = (party == ALICE);
	cfg.key_pem_path = pem_path(party == ALICE ? "alice_key" : "bob_key");
	cfg.peer_public_key_pem_path = pem_path(party == ALICE ? "bob_pub" : "alice_pub");
	cfg.socket_options.send_buffer_size = 128 * 1024;
	cfg.socket_options.receive_buffer_size = 128 * 1024;
	return cfg;
}

static int socket_buffer_size(int sock, int option) {
	int size = 0;
	socklen_t length = sizeof(size);
	expecting(::getsockopt(sock, SOL_SOCKET, option, &size, &length) == 0,
	          "TLSIO test: getsockopt failed");
#ifdef __linux__
	size /= 2;
#endif
	return size;
}

static void expect_socket_options(const TLSIO &io,
	                              const tcp::SocketOptions &options) {
	expecting(socket_buffer_size(io.fd(), SO_SNDBUF) >= options.send_buffer_size,
	          "TLSIO test: send buffer option was not applied");
	expecting(socket_buffer_size(io.fd(), SO_RCVBUF) >= options.receive_buffer_size,
	          "TLSIO test: receive buffer option was not applied");
}

static void run_adopted_socket_options_rejection() {
	expecting(dies([] {
		int sockets[2];
		expecting(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
		          "TLSIO test: socketpair failed");
		TLSConfig cfg;
		cfg.socket_options.receive_buffer_size = 128 * 1024;
		TLSIO io(sockets[0], true, cfg, true);
	}), "TLSIO test: adopted socket accepted pre-handshake options");
}

// A TLSConfig without both keys is refused before any handshake.
static void run_missing_key_rejection() {
	std::string err;
	const bool clean = run_capturing_stderr([] {
		int sockets[2];
		expecting(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
		          "TLSIO test: socketpair failed");
		TLSConfig cfg;
		cfg.key_pem_path = pem_path("alice_key");  // and no peer key
		TLSIO io(sockets[0], true, cfg, true);
	}, err);
	expecting(!clean && err.find("are required") != std::string::npos,
	          "TLSIO test: a TLSConfig without the peer's public key was accepted");
}

// -------------------------------------------------------------------------
// Pinned-key attacks, without ports: TLSIO adopts one end of a socketpair,
// and a raw OpenSSL peer on a thread holds the other. The attacker knows the
// pinned public key but not its private key, so it can put the pinned key in
// a certificate but cannot sign the handshake with it. TLSIO, in either role,
// must refuse a chain whose leaf carries the attacker's key and whose next
// certificate, styled as a CA, carries the pinned key; as a server it must
// refuse a client with no certificate. An honest raw peer is the control,
// and a TLSIO adopted from a socket has a session root.
// -------------------------------------------------------------------------

// A certificate carrying `subject_key`, signed by `signer`.
static X509 *raw_cert(EVP_PKEY *subject_key, EVP_PKEY *signer, bool is_ca) {
	X509 *cert = X509_new();
	if (!cert) key_die("X509_new");
	X509_NAME *name = X509_get_subject_name(cert);
	bool built =
	    X509_set_version(cert, X509_VERSION_3) == 1 &&
	    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) == 1 &&
	    X509_gmtime_adj(X509_getm_notBefore(cert), 0) != nullptr &&
	    X509_gmtime_adj(X509_getm_notAfter(cert), 60 * 60) != nullptr &&
	    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
	                               (const unsigned char *)"raw peer", -1, -1, 0) == 1 &&
	    X509_set_issuer_name(cert, name) == 1 &&
	    X509_set_pubkey(cert, subject_key) == 1;
	if (built && is_ca) {
		X509V3_CTX v3;
		X509V3_set_ctx_nodb(&v3);
		X509V3_set_ctx(&v3, cert, cert, nullptr, nullptr, 0);
		X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &v3, NID_basic_constraints,
		                                          "critical,CA:TRUE");
		built = ext != nullptr && X509_add_ext(cert, ext, -1) == 1;
		X509_EXTENSION_free(ext);
	}
	const int type = EVP_PKEY_get_id(signer);
	const EVP_MD *md =
	    (type == EVP_PKEY_ED25519 || type == EVP_PKEY_ED448) ? nullptr : EVP_sha256();
	if (!built || X509_sign(cert, signer, md) <= 0) key_die("raw_cert");
	return cert;
}

enum class RawChain { honest, attacker_then_pinned, none };

// The raw peer: a handshake on `fd` presenting `chain`, then one byte, sent by
// the TLS client to the TLS server. A failed handshake is how an attack ends.
static void raw_peer(int fd, bool is_server, RawChain chain, EVP_PKEY *pinned,
                     EVP_PKEY *attacker) {
	SSL_CTX *ctx = SSL_CTX_new(TLS_method());
	if (!ctx) key_die("SSL_CTX_new");
	SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
	SSL_CTX_set_num_tickets(ctx, 0);
	EVP_PKEY *signing_key = chain == RawChain::honest ? pinned : attacker;
	if (chain != RawChain::none) {
		X509 *leaf = raw_cert(signing_key, signing_key, false);
		SSL_CTX_use_certificate(ctx, leaf);
		X509_free(leaf);
		SSL_CTX_use_PrivateKey(ctx, signing_key);
	}
	if (chain == RawChain::attacker_then_pinned)
		SSL_CTX_add_extra_chain_cert(ctx, raw_cert(pinned, attacker, true));  // ctx owns it
	SSL *ssl = SSL_new(ctx);
	SSL_set_fd(ssl, fd);
	if ((is_server ? SSL_accept(ssl) : SSL_connect(ssl)) == 1) {
		unsigned char byte = 0x42;
		if (is_server) SSL_read(ssl, &byte, 1);
		else SSL_write(ssl, &byte, 1);
	}
	SSL_free(ssl);
	SSL_CTX_free(ctx);
}

static void run_pinned_key_attacks() {
	char dir_template[] = "/tmp/emp_tlsio_attack.XXXXXX";
	const char *dir = ::mkdtemp(dir_template);
	expecting(dir != nullptr, "TLSIO test: mkdtemp failed");
	EVP_PKEY *own = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
	EVP_PKEY *pinned = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
	EVP_PKEY *attacker = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
	if (!own || !pinned || !attacker) key_die("EVP_PKEY_Q_keygen");
	TLSConfig cfg;
	cfg.key_pem_path = std::string(dir) + "/own_key.pem";
	cfg.peer_public_key_pem_path = std::string(dir) + "/pinned_pub.pem";
	write_private_key(own, cfg.key_pem_path);
	write_public_key(pinned, cfg.peer_public_key_pem_path);

	// TLSIO against the raw peer in a child process, then `after` on the
	// channel; true if the child exited cleanly.
	auto run = [&](bool tlsio_is_server, RawChain chain, auto after, std::string &err) {
		return run_capturing_stderr([&] {
			int sockets[2];
			expecting(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
			          "TLSIO test: socketpair failed");
			std::thread peer([&] {
				raw_peer(sockets[1], !tlsio_is_server, chain, pinned, attacker);
			});
			TLSConfig role = cfg;
			role.is_tls_server = tlsio_is_server;
			TLSIO io(sockets[0], tlsio_is_server, role, true);
			unsigned char byte = 0;
			if (tlsio_is_server) {
				io.recv_data(&byte, 1);
			} else {
				io.send_data(&byte, 1);
				io.flush();
			}
			peer.join();
			after(io);
		}, err);
	};
	auto nothing = [](TLSIO &) {};
	std::string err;
	for (const bool server : {false, true}) {
		expecting(run(server, RawChain::honest, [](TLSIO &io) {
			expecting(io.has_session_root(),
			          "TLSIO test: an adopted channel has no session root");
		}, err), "TLSIO test: an honest raw peer was refused");
		expecting(!run(server, RawChain::attacker_then_pinned, nothing, err) &&
		              err.find("not the pinned one") != std::string::npos,
		          "TLSIO test: a chain with the pinned key behind another leaf "
		          "was accepted");
	}
	expecting(!run(true, RawChain::none, nothing, err),
	          "TLSIO test: a client without a certificate was accepted");

	EVP_PKEY_free(own);
	EVP_PKEY_free(pinned);
	EVP_PKEY_free(attacker);
	std::filesystem::remove_all(dir);
	cout << "TLSIO pinned-key attacks: OK\n";
}

// A peer that does not hold the pinned key is rejected. Each party runs its end
// in a child process, and "mallory" is pinned on one side at a time. When BOB
// pins it, ALICE's certificate fails BOB's check during the handshake. When
// ALICE pins it, BOB's certificate fails ALICE's check; a TLS 1.3 client
// finishes its handshake before the server checks the client's key, so BOB
// sees the rejection at the first read.
static void run_pinned_key_rejection(int port, int party) {
	for (const int rejecter : {BOB, ALICE}) {
		TLSConfig cfg = make_cfg(party);
		if (party == rejecter) cfg.peer_public_key_pem_path = pem_path("mallory_pub");
		const int at = rejecter == BOB ? port + 5 : port + 6;
		std::string err;
		const bool clean = run_capturing_stderr([&] {
			TLSIO io(party == ALICE ? nullptr : peer_ip(), at, cfg, true);
			unsigned char byte = 0;
			if (party == BOB) io.recv_data(&byte, 1);
		}, err);
		expecting(!clean, "TLSIO test: a handshake with a wrong pinned key went through");
		if (party == rejecter)
			expecting(err.find("not the pinned one") != std::string::npos,
			          "TLSIO test: the wrong pinned key was rejected for another reason");
	}
	if (party == ALICE) cout << "TLSIO pinned-key rejection: OK\n";
}

// -------------------------------------------------------------------------
// run_correctness(): byte stream round-trip at unaligned offsets, then bool
// packing round-trip at unaligned bool offsets. Same shape as test_netio.
// -------------------------------------------------------------------------
static void run_correctness(TLSIO *io, int party) {
	{
		int length = NETWORK_STAGING_BUFFER_SIZE / 5 + 100;
		char *data  = new char[length];
		char *data2 = new char[length];
		PRG prg(&zero_block);
		for (int i = 0; i < 1000; ++i) {
			if (party == ALICE) {
				prg.random_data_unaligned(data, length);
				io->send_data(data, length);
				io->send_data(data, length);
				io->recv_data(data2, length);
				expecting(memcmp(data, data2, length) == 0,
				          "TLSIO test: ALICE byte round-trip mismatch");
			} else {
				prg.random_data_unaligned(data2, length);
				io->recv_data(data, length);
				io->recv_data(data, length);
				io->send_data(data2, length);
				expecting(memcmp(data, data2, length) == 0,
				          "TLSIO test: BOB byte round-trip mismatch");
			}
		}
		io->flush();
		delete[] data;
		delete[] data2;
	}

	{
		PRG prg(&zero_block);
		bool *data  = new bool[1024 * 1024];
		bool *data2 = new bool[1024 * 1024];
		prg.random_bool(data, 1024 * 1024);
		if (party == ALICE) {
			io->send_bool(data, 1024 * 1024);
			io->send_bool(data + 7, 1024 * 1024 - 7);
		} else {
			io->recv_bool(data2, 1024 * 1024);
			expecting(memcmp(data2, data, 1024 * 1024) == 0,
			          "TLSIO test: aligned bool round-trip mismatch");
			memset(data2, 0, 1024 * 1024);
			io->recv_bool(data2 + 7, 1024 * 1024 - 7);
			expecting(memcmp(data2 + 7, data + 7, 1024 * 1024 - 7) == 0,
			          "TLSIO test: unaligned bool round-trip mismatch");
		}
		delete[] data;
		delete[] data2;
	}
	io->flush();

	if (party == ALICE) cout << "TLSIO correctness: OK\n";
}

// -------------------------------------------------------------------------
// run_send_only_regression(): mirrors test_netio's check that an IO doing
// only sends across a step still delivers its tail bytes via (a) explicit
// flush and (b) destructor. Under TLS, (b) also covers the destructor's
// flush before close_notify — a regression that closes the fd before
// flushing the staged record would manifest the same way as on NetIO:
// BOB's recv hits EOF / "error: net_recv_data".
// -------------------------------------------------------------------------
static void run_send_only_regression(int port, int party) {
	constexpr int N = 4096;
	char *data  = new char[N];
	char *data2 = new char[N];
	PRG prg(&zero_block);
	prg.random_data_unaligned(data, N);
	const TLSConfig cfg = make_cfg(party);

	{
		TLSIO io(party == ALICE ? nullptr : peer_ip(), port + 1, cfg, true);
		if (party == ALICE) {
			io.send_data(data, N);
			io.flush();
			char ack = 0;
			io.recv_data(&ack, 1);
			expecting(ack == 1, "TLSIO test: explicit-flush acknowledgement mismatch");
		} else {
			io.recv_data(data2, N);
			expecting(memcmp(data, data2, N) == 0,
			          "TLSIO test: explicit-flush payload mismatch");
			char ack = 1;
			io.send_data(&ack, 1);
			io.flush();
		}
	}

	{
		TLSIO *io = new TLSIO(party == ALICE ? nullptr : peer_ip(), port + 2, cfg, true);
		if (party == ALICE) {
			io->send_data(data, N);
			delete io;                  // must flush + SSL_shutdown
		} else {
			io->recv_data(data2, N);
			expecting(memcmp(data, data2, N) == 0,
			          "TLSIO test: destructor-flush payload mismatch");
			delete io;
		}
	}

	delete[] data;
	delete[] data2;
	if (party == ALICE) cout << "TLSIO send-only regression: OK\n";
}

// next_session_id() over TLS: the root comes from the TLS exporter, so the two
// ends agree on every id with no message on the wire. Each party then sends
// its ids to the other.
static void run_session_id(TLSIO *io, int party) {
	expecting(io->has_session_root(), "TLSIO test: the channel has no session root");
	const uint64_t sent = io->send_counter, received = io->recv_counter;
	block mine[3], theirs[3];
	for (int i = 0; i < 3; ++i) mine[i] = io->next_session_id().value();
	expecting(io->send_counter == sent && io->recv_counter == received,
	          "TLSIO test: next_session_id moved bytes");
	expecting(!cmpBlock(&mine[0], &mine[1], 1) && !cmpBlock(&mine[1], &mine[2], 1) &&
	              !cmpBlock(&mine[0], &mine[2], 1),
	          "TLSIO test: consecutive session ids repeat");
	if (party == ALICE) {
		io->send_block(mine, 3);
		io->recv_block(theirs, 3);
	} else {
		io->recv_block(theirs, 3);
		io->send_block(mine, 3);
		io->flush();
	}
	expecting(cmpBlock(mine, theirs, 3),
	          "TLSIO test: the two ends derived different session ids");
	if (party == ALICE) cout << "TLSIO session ids: OK\n";
}

// -------------------------------------------------------------------------
// run_teardown_order_regression(): each party holds two channels. ALICE
// destroys `aux` and then sends on `main`; BOB receives on `main` and only
// then destroys `aux`. NetIO completes this order. A TLSIO destructor that
// waited for the peer's close_notify would block ALICE in ~aux while BOB
// blocks in recv on `main`, until the run harness kills both.
// -------------------------------------------------------------------------
static void run_teardown_order_regression(int port, int party) {
	const TLSConfig cfg = make_cfg(party);
	const char *address = party == ALICE ? nullptr : peer_ip();
	auto main_io = std::make_unique<TLSIO>(address, port + 3, cfg, true);
	auto aux = std::make_unique<TLSIO>(address, port + 4, cfg, true);
	uint32_t x = 0;
	if (party == ALICE) {
		aux.reset();
		x = 0x5eed;
		main_io->send_data(&x, sizeof(x));
		main_io->flush();
	} else {
		main_io->recv_data(&x, sizeof(x));
		aux.reset();
		expecting(x == 0x5eed, "TLSIO test: teardown-order payload mismatch");
	}
	if (party == ALICE) cout << "TLSIO teardown-order regression: OK\n";
}

int main(int argc, char **argv) {
	int port, party;
	party = parse_party(argv);
	port = peer_port();
	g_port = port;
	run_adopted_socket_options_rejection();
	run_missing_key_rejection();
	run_pinned_key_attacks();

	// Key handoff. ALICE (party 1, the TCP listener) makes both key pairs
	// and writes them; BOB polls until they are complete. Each side then
	// configures its own private key and the other's public key.
	if (party == ALICE) generate_keys();
	else                wait_for_keys();

	const TLSConfig cfg = make_cfg(party);

	TLSIO *io = new TLSIO(party == ALICE ? nullptr : peer_ip(), port, cfg, true);
	expect_socket_options(*io, cfg.socket_options);
	run_correctness(io, party);
	run_session_id(io, party);
	io->sync();  // the IOChannel barrier over TLS
	run_send_only_regression(port, party);
	run_teardown_order_regression(port, party);
	delete io;
	run_pinned_key_rejection(port, party);
}
