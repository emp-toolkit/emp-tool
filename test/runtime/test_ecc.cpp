#include "emp-tool/emp-tool.h"
#include <cctype>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
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

static vector<unsigned char> enc(const Point &p) {
	vector<unsigned char> out(p.size());
	p.to_bin(out.data(), (int64_t)out.size());
	return out;
}

// RFC 9380 §5.3.1 expand_message_xmd with SHA-256 for the 96 bytes that
// hash_to_field reads, written independently of ec.cpp's.
static vector<unsigned char> xmd_sha256_96(const vector<unsigned char> &msg,
                                           const vector<unsigned char> &dst) {
	auto sha256 = [](const vector<unsigned char> &in) {
		vector<unsigned char> out(32);
		unsigned int len = 0;
		expecting(EVP_Digest(in.data(), in.size(), out.data(), &len,
		                     EVP_sha256(), nullptr) == 1 && len == 32,
		          "ECC test: EVP_Digest");
		return out;
	};
	vector<unsigned char> dst_prime = dst;
	dst_prime.push_back((unsigned char)dst.size());
	vector<unsigned char> in(64, 0);                       // Z_pad
	in.insert(in.end(), msg.begin(), msg.end());
	in.insert(in.end(), {0, 96, 0});                       // I2OSP(96, 2) || 0x00
	in.insert(in.end(), dst_prime.begin(), dst_prime.end());
	const vector<unsigned char> b0 = sha256(in);
	vector<unsigned char> out, b(32, 0);
	for (unsigned char i = 1; i <= 3; ++i) {               // b_i = H((b_0 ^ b_{i-1}) || i || DST')
		in.assign(32, 0);
		for (size_t j = 0; j < 32; ++j) in[j] = b0[j] ^ b[j];
		in.push_back(i);
		in.insert(in.end(), dst_prime.begin(), dst_prime.end());
		b = sha256(in);
		out.insert(out.end(), b.begin(), b.end());
	}
	return out;
}

// BN_mod_sqrt, with an expected nonsquare returned as false and its error
// popped off OpenSSL's queue.
static bool sqrt_mod_p(BIGNUM *out, const BIGNUM *v, const BIGNUM *p, BN_CTX *ctx) {
	ERR_set_mark();
	const bool square = BN_mod_sqrt(out, v, p, ctx) != nullptr;
	ERR_pop_to_mark();
	return square;
}

// out = x^3 + A·x + B mod p
static void curve_rhs(BIGNUM *out, const BIGNUM *x, const BIGNUM *A,
                      const BIGNUM *B, const BIGNUM *p, BN_CTX *ctx) {
	BN_CTX_start(ctx);
	BIGNUM *ax = BN_CTX_get(ctx);
	expecting(ax != nullptr &&
	              BN_mod_sqr(out, x, p, ctx) == 1 &&
	              BN_mod_mul(out, out, x, p, ctx) == 1 &&
	              BN_mod_mul(ax, A, x, p, ctx) == 1 &&
	              BN_mod_add(out, out, ax, p, ctx) == 1 &&
	              BN_mod_add(out, out, B, p, ctx) == 1,
	          "ECC test: curve_rhs");
	BN_CTX_end(ctx);
}

// RFC 9380 §6.6.2 simplified SWU as the RFC states it, one square root per
// candidate: the oracle for hash_to_point's one-exponentiation map. Counts in
// `second` the inputs that take the second candidate.
static void sswu_two_roots(BIGNUM *x, BIGNUM *y, const BIGNUM *u,
                           const BIGNUM *p, const BIGNUM *A, const BIGNUM *B,
                           const BIGNUM *Z, BN_CTX *ctx, int &second) {
	BN_CTX_start(ctx);
	BIGNUM *t = BN_CTX_get(ctx), *tv1 = BN_CTX_get(ctx);
	BIGNUM *tmp = BN_CTX_get(ctx), *gx = BN_CTX_get(ctx);
	expecting(gx != nullptr, "ECC test: BN_CTX_get");
	// t = Z·u^2 ; tv1 = t^2 + t
	expecting(BN_mod_sqr(tmp, u, p, ctx) == 1 &&
	              BN_mod_mul(t, Z, tmp, p, ctx) == 1 &&
	              BN_mod_sqr(tmp, t, p, ctx) == 1 &&
	              BN_mod_add(tv1, tmp, t, p, ctx) == 1,
	          "ECC test: SSWU t");
	if (BN_is_zero(tv1)) {
		// x1 = B / (Z·A)
		expecting(BN_mod_mul(tmp, Z, A, p, ctx) == 1 &&
		              BN_mod_inverse(tmp, tmp, p, ctx) != nullptr &&
		              BN_mod_mul(x, B, tmp, p, ctx) == 1,
		          "ECC test: SSWU exceptional x1");
	} else {
		// x1 = (-B / A)·(1 + 1/tv1)
		expecting(BN_mod_inverse(tmp, tv1, p, ctx) != nullptr &&
		              BN_add_word(tmp, 1) == 1 &&
		              BN_sub(gx, p, B) == 1 &&
		              BN_mod_mul(x, gx, tmp, p, ctx) == 1 &&
		              BN_mod_inverse(tmp, A, p, ctx) != nullptr &&
		              BN_mod_mul(x, x, tmp, p, ctx) == 1,
		          "ECC test: SSWU x1");
	}
	curve_rhs(gx, x, A, B, p, ctx);
	if (!sqrt_mod_p(y, gx, p, ctx)) {
		// x2 = t·x1
		++second;
		expecting(BN_mod_mul(x, t, x, p, ctx) == 1, "ECC test: SSWU x2");
		curve_rhs(gx, x, A, B, p, ctx);
		expecting(sqrt_mod_p(y, gx, p, ctx), "ECC test: no SSWU candidate square");
	}
	// sgn0(y) = sgn0(u)
	if (BN_is_bit_set(u, 0) != BN_is_bit_set(y, 0))
		expecting(BN_sub(y, p, y) == 1, "ECC test: SSWU sgn0");
	BN_CTX_end(ctx);
}

// hash_to_point composed from the pieces above (RFC 9380 §3): hash to two
// field elements, map each, add (P-256's cofactor is 1).
static Point hash_to_point_two_roots(ECGroup &G, const vector<unsigned char> &msg,
                                     const vector<unsigned char> &dst,
                                     BN_CTX *ctx, int &second) {
	BN_CTX_start(ctx);
	BIGNUM *p = BN_CTX_get(ctx), *A = BN_CTX_get(ctx), *B = BN_CTX_get(ctx);
	BIGNUM *Z = BN_CTX_get(ctx), *u = BN_CTX_get(ctx);
	BIGNUM *x = BN_CTX_get(ctx), *y = BN_CTX_get(ctx);
	expecting(y != nullptr &&
	              EC_GROUP_get_curve(G.ec_group(), p, A, B, ctx) == 1 &&
	              BN_set_word(Z, 10) == 1 && BN_sub(Z, p, Z) == 1,
	          "ECC test: P-256 constants");
	const vector<unsigned char> uniform = xmd_sha256_96(msg, dst);
	Point sum(&G);
	for (int i = 0; i < 2; ++i) {
		expecting(BN_bin2bn(uniform.data() + 48 * i, 48, u) != nullptr &&
		              BN_mod(u, u, p, ctx) == 1,
		          "ECC test: hash_to_field");
		sswu_two_roots(x, y, u, p, A, B, Z, ctx, second);
		Point Qi(&G);
		expecting(EC_POINT_set_affine_coordinates(G.ec_group(), Qi.point(),
		                                          x, y, ctx) == 1,
		          "ECC test: SSWU point");
		sum = sum.add(Qi);
	}
	BN_CTX_end(ctx);
	return sum;
}

// hash_to_point against the two-root oracle on random messages and DSTs. A
// map takes the second candidate for about half of its inputs, so both
// branches run many times. A caller's OpenSSL error stays on the queue
// throughout.
static bool check_h2c_two_roots(ECGroup &G) {
	BN_CTX *ctx = BN_CTX_new();
	expecting(ctx != nullptr, "ECC test: BN_CTX_new");
	PRG prg;
	ERR_clear_error();
	ERR_raise(ERR_LIB_USER, 0x346);
	const unsigned long sentinel = ERR_peek_last_error();
	const int kMessages = 1000;
	int second = 0;
	bool ok = true;
	for (int i = 0; i < kMessages; ++i) {
		unsigned char lens[2];
		prg.random_data_unaligned(lens, sizeof(lens));
		vector<unsigned char> msg(lens[0] % 200), dst(1 + lens[1] % 255);
		prg.random_data_unaligned(msg.data(), (int64_t)msg.size());
		prg.random_data_unaligned(dst.data(), (int64_t)dst.size());
		Point got = G.hash_to_point((const char *)msg.data(), (int64_t)msg.size(),
		                            (const char *)dst.data(), (int64_t)dst.size());
		ok = ok && ERR_peek_error() == sentinel && ERR_peek_last_error() == sentinel;
		Point want = hash_to_point_two_roots(G, msg, dst, ctx, second);
		ok = ok && got == want && enc(got) == enc(want);
	}
	ok = ok && second > kMessages / 2 && second < 3 * kMessages / 2;
	ERR_clear_error();
	BN_CTX_free(ctx);
	return ok;
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

	// Exercise the nonzero/range contract of this build's generator: OpenSSL's
	// private generator, or the deterministic path in a test-mode build.
	for (int i = 0; i < 64; ++i) {
		Scalar s = G.rand_scalar();
		ok = ok && !BN_is_zero(s.n()) && BN_cmp(s.n(), order.n()) < 0;
	}

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

	bool h2c_two_roots = check_h2c_two_roots(G);
	cout << "h2c == two-root SSWU oracle on random messages: " << h2c_two_roots << endl;
	ok = ok && h2c_two_roots;


	return ok ? 0 : 1;
}
