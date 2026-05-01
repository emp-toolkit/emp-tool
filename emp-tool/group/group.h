#ifndef EMP_GROUP_H__
#define EMP_GROUP_H__

#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <string>
#include <cstring>
#include "emp-tool/core/utils.h"

namespace emp {

// Owns a BIGNUM. Default-constructible (allocates a fresh BIGNUM); copy
// allocates a new BIGNUM and BN_copys; move steals the pointer. Most
// methods are const-correct; the raw handle is exposed via n() because
// OpenSSL's API takes BIGNUM* (not const BIGNUM*) almost everywhere.
class BigInt {
public:
	BigInt();
	BigInt(const BigInt & oth);
	BigInt(BigInt && oth) noexcept;
	BigInt & operator=(BigInt oth);
	~BigInt();

	int size() const;
	void to_bin(unsigned char * out) const;
	// Fixed-width big-endian encoding: pads with leading zero bytes so
	// the output is exactly `fixed_len` bytes. Errors if the integer is
	// too large to fit. Use this for wire formats that need a constant
	// width (e.g. 32 bytes for a P-256 scalar) instead of to_bin's
	// minimum-width encoding.
	void to_bin_padded(unsigned char * out, size_t fixed_len) const;
	void from_bin(const unsigned char * in, size_t length);

	// Raw add (no modular reduction). Use add_mod / mul_mod for
	// modular arithmetic; this returns the unbounded sum, which is
	// almost never what you want for group-element arithmetic.
	BigInt add(const BigInt & oth) const;
	BigInt mul(const BigInt & oth, BN_CTX * ctx) const;
	BigInt mod(const BigInt & oth, BN_CTX * ctx) const;
	BigInt add_mod(const BigInt & b, const BigInt & m, BN_CTX * ctx) const;
	BigInt mul_mod(const BigInt & b, const BigInt & m, BN_CTX * ctx) const;

	// OpenSSL-handle accessor. Mutable on purpose — BN_* APIs take
	// BIGNUM* (non-const) for most operations.
	BIGNUM * n() const { return n_; }

private:
	BIGNUM * n_ = nullptr;
};

class Group;

// Owns an EC_POINT bound to a Group. Default-constructible into a
// "zombie" state (point() == nullptr, group() == nullptr) so that
// Point[] arrays can be filled later via from_bin / hash_to_point. All
// other methods (add / mul / inv / to_bin / size / operator==) require
// a fully-initialized Point and assert it.
class Point {
public:
	Point() = default;                     // zombie; valid only as from_bin / hash_to_point target
	explicit Point(Group * g);
	Point(const Point & p);
	Point(Point && p) noexcept;
	Point & operator=(Point p);
	~Point();

	void to_bin(unsigned char * buf, size_t buf_len) const;
	size_t size() const;
	void from_bin(Group * g, const unsigned char * buf, size_t buf_len);

	Point add(const Point & rhs) const;
	Point mul(const BigInt & m) const;
	Point inv() const;
	bool operator==(const Point & rhs) const;

	EC_POINT * point() const { return point_; }
	Group * group() const { return group_; }

private:
	EC_POINT * point_ = nullptr;
	Group * group_ = nullptr;
	friend class Group;       // hash_to_point assigns point_ / group_ on lazy-init
};

// Wraps an EC_GROUP + BN_CTX + cached order. Curve is selectable at
// construction; default is P-256. Non-copyable, non-movable: it owns
// raw OpenSSL handles and a scratch buffer that would multi-free.
class Group {
public:
	explicit Group(int curve_nid = NID_X9_62_prime256v1);
	~Group();
	Group(const Group&)             = delete;
	Group& operator=(const Group&)  = delete;
	Group(Group&&)                  = delete;
	Group& operator=(Group&&)       = delete;

	// Scratch buffer used by IO::send_pt / recv_pt for staging point
	// serialization. resize_scratch only ever grows the buffer — never
	// shrinks — to avoid thrashing under alternating large/small
	// workloads. Worst-case allocation persists for the Group's lifetime.
	void resize_scratch(size_t size);
	unsigned char * scratch() const { return scratch_; }

	void get_rand_bn(BigInt & n);
	Point get_generator();
	Point mul_gen(const BigInt & m);

	// Hash an arbitrary byte string to a curve point per RFC 9380 §6
	// using the SSWU_RO_ suite for P-256. DST is hardcoded to the
	// canonical "QUUX-V01-CS02-with-P256_XMD:SHA-256_SSWU_RO_" so the
	// implementation is testable against §J.1.1 vectors. Asserts the
	// underlying curve is P-256.
	void hash_to_point(const char * msg, size_t length, Point & out);

	EC_GROUP * ec_group() const { return ec_group_; }
	BN_CTX * bn_ctx() const { return bn_ctx_; }
	const BigInt & order() const { return order_; }
	int curve_nid() const { return curve_nid_; }

private:
	EC_GROUP * ec_group_ = nullptr;
	BN_CTX * bn_ctx_ = nullptr;
	BigInt order_;
	unsigned char * scratch_ = nullptr;
	size_t scratch_size_ = 256;
	int curve_nid_ = NID_X9_62_prime256v1;
};

}  // namespace emp

#include "group_openssl.h"

#endif
