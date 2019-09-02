#ifndef GROUP_OPENSSL_H__
#define GROUP_OPENSSL_H__

#include "openssl/ec.h"
#include "openssl/bn.h"
#include "openssl/obj_mac.h"
#include <string>
#include <cstring>

namespace emp{
using std::string;

class Point;
class Group;
class BigInt
{
    BIGNUM *n;

public:
    BigInt()
    {
        n = BN_new();
    }
    BigInt(const BigInt &oth)
    {
        BN_copy(n, oth.n);
    }
    BigInt &operator=(const BigInt &oth)
    {
        BN_copy(n, oth.n);
        return *this;
    }
    ~BigInt()
    {
        BN_free(n);
    }
    void from_dec(const string &s)
    {

        char *p_str;
        p_str = new char[s.length() + 1];
        memcpy(p_str, s.c_str(), s.length());
        p_str[s.length()] = 0;
        BN_dec2bn(&n, p_str);
        delete[] p_str;
    }
    void from_hex(const char *s)
    {
        BN_hex2bn(&n, s);
    }
    char* to_hex()
    {
        char *number_str = BN_bn2hex(n);
        return number_str;
    }
    void from_bin(const char *tmp,int len){
        BN_bin2bn((const unsigned char*)tmp,len,n);
    }
    BigInt &add(const BigInt &oth)
    {
        BN_add(n, n, oth.n);
        return *this;
    }
    BigInt &mul(const BigInt &oth)
    {
        BN_mul(n, n, oth.n,NULL);
        return *this;
    }
    // mod
    BigInt &mod(const BigInt &oth)
    {
        BN_mod(n, n, oth.n, NULL);
        return *this;
    }
    friend class Group;
    friend class Point;
};

class Point
{
private:
    //EC_GROUP *ec_group;
    EC_POINT *p;

public:
    Point() {p=NULL;}
    ~Point()
    {
        if(!p)return;   
        EC_POINT_free(p);
    }

    Point(const Point &oth)
    {
        EC_POINT_copy(p, oth.p);
    }
    Point &operator=(const Point &oth)
    {
        EC_POINT_copy(p, oth.p);
        return *this;
    }

    friend class Group;
};

class Group
{
private:
    EC_GROUP *ec_group;

public:
    Group()
    {
        ec_group = EC_GROUP_new_by_curve_name(NID_secp256k1);
        precompute();
    }
    ~Group()
    {
    }

    void get_order(BigInt &n){
        EC_GROUP_get_order(ec_group,n.n,NULL);
    }
    void precompute()
    {
        EC_GROUP_precompute_mult(ec_group, NULL);
    }
    //precomputation table for some group element
    friend class Point;


	void get_generator(Point &g)
	{
		EC_POINT_copy(g.p, EC_GROUP_get0_generator(ec_group));
	}

	void init(Point &p)
	{
		p.p = EC_POINT_new(ec_group);
	}
	void add(Point &res, const Point &lhs, const Point &rhs)
	{
		EC_POINT_add(ec_group, res.p, lhs.p, rhs.p, NULL);
	}

	void inv(Point &res, const Point &p)
	{
		res = p;
		EC_POINT_invert(ec_group, res.p, NULL);
	}
	void mul(Point &res, const Point &lhs, const BigInt &m)
	{
		EC_POINT_mul(ec_group, res.p, NULL, lhs.p, m.n, NULL);
	}
	void mul_gen(Point &res, const BigInt &m)
	{
		EC_POINT_mul(ec_group, res.p, m.n ,NULL, NULL, NULL);
	}
	char* to_hex(const Point &p) const
	{
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

	void from_hex(Point &p,const char *s) const
	{
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