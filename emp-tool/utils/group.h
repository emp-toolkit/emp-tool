#ifndef GROUP_H__
#define GROUP_H__

#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <string>
#include <cstring>
#include "emp-tool/utils/utils.h"

//#ifdef ECC_USE_OPENSSL
//#else
//#include "group_relic.h"
//#endif
namespace emp {
class BigInt { public:
	BIGNUM *n = nullptr;
	BigInt() {
		n = BN_new();
	}
	BigInt(const BigInt &oth) {
		n = BN_new();
		BN_copy(n, oth.n);
	}
	BigInt &operator=(BigInt oth) {
		std::swap(n, oth.n);
		return *this;
	}
	~BigInt() {
		if (n != nullptr)
			BN_free(n);
	}

	void from_dec(const string &s) {
		char *p_str;
		p_str = new char[s.length() + 1];
		memcpy(p_str, s.c_str(), s.length());
		p_str[s.length()] = 0;
		BN_dec2bn(&n, p_str);
		delete[] p_str;
	}

	int size() {
		return BN_num_bytes(n);
	}
	
	void to_bin(unsigned char * in) {
		BN_bn2bin(n, in);
	}

	void from_bin(const unsigned char * in, int length) {
		BN_free(n);
		n = BN_bin2bn(in, length, nullptr);
	}

	BigInt &add(const BigInt &oth) {
		BN_add(n, n, oth.n);
		return *this;
	}

	BigInt &mul(const BigInt &oth) {
		BN_mul(n, n, oth.n, NULL);
		return *this;
	}

	BigInt &mod(const BigInt &oth) {
		BN_mod(n, n, oth.n, NULL);
		return *this;
	}
};
class Group;
class Point {
	public:
		EC_POINT *point = nullptr;
		Group * group = nullptr;
		Point (Group * g = nullptr);
		~Point();
		Point(const Point & p);
		Point& operator=(Point p);
		void to_bin(unsigned char * buf, size_t buf_len);
		size_t size();
		void from_bin(const unsigned char * buf, size_t buf_len);
		Point add(Point & rhs);
		Point mul(const BigInt &m);
		Point inv();
		bool operator==(Point & rhs);
};

class Group { public:
	EC_GROUP *ec_group = nullptr;
	BN_CTX * bn_ctx = nullptr;
	BigInt order;
	unsigned char * scratch;
	size_t scratch_size = 256;
	Group();
	~Group();
	void resize_scratch(size_t size);
	void get_rand_bn(BigInt & n);
	Point get_generator();
	Point mul_gen(const BigInt &m);
};
}
#include "group_openssl.h"


#endif
