#ifndef EMP_GROUP_H__
#define EMP_GROUP_H__

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
	BigInt();
	BigInt(const BigInt &oth);
	BigInt &operator=(BigInt oth);
	~BigInt();

	int size();
	void to_bin(unsigned char * in);
	void from_bin(const unsigned char * in, int length);

	BigInt add(const BigInt &oth);
	BigInt mul(const BigInt &oth, BN_CTX *ctx);
	BigInt mod(const BigInt &oth, BN_CTX *ctx);
	BigInt add_mod(const BigInt & b, const BigInt& m, BN_CTX *ctx);
	BigInt mul_mod(const BigInt & b, const BigInt& m, BN_CTX *ctx);
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
		void from_bin(Group * g, const unsigned char * buf, size_t buf_len);

		Point add(Point & rhs);
//		Point sub(Point & rhs);
//		bool is_at_infinity();
//		bool is_on_curve();
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
