#ifndef EMP_TLS_IO_CHANNEL_H__
#define EMP_TLS_IO_CHANNEL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <memory>
#include <string>

#include "emp-tool/runtime/core/utils.h"   // error()
#include "emp-tool/runtime/io/io_channel.h"
#include "emp-tool/runtime/io/tcp_socket.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <signal.h>
#include <unistd.h>

namespace emp {

// TLSIO — single-socket full-duplex IOChannel running TLS 1.3 over one
// TCP fd. Same shape as NetIO (32 KiB user-space send + recv staging,
// same flush contract, single-thread-owned, same telemetry counters);
// the wire primitives are SSL_write_ex / SSL_read_ex.
//
// Why no BIO_f_buffer on top of the socket BIO: small writes are already
// coalesced into the 32 KiB staging buffer above SSL_write, so each
// SSL_write produces at most one or two records.
//
// Why an SSL_CTX per channel family: a channel and its siblings share
// one context, and there is no global state. Independent TLSIOs each pay
// for reading the keys and signing a certificate.
//
// Authentication is by pinned public keys, with no CA: TLSConfig names
// this party's private key and the peer's public key. TLS 1.3 carries a
// key in a certificate, so each end presents a throwaway self-signed
// certificate for its key, and accepts the peer's only if it carries the
// pinned key; the handshake signature proves the peer holds the matching
// private key.

struct TLSConfig {
	// Role on the TLS handshake. Independent of the address-vs-port
	// role used to establish the underlying TCP connection.
	bool is_tls_server = false;

	// The keys that authenticate the connection, as PEM files, both
	// required: this party's private key, and the peer's public key (a
	// SubjectPublicKeyInfo, "BEGIN PUBLIC KEY", as `openssl pkey -pubout`
	// writes it). Any key type OpenSSL signs TLS 1.3 handshakes with
	// works (Ed25519, ECDSA, RSA), and the two parties' types may differ.
	std::string key_pem_path;
	std::string peer_public_key_pem_path;

	// "" leaves the library default TLS 1.3 ciphersuite list untouched.
	std::string ciphersuites;

	tcp::SocketOptions socket_options;
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
	// The OpenSSL error queue folds into one diagnostic line, reported
	// through error() like every other failure (docs/api_conventions.md).
	// error() uses _Exit, not exit(): a TLS I/O failure can fire on a
	// worker thread; running destructors / atexit while sibling threads
	// are live races their heap use.
	std::string msg = std::string("TLSIO: ") + what;
	for (unsigned long e = ERR_get_error(); e != 0; e = ERR_get_error()) {
		char buf[256];
		ERR_error_string_n(e, buf, sizeof(buf));
		msg += "; ";
		msg += buf;
	}
	error(msg.c_str());
}

// The private key in a PEM file.
inline EVP_PKEY *read_private_key(const std::string &path) {
	BIO *bio = BIO_new_file(path.c_str(), "r");
	if (!bio) die("cannot open key_pem_path");
	EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);
	if (!key) die("key_pem_path holds no private key");
	return key;
}

// The public key in a PEM file ("BEGIN PUBLIC KEY").
inline EVP_PKEY *read_public_key(const std::string &path) {
	BIO *bio = BIO_new_file(path.c_str(), "r");
	if (!bio) die("cannot open peer_public_key_pem_path");
	EVP_PKEY *key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);
	if (!key) die("peer_public_key_pem_path holds no public key");
	return key;
}

// A throwaway self-signed certificate for `key`. The peer reads only the key
// from it, so its name and dates carry no meaning.
inline X509 *self_signed_cert(EVP_PKEY *key) {
	X509 *cert = X509_new();
	if (!cert) die("X509_new");
	X509_NAME *name = X509_get_subject_name(cert);
	// Ed25519 and Ed448 sign the message itself, with no separate digest.
	const int type = EVP_PKEY_get_id(key);
	const EVP_MD *md =
	    (type == EVP_PKEY_ED25519 || type == EVP_PKEY_ED448) ? nullptr : EVP_sha256();
	const bool built =
	    X509_set_version(cert, X509_VERSION_3) == 1 &&
	    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) == 1 &&
	    X509_gmtime_adj(X509_getm_notBefore(cert), 0) != nullptr &&
	    X509_gmtime_adj(X509_getm_notAfter(cert), 24 * 60 * 60) != nullptr &&
	    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
	                               reinterpret_cast<const unsigned char *>("emp-tool"),
	                               -1, -1, 0) == 1 &&
	    X509_set_issuer_name(cert, name) == 1 &&
	    X509_set_pubkey(cert, key) == 1 &&
	    X509_sign(cert, key, md) > 0;
	if (!built) {
		X509_free(cert);
		die("cannot build a self-signed certificate for key_pem_path");
	}
	return cert;
}

}  // namespace tls_detail

class TLSIO : public IOChannel {
public:
	// Byte counts and flush count live on the IOChannel base.

	// NOT thread-safe (unlocked send-buffer coalescing, and an SSL*
	// mutates internal state on every read/write): one thread at a time
	// per channel. Races are not detected at runtime — use TSan.

	// (addr == nullptr) → TCP listener; otherwise TCP client. The TLS role
	// comes from TLSConfig and is independent of the TCP role. A listener
	// stays open while this channel or a sibling of it is alive, as with
	// NetIO, so make_sibling() can accept more connections on the port.
	TLSIO(const char *address, int port, const TLSConfig &cfg, bool quiet = false)
	    : quiet(quiet), is_tls_server(cfg.is_tls_server) {
		expecting(port >= 0 && port <= 65535,
		          "TLSIO: invalid port number");
		tls_detail::install_sigpipe_ignore_once();
		port_ = port;
		socket_options_ = cfg.socket_options;
		setup_context(cfg);
		int connected;
		if (address == nullptr) {
			listener = std::make_shared<tcp::ListenerHandle>(
			    tcp::open_listener(port, cfg.socket_options));
			// The accepted socket inherits the listener's options.
			connected = tcp::accept_one(listener->fd);
		} else {
			addr_ = address;
			connected = tcp::client_connect(address, port, cfg.socket_options);
		}
		handshake(connected);
		set_session_root(export_session_root());
		if (!quiet) std::cout << "TLS connected\n";
	}

	// Named factories owning their result (auto-freed via unique_ptr); listen()
	// takes no address, connect() requires one — mirrors NetIO::listen/connect.
	static std::unique_ptr<TLSIO> listen(int port, const TLSConfig &cfg, bool quiet = false) {
		return std::make_unique<TLSIO>(nullptr, port, cfg, quiet);
	}
	static std::unique_ptr<TLSIO> connect(const char *address, int port,
	                                      const TLSConfig &cfg, bool quiet = false) {
		return std::make_unique<TLSIO>(address, port, cfg, quiet);
	}

	// Wrap an already-connected socket fd. The TLS-role bit is
	// independent of who accepted the TCP connection, so the caller
	// passes it explicitly.
	TLSIO(int existing_sock, bool is_tls_server,
	      const TLSConfig &cfg, bool quiet = true)
	    : quiet(quiet), is_tls_server(is_tls_server) {
		expecting(cfg.socket_options.send_buffer_size == 0 &&
		              cfg.socket_options.receive_buffer_size == 0,
		          "TLSIO: socket buffer options cannot be applied to an already-connected socket");
		tls_detail::install_sigpipe_ignore_once();
		setup_context(cfg);
		handshake(existing_sock);
		set_session_root(export_session_root());
	}

	// Open another TLS channel to the same peer and port, like
	// NetIO::make_sibling(): related server channels share the listener, the
	// client connects again, and both ends must call it serially and in the
	// same order. The sibling shares this channel's TLS context (its key and
	// the pinned peer key, loaded once) but runs its own handshake, so it has
	// its own traffic keys. Its session root is this channel's next session
	// id. A channel adopted from a socket has no port to reconnect to.
	std::unique_ptr<TLSIO> make_sibling() {
		int connected;
		if (listener) {
			connected = tcp::accept_one(listener->fd);
		} else {
			expecting(!addr_.empty(),
			          "TLSIO::make_sibling: a channel adopted from a socket "
			          "cannot open siblings");
			connected = tcp::client_connect(addr_.c_str(), port_, socket_options_);
		}
		return std::unique_ptr<TLSIO>(
		    new TLSIO(*this, connected, next_session_id().value()));
	}

	// Non-copyable, non-movable: owns raw fd / OpenSSL handles /
	// buffers that would multi-free.
	TLSIO(const TLSIO&)             = delete;
	TLSIO& operator=(const TLSIO&)  = delete;
	TLSIO(TLSIO&&)                  = delete;
	TLSIO& operator=(TLSIO&&)       = delete;

	~TLSIO() {
		flush();
		if (!quiet)
			std::cout << get_statistics_string();
		// Send close_notify without waiting for the peer's. Waiting would
		// block until the peer destroys its end, so teardown orders that
		// complete over NetIO would deadlock here. Every recv has an exact
		// length, so a stream cut short already stops the process.
		if (ssl) {
			(void)SSL_shutdown(ssl);
			SSL_free(ssl);
			ssl = nullptr;
		}
		if (ctx) {
			SSL_CTX_free(ctx);
			ctx = nullptr;
		}
		EVP_PKEY_free(peer_key);
		if (sock >= 0) {
			::close(sock);
			sock = -1;
		}
		delete[] send_buf;
		delete[] recv_buf;
	}

	void flush() override {
		flush_unlocked();
	}

	// The connected socket, for socket options and diagnostics. Reading or
	// writing it directly bypasses TLS and corrupts the stream.
	int fd() const { return sock; }

protected:
	void send_data_internal(const void *data, int64_t len) override {
		expecting(len >= 0, "TLSIO::send_data: negative len");
		if (len == 0) return;
		if (len <= (int64_t)(NETWORK_STAGING_BUFFER_SIZE - send_ptr)) {
			memcpy(send_buf + send_ptr, data, len);
			send_ptr += len;
		} else {
			if (send_ptr) { ssl_write_all(send_buf, send_ptr); send_ptr = 0; }
			ssl_write_all(data, len);
		}
		send_dirty = true;
	}

	void recv_data_internal(void *data, int64_t len) override {
		expecting(len >= 0, "TLSIO::recv_data: negative len");
		if (len == 0) return;
		// Drain pending sends before blocking on the peer's reply, else
		// any send-then-recv pattern would deadlock with our bytes still
		// staged. SSL_read goes straight to the socket BIO, no implicit
		// drain, so this has to be explicit.
		flush_unlocked();
		int64_t got = 0;
		while (got < len) {
			if (recv_ptr == recv_fill) {
				size_t n = ssl_read_some(recv_buf, NETWORK_STAGING_BUFFER_SIZE);
				if (n == 0) tls_detail::die("recv_data: peer closed");
				recv_ptr = 0;
				recv_fill = n;
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
	bool quiet;
	bool is_tls_server;

	// Where siblings come from: the shared listener on the listening side,
	// the peer's address on the connecting side.
	std::shared_ptr<tcp::ListenerHandle> listener;
	std::string addr_;
	int port_ = -1;
	tcp::SocketOptions socket_options_;

	SSL_CTX *ctx = nullptr;
	SSL     *ssl = nullptr;

	// The peer's pinned public key, and whether the handshake met a
	// certificate carrying another key (for the error message).
	EVP_PKEY *peer_key = nullptr;
	bool peer_key_rejected = false;

	// Send-side staging — same shape as NetIO's send_buf path.
	char *send_buf = nullptr;
	size_t send_ptr = 0;
	bool send_dirty = false;

	// Recv-side staging — same shape as NetIO's recv_buf path.
	char *recv_buf = nullptr;
	size_t recv_ptr = 0;
	size_t recv_fill = 0;

	// A sibling (make_sibling()): shares `parent`'s TLS context and where its
	// connections come from, runs its own handshake on `connected`, and takes
	// `root` as its session root.
	TLSIO(TLSIO &parent, int connected, block root)
	    : quiet(true), is_tls_server(parent.is_tls_server),
	      listener(parent.listener), addr_(parent.addr_), port_(parent.port_),
	      socket_options_(parent.socket_options_) {
		if (SSL_CTX_up_ref(parent.ctx) != 1) tls_detail::die("SSL_CTX_up_ref");
		ctx = parent.ctx;
		if (EVP_PKEY_up_ref(parent.peer_key) != 1) tls_detail::die("EVP_PKEY_up_ref");
		peer_key = parent.peer_key;
		handshake(connected);
		set_session_root(root);
	}

	// The TLS context, shared with siblings: TLS 1.3 only, this party's key
	// and its throwaway certificate, and the check of the peer's key.
	void setup_context(const TLSConfig &cfg) {
		ctx = SSL_CTX_new(TLS_method());
		if (!ctx) tls_detail::die("SSL_CTX_new failed");
		// TLSIO never resumes a session, so a server sends no session
		// tickets: nothing arrives after the handshake for a send-only peer
		// to leave unread, and unread bytes at close() make the kernel
		// reset the connection, which can discard data its peer has not
		// read yet. Nor does the shared context cache sessions.
		if (SSL_CTX_set_num_tickets(ctx, 0) != 1)
			tls_detail::die("set_num_tickets");
		SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);

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

		if (cfg.key_pem_path.empty() || cfg.peer_public_key_pem_path.empty())
			tls_detail::die("key_pem_path and peer_public_key_pem_path are required");
		EVP_PKEY *key = tls_detail::read_private_key(cfg.key_pem_path);
		peer_key = tls_detail::read_public_key(cfg.peer_public_key_pem_path);
		X509 *cert = tls_detail::self_signed_cert(key);
		const bool loaded = SSL_CTX_use_certificate(ctx, cert) == 1 &&
		                    SSL_CTX_use_PrivateKey(ctx, key) == 1 &&
		                    SSL_CTX_check_private_key(ctx) == 1;
		X509_free(cert);
		EVP_PKEY_free(key);
		if (!loaded) tls_detail::die("cannot use key_pem_path for the handshake");

		// Both ends present a certificate and check the peer's, and the check
		// is verify_pinned_key alone. It is not a caller hook: TLSConfig has
		// no way to turn it off.
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
		                   &TLSIO::verify_pinned_key);
	}

	// Run this channel's handshake on a connected socket.
	void handshake(int connected) {
		sock = connected;
		tcp::set_nodelay(sock);
		send_buf = new char[NETWORK_STAGING_BUFFER_SIZE];
		recv_buf = new char[NETWORK_STAGING_BUFFER_SIZE];

		ssl = SSL_new(ctx);
		if (!ssl) tls_detail::die("SSL_new failed");
		if (SSL_set_fd(ssl, sock) != 1) tls_detail::die("SSL_set_fd");
		SSL_set_app_data(ssl, this);

		// A TLS 1.3 client finishes its handshake before the server has
		// checked the client's key, so a client the server rejects fails at
		// its next read instead.
		auto handshake_failed = [&](const char *what) {
			tls_detail::die(peer_key_rejected
			                    ? "the peer's public key is not the pinned one"
			                    : what);
		};
		if (is_tls_server) {
			SSL_set_accept_state(ssl);
			if (SSL_accept(ssl) != 1) handshake_failed("SSL_accept");
		} else {
			SSL_set_connect_state(ssl);
			if (SSL_connect(ssl) != 1) handshake_failed("SSL_connect");
		}

		// The callback already decided; this restates its outcome on the
		// finished handshake.
		X509 *peer = SSL_get0_peer_certificate(ssl);
		if (!peer || EVP_PKEY_eq(X509_get0_pubkey(peer), peer_key) != 1)
			tls_detail::die("the peer's public key is not the pinned one");
	}

	// SSL_CTX verify callback: accept the peer's certificate exactly when it
	// carries the pinned key. The certificate is self-signed, so OpenSSL's
	// chain verdict (preverify_ok) is expected to fail and carries no weight;
	// only the key of the certificate at depth 0, the peer's own, decides.
	static int verify_pinned_key(int /*preverify_ok*/, X509_STORE_CTX *store) {
		if (X509_STORE_CTX_get_error_depth(store) != 0) return 1;
		SSL *ssl = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(
		    store, SSL_get_ex_data_X509_STORE_CTX_idx()));
		TLSIO *self = static_cast<TLSIO *>(SSL_get_app_data(ssl));
		X509 *cert = X509_STORE_CTX_get_current_cert(store);
		const bool pinned =
		    cert != nullptr && EVP_PKEY_eq(X509_get0_pubkey(cert), self->peer_key) == 1;
		if (!pinned) self->peer_key_rejected = true;
		return pinned ? 1 : 0;
	}

	// The next_session_id() root comes from the TLS exporter (RFC 8446,
	// section 7.5) instead of a coin flip. Both ends derive it from the
	// handshake secret with no message, so it is bound to this handshake
	// with the peer holding the pinned key. The label is unregistered, so it
	// takes RFC 5705's private-use prefix.
	block export_session_root() {
		static constexpr char kLabel[] = "EXPERIMENTAL-emp-tool-session-id";
		block root;
		if (SSL_export_keying_material(ssl, reinterpret_cast<unsigned char *>(&root),
		                               sizeof(root), kLabel, sizeof(kLabel) - 1,
		                               nullptr, 0, /*use_context=*/0) != 1)
			tls_detail::die("SSL_export_keying_material");
		return root;
	}

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
	// EOF).
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
