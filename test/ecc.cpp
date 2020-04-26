#include "emp-tool/emp-tool.h"
#include <iostream>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>

using namespace std;
using namespace emp;


int main() {
	Group G;
	BigInt ia, ib, ic, id;
	G.get_rand_bn(ia);
	G.get_rand_bn(ib);
	G.get_rand_bn(ic);
	G.get_rand_bn(id);
	Point * a = new Point();
	Point b;
	*a = G.mul_gen(ia);//g^a
	b = G.mul_gen(ib);//g^a
	ic = ia.add_mod(ib, G.order, G.bn_ctx);
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
	
	return 0;
}
