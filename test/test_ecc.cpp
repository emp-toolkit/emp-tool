#include "emp-tool/emp-tool.h"
#include <iostream>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>

using namespace std;
using namespace emp;


int main() {
	ECGroup G;
	Scalar ia = G.rand_scalar();
	Scalar ib = G.rand_scalar();
	Scalar ic = G.rand_scalar();
	Scalar id = G.rand_scalar();
	Point * a = new Point();
	Point b;
	*a = G.mul_gen(ia);//g^a
	b = G.mul_gen(ib);//g^a
	ic = ia.add_mod(ib, G.order(), G.bn_ctx());
	Point c = G.mul_gen(ic);//g^{a+b}
	Point d = a->add(b);
	int res = (d == c);
	cout << res<<endl;


	c = a->mul(ib);//c=a^ib = g^ab
	d = b.mul(ia);//c=a^ib = g^ab
	
	res = (d == c);
	cout << res<<endl;

	int size = a->size();
	unsigned char * tmp = new unsigned char[size];
	a->to_bin(tmp, size);
	b.from_bin(&G, tmp, size);

	res = (*a==b);
	cout << res<<endl;

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
		cout << "h2c \"" << v.msg << "\" x match: " << (got == v.x_hex) << endl;
		OPENSSL_free(xh);
		BN_free(xb); BN_free(yb);
	}

	return 0;
}
