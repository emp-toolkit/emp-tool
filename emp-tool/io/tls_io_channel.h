#ifndef EMP_TLS_IO_CHANNEL_H__
#define EMP_TLS_IO_CHANNEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>

#include "emp-tool/io/io_channel.h"
#include "emp-tool/io/tcp_socket.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <signal.h>
#include <unistd.h>

namespace emp {

// TLSIO — single-socket full-duplex IOChannel that runs TLS 1.3 over
// one TCP fd. The shape (32 KiB user-space coalescing send buffer,
// 32 KiB recv staging, same flush contract, same single-thread-owned
// discipline, same telemetry counters) is copied verbatim from NetIO;
// only the wire primitives swap to SSL_write_ex / SSL_read_ex.
//
// Why no BIO_f_buffer on top of the socket BIO: this class already
// coalesces small writes into a 32 KiB staging buffer above SSL_write,
// so each SSL_write produces at most one or two records — the place
// where BIO_f_buffer would help is already covered. Adding it would
// just force an extra BIO_flush in flush_unlocked for no win.
//
// Why per-channel SSL_CTX: simplest ownership model, no global state.
// Callers that build hundreds of TLSIOs pay the per-channel cert parse
// cost; if a profile ever shows that mattering, share an SSL_CTX
// across channels via a thin factory. Out of scope today.
//
// Cert / key / CA material is caller-supplied via TLSConfig: PEM file
// paths, no auto self-signed default. The same flush contract that
// applies to NetIO applies here — see docs/io_channel.md.

struct TLSConfig {
	// Role on the TLS handshake. Independent of the address-vs-port
	// "is_server" bit on NetIO: a TCP-acceptor side could in principle
	// be a TLS client (reverse-direction handshake), though in practice
	// the bits track each other. TLSIO defaults to is_tls_server =
	// (address == nullptr) when constructed via the (addr, port) ctor.
	bool is_tls_server = false;

	// PEM file paths. cert_pem_path + key_pem_path are required when
	// is_tls_server, and required on the client when require_peer_cert
	// on the server side enables mTLS. ca_pem_path is the trust anchor
	// used to verify the peer; required on whichever side verifies.
	std::string cert_pem_path;
	std::string key_pem_path;
	std::string ca_pem_path;

	// Server-side mTLS toggle. When true, the server requests a client
	// cert and aborts the handshake if the client doesn't present one.
	bool require_peer_cert = true;

	// Dev / test escape hatch. Sets SSL_VERIFY_NONE on both sides — do
	// NOT use for real deployments.
	bool insecure_skip_verify = false;

	// "" leaves the library default TLS 1.3 ciphersuite list untouched.
	std::string ciphersuites;
};

namespace tls_detail {

// SIGPIPE is the OpenSSL footgun we inherit from raw write(2): an
// SSL_write to a peer that closed mid-stream raises SIGPIPE and kills
// the process by default. NetIO has the same hazard via fwrite. We
// install SIG_IGN once, the first time any TLSIO is constructed; the
// errno path inside SSL_write then surfaces the EPIPE through our
// usual fail-fast error path instead of terminating the process.
inline void install_sigpipe_ignore_once() {
	static const int once = []() {
		::signal(SIGPIPE, SIG_IGN);
		return 0;
	}();
	(void)once;
}

[[noreturn]] inline void die(const char *what) {
	std::fprintf(stderr, "TLSIO: %s\n", what);
	ERR_print_errors_fp(stderr);
	std::exit(1);
}

}  // namespace tls_detail

class TLSIO : public IOChannel { public:
	int sock = -1;
	bool is_server, quiet;
	bool is_tls_server;

	SSL_CTX *ctx = nullptr;
	SSL     *ssl = nullptr;

	// Send-side staging — same shape as NetIO's send_buf path.
	char *send_buf = nullptr;
	size_t send_ptr = 0;
	bool send_dirty = false;

	// Recv-side staging — same shape as NetIO's recv_buf path.
	char *recv_buf = nullptr;
	size_t recv_ptr = 0;
	size_t recv_fill = 0;

	uint64_t bytes_sent = 0;
	uint64_t bytes_recv = 0;
	uint64_t flushes_count = 0;

#ifndef NDEBUG
	// Debug-only concurrency assertion. TLSIO is not thread-safe (the
	// send_buf coalescing is unlocked, and an SSL* mutates internal
	// state on every read/write); a debug build aborts if two threads
	// enter send_data / recv_data / flush on the same instance at once.
	std::atomic<int> _in_use{0};
	struct touch_guard {
		std::atomic<int> &f;
		const char *op;
		touch_guard(std::atomic<int> &x, const char *o) : f(x), op(o) {
			if (f.fetch_add(1, std::memory_order_acq_rel) != 0) {
				std::fprintf(stderr,
				             "TLSIO race: concurrent %s on the same TLSIO. "
				             "TLSIO is not thread-safe — only one thread may "
				             "touch a given TLSIO at a time.\n",
				             op);
				std::abort();
			}
		}
		~touch_guard() { f.fetch_sub(1, std::memory_order_release); }
	};
#endif

	// (addr == nullptr) → TCP listener; otherwise TCP client. is_tls_server
	// defaults to mirror is_server but TLSConfig overrides if explicitly set.
	TLSIO(const char *address, int port, const TLSConfig &cfg, bool quiet = false)
	    : quiet(quiet) {
		if (port < 0 || port > 65535)
			throw std::runtime_error("Invalid port number!");
		tls_detail::install_sigpipe_ignore_once();
		is_server = (address == nullptr);
		is_tls_server = cfg.is_tls_server;
		init_from_sock(is_server ? tcp::server_listen(port)
		                         : tcp::client_connect(address, port),
		               cfg);
		if (!quiet) std::cout << "TLS connected\n";
	}

	// Wrap an already-connected socket fd. The TLS-role bit is now
	// independent of who accepted the TCP connection, so caller passes
	// it explicitly.
	TLSIO(int existing_sock, bool is_tls_server,
	      const TLSConfig &cfg, bool quiet = true)
	    : quiet(quiet), is_tls_server(is_tls_server) {
		tls_detail::install_sigpipe_ignore_once();
		is_server = false;
		init_from_sock(existing_sock, cfg);
	}

	~TLSIO() {
		flush();
		if (!quiet) {
			std::cout << "Data Sent: \t"     << bytes_sent     << "\n";
			std::cout << "Data Received: \t" << bytes_recv     << "\n";
			std::cout << "Flushes:\t"        << flushes_count  << "\n";
		}
		// Two-phase shutdown: first call sends close_notify, returning
		// 0 means the peer hasn't yet sent its close_notify. A second
		// call drains it if it's there. We do NOT loop on WANT_*: a
		// destructor that hangs (e.g. peer is gone) is a worse bug
		// than a dirty shutdown, and the second call is best-effort.
		if (ssl) {
			int r = SSL_shutdown(ssl);
			if (r == 0) (void)SSL_shutdown(ssl);
			SSL_free(ssl);
			ssl = nullptr;
		}
		if (ctx) {
			SSL_CTX_free(ctx);
			ctx = nullptr;
		}
		if (sock >= 0) {
			::close(sock);
			sock = -1;
		}
		delete[] send_buf;
		delete[] recv_buf;
	}

	void flush() override {
#ifndef NDEBUG
		touch_guard _g(_in_use, "flush");
#endif
		flush_unlocked();
	}

	void init_from_sock(int new_sock, const TLSConfig &cfg) {
		sock = new_sock;
		tcp::set_nodelay(sock);
		send_buf = new char[NETWORK_BUFFER_SIZE2];
		recv_buf = new char[NETWORK_BUFFER_SIZE2];

		ctx = SSL_CTX_new(TLS_method());
		if (!ctx) tls_detail::die("SSL_CTX_new failed");

		// Pin both ends of the version range to TLS 1.3 — known cipher
		// set, known post-handshake semantics, no surprise downgrade.
		if (SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) != 1)
			tls_detail::die("set_min_proto_version");
		if (SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1)
			tls_detail::die("set_max_proto_version");

		if (!cfg.ciphersuites.empty()) {
			if (SSL_CTX_set_ciphersuites(ctx, cfg.ciphersuites.c_str()) != 1)
				tls_detail::die("set_ciphersuites");
		}

		// Cert + key are required on the server, and on the client only
		// when the server is going to ask for them (mTLS). Loading them
		// when the path is non-empty keeps the client side flexible.
		const bool need_cert = cfg.is_tls_server || !cfg.cert_pem_path.empty();
		if (need_cert) {
			if (cfg.cert_pem_path.empty() || cfg.key_pem_path.empty())
				tls_detail::die("cert_pem_path / key_pem_path required");
			if (SSL_CTX_use_certificate_file(ctx, cfg.cert_pem_path.c_str(),
			                                 SSL_FILETYPE_PEM) != 1)
				tls_detail::die("use_certificate_file");
			if (SSL_CTX_use_PrivateKey_file(ctx, cfg.key_pem_path.c_str(),
			                                SSL_FILETYPE_PEM) != 1)
				tls_detail::die("use_PrivateKey_file");
			if (SSL_CTX_check_private_key(ctx) != 1)
				tls_detail::die("check_private_key");
		}

		// Verification setup. We do NOT install a custom verify callback
		// — those are footguns (callers reach for "return 1" and
		// silently disable verification). insecure_skip_verify is the
		// explicit escape hatch.
		if (cfg.insecure_skip_verify) {
			SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
		} else {
			if (cfg.ca_pem_path.empty())
				tls_detail::die("ca_pem_path required (or set insecure_skip_verify)");
			if (SSL_CTX_load_verify_locations(ctx, cfg.ca_pem_path.c_str(), nullptr) != 1)
				tls_detail::die("load_verify_locations");
			int mode = SSL_VERIFY_PEER;
			if (cfg.is_tls_server && cfg.require_peer_cert)
				mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
			SSL_CTX_set_verify(ctx, mode, nullptr);
		}

		ssl = SSL_new(ctx);
		if (!ssl) tls_detail::die("SSL_new failed");
		if (SSL_set_fd(ssl, sock) != 1) tls_detail::die("SSL_set_fd");

		if (is_tls_server) {
			SSL_set_accept_state(ssl);
			if (SSL_accept(ssl) != 1) tls_detail::die("SSL_accept");
		} else {
			SSL_set_connect_state(ssl);
			if (SSL_connect(ssl) != 1) tls_detail::die("SSL_connect");
		}

		// If verification is on, post-handshake check that the chain
		// validated and (when expected) the peer presented a cert.
		if (!cfg.insecure_skip_verify) {
			long v = SSL_get_verify_result(ssl);
			if (v != X509_V_OK)
				tls_detail::die("peer cert verify failed");
			const bool expect_peer_cert =
				is_tls_server ? cfg.require_peer_cert : true;
			if (expect_peer_cert) {
				X509 *peer = SSL_get_peer_certificate(ssl);
				if (!peer) tls_detail::die("peer presented no certificate");
				X509_free(peer);
			}
		}
	}

	// 1-byte ping/pong handshake to verify both directions are alive.
	// is_server (the TCP-acceptor bit) decides who sends first, matching
	// NetIO. Works fine over TLS.
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
#ifndef NDEBUG
		touch_guard _g(_in_use, "send_data");
#endif
		if (len + send_ptr <= (size_t)NETWORK_BUFFER_SIZE2) {
			memcpy(send_buf + send_ptr, data, len);
			send_ptr += len;
		} else {
			if (send_ptr) { ssl_write_all(send_buf, send_ptr); send_ptr = 0; }
			ssl_write_all(data, len);
		}
		send_dirty = true;
	}

	void recv_data_internal(void *data, size_t len) override {
#ifndef NDEBUG
		touch_guard _g(_in_use, "recv_data");
#endif
		// Drain pending sends before blocking on the peer's reply, else
		// any send-then-recv pattern would deadlock with our bytes still
		// staged. SSL_read goes straight to the socket BIO, no implicit
		// drain, so this has to be explicit — same as NetIO's raw read
		// path.
		flush_unlocked();
		bytes_recv += len;
		size_t got = 0;
		while (got < len) {
			if (recv_ptr == recv_fill) {
				size_t n = ssl_read_some(recv_buf, NETWORK_BUFFER_SIZE2);
				if (n == 0) tls_detail::die("recv_data: peer closed");
				recv_ptr = 0;
				recv_fill = n;
			}
			size_t take = recv_fill - recv_ptr;
			if (take > len - got) take = len - got;
			memcpy((char *)data + got, recv_buf + recv_ptr, take);
			recv_ptr += take;
			got += take;
		}
	}

private:
	void flush_unlocked() {
		if (!send_dirty) return;
		++flushes_count;
		if (send_ptr) { ssl_write_all(send_buf, send_ptr); send_ptr = 0; }
		send_dirty = false;
	}

	// Write `len` bytes via SSL_write_ex, retrying on WANT_READ /
	// WANT_WRITE. With SSL_MODE_AUTO_RETRY (default-on since 1.1) and a
	// blocking fd, partial writes don't occur — but TLS 1.3 post-
	// handshake messages (NewSessionTicket, key update) can flip a
	// "write" into WANT_READ, so the loop is required for correctness
	// even in steady state.
	void ssl_write_all(const void *data, size_t len) {
		bytes_sent += len;
		size_t off = 0;
		while (off < len) {
			size_t written = 0;
			ERR_clear_error();
			int rc = SSL_write_ex(ssl, (const char *)data + off, len - off, &written);
			if (rc == 1) {
				off += written;
				continue;
			}
			int err = SSL_get_error(ssl, rc);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
				continue;
			tls_detail::die("SSL_write_ex");
		}
	}

	// Read up to cap bytes via SSL_read_ex, retrying on WANT_READ /
	// WANT_WRITE. Returns whatever's in the next decrypted record (one
	// record at a time); 0 only on clean shutdown (caller treats as
	// EOF). Same partial-fill semantics as NetIO's raw ::read refill.
	size_t ssl_read_some(void *data, size_t cap) {
		while (true) {
			size_t got = 0;
			ERR_clear_error();
			int rc = SSL_read_ex(ssl, data, cap, &got);
			if (rc == 1) return got;
			int err = SSL_get_error(ssl, rc);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
				continue;
			if (err == SSL_ERROR_ZERO_RETURN) return 0;  // peer close_notify
			tls_detail::die("SSL_read_ex");
		}
	}
};

}  // namespace emp
#endif
