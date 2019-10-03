#ifndef GROUP_OPENSSL_H__
#define GROUP_OPENSSL_H__

#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <string>
#include <cstring>
#include "emp-tool/utils/utils.h"

namespace emp {
using std::string;

class Point;
class Group;
class BigInt {
	BIGNUM *n = nullptr;

	public:
	BigInt() {
		n = BN_new();
	}
	BigInt(const BigInt &oth) {
		n = BN_new();
		BN_copy(n, oth.n);
	}
	BigInt &operator=(const BigInt &oth) {
		n = BN_new();
		BN_copy(n, oth.n);
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

	void from_hex(const char *s) {
		BN_hex2bn(&n, s);
	}

	char* to_hex() {
		char *number_str = BN_bn2hex(n);
		return number_str;
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

	friend class Group;
	friend class Point;
};

class Point {
	//private:
	public:
		EC_POINT *p = nullptr;

		Point() {
		}

		~Point() {
			if(p!=nullptr)
				EC_POINT_free(p);
		}

		friend class Group;
};

class Group {
	public:
	friend class Point;
	//private:
		EC_GROUP *ec_group = nullptr;
		BN_CTX * bn_ctx = nullptr;
		BigInt p;
		unsigned char * scratch;
		size_t scratch_size = 256;
		Group() {
			ec_group = EC_GROUP_new_by_curve_name(NID_secp256k1);
			bn_ctx = BN_CTX_new();
			EC_GROUP_precompute_mult(ec_group, bn_ctx);
			EC_GROUP_get_order(ec_group, p.n, bn_ctx);
			scratch = new unsigned char[scratch_size];
		}

		~Group(){
			if(ec_group != nullptr)
				EC_GROUP_free(ec_group);

			if(bn_ctx != nullptr)
				BN_CTX_free(bn_ctx);

			if(scratch != nullptr)
				delete[] scratch;
		}
		
		void resize_scratch(size_t size) {
			if (size > scratch_size) {
				delete[] scratch;
				scratch_size = size;
				scratch = new unsigned char[scratch_size];
			}
		}

		void get_rand_bn(BigInt & n) {
			BN_rand_range(n.n, p.n);
		}

		void get_generator(Point &g) {
			int ret = EC_POINT_copy(g.p, EC_GROUP_get0_generator(ec_group));
			if(ret == 0) error("");
		}

		void init(Point &p) {
			p.p = EC_POINT_new(ec_group);
		}

		void add(Point &res, const Point &lhs, const Point &rhs) {
			int ret = EC_POINT_add(ec_group, res.p, lhs.p, rhs.p, bn_ctx);
			if(ret == 0) error("ECC ADD");
		}

		void inv(Point &res, const Point &p) {
			int ret = EC_POINT_copy(res.p, p.p);
			ret += EC_POINT_invert(ec_group, res.p, bn_ctx);
			if(ret != 2) error("ECC INV");
		}

		void mul(Point &res, const Point &lhs, const BigInt &m) {
			int ret = EC_POINT_mul(ec_group, res.p, NULL, lhs.p, m.n, bn_ctx);
			if(ret == 0) error("ECC MUL");
		}
		void mul_gen(Point &res, const BigInt &m) {
			int ret = EC_POINT_mul(ec_group, res.p, m.n ,NULL, NULL, bn_ctx);
			if(ret == 0) error("ECC GEN MUL");
		}

		void to_bin(unsigned char * buf, const Point * point, size_t buf_len) {
			int ret = EC_POINT_point2oct(ec_group, point->p, POINT_CONVERSION_UNCOMPRESSED, buf, buf_len, bn_ctx);
			if(ret == 0) error("ECC TO_BIN");
		}

		size_t size_bin(const Point * point) {
			size_t ret = EC_POINT_point2oct(ec_group, point->p, POINT_CONVERSION_UNCOMPRESSED, NULL, 0, bn_ctx);
			if(ret == 0) error("ECC SIZE_BIN");
			return ret;
		}

		void from_bin(const unsigned char * buf, Point * point, size_t buf_len) {
			int ret = EC_POINT_oct2point(ec_group, point->p, buf, buf_len, bn_ctx);
			if(ret == 0) error("ECC FROM_BIN");
		}

		char* to_hex(const Point &p) const {
			BigInt x, y;
			EC_POINT_get_affine_coordinates_GFp(ec_group, p.p, x.n, y.n, NULL);
			char *sx,*sy;
			sx=x.to_hex();
			sy=y.to_hex();
			int len=3+strlen(sx)+strlen(sy);
			char *res=new char[len+1];
			memset(res,0,len+1);
			strcat(res,"(");
			strcat(res,sx);
			strcat(res,",");
			strcat(res,sy);
			strcat(res,")");
			return res;
		}

		void from_hex(Point &p,const char *s) const {
			BigInt x, y;
			int len=strlen(s);
			char *t=new char[len+1];
			memset(t,0,len+1);
			t[len]=0;
			strcpy(t,s);
			for(int i=0;i<len;i++){
				if(s[i]==','){
					t[0]=0;
					t[i]=0;
					t[len-1]=0;
					x.from_hex(t+1);
					y.from_hex(t+i+1);
				}
			}
			EC_POINT_set_affine_coordinates_GFp(ec_group,p.p,x.n,y.n,NULL);
		}
};


}
#endif
