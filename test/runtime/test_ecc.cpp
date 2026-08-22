#include "emp-tool/emp-tool.h"
#include <cctype>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/obj_mac.h>
#include <cstring>
#include <sys/wait.h>
#include <utility>
#include <unistd.h>
#include <vector>

using namespace std;
using namespace emp;

// Rejection paths are fatal by design: error() _Exit(1)s the process.
template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) dup2(devnull, 2);
		f();
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

class MemoryIO : public IOChannel {
public:
	explicit MemoryIO(std::vector<unsigned char> in) : in_(std::move(in)) {}
	void send_data_internal(const void *data, int64_t nbyte) override {
		const auto *p = static_cast<const unsigned char *>(data);
		out_.insert(out_.end(), p, p + nbyte);
	}
	void recv_data_internal(void *data, int64_t nbyte) override {
		expecting(nbyte >= 0 && off_ <= in_.size() &&
		              static_cast<size_t>(nbyte) <= in_.size() - off_,
		          "MemoryIO: read past input");
		std::memcpy(data, in_.data() + off_, (size_t)nbyte);
		off_ += (size_t)nbyte;
	}
	std::vector<unsigned char> in_, out_;
	size_t off_ = 0;
};

static string hex256(const BIGNUM *n) {
	char *raw = BN_bn2hex(n);
	expecting(raw != nullptr, "ECC test: BN_bn2hex");
	string result(raw);
	OPENSSL_free(raw);
	for (char &c : result) c = (char)tolower((unsigned char)c);
	while (result.size() > 64 && result[0] == '0') result.erase(0, 1);
	while (result.size() < 64) result.insert(0, "0");
	return result;
}

int main() {
	bool ok = true;
	ECGroup G;
	bool unsupported_curve_rejected = dies([] {
		ECGroup unsupported(NID_secp224r1);
	});
	cout << "unsupported EC curve rejected: " << unsupported_curve_rejected << endl;
	ok = ok && unsupported_curve_rejected;
	const Scalar &order = G.order();
	const BIGNUM *order_handle = order.n();
	ok = ok && order_handle != nullptr;

	Scalar ia = G.rand_scalar();
	Scalar ib = G.rand_scalar();
	Scalar ic = G.rand_scalar();
	Scalar id = G.rand_scalar();
	Point a;
	Point b;
	a = G.mul_gen(ia);//g^a
	b = G.mul_gen(ib);//g^a
	ic = ia.add_mod(ib, G.order(), G.bn_ctx());
	Point c = G.mul_gen(ic);//g^{a+b}
	Point d = a.add(b);
	int res = (d == c);
	cout << res<<endl;
	ok = ok && res;


	c = a.mul(ib);//c=a^ib = g^ab
	d = b.mul(ia);//c=a^ib = g^ab
	
	res = (d == c);
	cout << res<<endl;
	ok = ok && res;

	int size = a.size();
	std::vector<unsigned char> tmp(size);
	a.to_bin(tmp.data(), size);
	b.from_bin(&G, tmp.data(), size);

	res = (a==b);
	cout << res<<endl;
	ok = ok && res;

	// Exercise the nonzero/range contract in both the private OpenSSL
	// generator and deterministic test-mode path.
	for (int i = 0; i < 32; ++i) {
		Scalar s = G.rand_scalar();
		ok = ok && !BN_is_zero(s.n()) && BN_cmp(s.n(), order.n()) < 0;
	}
	set_test_mode(true);
	reset_test_seed_counter();
	for (int i = 0; i < 32; ++i) {
		Scalar s = G.rand_scalar();
		ok = ok && !BN_is_zero(s.n()) && BN_cmp(s.n(), order.n()) < 0;
	}
	set_test_mode(false);

	// Peer-controlled point lengths must be rejected in Release too. This
	// exercises IOChannel::recv_pt without opening sockets.
	{
		uint32_t bad_len = MAX_POINT_BYTES + 1;
		std::vector<unsigned char> wire(sizeof(bad_len));
		std::memcpy(wire.data(), &bad_len, sizeof(bad_len));
		MemoryIO io(std::move(wire));
		Point p;
		bool rejected = dies([&] { io.recv_pt(&G, &p); });
		cout << "recv_pt oversized length rejected: " << rejected << endl;
		ok = ok && rejected;
	}

	{
		const unsigned char infinity[] = {0x00};
		const unsigned char malformed[] = {0x05};
		bool infinity_rejected = dies([&] {
			Point p;
			p.from_bin(&G, infinity, sizeof(infinity));
		});
		bool malformed_rejected = dies([&] {
			Point p;
			p.from_bin(&G, malformed, sizeof(malformed));
		});
		cout << "infinity/malformed points rejected: "
		     << (infinity_rejected && malformed_rejected) << endl;
		ok = ok && infinity_rejected && malformed_rejected;
	}

	// Signed public counts must fail before counter updates, multiplication,
	// pointer arithmetic, or conversion to an unsigned library size.
	{
		MemoryIO io(std::vector<unsigned char>{});
		char byte = 0;
		block blk = zero_block;
		bool bit = false;
		bool rejected =
			dies([&] { io.send_data(&byte, -1); }) &&
			dies([&] { io.recv_data(&byte, -1); }) &&
			dies([&] { io.send_block(&blk, -1); }) &&
			dies([&] { io.recv_block(&blk, -1); }) &&
			dies([&] { io.send_block(
				&blk, std::numeric_limits<int64_t>::max()); }) &&
			dies([&] { io.recv_block(
				&blk, std::numeric_limits<int64_t>::max()); }) &&
			dies([&] { io.send_bool(&bit, -1); }) &&
			dies([&] { io.recv_bool(&bit, -1); }) &&
			dies([&] { io.send_pt(nullptr, -1); }) &&
			dies([&] { io.recv_pt(&G, nullptr, -1); }) &&
			dies([&] {
				io.send_counter = std::numeric_limits<uint64_t>::max();
				io.send_data(&byte, 1);
			}) &&
			dies([&] {
				io.recv_counter = std::numeric_limits<uint64_t>::max();
				io.recv_data(&byte, 1);
			});
		cout << "IO negative/overflow counts rejected: " << rejected << endl;
		ok = ok && rejected;
	}

	// hash_to_point against RFC 9380 §J.1.1 P256_XMD:SHA-256_SSWU_RO_
	// vectors (DST = "QUUX-V01-CS02-with-P256_XMD:SHA-256_SSWU_RO_").
	struct H2C_Vec { const char *msg; const char *x_hex; const char *y_hex; };
	H2C_Vec vecs[] = {
		{"",                 "2c15230b26dbc6fc9a37051158c95b79656e17a1a920b11394ca91c44247d3e4",
		                         "8a7a74985cc5c776cdfe4b1f19884970453912e9d31528c060be9ab5c43e8415"},
		{"abc",              "0bb8b87485551aa43ed54f009230450b492fead5f1cc91658775dac4a3388a0f",
		                         "5c41b3d0731a27a7b14bc0bf0ccded2d8751f83493404c84a88e71ffd424212e"},
		{"abcdef0123456789", "65038ac8f2b1def042a5df0b33b1f4eca6bff7cb0f9c6c1526811864e544ed80",
		                         "cad44d40a656e7aff4002a8de287abc8ae0482b5ae825822bb870d6df9b56ca3"},
	};
	static constexpr const char kRFC9380DST[] =
		"QUUX-V01-CS02-with-P256_XMD:SHA-256_SSWU_RO_";
	bool empty_dst_rejected = dies([&] {
		G.hash_to_point("abc", 3, "", 0);
	});
	string long_dst(256, 'd');
	bool long_dst_rejected = dies([&] {
		G.hash_to_point("abc", 3, long_dst.data(), long_dst.size());
	});
	cout << "invalid H2C DST lengths rejected: "
	     << (empty_dst_rejected && long_dst_rejected) << endl;
	ok = ok && empty_dst_rejected && long_dst_rejected;

	ERR_clear_error();
	for (const auto &v : vecs) {
		Point P = G.hash_to_point(v.msg, strlen(v.msg),
		                          kRFC9380DST, sizeof(kRFC9380DST) - 1);
		bool queue_clean = ERR_peek_error() == 0;
		BIGNUM *xb = BN_new(), *yb = BN_new();
		expecting(xb && yb, "ECC test: BN_new");
		expecting(EC_POINT_get_affine_coordinates(
		              G.ec_group(), P.point(), xb, yb, G.bn_ctx()) == 1,
		          "ECC test: EC_POINT_get_affine_coordinates");
		bool match = hex256(xb) == v.x_hex && hex256(yb) == v.y_hex;
		cout << "h2c \"" << v.msg << "\" coordinates/error queue: "
		     << (match && queue_clean) << endl;
		ok = ok && match && queue_clean;
		BN_free(xb); BN_free(yb);
	}

	// A successful hash-to-curve must not consume or obscure errors owned by
	// its caller, even when an expected nonsquare occurs internally.
	ERR_clear_error();
	ERR_raise(ERR_LIB_USER, 0x345);
	const unsigned long sentinel = ERR_peek_last_error();
	expecting(sentinel != 0, "ECC test: install OpenSSL error sentinel");
	G.hash_to_point("preserve-error-queue", 20,
	                kRFC9380DST, sizeof(kRFC9380DST) - 1);
	bool sentinel_preserved = ERR_peek_error() == sentinel &&
	                          ERR_peek_last_error() == sentinel;
	cout << "h2c preserves caller OpenSSL errors: " << sentinel_preserved << endl;
	ok = ok && sentinel_preserved;
	ERR_clear_error();

	return ok ? 0 : 1;
}
