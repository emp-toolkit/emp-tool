// io/tls_io_channel.h — TLS-1.3 IOChannel over a single TCP fd.
//
// Public surface:
//   ctor(addr, port, cfg)              open one TCP fd + complete TLS handshake
//   ctor(existing_sock, role, cfg)     wrap an already-connected fd
//   send_data / recv_data              raw bytes
//   send_block / recv_block            block-typed wrapper (inherited)
//   send_bool / recv_bool              packed via bools_to_bits (inherited)
//   flush()                            drain outbound coalescing buffer
//   sync()                             1-byte ping/pong handshake
//
// Same flush contract and thread-safety rules as NetIO. The test
// mirrors test_netio.cpp's correctness + send-only regression + bench
// suite, exercising the same paths against TLSIO.
//
// Cert material is laid out the way a real mTLS deployment would be —
// five distinct files with five distinct roles, so the test reads as a
// tutorial for what each TLSConfig path actually is:
//
//   ca_cert.pem        VK_CA — the trust anchor both sides verify against
//   alice_cert.pem     VK_alice + Sign(SK_CA, VK_alice ‖ "alice")
//   alice_key.pem      SK_alice — ALICE's signing key
//   bob_cert.pem       VK_bob   + Sign(SK_CA, VK_bob   ‖ "bob")
//   bob_key.pem        SK_bob   — BOB's signing key
//
// ALICE generates everything (CA keypair, ALICE keypair, BOB keypair),
// signs ALICE's and BOB's certs with the CA, and writes the five PEM
// files. The CA's private key never touches disk — only its cert
// (= VK_CA) is published. In a real deployment ALICE and BOB would
// each generate their own SK locally and hand the CA only a CSR; the
// test conflates this for simplicity but the file layout matches.
//
// BOB polls for ca_cert.pem (written last as the readiness sentinel),
// then both sides build a TLSConfig pointing at their respective
// (cert, key, ca) triple. Real verification runs both directions —
// no insecure_skip_verify shortcut.

#include <cassert>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "emp-tool/emp-tool.h"

using namespace std;
using namespace emp;

static const char *CA_CERT    = "/tmp/emp_tlsio_test_ca_cert.pem";
static const char *ALICE_CERT = "/tmp/emp_tlsio_test_alice_cert.pem";
static const char *ALICE_KEY  = "/tmp/emp_tlsio_test_alice_key.pem";
static const char *BOB_CERT   = "/tmp/emp_tlsio_test_bob_cert.pem";
static const char *BOB_KEY    = "/tmp/emp_tlsio_test_bob_key.pem";

// Bail with the OpenSSL error queue dumped, on any non-success path.
// Used in lieu of assert() because the test runs Release-by-default and
// assert() is a no-op there — most of these calls have side effects.
[[noreturn]] static void cert_die(const char *what) {
	fprintf(stderr, "test_tlsio cert gen: %s failed\n", what);
	ERR_print_errors_fp(stderr);
	exit(1);
}

// One in-memory keypair + cert. `pkey` is the SK; `cert` carries the
// matching VK plus the CA's signature binding it to a name.
struct CertKey {
	EVP_PKEY *pkey = nullptr;
	X509     *cert = nullptr;
};

// EVP_PKEY_CTX route (vs the simpler EVP_PKEY_Q_keygen) so this
// compiles on OpenSSL 1.1 as well as 3.x.
static EVP_PKEY *gen_rsa_keypair() {
	EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
	if (!pctx) cert_die("EVP_PKEY_CTX_new_id");
	if (EVP_PKEY_keygen_init(pctx) != 1) cert_die("EVP_PKEY_keygen_init");
	if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) != 1)
		cert_die("EVP_PKEY_CTX_set_rsa_keygen_bits");
	EVP_PKEY *pkey = nullptr;
	if (EVP_PKEY_keygen(pctx, &pkey) != 1) cert_die("EVP_PKEY_keygen");
	EVP_PKEY_CTX_free(pctx);
	return pkey;
}

// Build an X509 with subject CN=common_name, public key VK from `pkey`.
// If `signer == nullptr`, the cert is self-signed (issuer = subject,
// signed with `pkey`) — that's the CA case. Otherwise issuer is
// `signer->cert`'s subject and the signature is by `signer->pkey` —
// that's the end-entity case (alice / bob).
static CertKey gen_certkey(const char *common_name, const CertKey *signer) {
	CertKey ck;
	ck.pkey = gen_rsa_keypair();
	ck.cert = X509_new();
	if (!ck.cert) cert_die("X509_new");

	ASN1_INTEGER_set(X509_get_serialNumber(ck.cert), 1);
	X509_gmtime_adj(X509_get_notBefore(ck.cert), 0);
	X509_gmtime_adj(X509_get_notAfter(ck.cert), 60L * 60 * 24);
	if (X509_set_pubkey(ck.cert, ck.pkey) != 1) cert_die("X509_set_pubkey");

	X509_NAME *subj = X509_get_subject_name(ck.cert);
	X509_NAME_add_entry_by_txt(subj, "CN", MBSTRING_ASC,
	                           (const unsigned char *)common_name, -1, -1, 0);

	X509_NAME *issuer = signer ? X509_get_subject_name(signer->cert) : subj;
	if (X509_set_issuer_name(ck.cert, issuer) != 1)
		cert_die("X509_set_issuer_name");

	EVP_PKEY *signing_key = signer ? signer->pkey : ck.pkey;
	if (X509_sign(ck.cert, signing_key, EVP_sha256()) <= 0)
		cert_die("X509_sign");
	return ck;
}

static void free_certkey(CertKey &ck) {
	if (ck.pkey) EVP_PKEY_free(ck.pkey);
	if (ck.cert) X509_free(ck.cert);
	ck.pkey = nullptr;
	ck.cert = nullptr;
}

static void write_cert_pem(X509 *cert, const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) cert_die("fopen cert");
	if (PEM_write_X509(f, cert) != 1) cert_die("PEM_write_X509");
	fclose(f);
}

// 0600 perms from creation (not chmod'd after) so we never publish a
// world-readable private key, even briefly.
static void write_key_pem(EVP_PKEY *key, const char *path) {
	int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) cert_die("open key");
	FILE *f = fdopen(fd, "wb");
	if (!f) cert_die("fdopen key");
	if (PEM_write_PrivateKey(f, key, nullptr, nullptr, 0, nullptr, nullptr) != 1)
		cert_die("PEM_write_PrivateKey");
	fclose(f);
}

// Set up the whole PKI: a CA that signs separate end-entity certs for
// ALICE and BOB. Writes 5 PEM files; CA's private key stays in memory
// and is freed at the end. CA_CERT is renamed-into-place last so it
// doubles as the readiness sentinel for BOB.
static void generate_pki() {
	CertKey ca    = gen_certkey("emp-tool-test CA", nullptr);
	CertKey alice = gen_certkey("alice", &ca);
	CertKey bob   = gen_certkey("bob",   &ca);

	write_cert_pem(alice.cert, ALICE_CERT);
	write_key_pem (alice.pkey, ALICE_KEY);
	write_cert_pem(bob.cert,   BOB_CERT);
	write_key_pem (bob.pkey,   BOB_KEY);

	std::string ca_tmp = std::string(CA_CERT) + ".tmp";
	write_cert_pem(ca.cert, ca_tmp.c_str());
	if (::rename(ca_tmp.c_str(), CA_CERT) != 0) cert_die("rename ca cert");

	free_certkey(alice);
	free_certkey(bob);
	free_certkey(ca);
}

// Block until CA_CERT exists. ALICE writes the four end-entity files
// first and renames-in CA_CERT last, so its presence implies all five
// files are fully written. 5 s cap so a missed-handoff bug surfaces as
// a test failure rather than a hang.
static void wait_for_pki() {
	struct stat sb;
	for (int i = 0; i < 5000; ++i) {
		if (::stat(CA_CERT, &sb) == 0) return;
		usleep(1000);
	}
	fprintf(stderr, "test_tlsio: timed out waiting for PKI files\n");
	exit(1);
}

static TLSConfig make_cfg(int party) {
	TLSConfig cfg;
	// Mirror test_netio.cpp: ALICE runs the TCP listener. We make ALICE
	// the TLS server too — both sides present a cert (mTLS) and verify
	// the peer's cert against the same CA.
	cfg.is_tls_server = (party == ALICE);
	cfg.cert_pem_path = (party == ALICE) ? ALICE_CERT : BOB_CERT;
	cfg.key_pem_path  = (party == ALICE) ? ALICE_KEY  : BOB_KEY;
	cfg.ca_pem_path   = CA_CERT;       // both trust the same CA
	cfg.require_peer_cert    = true;   // exercise the mTLS path
	cfg.insecure_skip_verify = false;
	return cfg;
}

// -------------------------------------------------------------------------
// run_correctness(): byte stream round-trip at unaligned offsets, then bool
// packing round-trip at unaligned bool offsets. Same shape as test_netio.
// -------------------------------------------------------------------------
static void run_correctness(TLSIO *io, int party) {
	{
		int length = NETWORK_BUFFER_SIZE2 / 5 + 100;
		char *data  = new char[length];
		char *data2 = new char[length];
		PRG prg(&zero_block);
		for (int i = 0; i < 1000; ++i) {
			if (party == ALICE) {
				prg.random_data_unaligned(data, length);
				io->send_data(data, length);
				io->send_data(data, length);
				io->recv_data(data2, length);
				assert(memcmp(data, data2, length) == 0);
			} else {
				prg.random_data_unaligned(data2, length);
				io->recv_data(data, length);
				io->recv_data(data, length);
				io->send_data(data2, length);
				assert(memcmp(data, data2, length) == 0);
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
			assert(memcmp(data2, data, 1024 * 1024) == 0);
			memset(data2, 0, 1024 * 1024);
			io->recv_bool(data2 + 7, 1024 * 1024 - 7);
			assert(memcmp(data2 + 7, data + 7, 1024 * 1024 - 7) == 0);
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
// flush and (b) destructor. Under TLS, (b) additionally exercises the
// two-phase SSL_shutdown drain — a regression that closes the fd before
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
		TLSIO io(party == ALICE ? nullptr : "127.0.0.1", port + 1, cfg, true);
		if (party == ALICE) {
			io.send_data(data, N);
			io.flush();
			char ack = 0;
			io.recv_data(&ack, 1);
			assert(ack == 1);
		} else {
			io.recv_data(data2, N);
			assert(memcmp(data, data2, N) == 0);
			char ack = 1;
			io.send_data(&ack, 1);
			io.flush();
		}
	}

	{
		TLSIO *io = new TLSIO(party == ALICE ? nullptr : "127.0.0.1", port + 2, cfg, true);
		if (party == ALICE) {
			io->send_data(data, N);
			delete io;                  // must flush + SSL_shutdown
		} else {
			io->recv_data(data2, N);
			assert(memcmp(data, data2, N) == 0);
			delete io;
		}
	}

	delete[] data;
	delete[] data2;
	if (party == ALICE) cout << "TLSIO send-only regression: OK\n";
}

// -------------------------------------------------------------------------
// bench(): loopback throughput sweep. Same shape as test_netio's bench.
// Expect TLS overhead to drop to a few % at 16+ KiB messages and be more
// pronounced on small messages (per-record AEAD).
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
	parse_party_and_port(argv, &party, &port);

	// PKI handoff. ALICE (party 1, the TCP listener) builds the whole
	// CA + ALICE + BOB PKI in memory and writes 5 PEM files; BOB polls
	// until ca_cert.pem appears (the readiness sentinel, renamed into
	// place last). Both sides then build a TLSConfig pointing at their
	// own (cert, key) and the shared CA, and run the suite.
	if (party == ALICE) generate_pki();
	else                wait_for_pki();

	const TLSConfig cfg = make_cfg(party);

	TLSIO *io = new TLSIO(party == ALICE ? nullptr : "127.0.0.1", port, cfg, true);
	run_correctness(io, party);
	run_send_only_regression(port, party);
	bench(io, party);
	delete io;
}
