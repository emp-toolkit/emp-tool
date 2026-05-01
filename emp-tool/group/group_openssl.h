#ifndef EMP_GROUP_OPENSSL_H__
#define EMP_GROUP_OPENSSL_H__

#include <openssl/evp.h>
#include <algorithm>

namespace emp {

// ===== RFC 9380 hash-to-curve helpers (P-256, SSWU_RO_) =====
// expand_message_xmd with SHA-256 — RFC 9380 §5.3.1.
inline void expand_message_xmd_sha256(
		unsigned char *out, size_t out_len,
		const unsigned char *msg, size_t msg_len,
		const unsigned char *dst, size_t dst_len) {
	const size_t b_in_bytes = 32;     // SHA-256 output
	const size_t s_in_bytes = 64;     // SHA-256 input block
	if (out_len > 65535 || dst_len > 255) error("H2C XMD len");

	size_t ell = (out_len + b_in_bytes - 1) / b_in_bytes;
	if (ell > 255) error("H2C XMD ell");

	// DST_prime = DST || I2OSP(len(DST), 1). Bounded above: dst_len ≤
	// 255 (checked above) plus the appended length byte = 256.
	constexpr size_t kMaxDstPrime = 256;
	unsigned char dst_prime[kMaxDstPrime];
	memcpy(dst_prime, dst, dst_len);
	dst_prime[dst_len] = (unsigned char)dst_len;
	size_t dst_prime_len = dst_len + 1;

	unsigned char z_pad[64] = {0};
	unsigned char l_i_b_str[2] = {
		(unsigned char)(out_len >> 8), (unsigned char)(out_len & 0xff)
	};
	unsigned char zero = 0x00, one = 0x01;

	unsigned char b0[32], bi[32], prev[32];
	unsigned int outlen = 0;
	EVP_MD_CTX *md = EVP_MD_CTX_new();
	if (md == nullptr) error("H2C XMD: EVP_MD_CTX_new");

	// b_0 = H(Z_pad || msg || l_i_b_str || 0x00 || DST_prime)
	EVP_DigestInit_ex(md, EVP_sha256(), NULL);
	EVP_DigestUpdate(md, z_pad, s_in_bytes);
	EVP_DigestUpdate(md, msg, msg_len);
	EVP_DigestUpdate(md, l_i_b_str, 2);
	EVP_DigestUpdate(md, &zero, 1);
	EVP_DigestUpdate(md, dst_prime, dst_prime_len);
	EVP_DigestFinal_ex(md, b0, &outlen);

	// b_1 = H(b_0 || 0x01 || DST_prime)
	EVP_DigestInit_ex(md, EVP_sha256(), NULL);
	EVP_DigestUpdate(md, b0, b_in_bytes);
	EVP_DigestUpdate(md, &one, 1);
	EVP_DigestUpdate(md, dst_prime, dst_prime_len);
	EVP_DigestFinal_ex(md, bi, &outlen);

	memcpy(out, bi, std::min(out_len, b_in_bytes));
	memcpy(prev, bi, b_in_bytes);

	// b_i = H((b_0 XOR b_{i-1}) || I2OSP(i,1) || DST_prime), i=2..ell
	for (size_t i = 2; i <= ell; i++) {
		unsigned char tmp[32];
		for (size_t j = 0; j < b_in_bytes; j++) tmp[j] = b0[j] ^ prev[j];
		unsigned char ib = (unsigned char)i;
		EVP_DigestInit_ex(md, EVP_sha256(), NULL);
		EVP_DigestUpdate(md, tmp, b_in_bytes);
		EVP_DigestUpdate(md, &ib, 1);
		EVP_DigestUpdate(md, dst_prime, dst_prime_len);
		EVP_DigestFinal_ex(md, bi, &outlen);

		size_t off = (i - 1) * b_in_bytes;
		memcpy(out + off, bi, std::min(out_len - off, b_in_bytes));
		memcpy(prev, bi, b_in_bytes);
	}
	EVP_MD_CTX_free(md);
}

// hash_to_field for P-256: count=2, L=48 (k=128 security).
inline void hash_to_field_p256(BIGNUM *u0, BIGNUM *u1, const BIGNUM *p,
		const unsigned char *msg, size_t msg_len,
		const unsigned char *dst, size_t dst_len, BN_CTX *ctx) {
	const size_t L = 48;
	unsigned char buf[2 * L];
	expand_message_xmd_sha256(buf, sizeof(buf), msg, msg_len, dst, dst_len);
	if (BN_bin2bn(buf,     L, u0) == nullptr) error("hash_to_field: BN_bin2bn u0");
	if (BN_mod(u0, u0, p, ctx) == 0)          error("hash_to_field: BN_mod u0");
	if (BN_bin2bn(buf + L, L, u1) == nullptr) error("hash_to_field: BN_bin2bn u1");
	if (BN_mod(u1, u1, p, ctx) == 0)          error("hash_to_field: BN_mod u1");
}

// Simplified SWU for P-256, A=-3, Z=-10. Non-optimized version per §6.6.2.
// Uses BN_mod_sqrt (Tonelli-Shanks); not constant-time, but P-256 has
// p ≡ 3 (mod 4) so OpenSSL takes the fast (p+1)/4 path internally.
//
// BN_mod_* return values aren't checked individually — they only fail on
// OOM, which `error()` would catch via downstream null derefs / shape
// mismatches anyway. The two failure modes that *can* hit valid inputs
// — BN_mod_inverse on a non-invertible value and BN_mod_sqrt on a
// non-residue — are checked explicitly. `error()` exits the process, so
// the BN_free cleanup at the bottom is unreached on the error path; not
// a leak in practice.
inline void map_to_curve_sswu_p256(BIGNUM *x_out, BIGNUM *y_out,
		const BIGNUM *u, const BIGNUM *p, const BIGNUM *A, const BIGNUM *B,
		const BIGNUM *Z, BN_CTX *ctx) {
	BIGNUM *u2 = BN_new(), *t = BN_new(), *tv1 = BN_new(), *tmp = BN_new();
	BIGNUM *x1 = BN_new(), *gx = BN_new(), *y = BN_new();

	// t = Z * u^2 ; tv1 = t^2 + t
	BN_mod_sqr(u2, u, p, ctx);
	BN_mod_mul(t, Z, u2, p, ctx);
	BN_mod_sqr(tmp, t, p, ctx);
	BN_mod_add(tv1, tmp, t, p, ctx);

	if (BN_is_zero(tv1)) {
		// x1 = B / (Z * A)
		BN_mod_mul(tmp, Z, A, p, ctx);
		if (BN_mod_inverse(tmp, tmp, p, ctx) == nullptr)
			error("SSWU: Z*A not invertible");
		BN_mod_mul(x1, B, tmp, p, ctx);
	} else {
		// x1 = (-B / A) * (1 + tv1^{-1})
		BIGNUM *inv = BN_new(), *one_plus = BN_new();
		if (BN_mod_inverse(inv, tv1, p, ctx) == nullptr)
			error("SSWU: tv1 not invertible");
		BN_one(one_plus);
		BN_mod_add(one_plus, one_plus, inv, p, ctx);
		BIGNUM *negB = BN_new(); BN_sub(negB, p, B);
		BIGNUM *Ainv = BN_new();
		if (BN_mod_inverse(Ainv, A, p, ctx) == nullptr)
			error("SSWU: A not invertible");
		BN_mod_mul(tmp, negB, Ainv, p, ctx);
		BN_mod_mul(x1, tmp, one_plus, p, ctx);
		BN_free(inv); BN_free(one_plus); BN_free(negB); BN_free(Ainv);
	}

	// gx1 = x1^3 + A*x1 + B
	BN_mod_sqr(tmp, x1, p, ctx);
	BN_mod_mul(gx, tmp, x1, p, ctx);
	BN_mod_mul(tmp, A, x1, p, ctx);
	BN_mod_add(gx, gx, tmp, p, ctx);
	BN_mod_add(gx, gx, B, p, ctx);

	// BN_mod_sqrt's prototype takes BIGNUM* (non-const) for the modulus
	// even though it doesn't mutate it; the const_cast is safe.
	if (BN_mod_sqrt(y, gx, const_cast<BIGNUM *>(p), ctx) != NULL) {
		BN_copy(x_out, x1);
	} else {
		// x2 = t * x1 ; gx2 = x2^3 + A*x2 + B
		BN_mod_mul(x1, t, x1, p, ctx);
		BN_mod_sqr(tmp, x1, p, ctx);
		BN_mod_mul(gx, tmp, x1, p, ctx);
		BN_mod_mul(tmp, A, x1, p, ctx);
		BN_mod_add(gx, gx, tmp, p, ctx);
		BN_mod_add(gx, gx, B, p, ctx);
		if (BN_mod_sqrt(y, gx, const_cast<BIGNUM *>(p), ctx) == NULL)
			error("H2C SSWU: neither candidate square");
		BN_copy(x_out, x1);
	}

	// sgn0 correction: if sgn0(u) != sgn0(y), y = -y
	if (BN_is_bit_set(u, 0) != BN_is_bit_set(y, 0))
		BN_sub(y, p, y);
	BN_copy(y_out, y);

	BN_free(u2); BN_free(t); BN_free(tv1); BN_free(tmp);
	BN_free(x1); BN_free(gx); BN_free(y);
}

// ===== BigInt =====

inline BigInt::BigInt() {
	n_ = BN_new();
	if (n_ == nullptr) error("BigInt: BN_new");
}
inline BigInt::BigInt(const BigInt &oth) {
	n_ = BN_new();
	if (n_ == nullptr) error("BigInt: BN_new");
	BN_copy(n_, oth.n_);
}
inline BigInt::BigInt(BigInt && oth) noexcept : n_(oth.n_) {
	oth.n_ = nullptr;
}
inline BigInt & BigInt::operator=(BigInt oth) {
	std::swap(n_, oth.n_);
	return *this;
}
inline BigInt::~BigInt() {
	if (n_ != nullptr) BN_free(n_);
}

inline int BigInt::size() const {
	return BN_num_bytes(n_);
}

inline void BigInt::to_bin(unsigned char * out) const {
	BN_bn2bin(n_, out);
}

inline void BigInt::to_bin_padded(unsigned char * out, size_t fixed_len) const {
	int actual = BN_num_bytes(n_);
	if ((size_t)actual > fixed_len) error("BigInt::to_bin_padded: number too large");
	memset(out, 0, fixed_len - (size_t)actual);
	BN_bn2bin(n_, out + (fixed_len - (size_t)actual));
}

inline void BigInt::from_bin(const unsigned char * in, size_t length) {
	if (n_ != nullptr) BN_free(n_);
	n_ = BN_bin2bn(in, (int)length, nullptr);
	if (n_ == nullptr) error("BigInt::from_bin: BN_bin2bn");
}

inline BigInt BigInt::add(const BigInt &oth) const {
	BigInt ret;
	BN_add(ret.n_, n_, oth.n_);
	return ret;
}

inline BigInt BigInt::mul_mod(const BigInt & b, const BigInt &m, BN_CTX *ctx) const {
	BigInt ret;
	BN_mod_mul(ret.n_, n_, b.n_, m.n_, ctx);
	return ret;
}

inline BigInt BigInt::add_mod(const BigInt & b, const BigInt &m, BN_CTX *ctx) const {
	BigInt ret;
	BN_mod_add(ret.n_, n_, b.n_, m.n_, ctx);
	return ret;
}

inline BigInt BigInt::mul(const BigInt &oth, BN_CTX *ctx) const {
	BigInt ret;
	BN_mul(ret.n_, n_, oth.n_, ctx);
	return ret;
}

inline BigInt BigInt::mod(const BigInt &oth, BN_CTX *ctx) const {
	BigInt ret;
	BN_mod(ret.n_, n_, oth.n_, ctx);
	return ret;
}

// ===== Point =====

inline Point::Point(Group * g) {
	if (g == nullptr) return;
	group_ = g;
	point_ = EC_POINT_new(g->ec_group());
	if (point_ == nullptr) error("Point: EC_POINT_new");
}

inline Point::~Point() {
	if (point_ != nullptr) EC_POINT_free(point_);
}

inline Point::Point(const Point & p) {
	if (p.group_ == nullptr) return;
	group_ = p.group_;
	point_ = EC_POINT_new(group_->ec_group());
	if (point_ == nullptr) error("Point: EC_POINT_new");
	if (EC_POINT_copy(point_, p.point_) == 0) error("ECC COPY");
}

inline Point::Point(Point && p) noexcept
		: point_(p.point_), group_(p.group_) {
	p.point_ = nullptr;
	p.group_ = nullptr;
}

inline Point& Point::operator=(Point p) {
	std::swap(p.point_, point_);
	std::swap(p.group_, group_);
	return *this;
}

inline void Point::to_bin(unsigned char * buf, size_t buf_len) const {
	if (group_ == nullptr) error("Point::to_bin on uninitialized point");
	int ret = EC_POINT_point2oct(group_->ec_group(), point_,
	                             POINT_CONVERSION_UNCOMPRESSED, buf, buf_len, group_->bn_ctx());
	if (ret == 0) error("ECC TO_BIN");
}

inline size_t Point::size() const {
	if (group_ == nullptr) error("Point::size on uninitialized point");
	size_t ret = EC_POINT_point2oct(group_->ec_group(), point_,
	                                POINT_CONVERSION_UNCOMPRESSED, NULL, 0, group_->bn_ctx());
	if (ret == 0) error("ECC SIZE_BIN");
	return ret;
}

inline void Point::from_bin(Group * g, const unsigned char * buf, size_t buf_len) {
	if (g == nullptr) error("Point::from_bin: null group");
	// If already initialized against a different group, drop the old
	// point and re-allocate against `g`. Without this, the silent fall-
	// through used the prior group's curve while ignoring `g`.
	if (point_ != nullptr && group_ != g) {
		EC_POINT_free(point_);
		point_ = nullptr;
	}
	if (point_ == nullptr) {
		group_ = g;
		point_ = EC_POINT_new(group_->ec_group());
		if (point_ == nullptr) error("Point::from_bin: EC_POINT_new");
	}
	int ret = EC_POINT_oct2point(group_->ec_group(), point_, buf, buf_len, group_->bn_ctx());
	if (ret == 0) error("ECC FROM_BIN");
}

inline Point Point::add(const Point & rhs) const {
	if (group_ == nullptr) error("Point::add on uninitialized point");
	if (rhs.group_ != group_) error("Point::add: group mismatch");
	Point ret(group_);
	int res = EC_POINT_add(group_->ec_group(), ret.point_, point_, rhs.point_, group_->bn_ctx());
	if (res == 0) error("ECC ADD");
	return ret;
}

inline Point Point::mul(const BigInt &m) const {
	if (group_ == nullptr) error("Point::mul on uninitialized point");
	Point ret(group_);
	int res = EC_POINT_mul(group_->ec_group(), ret.point_, NULL, point_, m.n(), group_->bn_ctx());
	if (res == 0) error("ECC MUL");
	return ret;
}

inline Point Point::inv() const {
	if (group_ == nullptr) error("Point::inv on uninitialized point");
	Point ret(*this);
	int res = EC_POINT_invert(group_->ec_group(), ret.point_, group_->bn_ctx());
	if (res == 0) error("ECC INV");
	return ret;
}

inline bool Point::operator==(const Point & rhs) const {
	if (group_ == nullptr || rhs.group_ == nullptr)
		error("Point::operator== on uninitialized point");
	if (rhs.group_ != group_) error("Point::operator==: group mismatch");
	int ret = EC_POINT_cmp(group_->ec_group(), point_, rhs.point_, group_->bn_ctx());
	if (ret == -1) error("ECC CMP");
	return (ret == 0);
}

// ===== Group =====

inline Group::Group(int curve_nid) : curve_nid_(curve_nid) {
	ec_group_ = EC_GROUP_new_by_curve_name(curve_nid);
	if (ec_group_ == nullptr) error("Group: unsupported curve NID");
	bn_ctx_ = BN_CTX_new();
	if (bn_ctx_ == nullptr) error("Group: BN_CTX_new");
	if (EC_GROUP_get_order(ec_group_, order_.n(), bn_ctx_) == 0)
		error("Group: EC_GROUP_get_order");
	scratch_ = new unsigned char[scratch_size_];
}

inline Group::~Group() {
	// Manual frees first, nulled out so the (no-op) member dtors that
	// run after this body can't observe stale handles.
	if (ec_group_ != nullptr) { EC_GROUP_free(ec_group_); ec_group_ = nullptr; }
	if (bn_ctx_ != nullptr)   { BN_CTX_free(bn_ctx_);     bn_ctx_   = nullptr; }
	if (scratch_ != nullptr)  { delete[] scratch_;        scratch_  = nullptr; }
	// order_ is destroyed by its member dtor; BN_free(order_.n_) doesn't
	// need bn_ctx_, so the freed bn_ctx_ above is fine.
}

inline void Group::resize_scratch(size_t size) {
	if (size > scratch_size_) {
		delete[] scratch_;
		scratch_size_ = size;
		scratch_ = new unsigned char[scratch_size_];
	}
}

inline void Group::get_rand_bn(BigInt & n) {
	BN_rand_range(n.n(), order_.n());
}

inline Point Group::get_generator() {
	Point res(this);
	int ret = EC_POINT_copy(res.point_, EC_GROUP_get0_generator(ec_group_));
	if (ret == 0) error("ECC GEN");
	return res;
}

inline Point Group::mul_gen(const BigInt &m) {
	Point res(this);
	int ret = EC_POINT_mul(ec_group_, res.point_, m.n(), NULL, NULL, bn_ctx_);
	if (ret == 0) error("ECC GEN MUL");
	return res;
}

inline void Group::hash_to_point(const char * msg, size_t length, Point & out) {
	if (curve_nid_ != NID_X9_62_prime256v1)
		error("hash_to_point: only P-256 supported (SSWU_RO_ params hardcoded)");

	// Hardcoded to the canonical RFC 9380 §J.1.1 DST so the result can
	// be validated against published test vectors. Lift to a parameter
	// if you need per-protocol domain separation.
	static const unsigned char DST[] =
		"QUUX-V01-CS02-with-P256_XMD:SHA-256_SSWU_RO_";
	const size_t dst_len = sizeof(DST) - 1;

	BIGNUM *p = BN_new(), *A = BN_new(), *B = BN_new(), *Z = BN_new();
	EC_GROUP_get_curve(ec_group_, p, A, B, bn_ctx_);
	BN_set_word(Z, 10); BN_sub(Z, p, Z);   // Z = -10 mod p

	BIGNUM *u0 = BN_new(), *u1 = BN_new();
	hash_to_field_p256(u0, u1, p,
	                   reinterpret_cast<const unsigned char*>(msg),
	                   length, DST, dst_len, bn_ctx_);

	BIGNUM *x0 = BN_new(), *y0 = BN_new();
	BIGNUM *x1 = BN_new(), *y1 = BN_new();
	map_to_curve_sswu_p256(x0, y0, u0, p, A, B, Z, bn_ctx_);
	map_to_curve_sswu_p256(x1, y1, u1, p, A, B, Z, bn_ctx_);

	EC_POINT *Q0 = EC_POINT_new(ec_group_);
	EC_POINT *Q1 = EC_POINT_new(ec_group_);
	EC_POINT_set_affine_coordinates(ec_group_, Q0, x0, y0, bn_ctx_);
	EC_POINT_set_affine_coordinates(ec_group_, Q1, x1, y1, bn_ctx_);

	// If `out` was previously bound to a different Group, drop it and
	// re-allocate. Skipping this would add Q0+Q1 (in *this's curve)
	// into out.point_ (in another curve) — undefined behavior.
	if (out.point_ != nullptr && out.group_ != this) {
		EC_POINT_free(out.point_);
		out.point_ = nullptr;
	}
	if (out.point_ == nullptr) {
		out.group_ = this;
		out.point_ = EC_POINT_new(ec_group_);
	}
	if (EC_POINT_add(ec_group_, out.point_, Q0, Q1, bn_ctx_) == 0)
		error("H2C ADD");
	// P-256 cofactor h = 1 — clear_cofactor is identity, skip it.
	if (EC_POINT_is_on_curve(ec_group_, out.point_, bn_ctx_) != 1)
		error("H2C result not on curve");

	EC_POINT_free(Q0); EC_POINT_free(Q1);
	BN_free(p); BN_free(A); BN_free(B); BN_free(Z);
	BN_free(u0); BN_free(u1);
	BN_free(x0); BN_free(y0); BN_free(x1); BN_free(y1);
}

}  // namespace emp
#endif
