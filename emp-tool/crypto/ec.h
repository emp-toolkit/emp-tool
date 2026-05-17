#ifndef EMP_EC_H__
#define EMP_EC_H__

#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <cstddef>

namespace emp {

// Owns a BIGNUM. Default-construction allocates a fresh BIGNUM; copy
// allocates a new BIGNUM and BN_copys; move steals the pointer. Most
// methods are const-correct; the raw handle is exposed via n() because
// OpenSSL's API takes BIGNUM* (not const BIGNUM*) almost everywhere.
class Scalar {
public:
	Scalar();
	Scalar(const Scalar & oth);
	Scalar(Scalar && oth) noexcept;
	Scalar & operator=(Scalar oth);
	~Scalar();

	int size() const;
	void to_bin(unsigned char * out) const;
	// Fixed-width big-endian encoding: pads with leading zero bytes so
	// the output is exactly `fixed_len` bytes. Errors if the integer is
	// too large to fit.
	void to_bin_padded(unsigned char * out, size_t fixed_len) const;
	void from_bin(const unsigned char * in, size_t length);

	// Raw add (no modular reduction). Use add_mod / mul_mod for
	// modular arithmetic.
	Scalar add(const Scalar & oth) const;
	Scalar mul(const Scalar & oth, BN_CTX * ctx) const;
	Scalar mod(const Scalar & oth, BN_CTX * ctx) const;
	Scalar add_mod(const Scalar & b, const Scalar & m, BN_CTX * ctx) const;
	Scalar mul_mod(const Scalar & b, const Scalar & m, BN_CTX * ctx) const;

	// OpenSSL-handle accessor. Mutable on purpose — BN_* APIs take
	// BIGNUM* (non-const) for most operations.
	BIGNUM * n() const { return n_; }

private:
	BIGNUM * n_ = nullptr;
};

class ECGroup;

// Owns an EC_POINT bound to an ECGroup. Default-constructible into a
// "zombie" state (point() == nullptr, group() == nullptr) so that
// Point[] arrays can be filled later via from_bin. All other methods
// require a fully-initialized Point and assert it.
class Point {
public:
	Point() = default;                     // zombie; valid only as from_bin target
	explicit Point(ECGroup * g);
	Point(const Point & p);
	Point(Point && p) noexcept;
	Point & operator=(Point p);
	~Point();

	void to_bin(unsigned char * buf, size_t buf_len) const;
	size_t size() const;
	void from_bin(ECGroup * g, const unsigned char * buf, size_t buf_len);

	Point add(const Point & rhs) const;
	Point mul(const Scalar & m) const;
	Point inv() const;
	bool operator==(const Point & rhs) const;

	EC_POINT * point() const { return point_; }
	ECGroup * group() const { return group_; }

private:
	EC_POINT * point_ = nullptr;
	ECGroup * group_ = nullptr;
	friend class ECGroup;       // hash_to_point assigns point_ / group_ on lazy-init
};

// Wraps an EC_GROUP + BN_CTX + cached order. Curve is selectable at
// construction; default is P-256. Non-copyable, non-movable: it owns
// raw OpenSSL handles and a scratch buffer that would multi-free.
class ECGroup {
public:
	explicit ECGroup(int curve_nid = NID_X9_62_prime256v1);
	~ECGroup();
	ECGroup(const ECGroup&)             = delete;
	ECGroup& operator=(const ECGroup&)  = delete;
	ECGroup(ECGroup&&)                  = delete;
	ECGroup& operator=(ECGroup&&)       = delete;

	// Scratch buffer used by IO::send_pt / recv_pt for staging point
	// serialization. resize_scratch only ever grows the buffer — never
	// shrinks — to avoid thrashing under alternating large/small
	// workloads. Worst-case allocation persists for the ECGroup's lifetime.
	void resize_scratch(size_t size);
	unsigned char * scratch() const { return scratch_; }

	Scalar rand_scalar();
	Point get_generator();
	Point mul_gen(const Scalar & m);

	// Hash an arbitrary byte string to a curve point per RFC 9380 §6
	// using the SSWU_RO_ suite. `dst` is the per-protocol domain-
	// separation tag (RFC 9380 §3.1) — pick one stable string per
	// protocol; the canonical "QUUX-V01-CS02-with-<suite>" is only for
	// validating against §J vectors. Only P-256 is wired up today;
	// other curves error at runtime.
	Point hash_to_point(const char * msg, size_t length,
	                    const char * dst, size_t dst_len);

	EC_GROUP * ec_group() const { return ec_group_; }
	BN_CTX * bn_ctx() const { return bn_ctx_; }
	const Scalar & order() const { return order_; }
	int curve_nid() const { return curve_nid_; }

private:
	EC_GROUP * ec_group_ = nullptr;
	BN_CTX * bn_ctx_ = nullptr;
	Scalar order_;
	unsigned char * scratch_ = nullptr;
	size_t scratch_size_ = 256;
	int curve_nid_ = NID_X9_62_prime256v1;
};

}  // namespace emp

#endif
