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
//
// Same flush contract and thread-safety rules as NetIO. This benchmark runs
// the TLSIO loopback throughput sweep.
//
// Each party holds its own private key and the other party's public key, as
// in a deployment: ALICE generates both Ed25519 key pairs and writes
// alice_key.pem, alice_pub.pem, bob_key.pem and bob_pub.pem (the last one
// renamed into place, so its presence tells BOB the others are complete).

#include <cassert>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "emp-tool/emp-tool.h"

using namespace std;
using namespace emp;

// The key files live under the run's port, so runs on other ports never share
// them.
static int g_port = 0;
static std::string pem_path(const char *name) {
	return "/tmp/emp_tlsio_bench_" + std::to_string(g_port) + "_" + name + ".pem";
}

// Bail with the OpenSSL error queue dumped, on any non-success path.
[[noreturn]] static void key_die(const char *what) {
	fprintf(stderr, "bench_tlsio key gen: %s failed\n", what);
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

static void generate_keys() {
	EVP_PKEY *alice = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
	EVP_PKEY *bob = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
	if (!alice || !bob) key_die("EVP_PKEY_Q_keygen");
	write_private_key(alice, pem_path("alice_key"));
	write_public_key(alice, pem_path("alice_pub"));
	write_private_key(bob, pem_path("bob_key"));
	const std::string ready = pem_path("bob_pub");
	write_public_key(bob, ready + ".tmp");
	if (::rename((ready + ".tmp").c_str(), ready.c_str()) != 0)
		key_die("rename bob_pub");
	EVP_PKEY_free(alice);
	EVP_PKEY_free(bob);
}

// Block until bob_pub.pem exists, with a 5 s cap so a missed handoff
// surfaces as a failure rather than a hang.
static void wait_for_keys() {
	const std::string ready = pem_path("bob_pub");
	struct stat sb;
	for (int i = 0; i < 5000; ++i) {
		if (::stat(ready.c_str(), &sb) == 0) return;
		usleep(1000);
	}
	fprintf(stderr, "bench_tlsio: timed out waiting for the key files\n");
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
	return cfg;
}

// -------------------------------------------------------------------------
// bench(): loopback throughput sweep. Same shape as bench_netio.
// TLS overhead is largest on small messages (per-record AEAD) and shrinks as
// messages grow.
// -------------------------------------------------------------------------
static void bench(TLSIO *io, int party) {
	if (party == ALICE) cout << "--- TLSIO bench ---\n";
	for (long long length = 2; length <= 8192 * 16; length *= 2) {
		long long times = 1024 * 1024 * 128 / length;
		block *data = new block[length];
		if (party == ALICE) {
			auto start = clock_start();
			for (long long i = 0; i < times; ++i)
				io->send_block(data, length);
			double interval = time_from(start);
			cout << "TLSIO loopback size " << length << " :\t"
			     << (length * times * 128) / (interval + 0.0) * 1e6 * 1e-9
			     << " Gbps\n";
		} else {
			for (long long i = 0; i < times; ++i)
				io->recv_block(data, length);
		}
		delete[] data;
	}
}

int main(int argc, char **argv) {
	int port, party;
	party = parse_party(argv);
	port = peer_port();
	g_port = port;

	// Key handoff: ALICE makes both key pairs, BOB waits for them, and each
	// side configures its own private key and the other's public key.
	if (party == ALICE) generate_keys();
	else                wait_for_keys();

	const TLSConfig cfg = make_cfg(party);

	auto io = (party == ALICE) ? TLSIO::listen(port, cfg, true)
	                           : TLSIO::connect(peer_ip(), port, cfg, true);
	bench(io.get(), party);
}
