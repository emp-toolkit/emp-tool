#include "emp-tool/emp-tool.h"
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <openssl/ec.h>
#include <openssl/bn.h>
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

int main() {
	bool ok = true;
	ECGroup G;
	bool unsupported_curve_rejected = dies([] {
		ECGroup unsupported(NID_secp224r1);
	});
	cout << "unsupported EC curve rejected: " << unsupported_curve_rejected << endl;
	ok = ok && unsupported_curve_rejected;

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
	struct H2C_Vec { const char *msg; const char *x_hex; };
	H2C_Vec vecs[] = {
		{"",                 "2c15230b26dbc6fc9a37051158c95b79656e17a1a920b11394ca91c44247d3e4"},
		{"abc",              "0bb8b87485551aa43ed54f009230450b492fead5f1cc91658775dac4a3388a0f"},
		{"abcdef0123456789", "65038ac8f2b1def042a5df0b33b1f4eca6bff7cb0f9c6c1526811864e544ed80"},
	};
	static constexpr const char kRFC9380DST[] =
		"QUUX-V01-CS02-with-P256_XMD:SHA-256_SSWU_RO_";
	for (const auto &v : vecs) {
		Point P = G.hash_to_point(v.msg, strlen(v.msg),
		                          kRFC9380DST, sizeof(kRFC9380DST) - 1);
		BIGNUM *xb = BN_new(), *yb = BN_new();
		EC_POINT_get_affine_coordinates(G.ec_group(), P.point(), xb, yb, G.bn_ctx());
		char *xh = BN_bn2hex(xb);
		// OpenSSL emits uppercase; lowercase for comparison.
		std::string got(xh);
		for (auto &c : got) c = (char)tolower((unsigned char)c);
		// Strip leading zeros from got to match fixed-width vector.
		while (got.size() > 64 && got[0] == '0') got.erase(0, 1);
		while (got.size() < 64) got.insert(0, "0");
		bool match = (got == v.x_hex);
		cout << "h2c \"" << v.msg << "\" x match: " << match << endl;
		ok = ok && match;
		OPENSSL_free(xh);
		BN_free(xb); BN_free(yb);
	}

	return ok ? 0 : 1;
}
